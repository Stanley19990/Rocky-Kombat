// RKCGameModePresentationSelfTest.cpp
//
// The presentation half of the in-engine self-test: the audio catalog and the
// event-to-sound planner, the front-end state machine, and the touch pad.
//
// WHY THIS IS A SEPARATE FILE: the match half (RKCGameMode.cpp) proves the
// combat simulation. This file proves the things a player actually looks at and
// touches. It is split out so the match self-test stays readable, and so the
// evidence record clearly separates "the fight is correct" from "the game
// presents the fight correctly".
//
// WHAT IS ACTUALLY VERIFIED HERE, AND WHAT IS NOT:
//   * The catalog, the planner and the touch pad are pure data and pure logic,
//     so every check below is a real assertion about behaviour.
//   * The front-end machine is driven with synthetic input at a fixed step,
//     through the same Tick() the director calls. What is verified is that the
//     machine reaches the right screens and returns the right requests. What is
//     NOT verified here is that a human finds the menus pleasant, or that the
//     pixels look right: those need a person and a screenshot, and the record
//     says so rather than implying otherwise.
//
// Every check reports the measured number, not just a verdict, so the record
// can be read without re-running the test.

#include "Match/RKCGameMode.h"

#include "Audio/RKCAudio.h"
#include "Match/RKCPlayerController.h"
#include "UI/RKCFrontEnd.h"
#include "UI/RKCTouchControls.h"

#include "HAL/IConsoleManager.h"

namespace
{
// ---------------------------------------------------------------------------
// Item ids used by the drive.
//
// These are literals rather than references because the machine's own Id*
// constants live in an anonymous namespace inside RKCFrontEnd.cpp. A typo here
// cannot silently pass: every drive step asserts the screen it arrived on, and
// an id that does not exist leaves the cursor where it was, so the next check
// fails loudly.
// ---------------------------------------------------------------------------
const TCHAR* const ItemPlay = TEXT("play");
const TCHAR* const ItemControls = TEXT("controls");
const TCHAR* const ItemOptions = TEXT("options");
const TCHAR* const ItemExpRba = TEXT("exp_rba");
const TCHAR* const ItemExpStreet = TEXT("exp_street");
const TCHAR* const ItemArrCpu = TEXT("arr_cpu");
const TCHAR* const ItemArrLocal = TEXT("arr_local");
const TCHAR* const ItemArrOnline = TEXT("arr_online");
const TCHAR* const ItemBack = TEXT("back");
const TCHAR* const ItemFighterRocky = TEXT("fighter_rocky");
const TCHAR* const ItemFighterShadow = TEXT("fighter_shadow");
const TCHAR* const ItemDiffContender = TEXT("diff_contender");
const TCHAR* const ItemDiffChampion = TEXT("diff_champion");
const TCHAR* const ItemFight = TEXT("fight");

using FMachine = RKCFrontEnd::FMachine;
using FInput = RKCFrontEnd::FInput;
using EScreen = RKCFrontEnd::EScreen;

/** The drive step. Well under the machine's 0.25 s clamp, so a drive can step
 *  the clock precisely instead of depending on the frame rate. */
constexpr float DriveStep = 0.05f;

/** How many simulated seconds a drive is allowed before it is called stuck. */
constexpr float DriveBudgetSeconds = 40.0f;

FString ScreenText(const FMachine& Machine)
{
    return FMachine::ScreenName(Machine.GetScreen());
}

int32 IndexOfItem(const FMachine& Machine, const TCHAR* Id)
{
    const TArray<RKCFrontEnd::FMenuItem>& Items = Machine.GetItems();
    for (int32 Index = 0; Index < Items.Num(); ++Index)
    {
        if (Items[Index].Id == Id)
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

bool HasItem(const FMachine& Machine, const TCHAR* Id)
{
    return IndexOfItem(Machine, Id) != INDEX_NONE;
}

FString CurrentItemId(const FMachine& Machine)
{
    const TArray<RKCFrontEnd::FMenuItem>& Items = Machine.GetItems();
    const int32 Selection = Machine.GetSelection();
    return Items.IsValidIndex(Selection) ? Items[Selection].Id : FString(TEXT("<none>"));
}

bool CurrentItemEnabled(const FMachine& Machine)
{
    const TArray<RKCFrontEnd::FMenuItem>& Items = Machine.GetItems();
    const int32 Selection = Machine.GetSelection();
    return Items.IsValidIndex(Selection) && Items[Selection].bEnabled;
}

/** Advances the machine with no input at all, which is what a player who is
 *  reading the screen does. */
void TickEmpty(FMachine& Machine, float Seconds, bool bWarmup = true)
{
    const int32 Steps = FMath::Max(1, FMath::CeilToInt(Seconds / DriveStep));
    const FInput None;
    for (int32 Index = 0; Index < Steps; ++Index)
    {
        Machine.Tick(DriveStep, None, bWarmup);
    }
}

void TickInput(FMachine& Machine, const FInput& Input)
{
    Machine.Tick(DriveStep, Input, true);
}

void TickAccept(FMachine& Machine)
{
    FInput In;
    In.bAccept = true;
    Machine.Tick(DriveStep, In, true);
}

void TickBack(FMachine& Machine)
{
    FInput In;
    In.bBack = true;
    Machine.Tick(DriveStep, In, true);
}

void TickPause(FMachine& Machine)
{
    FInput In;
    In.bPause = true;
    Machine.Tick(DriveStep, In, true);
}

/** Walks the cursor down (wrapping, exactly as a player pressing Down does)
 *  until the wanted row is highlighted. */
bool MoveTo(FMachine& Machine, const TCHAR* Id)
{
    for (int32 Attempt = 0; Attempt < 24; ++Attempt)
    {
        if (CurrentItemId(Machine) == Id)
        {
            return true;
        }
        FInput In;
        In.Navigate = 1;
        Machine.Tick(DriveStep, In, true);
    }
    return false;
}

/** Highlights a row and confirms it. Returns false only when the row could not
 *  be reached at all; a refused confirmation (a disabled row) still returns
 *  true, so the caller can assert what happened to the screen. */
bool Activate(FMachine& Machine, const TCHAR* Id)
{
    if (!MoveTo(Machine, Id))
    {
        return false;
    }
    TickAccept(Machine);
    return true;
}

/** Steps a machine until a screen is reached or the budget runs out, charging
 *  the loading progress the director would charge. Returns the seconds used,
 *  negative when the screen was never reached. */
float TickUntilScreen(FMachine& Machine, EScreen Wanted, float BudgetSeconds, bool bWarmup = true)
{
    const int32 Steps = FMath::Max(1, FMath::CeilToInt(BudgetSeconds / DriveStep));
    const FInput None;
    for (int32 Index = 0; Index < Steps; ++Index)
    {
        Machine.SetLoadingProgress(1.0f);
        Machine.Tick(DriveStep, None, bWarmup);
        if (Machine.GetScreen() == Wanted)
        {
            return (Index + 1) * DriveStep;
        }
    }
    return -1.0f;
}

bool InCatalog(const TArray<FRKCAudioCatalogEntry>& Catalog, const FString& Folder, const FString& Stem)
{
    for (const FRKCAudioCatalogEntry& Entry : Catalog)
    {
        if (Entry.Folder == Folder && Entry.Stem == Stem)
        {
            return true;
        }
    }
    return false;
}

FString DescribePlan(const FRKCSoundPlan& Plan)
{
    if (!Plan.HasPrimary() && !Plan.HasVoice())
    {
        return TEXT("silent");
    }
    return FString::Printf(TEXT("%s/%s%s"), *Plan.PrimaryFolder, *Plan.PrimaryStem,
                           Plan.HasVoice()
                               ? *FString::Printf(TEXT(" + %s/%s"), *Plan.VoiceFolder, *Plan.VoiceStem)
                               : TEXT(""));
}
} // namespace

// ---------------------------------------------------------------------------
// The entry point, called from RunInEngineSelfTest after the match half.
// ---------------------------------------------------------------------------
void ARKCGameMode::RunSelfTestPresentationSystems()
{
    const TArray<FRKCAudioCatalogEntry>& Catalog = RKCAudioCatalog();

    SelfTestLog += TEXT("\n--- PRESENTATION SYSTEMS ---\n");
    SelfTestLog += TEXT("Checked here: the audio catalog and the event planner, the front-end state\n"
                        "machine driven with synthetic input, and the touch pad. Not checked here:\n"
                        "how any of it looks or sounds to a person.\n\n");

    // =======================================================================
    // A. Audio catalog
    // =======================================================================
    SelfTestLog += TEXT("--- A. AUDIO CATALOG ---\n");

    // The catalog is two layers. The authored 70 are the synthesised set the
    // procedural generator produces; the recorded layer adds one entry per take
    // the slicer cut from the supplied CC0 recording. The recorded count is
    // derived from the same table the planner reads, so this assertion follows
    // the supplied recording instead of pinning a number that goes stale the
    // moment a take is added or held back.
    int32 RecordedEntryCount = 0;
    for (const FRKCAudioCatalogEntry& Entry : Catalog)
    {
        RecordedEntryCount += Entry.Folder.StartsWith(TEXT("Recorded/")) ? 1 : 0;
    }

    // The loop variable is RoleName rather than Role: inside an actor member
    // function a bare 'Role' collides with AActor::Role, and warnings are errors.
    int32 ExpectedRecordedCount = 0;
    for (RKC::ERoster Roster : { RKC::ERoster::Rocky, RKC::ERoster::Shadow })
    {
        for (const FString& RoleName : URKCAudioSystem::RecordedVoiceRoles())
        {
            ExpectedRecordedCount += URKCAudioSystem::RecordedVoiceVariantCount(Roster, *RoleName);
        }
    }

    SelfTestCheck(Catalog.Num() - RecordedEntryCount == 70,
                  FString::Printf(TEXT("the audio catalog holds the 70 authored entries (got %d)"),
                                  Catalog.Num() - RecordedEntryCount));
    SelfTestCheck(RecordedEntryCount == ExpectedRecordedCount,
                  FString::Printf(TEXT("the catalog lists exactly the recorded takes the planner asks for "
                                       "(%d recorded, %d expected)"),
                                  RecordedEntryCount, ExpectedRecordedCount));

    int32 LoopingCount = 0;
    int32 EmptyFolderCount = 0;
    int32 EmptyStemCount = 0;
    int32 DuplicateCount = 0;
    TSet<FString> SeenKeys;
    for (const FRKCAudioCatalogEntry& Entry : Catalog)
    {
        LoopingCount += Entry.bLooping ? 1 : 0;
        EmptyFolderCount += Entry.Folder.IsEmpty() ? 1 : 0;
        EmptyStemCount += Entry.Stem.IsEmpty() ? 1 : 0;
        const FString Key = Entry.Folder + TEXT("/") + Entry.Stem;
        if (SeenKeys.Contains(Key))
        {
            ++DuplicateCount;
        }
        SeenKeys.Add(Key);
    }

    SelfTestLog += FString::Printf(TEXT("catalog %d entries (%d authored + %d recorded takes), "
                                        "%d looping, %d empty fields, %d duplicate keys\n"),
                                   Catalog.Num(), Catalog.Num() - RecordedEntryCount,
                                   RecordedEntryCount, LoopingCount,
                                   EmptyFolderCount + EmptyStemCount, DuplicateCount);

    SelfTestCheck(LoopingCount == 4,
                  FString::Printf(TEXT("exactly the music and ambience beds loop (%d looping)"),
                                  LoopingCount));
    SelfTestCheck(EmptyFolderCount == 0 && EmptyStemCount == 0,
                  FString::Printf(TEXT("every entry names its folder and stem (empty: %d)"),
                                  EmptyFolderCount + EmptyStemCount));
    SelfTestCheck(DuplicateCount == 0,
                  FString::Printf(TEXT("no two entries share folder and stem (%d duplicates)"),
                                  DuplicateCount));

    URKCAudioSystem* Audio = GetAudioSystem();
    SelfTestCheck(Audio != nullptr, TEXT("the presentation audio system exists in this run"));

    if (Audio != nullptr)
    {
        SelfTestCheck(Audio->IsInitialised(), TEXT("the audio system reported itself initialised"));

        const FRKCAudioDiagnostics& AudioDiag = Audio->GetDiagnostics();
        SelfTestLog += FString::Printf(TEXT("audio: catalog %d, resolved %d, missing %d, music starts %d, "
                                            "music stops %d, ambience starts %d\n"),
                                       AudioDiag.CatalogSize, AudioDiag.AssetsResolved,
                                       AudioDiag.AssetsMissing, AudioDiag.MusicStarts,
                                       AudioDiag.MusicStops, AudioDiag.AmbienceStarts);

        // The presentation-only drivers - footfalls and the pre-round bell - have
        // no combat event behind them, so the event feed cannot show whether they
        // ran. They are counted by the audio system instead, and this is the only
        // place the record says so. The numbers are produced by the matches the
        // match half of this test already played, through the same
        // AdvanceMatchFrame() that Tick() calls, so a zero here means a player
        // would hear silence where the design calls for a sound.
        //
        // The round-start and round-end stingers have no entry point of their own:
        // they are planned from the combat event feed exactly like every hit, so the
        // per-event tally is the only place the record can show them. The matches
        // the match half of this run already played are the source of those numbers,
        // so a zero here means the round announcements went silent, not that a
        // counter went unwired. Reading them from PlaysByEventType also ties this
        // check to the same dedupe the planner applies, which a second code path
        // into the same stinger would have bypassed.
        //
        // The expectation is an exact count, not "at least one per match". The match
        // half accumulates how many rounds actually started and how many of those
        // actually broke, and the repeat run of the determinism block contributes to
        // those totals as well. "At least one per match" would pass on a card of
        // three first-round knockouts while the round-end cue was wired to nothing,
        // which is precisely the failure this check exists to catch.
        auto StingerPlays = [&AudioDiag](const TCHAR* EventName) -> int32
        {
            const int32* Found = AudioDiag.PlaysByEventType.Find(EventName);
            return (Found != nullptr) ? *Found : 0;
        };
        const int32 RoundStartStingers = StingerPlays(TEXT("RoundStarted"));
        const int32 RoundEndStingers = StingerPlays(TEXT("RoundEnded"));

        // What the matches of this run owed the audio layer. A knockout stops the
        // contest from inside the round, so that round never breaks and owes no
        // round-end cue; every other ending breaks the round first.
        SelfTestLog += FString::Printf(
            TEXT("rounds owed: %d round-start cue(s), %d round-end cue(s) ")
            TEXT("(%d round(s) started, %d broke; a knockout round breaks nothing)\n"),
            SelfTestRoundsStarted, SelfTestRoundsThatBroke, SelfTestRoundsStarted,
            SelfTestRoundsThatBroke);

        SelfTestLog += FString::Printf(
            TEXT("audio drivers: %d footstep(s), %d countdown cue(s), ")
            TEXT("%d round-start stinger(s), %d round-end stinger(s), ")
            TEXT("%d recorded take(s) played, %d fallback(s) to the synthesised voice, ")
            TEXT("%d unresolved voice(s), %d play(s) suppressed as duplicate\n"),
            AudioDiag.FootstepPlays, AudioDiag.CountdownCues,
            RoundStartStingers, RoundEndStingers,
            AudioDiag.RecordedVoicePlays, AudioDiag.VoiceFallbacksToSynthetic,
            AudioDiag.VoicePlansUnresolved, AudioDiag.PlaysSuppressedAsDuplicate);

        SelfTestCheck(AudioDiag.FootstepPlays > 0,
                      FString::Printf(TEXT("walking fighters were heard walking (%d footstep(s)) "),
                                      AudioDiag.FootstepPlays));
        SelfTestCheck(AudioDiag.CountdownCues > 0,
                      FString::Printf(TEXT("the pre-round countdown was voiced (%d cue(s)) "),
                                      AudioDiag.CountdownCues));
        SelfTestCheck(RoundStartStingers == SelfTestRoundsStarted,
                      FString::Printf(TEXT("every round that started was announced by the round-start ")
                                      TEXT("stinger (%d play(s) for %d round(s)) "),
                                      RoundStartStingers, SelfTestRoundsStarted));
        SelfTestCheck(RoundEndStingers == SelfTestRoundsThatBroke,
                      FString::Printf(TEXT("every round that broke was closed by the round-end stinger ")
                                      TEXT("(%d play(s) for %d round(s)) "),
                                      RoundEndStingers, SelfTestRoundsThatBroke));
        SelfTestCheck(AudioDiag.RecordedVoicePlays > 0,
                      FString::Printf(TEXT("the recorded performances were the ones actually played ")
                                      TEXT("(%d take(s), %d fallback(s))"),
                                      AudioDiag.RecordedVoicePlays,
                                      AudioDiag.VoiceFallbacksToSynthetic));
        SelfTestCheck(AudioDiag.VoicePlansUnresolved == 0,
                      FString::Printf(TEXT("every voice the planner named resolved to an asset ")
                                      TEXT("(%d unresolved)"),
                                      AudioDiag.VoicePlansUnresolved));

        SelfTestCheck(AudioDiag.CatalogSize == Catalog.Num(),
                      FString::Printf(TEXT("the system resolved the same catalog the test can see (%d vs %d)"),
                                      AudioDiag.CatalogSize, Catalog.Num()));
        SelfTestCheck(AudioDiag.AssetsResolved + AudioDiag.AssetsMissing == AudioDiag.CatalogSize,
                      TEXT("every catalog entry was either resolved or counted as missing"));
        SelfTestCheck(AudioDiag.AssetsMissing == 0,
                      FString::Printf(TEXT("every catalog entry resolves to an imported asset (%d missing)"),
                                      AudioDiag.AssetsMissing));

        const TArray<FString> Described = Audio->DescribeCatalog();
        SelfTestCheck(Described.Num() == Catalog.Num(),
                      FString::Printf(TEXT("the manifest description covers the whole catalog (%d lines)"),
                                      Described.Num()));
    }

    // =======================================================================
    // B. Event-to-sound planning
    // =======================================================================
    SelfTestLog += TEXT("\n--- B. EVENT PLANNING ---\n");

    auto MakeContext = [](RKC::ECombatEvent Type, RKC::EAction Action, float Value, int32 Tick,
                          RKC::ERoster Attacker, RKC::ERoster Defender)
    {
        FRKCEventContext Context;
        Context.Type = Type;
        Context.Side = RKC::ESide::A;
        Context.Action = Action;
        Context.Value = Value;
        Context.Tick = Tick;
        Context.Attacker = Attacker;
        Context.Defender = Defender;
        return Context;
    };

    // The planner is pure and only reachable through the audio system, so no
    // stand-in system is needed here (the system is asserted to exist above).

    if (Audio != nullptr)
    {
        auto Plan = [Audio](const FRKCEventContext& Context)
        {
            return Audio->PlanEvent(Context);
        };

        // A voice check that follows the supplied recording rather than an asset
        // name. The planner names the recorded take when the slicer produced one
        // for that fighter and role, and the synthesised performance of the same
        // role when it did not. Both are correct answers, so the expectation is
        // derived from the same table the planner reads: pinning the synthesised
        // stem would fail the moment a take exists, and pinning the recorded stem
        // would fail on a build without the recorded layer.
        // The parameter is named VoiceRole rather than Role because this runs
        // inside an actor member function, where bare 'Role' is AActor::Role.
        auto VoiceIsRole = [](const FRKCSoundPlan& Candidate, RKC::ERoster Who, const TCHAR* VoiceRole,
                              const TCHAR* SynthBase)
        {
            if (!Candidate.HasVoice())
            {
                return false;
            }
            const FString Fighter = (Who == RKC::ERoster::Shadow) ? TEXT("Shadow") : TEXT("Rocky");
            if (URKCAudioSystem::RecordedVoiceVariantCount(Who, VoiceRole) > 0)
            {
                return Candidate.VoiceFolder == URKCAudioSystem::RecordedVoiceFolder(Who) &&
                       Candidate.VoiceStem.StartsWith(
                           FString::Printf(TEXT("VO_Rec_%s_%s_"), *Fighter, VoiceRole));
            }
            return Candidate.VoiceFolder == Fighter &&
                   Candidate.VoiceStem.StartsWith(
                       FString::Printf(TEXT("VO_%s_%s_"), *Fighter, SynthBase));
        };

        // ---- a whiff is air plus the attacker's effort, never an impact -----
        {
            const FRKCEventContext Light = MakeContext(RKC::ECombatEvent::AttackWhiffed,
                                                       RKC::EAction::Jab, 0.0f, 100, RKC::ERoster::Rocky,
                                                       RKC::ERoster::Shadow);
            const FRKCSoundPlan WhiffLight = Plan(Light);
            SelfTestLog += FString::Printf(TEXT("whiff (light): %s\n"), *DescribePlan(WhiffLight));
            SelfTestCheck(WhiffLight.HasPrimary() && WhiffLight.PrimaryFolder == TEXT("Combat") &&
                              WhiffLight.PrimaryStem.StartsWith(TEXT("Whiff_Light_")),
                          TEXT("a light whiff uses the light whoosh family"));
            SelfTestCheck(VoiceIsRole(WhiffLight, RKC::ERoster::Rocky, TEXT("ExertLight"),
                                      TEXT("ExertLight")),
                          TEXT("a light whiff voices the attacker's light effort"));

            // The fallback is only planned when a recorded take exists, so its
            // presence has to mean the preferred voice is the recorded one.
            SelfTestCheck(!WhiffLight.HasVoiceFallback() ||
                              WhiffLight.VoiceFolder ==
                                  URKCAudioSystem::RecordedVoiceFolder(RKC::ERoster::Rocky),
                          TEXT("the recorded take is preferred wherever the recorded layer has one"));
            SelfTestCheck(!WhiffLight.HasVoiceFallback() ||
                              WhiffLight.VoiceFallbackFolder == TEXT("Rocky") &&
                                  WhiffLight.VoiceFallbackStem.StartsWith(TEXT("VO_Rocky_ExertLight_")),
                          TEXT("the synthesised voice of the same role is kept as the fallback"));

            const FRKCEventContext Heavy = MakeContext(RKC::ECombatEvent::AttackWhiffed,
                                                       RKC::EAction::HeavyPunch, 0.0f, 100,
                                                       RKC::ERoster::Shadow, RKC::ERoster::Rocky);
            const FRKCSoundPlan WhiffHeavy = Plan(Heavy);
            SelfTestCheck(WhiffHeavy.PrimaryStem.StartsWith(TEXT("Whiff_Heavy_")) &&
                              VoiceIsRole(WhiffHeavy, RKC::ERoster::Shadow, TEXT("ExertHeavy"),
                                          TEXT("ExertHeavy")),
                          TEXT("a heavy whiff uses the heavy whoosh and the attacker's heavy effort"));
        }

        // ---- a clean hit is the attacker's impact plus the defender's hurt --
        {
            const FRKCEventContext Hit = MakeContext(RKC::ECombatEvent::AttackHit, RKC::EAction::Jab,
                                                     0.0f, 120, RKC::ERoster::Rocky, RKC::ERoster::Shadow);
            const FRKCSoundPlan HitPlan = Plan(Hit);
            SelfTestLog += FString::Printf(TEXT("hit (Rocky on Shadow): %s\n"), *DescribePlan(HitPlan));
            SelfTestCheck(HitPlan.HasPrimary() && HitPlan.PrimaryFolder == TEXT("Rocky") &&
                              HitPlan.PrimaryStem.StartsWith(TEXT("GloveImpact_")),
                          TEXT("a hit by Rocky uses Rocky's padded-glove impact"));
            // React is the recorded role name; the synthesised stem for the same
            // job is Hurt, so the two layers name the same performance differently.
            SelfTestCheck(VoiceIsRole(HitPlan, RKC::ERoster::Shadow, TEXT("React"), TEXT("Hurt")),
                          TEXT("a hit on Shadow voices Shadow's hurt reaction"));

            const FRKCEventContext Swapped = MakeContext(RKC::ECombatEvent::AttackHit,
                                                         RKC::EAction::Jab, 0.0f, 120,
                                                         RKC::ERoster::Shadow, RKC::ERoster::Rocky);
            const FRKCSoundPlan SwappedPlan = Plan(Swapped);
            SelfTestCheck(SwappedPlan.PrimaryFolder == TEXT("Shadow") &&
                              SwappedPlan.PrimaryStem.StartsWith(TEXT("StrikeImpact_")) &&
                              VoiceIsRole(SwappedPlan, RKC::ERoster::Rocky, TEXT("React"),
                                          TEXT("Hurt")),
                          TEXT("ownership swaps the impact and voice families with the fighters"));
        }

        // ---- a block is a block, not a hit ---------------------------------
        {
            const FRKCEventContext Blocked = MakeContext(RKC::ECombatEvent::AttackBlocked,
                                                         RKC::EAction::Jab, 0.0f, 140,
                                                         RKC::ERoster::Rocky, RKC::ERoster::Shadow);
            const FRKCSoundPlan BlockPlan = Plan(Blocked);
            SelfTestLog += FString::Printf(TEXT("blocked: %s\n"), *DescribePlan(BlockPlan));
            SelfTestCheck(BlockPlan.HasPrimary() && BlockPlan.PrimaryFolder == TEXT("Combat") &&
                              BlockPlan.PrimaryStem.StartsWith(TEXT("Block_")),
                          TEXT("a blocked hit uses the glove-on-glove block family"));
            SelfTestCheck(!BlockPlan.PrimaryStem.StartsWith(TEXT("Impact_")) &&
                              BlockPlan.PrimaryVolume < 1.0f,
                          TEXT("a blocked hit is not voiced as an impact and is mixed below one"));
        }

        // ---- a swing on its own is silent ----------------------------------
        {
            const FRKCEventContext Started = MakeContext(RKC::ECombatEvent::AttackStarted,
                                                         RKC::EAction::Jab, 0.0f, 160,
                                                         RKC::ERoster::Rocky, RKC::ERoster::Shadow);
            const FRKCSoundPlan StartedPlan = Plan(Started);
            SelfTestCheck(!StartedPlan.HasPrimary() && !StartedPlan.HasVoice(),
                          TEXT("AttackStarted is silent so one swing cannot make two sounds"));
        }

        // ---- match end: victory or defeat, plus the winner's call ----------
        {
            const FRKCEventContext Won = MakeContext(RKC::ECombatEvent::MatchEnded, RKC::EAction::Idle,
                                                     1.0f, 180, RKC::ERoster::Rocky,
                                                     RKC::ERoster::Shadow);
            const FRKCSoundPlan WonPlan = Plan(Won);
            SelfTestLog += FString::Printf(TEXT("match ended (win): %s\n"), *DescribePlan(WonPlan));
            SelfTestCheck(WonPlan.PrimaryFolder == TEXT("Stingers") &&
                              WonPlan.PrimaryStem == TEXT("STG_Victory"),
                          TEXT("a win plays the victory stinger"));
            SelfTestCheck(WonPlan.HasVoice() && WonPlan.VoiceStem.StartsWith(TEXT("VO_Rocky_Victory_")),
                          TEXT("a win adds the winner's victory call"));

            const FRKCEventContext Lost = MakeContext(RKC::ECombatEvent::MatchEnded, RKC::EAction::Idle,
                                                      0.0f, 180, RKC::ERoster::Rocky,
                                                      RKC::ERoster::Shadow);
            const FRKCSoundPlan LostPlan = Plan(Lost);
            SelfTestCheck(LostPlan.PrimaryStem == TEXT("STG_Defeat") && !LostPlan.HasVoice(),
                          TEXT("a loss plays the defeat stinger with no victory call"));
        }

        // ---- stamina is a breath, never an impact --------------------------
        {
            const FRKCEventContext Tired = MakeContext(RKC::ECombatEvent::StaminaExhausted,
                                                       RKC::EAction::Idle, 0.0f, 200,
                                                       RKC::ERoster::Shadow, RKC::ERoster::Rocky);
            const FRKCSoundPlan TiredPlan = Plan(Tired);
            SelfTestLog += FString::Printf(TEXT("stamina exhausted: %s\n"), *DescribePlan(TiredPlan));
            SelfTestCheck(!TiredPlan.HasPrimary(),
                          TEXT("stamina exhaustion makes no impact sound at all"));
            SelfTestCheck(VoiceIsRole(TiredPlan, RKC::ERoster::Shadow, TEXT("ExertHeavy"),
                                      TEXT("ExertHeavy")),
                          TEXT("stamina exhaustion voices only the fighter's heavy breath"));
        }

        // ---- round cues and the unmapped default ---------------------------
        {
            FRKCEventContext RoundStart = MakeContext(RKC::ECombatEvent::RoundStarted, RKC::EAction::Idle,
                                                      0.0f, 220, RKC::ERoster::Rocky,
                                                      RKC::ERoster::Shadow);
            FRKCEventContext RoundEnd = RoundStart;
            RoundEnd.Type = RKC::ECombatEvent::RoundEnded;
            SelfTestCheck(Plan(RoundStart).PrimaryStem == TEXT("STG_RoundStart") &&
                              Plan(RoundStart).PrimaryFolder == TEXT("Stingers"),
                          TEXT("round start plays the round-start stinger"));
            SelfTestCheck(Plan(RoundEnd).PrimaryStem == TEXT("STG_RoundEnd"),
                          TEXT("round end plays the round-end stinger"));

            FRKCEventContext Nothing = MakeContext(RKC::ECombatEvent::None, RKC::EAction::Idle, 0.0f, 240,
                                                   RKC::ERoster::Rocky, RKC::ERoster::Shadow);
            const FRKCSoundPlan NothingPlan = Plan(Nothing);
            SelfTestCheck(!NothingPlan.HasPrimary() && !NothingPlan.HasVoice() &&
                              !NothingPlan.Reason.IsEmpty(),
                          TEXT("an unmapped event is silent and says why"));
        }

        // ---- determinism: the same tick replays the same sound -------------
        {
            const FRKCEventContext Context = MakeContext(RKC::ECombatEvent::AttackHit,
                                                         RKC::EAction::HeavyPunch, 0.0f, 333,
                                                         RKC::ERoster::Shadow,
                                                         RKC::ERoster::Rocky);
            const FRKCSoundPlan First = Plan(Context);
            const FRKCSoundPlan Second = Plan(Context);
            SelfTestCheck(First.PrimaryFolder == Second.PrimaryFolder &&
                              First.PrimaryStem == Second.PrimaryStem &&
                              First.VoiceStem == Second.VoiceStem &&
                              FMath::IsNearlyEqual(First.PrimaryVolume, Second.PrimaryVolume),
                          TEXT("planning the same event twice gives the same sound (replays match)"));
        }

        // ---- every planned sound exists in the catalog, for every variant --
        {
            const RKC::ECombatEvent EveryEvent[] = {
                RKC::ECombatEvent::AttackWhiffed, RKC::ECombatEvent::AttackHit,
                RKC::ECombatEvent::AttackBlocked, RKC::ECombatEvent::GuardBroken,
                RKC::ECombatEvent::DodgeStarted,  RKC::ECombatEvent::Knockdown,
                RKC::ECombatEvent::GetUp,         RKC::ECombatEvent::Knockout,
                RKC::ECombatEvent::RoundStarted,  RKC::ECombatEvent::RoundEnded,
                RKC::ECombatEvent::MatchEnded,    RKC::ECombatEvent::StaminaExhausted,
                RKC::ECombatEvent::AttackStarted};
            const RKC::EAction Actions[] = {RKC::EAction::Jab, RKC::EAction::HeavyPunch};

            int32 Planned = 0;
            int32 MissingFromCatalog = 0;
            int32 WithPrimary = 0;
            int32 WithVoice = 0;
            int32 Silent = 0;
            TSet<FString> DistinctPrimaryStems;

            for (const RKC::ECombatEvent Event : EveryEvent)
            {
                for (const RKC::EAction Action : Actions)
                {
                    for (int32 Tick = 0; Tick < 96; ++Tick)
                    {
                        const FRKCEventContext Context = MakeContext(
                            Event, Action, (Tick % 2 == 0) ? 1.0f : 0.0f, Tick, RKC::ERoster::Rocky,
                            RKC::ERoster::Shadow);
                        const FRKCSoundPlan PlanResult = Plan(Context);
                        ++Planned;

                        if (PlanResult.HasPrimary())
                        {
                            ++WithPrimary;
                            DistinctPrimaryStems.Add(PlanResult.PrimaryFolder + TEXT("/") +
                                                    PlanResult.PrimaryStem);
                            if (!InCatalog(Catalog, PlanResult.PrimaryFolder, PlanResult.PrimaryStem))
                            {
                                ++MissingFromCatalog;
                            }
                        }
                        else
                        {
                            ++Silent;
                        }

                        if (PlanResult.HasVoice())
                        {
                            ++WithVoice;
                            if (!InCatalog(Catalog, PlanResult.VoiceFolder, PlanResult.VoiceStem))
                            {
                                ++MissingFromCatalog;
                            }
                        }
                    }
                }
            }

            SelfTestLog += FString::Printf(
                TEXT("planner sweep: %d events, %d with a primary, %d with a voice, %d silent, "
                     "%d distinct primary sounds\n"),
                Planned, WithPrimary, WithVoice, Silent, DistinctPrimaryStems.Num());

            SelfTestCheck(Planned > 2000, TEXT("the planner sweep covered the whole event matrix"));
            SelfTestCheck(MissingFromCatalog == 0,
                          FString::Printf(TEXT("every planned sound exists in the catalog (%d outside it)"),
                                          MissingFromCatalog));
            SelfTestCheck(WithPrimary > 0 && WithVoice > 0 && Silent > 0,
                          TEXT("the planner uses impacts, voices and deliberate silence"));
            SelfTestCheck(DistinctPrimaryStems.Num() > 10,
                          FString::Printf(TEXT("the planner varies its variants (%d distinct sounds)"),
                                          DistinctPrimaryStems.Num()));
        }

        // ---- the impact stems a test can name come from the same table -----
        {
            int32 ResolvedImpactStems = 0;
            int32 ImpactStemsMissing = 0;
            for (const RKC::ERoster Who : {RKC::ERoster::Rocky, RKC::ERoster::Shadow})
            {
                const FString Folder = (Who == RKC::ERoster::Shadow) ? TEXT("Shadow") : TEXT("Rocky");
                for (int32 Variant = 1; Variant <= URKCAudioSystem::RosterImpactVariantCount(); ++Variant)
                {
                    ++ResolvedImpactStems;
                    if (!InCatalog(Catalog, Folder, URKCAudioSystem::RosterImpactStem(Who, Variant)))
                    {
                        ++ImpactStemsMissing;
                    }
                }
            }
            SelfTestCheck(ResolvedImpactStems == 6 && ImpactStemsMissing == 0,
                          FString::Printf(TEXT("both fighters' impact stems resolve through the planner's table "
                                               "(%d stems, %d missing)"),
                                          ResolvedImpactStems, ImpactStemsMissing));
        }
    }
    else
    {
        SelfTestLog += TEXT("no audio system: the planner was NOT exercised in this run\n");
    }

    // =======================================================================
    // C. The front-end state machine
    // =======================================================================
    SelfTestLog += TEXT("\n--- C. FRONT END ---\n");
    SelfTestLog += FString::Printf(TEXT("director state: front end %s, live screen %s\n"),
                                   IsFrontEndEnabled() ? TEXT("enabled") : TEXT("disabled (self-test)"),
                                   *ScreenText(GetFrontEnd()));

    // ---- C1. a first run, all the way to a match ---------------------------
    {
        FMachine Machine;
        Machine.SetAudio(Audio);
        Machine.Init(false);

        SelfTestCheck(Machine.GetScreen() == EScreen::Boot,
                      TEXT("a first run opens on the boot sting"));
        // The boot and title cards are splash cards, not menus: they must carry no
        // rows at all, so nothing can be activated while one of them is up.
        SelfTestCheck(Machine.GetItems().Num() == 0,
                      FString::Printf(TEXT("the boot card carries no menu rows (%d)"),
                                      Machine.GetItems().Num()));
        SelfTestCheck(Machine.IsFrontEndScreen() && !Machine.ShouldSimulationRun(),
                      TEXT("no match can run while a front-end screen owns the screen"));

        TickEmpty(Machine, 0.3f);
        SelfTestCheck(Machine.GetScreen() == EScreen::Boot,
                      TEXT("the boot sting is not skipped before it has been seen"));

        TickEmpty(Machine, 2.2f);
        SelfTestCheck(Machine.GetScreen() == EScreen::Title,
                      FString::Printf(TEXT("the boot sting leads to the title card (on %s)"),
                                      *ScreenText(Machine)));

        TickAccept(Machine);
        SelfTestCheck(Machine.GetScreen() == EScreen::Menu,
                      FString::Printf(TEXT("confirming the title reaches the main menu (on %s)"),
                                      *ScreenText(Machine)));

        // One press must do exactly one thing. This counter records item
        // activations, so a press that both acknowledged the title card and then
        // fired the menu's first row would be counted here - and would have skipped
        // the menu in the same frame.
        SelfTestCheck(Machine.GetDiagnostics().Accepts == 0,
                      FString::Printf(TEXT("the title press did not also activate a menu row "
                                           "(%d item activations)"),
                                      Machine.GetDiagnostics().Accepts));

        SelfTestCheck(HasItem(Machine, ItemPlay) && HasItem(Machine, ItemControls) &&
                          HasItem(Machine, ItemOptions),
                      TEXT("the main menu offers play, controls and settings"));

        // Play -> experience, with the only built experience pre-selected.
        SelfTestCheck(Activate(Machine, ItemPlay), TEXT("the play row is reachable"));

        // The counter above is only meaningful if it does move when a row really is
        // activated - otherwise "0 activations" would pass for any reason at all.
        SelfTestCheck(Machine.GetDiagnostics().Accepts == 1,
                      FString::Printf(TEXT("activating a menu row is counted as one activation (%d)"),
                                      Machine.GetDiagnostics().Accepts));
        SelfTestCheck(Machine.GetScreen() == EScreen::Experience &&
                          CurrentItemId(Machine) == ItemExpRba,
                      FString::Printf(TEXT("play leads to the experience choice with the built arena "
                                           "pre-selected (screen %s, row %s)"),
                                      *ScreenText(Machine), *CurrentItemId(Machine)));

        SelfTestCheck(IndexOfItem(Machine, ItemExpStreet) != INDEX_NONE &&
                          !Machine.GetItems()[IndexOfItem(Machine, ItemExpStreet)].bEnabled,
                      TEXT("the unimplemented experiences are listed as disabled, not hidden"));

        // A disabled row must never be reachable by navigating.
        {
            const int32 Before = Machine.GetSelection();
            FInput In;
            In.Navigate = 1;
            TickInput(Machine, In);
            SelfTestCheck(Machine.GetScreen() == EScreen::Experience &&
                              Machine.GetSelection() != Before && CurrentItemEnabled(Machine),
                          FString::Printf(TEXT("navigating skips the disabled rows (row %s)"),
                                          *CurrentItemId(Machine)));
        }

        SelfTestCheck(Activate(Machine, ItemExpRba), TEXT("the arena row is reachable"));
        SelfTestCheck(Machine.GetScreen() == EScreen::Arrangement,
                      FString::Printf(TEXT("choosing the arena leads to the arrangement choice (on %s)"),
                                      *ScreenText(Machine)));

        SelfTestCheck(Activate(Machine, ItemArrCpu), TEXT("the versus-computer row is reachable"));
        SelfTestCheck(Machine.GetScreen() == EScreen::CharacterSelect,
                      FString::Printf(TEXT("versus computer leads to fighter selection (on %s)"),
                                      *ScreenText(Machine)));

        SelfTestCheck(Activate(Machine, ItemFighterRocky), TEXT("the Rocky row is reachable"));
        SelfTestCheck(Machine.GetScreen() == EScreen::CharacterSelect &&
                          Machine.GetMatchRequest().FighterA == RKC::ERoster::Rocky,
                      TEXT("the first pick sets side A and stays on selection for side B"));

        SelfTestCheck(Activate(Machine, ItemFighterShadow), TEXT("the Shadow row is reachable"));
        SelfTestCheck(Machine.GetScreen() == EScreen::Difficulty,
                      FString::Printf(TEXT("both picks against a CPU lead to difficulty (on %s)"),
                                      *ScreenText(Machine)));
        // The graded default is the hardest grade now: the offline opponent is the
        // only opponent the build has, and it is meant to be hard to beat.
        SelfTestCheck(CurrentItemId(Machine) == ItemDiffChampion,
                      FString::Printf(TEXT("the graded default is the hardest grade (%s)"),
                                      *CurrentItemId(Machine)));

        SelfTestCheck(Activate(Machine, ItemDiffChampion), TEXT("the Champion row is reachable"));
        SelfTestCheck(Machine.GetScreen() == EScreen::Confirm &&
                          Machine.GetMatchRequest().Difficulty == RKC::EBotDifficulty::Champion,
                      FString::Printf(TEXT("a difficulty choice leads to the confirm card (on %s)"),
                                      *ScreenText(Machine)));

        const RKCFrontEnd::FRKCMatchRequest& Request = Machine.GetMatchRequest();
        SelfTestCheck(Request.FighterA == RKC::ERoster::Rocky &&
                          Request.FighterB == RKC::ERoster::Shadow && Request.bHumanA &&
                          !Request.bHumanB,
                      TEXT("the confirm card carries Rocky human against a CPU Shadow"));
        SelfTestCheck(Request.Arrangement == RKCFrontEnd::EArrangement::VsComputer,
                      TEXT("the confirm card remembers the arrangement that was chosen"));

        SelfTestCheck(Activate(Machine, ItemFight), TEXT("the fight row is reachable"));
        SelfTestCheck(Machine.GetScreen() == EScreen::Loading,
                      FString::Printf(TEXT("confirming the fight leads to the loading card (on %s)"),
                                      *ScreenText(Machine)));

        // The loading card must wait for real work, not for a timer.
        TickEmpty(Machine, 1.2f, /*bWarmup=*/false);
        SelfTestCheck(Machine.GetScreen() == EScreen::Loading,
                      FString::Printf(TEXT("loading waits while the renderer is still warming up (on %s)"),
                                      *ScreenText(Machine)));

        Machine.SetLoadingProgress(1.0f);
        const float ToVersus = TickUntilScreen(Machine, EScreen::Versus, 3.0f);
        SelfTestCheck(ToVersus > 0.0f,
                      FString::Printf(TEXT("loading completes once warm-up is done (arrived after %.2f s)"),
                                      ToVersus));

        const float ToMatch = TickUntilScreen(Machine, EScreen::Match, 4.0f);
        SelfTestCheck(ToMatch > 0.0f,
                      FString::Printf(TEXT("the versus card leads into the match (arrived after %.2f s)"),
                                      ToMatch));
        SelfTestCheck(Machine.ConsumeMatchStartRequest(),
                      TEXT("the machine asked the director to start the match"));
        SelfTestCheck(!Machine.ConsumeMatchStartRequest(),
                      TEXT("the match-start request is consumed exactly once"));

        SelfTestCheck(Machine.IsMatchPlayable() && Machine.ShouldSimulationRun() &&
                          !Machine.IsFrontEndScreen(),
                      TEXT("the machine reports a playable match with the simulation running"));
        SelfTestCheck(Audio == nullptr || Audio->GetCurrentMusic() == ERKCMusic::Arena,
                      TEXT("entering the match hands the arena bed to the audio system"));

        // ---- pause and resume inside the match -----------------------------
        TickPause(Machine);
        SelfTestCheck(Machine.IsGamePaused() && !Machine.ShouldSimulationRun(),
                      TEXT("the pause input freezes the match and the simulation"));
        TickPause(Machine);
        SelfTestCheck(Machine.GetScreen() == EScreen::Match &&
                          Machine.ShouldSimulationRun(),
                      FString::Printf(TEXT("the pause input resumes the match it paused (on %s)"),
                                      *ScreenText(Machine)));

        // ---- results, and no stray arena music behind the menu -------------
        Machine.NotifyMatchEnded(true, TEXT("ROCKY"));
        SelfTestCheck(Machine.GetScreen() == EScreen::Results,
                      FString::Printf(TEXT("a finished match lands on the results card (on %s)"),
                                      *ScreenText(Machine)));
        SelfTestCheck(HasItem(Machine, TEXT("rematch")) && HasItem(Machine, TEXT("main_menu")),
                      TEXT("the results card offers a rematch and a way back to the menu"));

        TickBack(Machine);
        SelfTestCheck(Machine.GetScreen() == EScreen::Menu,
                      FString::Printf(TEXT("leaving the results card returns to the main menu (on %s)"),
                                      *ScreenText(Machine)));
        SelfTestCheck(Audio == nullptr || Audio->GetCurrentMusic() != ERKCMusic::Arena,
                      TEXT("the arena bed does not keep playing behind the main menu"));

        SelfTestLog += FString::Printf(TEXT("first-run drive: %d screen changes, exit log:\n"),
                                       Machine.GetDiagnostics().ScreenChanges);
        for (const FString& Line : Machine.GetDiagnostics().TransitionLog)
        {
            SelfTestLog += FString::Printf(TEXT("    %s\n"), *Line);
        }
    }

    // ---- C2. local versus: two humans, no AI, and back-navigation ----------
    {
        FMachine Machine;
        Machine.SetAudio(Audio);
        Machine.Init(true);
        SelfTestCheck(Machine.GetScreen() == EScreen::Boot,
                      TEXT("a returning run still opens on the boot sting"));

        const float ToTitle = TickUntilScreen(Machine, EScreen::Title, 2.0f);
        SelfTestCheck(ToTitle > 0.0f && ToTitle < 1.0f,
                      FString::Printf(TEXT("a returning player leaves the shortened sting quickly (%.2f s)"),
                                      ToTitle));

        SelfTestCheck(Activate(Machine, ItemPlay), TEXT("the play row is reachable on a returning run"));
        SelfTestCheck(Activate(Machine, ItemExpRba), TEXT("the arena row is reachable"));
        SelfTestCheck(Activate(Machine, ItemArrLocal), TEXT("the local-versus row is reachable"));
        SelfTestCheck(Machine.GetScreen() == EScreen::LocalVersusJoin,
                      FString::Printf(TEXT("local versus leads to the device-assignment card (on %s)"),
                                      *ScreenText(Machine)));

        SelfTestCheck(Activate(Machine, ItemBack), TEXT("the continue row is reachable"));
        SelfTestCheck(Machine.GetScreen() == EScreen::CharacterSelect,
                      FString::Printf(TEXT("continuing leads to fighter selection (on %s)"),
                                      *ScreenText(Machine)));

        SelfTestCheck(Activate(Machine, ItemFighterRocky), TEXT("the Rocky row is reachable"));
        SelfTestCheck(Activate(Machine, ItemFighterShadow), TEXT("the Shadow row is reachable"));
        SelfTestCheck(Machine.GetScreen() == EScreen::Confirm,
                      FString::Printf(TEXT("two local picks skip the CPU difficulty step (on %s)"),
                                      *ScreenText(Machine)));

        const RKCFrontEnd::FRKCMatchRequest& Request = Machine.GetMatchRequest();
        SelfTestCheck(Request.Arrangement == RKCFrontEnd::EArrangement::LocalVersus &&
                          Request.bHumanA && Request.bHumanB,
                      TEXT("local versus marks both sides human"));
        SelfTestCheck(Request.RulesSummary.Contains(TEXT("no AI")),
                      FString::Printf(TEXT("the local-versus rules summary says no AI (%s)"),
                                      *Request.RulesSummary));

        SelfTestCheck(Activate(Machine, ItemFight), TEXT("the fight row is reachable"));
        Machine.SetLoadingProgress(1.0f);
        SelfTestCheck(TickUntilScreen(Machine, EScreen::Match, 6.0f) > 0.0f,
                      TEXT("local versus reaches a match through loading and the versus card"));
        SelfTestCheck(Machine.ConsumeMatchStartRequest(),
                      TEXT("local versus asks the director to start the match"));

        // Back-navigation must not lose the choices already made.
        Machine.NotifyMatchEnded(false, TEXT("SHADOW"));
        TickBack(Machine);
        SelfTestCheck(Machine.GetScreen() == EScreen::Menu,
                      TEXT("leaving a local-versus result returns to the main menu"));
        SelfTestCheck(Machine.GetMatchRequest().bHumanB,
                      TEXT("the local-versus arrangement survives the return to the menu"));
    }

    // ---- C3. the demonstration autopilot reaches a match on its own --------
    {
        FMachine Machine;
        Machine.SetAudio(Audio);
        Machine.Init(true);
        Machine.SetAutoPilot(true, 0.3f);

        const float ToMatch = TickUntilScreen(Machine, EScreen::Match, DriveBudgetSeconds);
        SelfTestCheck(ToMatch > 0.0f,
                      FString::Printf(TEXT("the autopilot walks the whole menu to a match unattended "
                                           "(%.1f s of simulated time)"),
                                      ToMatch));
        SelfTestCheck(Machine.GetDiagnostics().ScreenChanges >= 8,
                      FString::Printf(TEXT("the autopilot visited the whole flow (%d screen changes)"),
                                      Machine.GetDiagnostics().ScreenChanges));
        SelfTestCheck(Machine.GetDiagnostics().TransitionLog.Num() >= 8,
                      FString::Printf(TEXT("every hop was logged for review (%d transitions)"),
                                      Machine.GetDiagnostics().TransitionLog.Num()));

        SelfTestLog += FString::Printf(TEXT("autopilot drive: %d screen changes in %.1f s, path:\n"),
                                       Machine.GetDiagnostics().ScreenChanges, ToMatch);
        const TArray<FString>& Log = Machine.GetDiagnostics().TransitionLog;
        const int32 FirstShown = FMath::Max(0, Log.Num() - 12);
        for (int32 Index = FirstShown; Index < Log.Num(); ++Index)
        {
            SelfTestLog += FString::Printf(TEXT("    %s\n"), *Log[Index]);
        }
    }

    // =======================================================================
    // D. Touch controls
    // =======================================================================
    SelfTestLog += TEXT("\n--- D. TOUCH CONTROLS ---\n");
    {
        RKCTouch::FController Pad;
        Pad.ResetDiagnostics();

        const TArray<RKCTouch::FWidget>& Widgets = Pad.GetWidgets();
        SelfTestCheck(Widgets.Num() == 6,
                      FString::Printf(TEXT("the pad lays out six controls (got %d)"), Widgets.Num()));
        if (Widgets.Num() == 6)
        {
            SelfTestCheck(Widgets[0].Control == RKCTouch::EControl::Stick && Widgets[0].bIsStick,
                          TEXT("the first control is the movement stick"));
            SelfTestCheck(Widgets[1].Control == RKCTouch::EControl::Jab &&
                              Widgets[2].Control == RKCTouch::EControl::Heavy &&
                              Widgets[3].Control == RKCTouch::EControl::Block &&
                              Widgets[4].Control == RKCTouch::EControl::Dodge &&
                              Widgets[5].Control == RKCTouch::EControl::Pause,
                          TEXT("the action buttons keep their jab, heavy, block, dodge, pause order"));
            for (const RKCTouch::FWidget& Widget : Widgets)
            {
                if (Widget.Label.IsEmpty() || Widget.RadiusFraction <= 0.0f)
                {
                    SelfTestCheck(false,
                                  FString::Printf(TEXT("every control is labelable and touchable (%s)"),
                                                  *Widget.Label));
                }
            }
        }

        const FVector2D Viewport(1280.0, 720.0);
        Pad.SetViewportSize(Viewport);

        // The layout is authored in the bottom half of the screen, out of the way
        // of the fighters, and the pause control is out of the thumbs' reach.
        int32 BelowMiddle = 0;
        for (const RKCTouch::FWidget& Widget : Widgets)
        {
            if (!Widget.bIsStick)
            {
                continue;
            }
            if (Widget.Centre.Y > 0.5)
            {
                ++BelowMiddle;
            }
        }
        const FVector2D StickCentre = RKCTouch::FController::CentrePx(Widgets[0], Viewport);
        SelfTestCheck(BelowMiddle == 1 && StickCentre.Y > 300.0,
                      FString::Printf(TEXT("the stick sits in the lower half for a thumb (y=%.0f px)"),
                                      StickCentre.Y));

        const FVector2D PauseCentre = RKCTouch::FController::CentrePx(Widgets[5], Viewport);
        SelfTestCheck(PauseCentre.Y < 150.0 && PauseCentre.X > 1000.0,
                      FString::Printf(TEXT("pause sits in the top corner, away from the fight (%.0f,%.0f)"),
                                      PauseCentre.X, PauseCentre.Y));

        // ---- a pad that is off must inject nothing at all ------------------
        {
            RKC::FInputState Input;
            Input.bMoveLeft = true;
            const bool bBegin = Pad.TouchBegin(1, StickCentre);
            Pad.ApplyTo(Input);
            SelfTestCheck(!bBegin && Input.bMoveLeft && !Pad.IsEnabled(),
                          TEXT("a disabled pad refuses touches and leaves the fighter input untouched"));
        }

        Pad.SetEnabled(true);
        SelfTestCheck(Pad.IsEnabled(), TEXT("the pad can be switched on"));

        // ---- stick: dead zone, then full deflection ------------------------
        {
            SelfTestCheck(Pad.TouchBegin(2, StickCentre),
                          TEXT("a thumb on the stick centre is accepted"));
            RKC::FInputState Input;
            Pad.ApplyTo(Input);
            SelfTestCheck(!Input.bMoveLeft && !Input.bMoveRight,
                          TEXT("a thumb resting on the stick centre does not walk"));

            Pad.TouchMove(2, StickCentre + FVector2D(100.0, 0.0));
            RKC::FInputState Right;
            Pad.ApplyTo(Right);
            SelfTestCheck(Right.bMoveRight && !Right.bMoveLeft,
                          FString::Printf(TEXT("pushing the stick right walks right (axis %.2f)"),
                                          Pad.GetStickAxis()));

            Pad.TouchMove(2, StickCentre - FVector2D(100.0, 0.0));
            RKC::FInputState Left;
            Pad.ApplyTo(Left);
            SelfTestCheck(Left.bMoveLeft && !Left.bMoveRight,
                          FString::Printf(TEXT("pushing the stick left walks left (axis %.2f)"),
                                          Pad.GetStickAxis()));

            const bool bSecondStickFinger = Pad.TouchBegin(3, StickCentre + FVector2D(0.0, -60.0));
            SelfTestCheck(!bSecondStickFinger,
                          TEXT("a second finger cannot fight the first over movement"));

            Pad.TouchEnd(2, TEXT("self-test"));
            RKC::FInputState AfterEnd;
            Pad.ApplyTo(AfterEnd);
            SelfTestCheck(!AfterEnd.bMoveLeft && !AfterEnd.bMoveRight && !Pad.IsStickActive(),
                          TEXT("lifting the thumb stops movement immediately"));
        }

        // ---- a touch outside every control is ignored ----------------------
        {
            const bool bMiddle = Pad.TouchBegin(4, FVector2D(640.0, 360.0));
            SelfTestCheck(!bMiddle && Pad.GetTrackedFingerCount() == 0,
                          TEXT("a touch in the empty middle of the screen is ignored"));
        }

        // ---- action buttons: one press, one edge ---------------------------
        {
            const FVector2D JabCentre = RKCTouch::FController::CentrePx(Widgets[1], Viewport);
            const FVector2D HeavyCentre = RKCTouch::FController::CentrePx(Widgets[2], Viewport);
            const FVector2D DodgeCentre = RKCTouch::FController::CentrePx(Widgets[4], Viewport);

            SelfTestCheck(Pad.TouchBegin(5, JabCentre) && Pad.TouchBegin(6, HeavyCentre) &&
                              Pad.TouchBegin(7, DodgeCentre),
                          TEXT("three simultaneous presses are all accepted"));

            RKC::FInputState Input;
            Pad.ApplyTo(Input);
            SelfTestCheck(Input.bJabPressed && Input.bHeavyPressed && Input.bDodgePressed,
                          TEXT("multi-touch delivers jab, heavy and dodge in one frame"));

            RKC::FInputState Second;
            Pad.ApplyTo(Second);
            SelfTestCheck(!Second.bJabPressed && !Second.bHeavyPressed && !Second.bDodgePressed,
                          TEXT("a press is delivered exactly once, not held down"));

            const RKCTouch::FDiagnostics& Diag = Pad.GetDiagnostics();
            SelfTestCheck(Diag.MaxSimultaneousFingers >= 3,
                          FString::Printf(TEXT("the pad tracked three fingers at once (max %d)"),
                                          Diag.MaxSimultaneousFingers));

            Pad.TouchEnd(5, TEXT("self-test"));
            Pad.TouchEnd(6, TEXT("self-test"));
            Pad.TouchEnd(7, TEXT("self-test"));
            SelfTestCheck(Pad.GetTrackedFingerCount() == 0, TEXT("all three fingers released cleanly"));
        }

        // ---- block is a hold, and only clears when the last finger leaves --
        {
            const FVector2D BlockCentre = RKCTouch::FController::CentrePx(Widgets[3], Viewport);
            SelfTestCheck(Pad.TouchBegin(8, BlockCentre), TEXT("a thumb on block is accepted"));
            RKC::FInputState Held;
            Pad.ApplyTo(Held);
            SelfTestCheck(Held.bHoldBlock && Pad.IsBlockHeld(), TEXT("block is held while the thumb is down"));

            SelfTestCheck(Pad.TouchBegin(9, BlockCentre), TEXT("a second thumb on block is accepted"));
            Pad.TouchEnd(8, TEXT("self-test"));
            SelfTestCheck(Pad.IsBlockHeld(), TEXT("releasing one of two block fingers keeps the guard up"));

            Pad.TouchEnd(9, TEXT("self-test"));
            RKC::FInputState Released;
            Pad.ApplyTo(Released);
            SelfTestCheck(!Released.bHoldBlock && !Pad.IsBlockHeld(),
                          TEXT("the guard drops when the last block finger leaves"));
        }

        // ---- pause is a request, consumed once -----------------------------
        {
            const FVector2D PauseButton = RKCTouch::FController::CentrePx(Widgets[5], Viewport);
            SelfTestCheck(Pad.TouchBegin(10, PauseButton), TEXT("a thumb on pause is accepted"));
            SelfTestCheck(Pad.ConsumePauseRequest(),
                          TEXT("the touch pause button raises the same pause request as the key"));
            SelfTestCheck(!Pad.ConsumePauseRequest(),
                          TEXT("the pause request is consumed exactly once"));
            Pad.TouchEnd(10, TEXT("self-test"));
        }

        // ---- the watchdog clears a finger the device never reported --------
        {
            const FVector2D JabCentre = RKCTouch::FController::CentrePx(Widgets[1], Viewport);
            const FVector2D BlockCentre = RKCTouch::FController::CentrePx(Widgets[3], Viewport);
            Pad.TouchBegin(11, JabCentre);
            Pad.TouchBegin(12, BlockCentre);
            Pad.Tick(RKCTouch::FController::StaleTouchSeconds + 1.0f);

            const RKCTouch::FDiagnostics& Diag = Pad.GetDiagnostics();
            SelfTestCheck(Pad.GetTrackedFingerCount() == 0 && !Pad.IsBlockHeld() &&
                              !Pad.IsStickActive(),
                          TEXT("a finger the device never released is dropped by the watchdog"));
            SelfTestCheck(Diag.StuckTouchesCleared >= 2,
                          FString::Printf(TEXT("the watchdog counted every stuck finger (%d cleared)"),
                                          Diag.StuckTouchesCleared));
        }

        // ---- cancellation clears everything at once ------------------------
        {
            const FVector2D StickCentrePx = RKCTouch::FController::CentrePx(Widgets[0], Viewport);
            const FVector2D BlockCentre = RKCTouch::FController::CentrePx(Widgets[3], Viewport);
            Pad.TouchBegin(13, StickCentrePx);
            Pad.TouchMove(13, StickCentrePx + FVector2D(100.0, 0.0));
            Pad.TouchBegin(14, BlockCentre);

            const int32 Dropped = Pad.CancelAllTouches(TEXT("self-test-cancel"));
            RKC::FInputState Input;
            Pad.ApplyTo(Input);
            SelfTestCheck(Dropped == 2 && Pad.GetTrackedFingerCount() == 0 && !Pad.IsBlockHeld() &&
                              !Pad.IsStickActive() && !Input.bMoveLeft && !Input.bMoveRight &&
                              !Input.bHoldBlock,
                          FString::Printf(TEXT("one cancellation clears the stick and the guard (dropped %d)"),
                                          Dropped));
        }

        // ---- switching the pad off is itself a cancellation ----------------
        {
            const FVector2D BlockCentre = RKCTouch::FController::CentrePx(Widgets[3], Viewport);
            Pad.TouchBegin(15, BlockCentre);
            Pad.SetEnabled(false);
            RKC::FInputState Input;
            Input.bMoveLeft = true;
            Pad.ApplyTo(Input);
            SelfTestCheck(Pad.GetTrackedFingerCount() == 0 && !Pad.IsBlockHeld() && Input.bMoveLeft &&
                              !Pad.IsEnabled(),
                          TEXT("switching the pad off clears every latch and injects nothing"));
        }

        SelfTestLog += FString::Printf(
            TEXT("touch: %d begins, %d moves, %d ends, %d cancellations, %d stuck clears, "
                 "%d rejected, %d edges delivered, max %d fingers\n"),
            Pad.GetDiagnostics().Begins, Pad.GetDiagnostics().Moves, Pad.GetDiagnostics().Ends,
            Pad.GetDiagnostics().Cancellations, Pad.GetDiagnostics().StuckTouchesCleared,
            Pad.GetDiagnostics().BeginsRejectedOutsideControls, Pad.GetDiagnostics().EdgesDelivered,
            Pad.GetDiagnostics().MaxSimultaneousFingers);

        // ---- the live pad the game actually owns ---------------------------
        if (ARKCPlayerController* PC = GetRKCPlayerController())
        {
            const RKCTouch::FController& Live = PC->GetTouchController();
            SelfTestLog += FString::Printf(TEXT("live pad: %d widgets, enabled %s, visible %s, tracked %d\n"),
                                           Live.GetWidgets().Num(),
                                           Live.IsEnabled() ? TEXT("yes") : TEXT("no"),
                                           Live.IsVisible() ? TEXT("yes") : TEXT("no"),
                                           Live.GetTrackedFingerCount());
            SelfTestCheck(Live.GetWidgets().Num() == 6,
                          TEXT("the controller the game owns carries the same six controls"));
        }
        else
        {
            SelfTestLog += TEXT("no player controller in this run: the live pad was NOT inspected\n");
        }
    }

    // =======================================================================
    // E. Safe area (the mobile layout the pad is drawn inside)
    // =======================================================================
    // A phone held sideways is not all usable: a camera cut-out eats part of one
    // short edge and the system gesture bar eats part of the bottom, and a control
    // under either cannot be pressed reliably. What is asserted here is therefore
    // geometric, not cosmetic: that the drawn pad, the hit-tested pad and the
    // reported rectangle are the same rectangle, that an absurd metric cannot make
    // the pad unusable, and that the whole thing reduces to the old layout when no
    // metric is reported at all.
    SelfTestLog += TEXT("\n--- E. SAFE AREA ---\n");
    {
        RKCTouch::FController Pad;
        const TArray<RKCTouch::FWidget>& Widgets = Pad.GetWidgets();
        const FVector2D Landscape(2400.0, 1080.0);
        Pad.SetViewportSize(Landscape);

        // ---- with no metric the layout is the old layout exactly -----------
        {
            int32 Matching = 0;
            for (const RKCTouch::FWidget& Widget : Widgets)
            {
                const FVector2D Expected(Widget.Centre.X * Landscape.X, Widget.Centre.Y * Landscape.Y);
                const FVector2D Actual = RKCTouch::FController::CentrePx(Widget, Landscape, FMargin(0.0f));
                const float ExpectedRadius = static_cast<float>(
                    Widget.RadiusFraction * FMath::Min(Landscape.X, Landscape.Y));
                const float ActualRadius = RKCTouch::FController::RadiusPx(Widget, Landscape, FMargin(0.0f));
                if (Actual.Equals(Expected, 0.01) && FMath::IsNearlyEqual(ActualRadius, ExpectedRadius, 0.01f))
                {
                    ++Matching;
                }
            }
            SelfTestCheck(Matching == Widgets.Num(),
                          FString::Printf(TEXT("no safe area reported -> the layout is exactly the plain "
                                               "viewport mapping (%d of %d controls)"),
                                          Matching, Widgets.Num()));
        }

        // ---- a realistic landscape metric gives the expected rectangle -----
        {
            // The shape Android reports on a phone held sideways: a cut-out on one
            // short edge (left, once rotated) and the gesture bar along the bottom.
            const FMargin Requested(40.0f, 0.0f, 40.0f, 30.0f);
            FVector2D SafeMin;
            FVector2D SafeMax;
            RKCTouch::FController::GetSafeRect(Landscape, Requested, SafeMin, SafeMax);

            SelfTestCheck(FMath::IsNearlyEqual(SafeMin.X, 40.0, 0.5) &&
                              FMath::IsNearlyEqual(SafeMin.Y, 0.0, 0.5) &&
                              FMath::IsNearlyEqual(SafeMax.X, 2360.0, 0.5) &&
                              FMath::IsNearlyEqual(SafeMax.Y, 1050.0, 0.5),
                          FString::Printf(TEXT("a cut-out and a gesture bar shrink the usable rectangle to "
                                               "(%.0f,%.0f)-(%.0f,%.0f)"),
                                          SafeMin.X, SafeMin.Y, SafeMax.X, SafeMax.Y));

            // Every drawn control must sit wholly inside it: a button whose disc
            // reaches under the cut-out is a button the player cannot press, and
            // that is the whole reason this exists.
            int32 Inside = 0;
            for (const RKCTouch::FWidget& Widget : Widgets)
            {
                const FVector2D Centre = RKCTouch::FController::CentrePx(Widget, Landscape, Requested);
                const float Radius = RKCTouch::FController::RadiusPx(Widget, Landscape, Requested);
                const bool bFits = Centre.X - Radius >= SafeMin.X - 0.5 &&
                                   Centre.Y - Radius >= SafeMin.Y - 0.5 &&
                                   Centre.X + Radius <= SafeMax.X + 0.5 &&
                                   Centre.Y + Radius <= SafeMax.Y + 0.5;
                if (bFits)
                {
                    ++Inside;
                }
                else
                {
                    SelfTestLog += FString::Printf(
                        TEXT("  control %s at (%.0f,%.0f) r=%.0f does NOT fit inside the safe rectangle\n"),
                        *Widget.Label, Centre.X, Centre.Y, Radius);
                }
            }
            SelfTestCheck(Inside == Widgets.Num(),
                          FString::Printf(TEXT("every control is drawn wholly inside the safe rectangle "
                                               "(%d of %d)"),
                                          Inside, Widgets.Num()));
        }

        // ---- hit testing follows the drawn pad -----------------------------
        {
            // Insets large enough that the pad genuinely moves: 300 px off each
            // side of a 2400 px landscape screen and 120 px off the bottom.
            const FMargin Requested(300.0f, 0.0f, 300.0f, 120.0f);
            const RKCTouch::FWidget& Pause = Widgets[5];
            const FVector2D NowCentre = RKCTouch::FController::CentrePx(Pause, Landscape, Requested);
            const FVector2D BeforeCentre = RKCTouch::FController::CentrePx(Pause, Landscape, FMargin(0.0f));

            Pad.SetSafeInsetsPx(Requested);
            const RKCTouch::EControl AtNow = Pad.ControlAt(NowCentre);
            const RKCTouch::EControl AtBefore = Pad.ControlAt(BeforeCentre);
            const double Shift = FVector2D::Distance(NowCentre, BeforeCentre);

            SelfTestCheck(AtNow == RKCTouch::EControl::Pause &&
                              AtBefore != RKCTouch::EControl::Pause &&
                              Pad.GetTrackedFingerCount() == 0,
                          FString::Printf(TEXT("hit testing follows the drawn pad: the pause control moved "
                                               "%.0f px and is live only at its new centre (old spot: %s)"),
                                          Shift,
                                          AtBefore == RKCTouch::EControl::None ? TEXT("nothing")
                                                                               : TEXT("something else")));
        }

        // ---- an absurd metric cannot make the pad unusable -----------------
        {
            // Wrong in both directions at once: insets larger than the screen, and
            // a negative one (what a platform reports for "no safe zone").
            const FMargin Absurd(5000.0f, -80.0f, 5000.0f, 5000.0f);
            const FVector2D Small(1280.0, 720.0);
            FVector2D SafeMin;
            FVector2D SafeMax;
            RKCTouch::FController::GetSafeRect(Small, Absurd, SafeMin, SafeMax);
            const double Width = SafeMax.X - SafeMin.X;
            const double Height = SafeMax.Y - SafeMin.Y;

            SelfTestCheck(SafeMin.X >= 0.0 && SafeMin.Y >= 0.0 && Width <= Small.X && Height <= Small.Y &&
                              Width >= Small.X * RKCTouch::FController::SafeRectMinFraction - 0.5 &&
                              Height >= Small.Y * RKCTouch::FController::SafeRectMinFraction - 0.5,
                          FString::Printf(TEXT("an absurd metric is clamped, not believed: the usable rectangle "
                                               "keeps %.0fx%.0f of %.0fx%.0f px (negative inset -> %.0f)"),
                                          Width, Height, Small.X, Small.Y, SafeMin.Y));

            int32 Inside = 0;
            for (const RKCTouch::FWidget& Widget : Widgets)
            {
                const FVector2D Centre = RKCTouch::FController::CentrePx(Widget, Small, Absurd);
                if (Centre.X >= SafeMin.X - 0.5 && Centre.X <= SafeMax.X + 0.5 &&
                    Centre.Y >= SafeMin.Y - 0.5 && Centre.Y <= SafeMax.Y + 0.5)
                {
                    ++Inside;
                }
            }
            SelfTestCheck(Inside == Widgets.Num(),
                          FString::Printf(TEXT("every control still lands inside the clamped rectangle "
                                               "(%d of %d)"),
                                          Inside, Widgets.Num()));
        }

        // ---- a changed rectangle drops live touches, an unchanged one does not
        {
            RKCTouch::FController Steady;
            Steady.SetEnabled(true);
            Steady.SetViewportSize(Landscape);
            Steady.SetSafeInsetsPx(FMargin(40.0f, 0.0f, 40.0f, 30.0f));

            const FVector2D Block = RKCTouch::FController::CentrePx(
                Widgets[3], Landscape, Steady.GetSafeInsetsPx());
            Steady.TouchBegin(1, Block);
            Steady.TouchMove(1, Block + FVector2D(0.5, 0.0));

            // The player controller re-reads the metric once a second, so 59 reads
            // out of 60 return the same numbers. Those must be inert: cancelling on
            // every read would make the pad unusable because a held guard would be
            // released once a second.
            Steady.SetSafeInsetsPx(FMargin(40.0f, 0.0f, 40.0f, 30.0f));
            const bool bSurvivedRepeat = Steady.IsBlockHeld() && Steady.GetTrackedFingerCount() == 1;

            // A genuinely different metric (a rotation, or the gesture bar
            // appearing) has moved the control out from under the finger.
            Steady.SetSafeInsetsPx(FMargin(80.0f, 0.0f, 40.0f, 30.0f));
            SelfTestCheck(bSurvivedRepeat && Steady.GetTrackedFingerCount() == 0 && !Steady.IsBlockHeld(),
                          FString::Printf(TEXT("re-reading the same safe area keeps a held guard (%s); a "
                                               "changed one drops it (fingers now %d)"),
                                          bSurvivedRepeat ? TEXT("yes") : TEXT("no"),
                                          Steady.GetTrackedFingerCount()));
        }

        // ---- the documented override switch is real and is honoured --------
        {
            IConsoleVariable* Override = IConsoleManager::Get().FindConsoleVariable(TEXT("rkc.SafeAreaInsets"));
            const bool bExists = Override != nullptr;
            FString Previous;
            if (bExists)
            {
                Previous = Override->GetString();
                Override->Set(TEXT("63,0,17,29"), ECVF_SetByCode);
            }
            const FMargin Read = RKCTouch::ReadPlatformSafeInsetsPx(Landscape);
            const bool bHonoured = FMath::IsNearlyEqual(Read.Left, 63.0f, 0.01f) &&
                                   FMath::IsNearlyEqual(Read.Top, 0.0f, 0.01f) &&
                                   FMath::IsNearlyEqual(Read.Right, 17.0f, 0.01f) &&
                                   FMath::IsNearlyEqual(Read.Bottom, 29.0f, 0.01f);
            if (bExists)
            {
                // Restored, so a run started with -rkc.SafeAreaInsets keeps the
                // value it was asked for after the self-test has finished with it.
                Override->Set(*Previous, ECVF_SetByCode);
            }

            SelfTestCheck(bExists && bHonoured,
                          FString::Printf(TEXT("-rkc.SafeAreaInsets reaches the reader as an override: the "
                                               "variable %s and 63,0,17,29 came back as %.0f,%.0f,%.0f,%.0f"),
                                          bExists ? TEXT("exists") : TEXT("is MISSING"),
                                          Read.Left, Read.Top, Read.Right, Read.Bottom));
        }

        // ---- what this run actually applied --------------------------------
        if (ARKCPlayerController* PC = GetRKCPlayerController())
        {
            const RKCTouch::FController& Live = PC->GetTouchController();
            const FMargin Insets = Live.GetSafeInsetsPx();
            const FVector2D Size = Live.GetViewportSize();
            SelfTestLog += FString::Printf(
                TEXT("live pad safe area: L=%.0f T=%.0f R=%.0f B=%.0f px of %.0fx%.0f -> %s\n"),
                Insets.Left, Insets.Top, Insets.Right, Insets.Bottom, Size.X, Size.Y,
                Live.HasSafeAreaInset()
                    ? TEXT("laid out inside the safe rectangle")
                    : TEXT("no inset reported on this machine: the pad uses the whole viewport"));
        }
        else
        {
            SelfTestLog += TEXT("no player controller in this run: the live pad's safe area was NOT inspected\n");
        }
    }

    SelfTestLog += TEXT("\npresentation systems: checks complete (see the result section for the verdict)\n");
}
