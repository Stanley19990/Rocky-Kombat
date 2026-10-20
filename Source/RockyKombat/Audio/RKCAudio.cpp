// RKCAudio.cpp

#include "Audio/RKCAudio.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "RockyKombat.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "UObject/UObjectGlobals.h"

namespace
{
/** The save slot the volume settings and the first-run flag live in. */
const TCHAR* SettingsSlotName = TEXT("RKC_Settings");
constexpr int32 SettingsUserIndex = 0;

/** Root of the generated audio content. */
const TCHAR* AudioContentRoot = TEXT("/Game/RockyKombat/Audio");

/** Roster folder name for the character-specific sounds. */
FString RosterFolder(RKC::ERoster Roster)
{
    return (Roster == RKC::ERoster::Shadow) ? TEXT("Shadow") : TEXT("Rocky");
}

/** Character impact stems. Rocky throws a padded glove; Shadow throws a sharper
 *  strike. The two families are the audible identity of each fighter. */
const TCHAR* RosterImpactStemBase(RKC::ERoster Roster)
{
    return (Roster == RKC::ERoster::Shadow) ? TEXT("StrikeImpact") : TEXT("GloveImpact");
}

/** How many recorded takes the slicer produced for one fighter and one voice
 *  role.
 *
 *  This is a contract with Tools/Audio/rkc_slice_freesound.py: the layout it
 *  writes (ArtSource/Audio/Recorded/recorded_layout.json, key
 *  "per_fighter_counts") must match this table. The catalog and the planner both
 *  read it instead of repeating the numbers, so a count cannot drift in one
 *  place and not the other. A build that is missing the recorded layer still
 *  works: the planner falls back to the synthesised voice of the same role.
 *
 *  Rocky takes the darker half of the supplied performance and Shadow the
 *  brighter half, which is why the two columns differ by one. */
struct FRecordedVoiceSet
{
    const TCHAR* Role;
    int32 Rocky;
    int32 Shadow;
};

const FRecordedVoiceSet RecordedVoiceSets[] = {
    { TEXT("ExertLight"), 3, 4 },
    { TEXT("React"),      4, 4 },
    { TEXT("ExertHeavy"), 4, 4 },
};

FString Numbered(const TCHAR* Base, int32 Index)
{
    return FString::Printf(TEXT("%s_%02d"), Base, Index);
}

/** Deterministic variant pick. The simulation tick is the seed, so the same
 *  match replays with exactly the same sounds - the audio layer has no RNG of
 *  its own and cannot make two identical runs diverge. */
int32 PickVariant(int32 Tick, int32 Salt, int32 Count)
{
    if (Count <= 0)
    {
        return 0;
    }
    const uint32 Hash = static_cast<uint32>(Tick) * 2654435761u + static_cast<uint32>(Salt) * 40503u + 17u;
    return static_cast<int32>(Hash % static_cast<uint32>(Count));
}

/** Small deterministic pitch offset, so repeated hits of the same family do not
 *  sound like a machine gun. Kept under a semitone. */
float PitchFor(int32 Tick, int32 Salt)
{
    const uint32 Hash = static_cast<uint32>(Tick) * 2246822519u + static_cast<uint32>(Salt) * 3266489917u;
    const int32 Step = static_cast<int32>(Hash % 7u) - 3; // -3..3
    return 1.0f + 0.018f * static_cast<float>(Step);
}

FString EventTypeName(RKC::ECombatEvent Type)
{
    switch (Type)
    {
    case RKC::ECombatEvent::AttackStarted: return TEXT("AttackStarted");
    case RKC::ECombatEvent::AttackHit: return TEXT("AttackHit");
    case RKC::ECombatEvent::AttackBlocked: return TEXT("AttackBlocked");
    case RKC::ECombatEvent::GuardBroken: return TEXT("GuardBroken");
    case RKC::ECombatEvent::DodgeStarted: return TEXT("DodgeStarted");
    case RKC::ECombatEvent::Knockdown: return TEXT("Knockdown");
    case RKC::ECombatEvent::GetUp: return TEXT("GetUp");
    case RKC::ECombatEvent::Knockout: return TEXT("Knockout");
    case RKC::ECombatEvent::RoundStarted: return TEXT("RoundStarted");
    case RKC::ECombatEvent::RoundEnded: return TEXT("RoundEnded");
    case RKC::ECombatEvent::MatchEnded: return TEXT("MatchEnded");
    case RKC::ECombatEvent::StaminaExhausted: return TEXT("StaminaExhausted");
    case RKC::ECombatEvent::AttackWhiffed: return TEXT("AttackWhiffed");
    default: return TEXT("None");
    }
}
}

// ---------------------------------------------------------------------------
// Catalog
// ---------------------------------------------------------------------------

const TArray<FRKCAudioCatalogEntry>& RKCAudioCatalog()
{
    static TArray<FRKCAudioCatalogEntry> Catalog;
    if (Catalog.Num() > 0)
    {
        return Catalog;
    }

    auto Add = [](const TCHAR* Folder, const TCHAR* Stem, bool bLooping)
    {
        FRKCAudioCatalogEntry Entry;
        Entry.Folder = Folder;
        Entry.Stem = Stem;
        Entry.bLooping = bLooping;
        Catalog.Add(Entry);
    };

    // Music and ambience: the only looping entries, and therefore the only ones
    // whose loop seam was verified by the generator.
    Add(TEXT("Music"), TEXT("MUS_Title"), true);
    Add(TEXT("Music"), TEXT("MUS_Menu"), true);
    Add(TEXT("Music"), TEXT("MUS_Arena"), true);
    Add(TEXT("Ambience"), TEXT("AMB_Rooftop"), true);

    Add(TEXT("Stingers"), TEXT("STG_RoundStart"), false);
    Add(TEXT("Stingers"), TEXT("STG_RoundEnd"), false);
    Add(TEXT("Stingers"), TEXT("STG_KO"), false);
    Add(TEXT("Stingers"), TEXT("STG_Victory"), false);
    Add(TEXT("Stingers"), TEXT("STG_Defeat"), false);
    Add(TEXT("Stingers"), TEXT("STG_Bell"), false);

    Add(TEXT("UI"), TEXT("UI_Focus"), false);
    Add(TEXT("UI"), TEXT("UI_Select"), false);
    Add(TEXT("UI"), TEXT("UI_Back"), false);
    Add(TEXT("UI"), TEXT("UI_Disabled"), false);

    for (int32 i = 1; i <= 3; ++i) { Add(TEXT("Combat"), *Numbered(TEXT("Whiff_Light"), i), false); }
    for (int32 i = 1; i <= 3; ++i) { Add(TEXT("Combat"), *Numbered(TEXT("Whiff_Heavy"), i), false); }
    for (int32 i = 1; i <= 4; ++i) { Add(TEXT("Combat"), *Numbered(TEXT("Impact_Light"), i), false); }
    for (int32 i = 1; i <= 4; ++i) { Add(TEXT("Combat"), *Numbered(TEXT("Impact_Heavy"), i), false); }
    for (int32 i = 1; i <= 3; ++i) { Add(TEXT("Combat"), *Numbered(TEXT("Block"), i), false); }
    for (int32 i = 1; i <= 3; ++i) { Add(TEXT("Combat"), *Numbered(TEXT("Dodge"), i), false); }
    for (int32 i = 1; i <= 4; ++i) { Add(TEXT("Combat"), *Numbered(TEXT("Footstep"), i), false); }
    for (int32 i = 1; i <= 2; ++i) { Add(TEXT("Combat"), *Numbered(TEXT("Land"), i), false); }
    for (int32 i = 1; i <= 2; ++i) { Add(TEXT("Combat"), *Numbered(TEXT("KO"), i), false); }

    for (const TCHAR* Folder : { TEXT("Rocky"), TEXT("Shadow") })
    {
        for (int32 i = 1; i <= 3; ++i)
        {
            Add(Folder, *FString::Printf(TEXT("VO_%s_ExertLight_%02d"), Folder, i), false);
            Add(Folder, *FString::Printf(TEXT("VO_%s_ExertHeavy_%02d"), Folder, i), false);
            Add(Folder, *FString::Printf(TEXT("VO_%s_Hurt_%02d"), Folder, i), false);
        }
        for (int32 i = 1; i <= 2; ++i)
        {
            Add(Folder, *FString::Printf(TEXT("VO_%s_Victory_%02d"), Folder, i), false);
        }
        const TCHAR* ImpactBase = (FCString::Strcmp(Folder, TEXT("Shadow")) == 0)
                                      ? TEXT("StrikeImpact") : TEXT("GloveImpact");
        for (int32 i = 1; i <= 3; ++i)
        {
            Add(Folder, *Numbered(ImpactBase, i), false);
        }
    }

    // Recorded voice layer: real human performances cut from a CC0 Freesound take
    // by Tools/Audio/rkc_slice_freesound.py. Counts come from RecordedVoiceSets,
    // the same table the planner reads, so the catalog cannot claim a take the
    // planner will not ask for, or miss one it will.
    for (const TCHAR* Folder : { TEXT("Rocky"), TEXT("Shadow") })
    {
        const RKC::ERoster Roster = (FCString::Strcmp(Folder, TEXT("Shadow")) == 0)
                                        ? RKC::ERoster::Shadow
                                        : RKC::ERoster::Rocky;
        const FString RecordedRoot = URKCAudioSystem::RecordedVoiceFolder(Roster);
        for (const FRecordedVoiceSet& Set : RecordedVoiceSets)
        {
            const int32 Count = URKCAudioSystem::RecordedVoiceVariantCount(Roster, Set.Role);
            for (int32 i = 1; i <= Count; ++i)
            {
                Add(*RecordedRoot, *FString::Printf(TEXT("VO_Rec_%s_%s_%02d"), Folder, Set.Role, i), false);
            }
        }
    }

    return Catalog;
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

int32 URKCAudioSystem::Initialise(UWorld* InWorld, bool bInAllowPlayback)
{
    World = InWorld;
    bAllowPlayback = bInAllowPlayback;
    bInitialised = true;
    CurrentMusic = ERKCMusic::None;
    bPaused = false;
    bAmbienceWanted = false;
    MusicComponent = nullptr;
    AmbienceComponent = nullptr;
    RecentKeys.Init(FString(), DuplicateGuardDepth);
    RecentKeyCursor = 0;

    LoadSettings();

    const TArray<FRKCAudioCatalogEntry>& Catalog = RKCAudioCatalog();
    Diagnostics.CatalogSize = Catalog.Num();
    Diagnostics.AssetsResolved = 0;
    Diagnostics.MissingAssets.Reset();

    for (const FRKCAudioCatalogEntry& Entry : Catalog)
    {
        if (Resolve(Entry.Folder, Entry.Stem) != nullptr)
        {
            ++Diagnostics.AssetsResolved;
        }
        else
        {
            ++Diagnostics.AssetsMissing;
            Diagnostics.MissingAssets.Add(
                FString::Printf(TEXT("%s/%s"), *Entry.Folder, *Entry.Stem));
        }
    }

    UE_LOG(LogRockyKombat, Log,
           TEXT("Audio: %d/%d assets resolved (missing %d). Playback %s."),
           Diagnostics.AssetsResolved, Diagnostics.CatalogSize, Diagnostics.AssetsMissing,
           bAllowPlayback ? TEXT("enabled") : TEXT("disabled"));

    if (Diagnostics.AssetsMissing > 0)
    {
        UE_LOG(LogRockyKombat, Warning, TEXT("Audio: missing assets: %s"),
               *FString::Join(Diagnostics.MissingAssets, TEXT(", ")));
    }

    return Diagnostics.AssetsResolved;
}

void URKCAudioSystem::Shutdown()
{
    StopMusic(0.0f);
    StopAmbience();
    SoundCache.Empty();
    bInitialised = false;
}

FString URKCAudioSystem::BuildPath(const FString& Folder, const FString& Stem)
{
    return FString::Printf(TEXT("%s/%s/%s.%s"), AudioContentRoot, *Folder, *Stem, *Stem);
}

USoundBase* URKCAudioSystem::ResolveByPath(const FString& FullPath)
{
    if (TObjectPtr<USoundBase>* Found = SoundCache.Find(FName(*FullPath)))
    {
        return Found->Get();
    }
    USoundBase* Loaded = LoadObject<USoundBase>(nullptr, *FullPath);
    SoundCache.Add(FName(*FullPath), Loaded);
    return Loaded;
}

USoundBase* URKCAudioSystem::Resolve(const FString& Folder, const FString& Stem)
{
    if (Stem.IsEmpty())
    {
        return nullptr;
    }
    return ResolveByPath(BuildPath(Folder, Stem));
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

float URKCAudioSystem::GetMusicVolume() const { return IsMuted() ? 0.0f : Settings->MusicVolume; }
float URKCAudioSystem::GetSfxVolume() const { return IsMuted() ? 0.0f : Settings->SfxVolume; }
float URKCAudioSystem::GetVoiceVolume() const { return IsMuted() ? 0.0f : Settings->VoiceVolume; }
float URKCAudioSystem::GetUiVolume() const { return IsMuted() ? 0.0f : Settings->UiVolume; }
bool URKCAudioSystem::IsMuted() const { return Settings && Settings->bMuted; }

bool URKCAudioSystem::SetMusicVolume(float InVolume)
{
    const float Clamped = FMath::Clamp(InVolume, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(Settings->MusicVolume, Clamped, 1e-4f))
    {
        return false;
    }
    Settings->MusicVolume = Clamped;
    if (CurrentMusic != ERKCMusic::None && MusicComponent && MusicComponent->IsPlaying())
    {
        MusicComponent->SetVolumeMultiplier(GetMusicVolume());
    }
    SaveSettings();
    return true;
}

bool URKCAudioSystem::SetSfxVolume(float InVolume)
{
    const float Clamped = FMath::Clamp(InVolume, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(Settings->SfxVolume, Clamped, 1e-4f))
    {
        return false;
    }
    Settings->SfxVolume = Clamped;
    SaveSettings();
    return true;
}

bool URKCAudioSystem::SetVoiceVolume(float InVolume)
{
    const float Clamped = FMath::Clamp(InVolume, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(Settings->VoiceVolume, Clamped, 1e-4f))
    {
        return false;
    }
    Settings->VoiceVolume = Clamped;
    SaveSettings();
    return true;
}

bool URKCAudioSystem::SetUiVolume(float InVolume)
{
    const float Clamped = FMath::Clamp(InVolume, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(Settings->UiVolume, Clamped, 1e-4f))
    {
        return false;
    }
    Settings->UiVolume = Clamped;
    SaveSettings();
    return true;
}

bool URKCAudioSystem::SetMuted(bool bInMuted)
{
    if (Settings->bMuted == bInMuted)
    {
        return false;
    }
    Settings->bMuted = bInMuted;
    if (MusicComponent)
    {
        MusicComponent->SetVolumeMultiplier(GetMusicVolume());
    }
    if (AmbienceComponent)
    {
        AmbienceComponent->SetVolumeMultiplier(GetMusicVolume() * 0.9f);
    }
    SaveSettings();
    return true;
}

bool URKCAudioSystem::SetHasPlayedBefore(bool bInValue)
{
    if (Settings->bHasPlayedBefore == bInValue)
    {
        return false;
    }
    Settings->bHasPlayedBefore = bInValue;
    SaveSettings();
    return true;
}

bool URKCAudioSystem::GetHasPlayedBefore() const { return Settings && Settings->bHasPlayedBefore; }

bool URKCAudioSystem::SetTouchControlsEnabled(bool bInEnabled)
{
    if (!Settings)
    {
        return false;
    }

    // Reaching this function at all is the player making a choice, and that is
    // recorded even when the value does not change: on a device whose platform
    // default is "on", turning the pad off has to be remembered as a decision
    // rather than look like the setting was never touched.
    const bool bAlreadyChosen = Settings->bTouchControlsChosen;
    Settings->bTouchControlsChosen = true;

    if (Settings->bTouchControlsEnabled == bInEnabled)
    {
        if (!bAlreadyChosen)
        {
            SaveSettings();
        }
        return false;
    }

    Settings->bTouchControlsEnabled = bInEnabled;
    SaveSettings();
    return true;
}

bool URKCAudioSystem::GetTouchControlsEnabled() const { return Settings && Settings->bTouchControlsEnabled; }

bool URKCAudioSystem::WasTouchControlsChosen() const { return Settings && Settings->bTouchControlsChosen; }

bool URKCAudioSystem::SaveSettings()
{
    if (!Settings)
    {
        return false;
    }
    const bool bOk = UGameplayStatics::SaveGameToSlot(Settings, SettingsSlotName, SettingsUserIndex);
    if (!bOk)
    {
        UE_LOG(LogRockyKombat, Warning, TEXT("Audio: settings could not be saved to slot '%s'."),
               SettingsSlotName);
    }
    return bOk;
}

bool URKCAudioSystem::LoadSettings()
{
    if (UGameplayStatics::DoesSaveGameExist(SettingsSlotName, SettingsUserIndex))
    {
        Settings = Cast<URKCSettingsSaveGame>(
            UGameplayStatics::LoadGameFromSlot(SettingsSlotName, SettingsUserIndex));
    }
    if (!Settings)
    {
        Settings = Cast<URKCSettingsSaveGame>(
            UGameplayStatics::CreateSaveGameObject(URKCSettingsSaveGame::StaticClass()));
    }
    if (!Settings)
    {
        // Without a settings object the layer still has to work; fall back to a
        // transient default so a missing save system cannot break audio.
        Settings = NewObject<URKCSettingsSaveGame>(this);
        UE_LOG(LogRockyKombat, Warning,
               TEXT("Audio: settings object unavailable; using transient defaults."));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Music and ambience
// ---------------------------------------------------------------------------

void URKCAudioSystem::PlayMusic(ERKCMusic Track, float FadeSeconds)
{
    if (!bInitialised || Track == ERKCMusic::None)
    {
        return;
    }

    if (CurrentMusic == Track && MusicComponent && MusicComponent->IsPlaying())
    {
        // Already playing this bed: re-issuing it would restart the loop and
        // produce an audible seam for no reason.
        return;
    }

    if (MusicComponent)
    {
        MusicComponent->FadeOut(FMath::Max(FadeSeconds, 0.01f), 0.0f);
        MusicComponent = nullptr;
        ++Diagnostics.MusicStops;
    }

    const TCHAR* Stem = nullptr;
    switch (Track)
    {
    case ERKCMusic::Title: Stem = TEXT("MUS_Title"); break;
    case ERKCMusic::Menu: Stem = TEXT("MUS_Menu"); break;
    case ERKCMusic::Arena: Stem = TEXT("MUS_Arena"); break;
    default: break;
    }

    CurrentMusic = Track;
    USoundBase* Sound = Stem ? Resolve(TEXT("Music"), Stem) : nullptr;
    if (!Sound)
    {
        UE_LOG(LogRockyKombat, Warning, TEXT("Audio: music '%s' is unavailable."),
               Stem ? Stem : TEXT("<none>"));
        return;
    }

    // Guarantee the bed actually loops even if the import did not set the flag:
    // a music asset that stops after one pass is a fault the player would hear.
    if (USoundWave* Wave = Cast<USoundWave>(Sound))
    {
        if (!Wave->bLooping)
        {
            Wave->bLooping = true;
            UE_LOG(LogRockyKombat, Warning,
                   TEXT("Audio: '%s' was not flagged as looping; forced at runtime."), Stem);
        }
    }

    UWorld* WorldPtr = World.Get();
    if (!WorldPtr || !bAllowPlayback)
    {
        ++Diagnostics.MusicStarts;
        return;
    }

    UAudioComponent* Component = UGameplayStatics::CreateSound2D(
        WorldPtr, Sound, GetMusicVolume(), 1.0f, 0.0f, nullptr, false, false);
    if (!Component)
    {
        UE_LOG(LogRockyKombat, Warning, TEXT("Audio: music component could not be created for '%s'."), Stem);
        return;
    }
    Component->bIsUISound = true;
    Component->bAutoDestroy = false;
    Component->FadeIn(FMath::Max(FadeSeconds, 0.01f), GetMusicVolume());
    MusicComponent = Component;
    ++Diagnostics.MusicStarts;
    UE_LOG(LogRockyKombat, Log, TEXT("Audio: music -> %s"), Stem);
}

void URKCAudioSystem::StopMusic(float FadeSeconds)
{
    if (MusicComponent)
    {
        MusicComponent->FadeOut(FMath::Max(FadeSeconds, 0.01f), 0.0f);
        MusicComponent = nullptr;
        ++Diagnostics.MusicStops;
    }
    CurrentMusic = ERKCMusic::None;
}

void URKCAudioSystem::PlayAmbience()
{
    bAmbienceWanted = true;
    if (!bInitialised)
    {
        return;
    }
    if (AmbienceComponent && AmbienceComponent->IsPlaying())
    {
        return;
    }

    USoundBase* Sound = Resolve(TEXT("Ambience"), TEXT("AMB_Rooftop"));
    if (!Sound)
    {
        UE_LOG(LogRockyKombat, Warning, TEXT("Audio: rooftop ambience is unavailable."));
        return;
    }
    if (USoundWave* Wave = Cast<USoundWave>(Sound))
    {
        if (!Wave->bLooping)
        {
            Wave->bLooping = true;
        }
    }

    UWorld* WorldPtr = World.Get();
    if (!WorldPtr || !bAllowPlayback)
    {
        ++Diagnostics.AmbienceStarts;
        return;
    }

    UAudioComponent* Component = UGameplayStatics::CreateSound2D(
        WorldPtr, Sound, GetMusicVolume() * 0.9f, 1.0f, 0.0f, nullptr, false, false);
    if (!Component)
    {
        UE_LOG(LogRockyKombat, Warning, TEXT("Audio: ambience component could not be created."));
        return;
    }
    Component->bIsUISound = true;
    Component->bAutoDestroy = false;
    Component->FadeIn(0.8f, GetMusicVolume() * 0.9f);
    AmbienceComponent = Component;
    ++Diagnostics.AmbienceStarts;
}

void URKCAudioSystem::IncrementMatchesPlayed()
{
    if (!Settings)
    {
        return;
    }
    ++Settings->MatchesPlayed;
    SaveSettings();
}

void URKCAudioSystem::StopAmbience()
{
    bAmbienceWanted = false;
    if (AmbienceComponent)
    {
        AmbienceComponent->FadeOut(0.5f, 0.0f);
        AmbienceComponent = nullptr;
        ++Diagnostics.AmbienceStops;
    }
}

// ---------------------------------------------------------------------------
// One-shots
// ---------------------------------------------------------------------------

void URKCAudioSystem::IssueOneShot(USoundBase* Sound, float Volume, float Pitch, const TCHAR* Category)
{
    if (!Sound)
    {
        return;
    }
    Diagnostics.bPlaybackAttempted = Diagnostics.bPlaybackAttempted || bAllowPlayback;
    UWorld* WorldPtr = World.Get();
    if (!WorldPtr || !bAllowPlayback)
    {
        return;
    }
    if (Volume <= 0.0001f)
    {
        return;
    }
    UGameplayStatics::PlaySound2D(WorldPtr, Sound, Volume, Pitch, 0.0f, nullptr, nullptr, true);
    ++Diagnostics.PlaysIssued;
    (void)Category;
}

void URKCAudioSystem::PlayStinger(ERKCStinger Stinger)
{
    const TCHAR* Stem = nullptr;
    switch (Stinger)
    {
    case ERKCStinger::RoundStart: Stem = TEXT("STG_RoundStart"); break;
    case ERKCStinger::RoundEnd:   Stem = TEXT("STG_RoundEnd"); break;
    case ERKCStinger::Knockout:   Stem = TEXT("STG_KO"); break;
    case ERKCStinger::Victory:    Stem = TEXT("STG_Victory"); break;
    case ERKCStinger::Defeat:     Stem = TEXT("STG_Defeat"); break;
    case ERKCStinger::Bell:       Stem = TEXT("STG_Bell"); break;
    default: break;
    }
    IssueOneShot(Resolve(TEXT("Stingers"), Stem ? Stem : TEXT("")), GetSfxVolume(), 1.0f, TEXT("stinger"));
}

void URKCAudioSystem::PlayUi(ERKCUiSound Sound)
{
    const TCHAR* Stem = nullptr;
    switch (Sound)
    {
    case ERKCUiSound::Focus:    Stem = TEXT("UI_Focus"); break;
    case ERKCUiSound::Select:   Stem = TEXT("UI_Select"); break;
    case ERKCUiSound::Back:     Stem = TEXT("UI_Back"); break;
    case ERKCUiSound::Disabled: Stem = TEXT("UI_Disabled"); break;
    default: break;
    }
    IssueOneShot(Resolve(TEXT("UI"), Stem ? Stem : TEXT("")), GetUiVolume(), 1.0f, TEXT("ui"));
}

void URKCAudioSystem::PlayCombatFamily(ERKCCombatFamily Family, int32 Tick, float VolumeScale)
{
    const TCHAR* Base = nullptr;
    int32 Count = 1;
    int32 Salt = 0;
    switch (Family)
    {
    case ERKCCombatFamily::WhiffLight:   Base = TEXT("Whiff_Light");  Count = 3; Salt = 11; break;
    case ERKCCombatFamily::WhiffHeavy:   Base = TEXT("Whiff_Heavy");  Count = 3; Salt = 12; break;
    case ERKCCombatFamily::ImpactLight:  Base = TEXT("Impact_Light"); Count = 4; Salt = 13; break;
    case ERKCCombatFamily::ImpactHeavy:  Base = TEXT("Impact_Heavy"); Count = 4; Salt = 14; break;
    case ERKCCombatFamily::Block:        Base = TEXT("Block");        Count = 3; Salt = 15; break;
    case ERKCCombatFamily::Dodge:        Base = TEXT("Dodge");        Count = 3; Salt = 16; break;
    case ERKCCombatFamily::Footstep:     Base = TEXT("Footstep");     Count = 4; Salt = 17; break;
    case ERKCCombatFamily::Land:         Base = TEXT("Land");         Count = 2; Salt = 18; break;
    case ERKCCombatFamily::BodyFall:     Base = TEXT("KO");           Count = 2; Salt = 19; break;
    default: break;
    }
    if (!Base)
    {
        return;
    }
    const int32 Variant = PickVariant(Tick, Salt, Count) + 1;
    USoundBase* Sound = Resolve(TEXT("Combat"), Numbered(Base, Variant));
    IssueOneShot(Sound, GetSfxVolume() * VolumeScale * 0.9f, PitchFor(Tick, Salt), TEXT("combat"));
}

// ---------------------------------------------------------------------------
// Event mapping
// ---------------------------------------------------------------------------

FString URKCAudioSystem::RosterImpactStem(RKC::ERoster Roster, int32 VariantIndex)
{
    return Numbered(RosterImpactStemBase(Roster), VariantIndex);
}

int32 URKCAudioSystem::RosterImpactVariantCount()
{
    return 3;
}

// ---------------------------------------------------------------------------
// Recorded voice layer
// ---------------------------------------------------------------------------

int32 URKCAudioSystem::RecordedVoiceVariantCount(RKC::ERoster Roster, const TCHAR* Role)
{
    if (!Role)
    {
        return 0;
    }
    for (const FRecordedVoiceSet& Set : RecordedVoiceSets)
    {
        if (FCString::Strcmp(Set.Role, Role) == 0)
        {
            return (Roster == RKC::ERoster::Shadow) ? Set.Shadow : Set.Rocky;
        }
    }
    // Victory is deliberately absent from the table: the supplied recording is a
    // take of fight grunts, and a grunt is not a victory cry. A missing role
    // returns 0, which makes SetVoice use the synthesised voice and plan no
    // fallback - the honest outcome rather than a mislabelled performance.
    return 0;
}

const TArray<FString>& URKCAudioSystem::RecordedVoiceRoles()
{
    static TArray<FString> Roles;
    if (Roles.Num() == 0)
    {
        for (const FRecordedVoiceSet& Set : RecordedVoiceSets)
        {
            Roles.Add(Set.Role);
        }
    }
    return Roles;
}

FString URKCAudioSystem::RecordedVoiceFolder(RKC::ERoster Roster)
{
    // Relative to the audio content root, so the catalog entry, the runtime path
    // and the importer's destination all agree: the importer makes a folder from
    // each subdirectory of ArtSource/Audio, and the recorded takes live in
    // ArtSource/Audio/Recorded/<Fighter>.
    return FString::Printf(TEXT("Recorded/%s"), *RosterFolder(Roster));
}

// ---------------------------------------------------------------------------
// Presentation-only drivers
// ---------------------------------------------------------------------------
//
// Footsteps and the pre-round countdown are presentation timing, not simulation
// state: the simulation has no move event and no countdown tick. These live here
// rather than in the simulation so audio can never influence a match result.

void URKCAudioSystem::PlayFootstep(int32 Tick, int32 FighterSalt, float VolumeScale)
{
    const int32 Count = 4;
    const int32 Salt = 50 + FighterSalt;
    USoundBase* Sound = Resolve(TEXT("Combat"),
                                Numbered(TEXT("Footstep"), PickVariant(Tick, Salt, Count) + 1));
    if (!Sound)
    {
        return;
    }
    const FString Key = FString::Printf(TEXT("%d|footstep/%d"), Tick, FighterSalt);
    if (WouldDuplicate(Tick, Key))
    {
        ++Diagnostics.PlaysSuppressedAsDuplicate;
        return;
    }
    if (IsMuted() || GetSfxVolume() <= 0.0001f)
    {
        ++Diagnostics.PlaysSuppressedByMute;
        return;
    }
    IssueOneShot(Sound, GetSfxVolume() * VolumeScale * 0.55f, PitchFor(Tick, Salt), TEXT("footstep"));
    ++Diagnostics.FootstepPlays;
}

void URKCAudioSystem::PlayCountdownCue(int32 Tick, int32 SecondsRemaining)
{
    // The bell is the cue; the pitch rises as the round approaches so the player
    // can hear how close the start is without watching the banner.
    USoundBase* Sound = Resolve(TEXT("Stingers"), TEXT("STG_Bell"));
    if (!Sound)
    {
        return;
    }
    const FString Key = FString::Printf(TEXT("%d|countdown/%d"), Tick, SecondsRemaining);
    if (WouldDuplicate(Tick, Key))
    {
        ++Diagnostics.PlaysSuppressedAsDuplicate;
        return;
    }
    if (IsMuted() || GetSfxVolume() <= 0.0001f)
    {
        ++Diagnostics.PlaysSuppressedByMute;
        return;
    }
    const int32 Steps = FMath::Clamp(SecondsRemaining, 1, 3);
    const float Pitch = 0.94f + 0.06f * static_cast<float>(3 - Steps);
    IssueOneShot(Sound, GetSfxVolume() * 0.5f, Pitch, TEXT("countdown"));
    ++Diagnostics.CountdownCues;
}

FRKCSoundPlan URKCAudioSystem::PlanEvent(const FRKCEventContext& Context) const
{
    FRKCSoundPlan Plan;
    const bool bHeavy = (Context.Action == RKC::EAction::HeavyPunch);
    const FString AttackerVoice = RosterFolder(Context.Attacker);
    const int32 Tick = Context.Tick;

    // The voice layer, decided in one place.
    //
    // A real recorded performance is preferred, because it is an actual human
    // take. The synthesised voice of the same role rides along as the fallback, so
    // a cook that is missing the recorded layer still has voices instead of
    // silence. This function stays pure: it names both and stops there. Whether the
    // recorded take actually played, or the synthetic one had to stand in, is
    // counted by PlayEvent rather than assumed here.
    //
    // Role names are the recorded ones. SynthBase is the synthesised stem for the
    // same job, which for a reaction is 'Hurt' rather than 'React'.
    auto SetVoice = [&Plan](RKC::ERoster Who, const TCHAR* Role, const TCHAR* SynthBase,
                            int32 SynthCount, int32 InTick, int32 Salt)
    {
        const FString Folder = RosterFolder(Who);
        const int32 RecordedCount = URKCAudioSystem::RecordedVoiceVariantCount(Who, Role);
        if (RecordedCount > 0)
        {
            Plan.VoiceFolder = URKCAudioSystem::RecordedVoiceFolder(Who);
            Plan.VoiceStem = FString::Printf(TEXT("VO_Rec_%s_%s_%02d"), *Folder, Role,
                                             PickVariant(InTick, Salt, RecordedCount) + 1);
            Plan.VoiceFallbackFolder = Folder;
            Plan.VoiceFallbackStem = FString::Printf(TEXT("VO_%s_%s_%02d"), *Folder, SynthBase,
                                                     PickVariant(InTick, Salt + 500, SynthCount) + 1);
        }
        else
        {
            Plan.VoiceFolder = Folder;
            Plan.VoiceStem = FString::Printf(TEXT("VO_%s_%s_%02d"), *Folder, SynthBase,
                                             PickVariant(InTick, Salt, SynthCount) + 1);
        }
    };

    switch (Context.Type)
    {
    case RKC::ECombatEvent::AttackWhiffed:
        Plan.PrimaryFolder = TEXT("Combat");
        Plan.PrimaryStem = bHeavy
            ? Numbered(TEXT("Whiff_Heavy"), PickVariant(Tick, 21, 3) + 1)
            : Numbered(TEXT("Whiff_Light"), PickVariant(Tick, 20, 3) + 1);
        Plan.PrimaryVolume = 0.9f;
        Plan.VoiceFolder = AttackerVoice;
        Plan.VoiceStem = bHeavy
            ? FString::Printf(TEXT("VO_%s_ExertHeavy_%02d"), *AttackerVoice, PickVariant(Tick, 22, 3) + 1)
            : FString::Printf(TEXT("VO_%s_ExertLight_%02d"), *AttackerVoice, PickVariant(Tick, 23, 3) + 1);
        SetVoice(Context.Attacker, bHeavy ? TEXT("ExertHeavy") : TEXT("ExertLight"),
                 bHeavy ? TEXT("ExertHeavy") : TEXT("ExertLight"), 3, Tick, bHeavy ? 22 : 23);
        Plan.VoiceVolume = 0.55f;
        Plan.Reason = TEXT("missed attack: air-only whoosh plus the attacker's effort breath, no impact");
        break;

    case RKC::ECombatEvent::AttackHit:
        Plan.PrimaryFolder = AttackerVoice;
        Plan.PrimaryStem = RosterImpactStem(Context.Attacker, PickVariant(Tick, 24, RosterImpactVariantCount()) + 1);
        Plan.PrimaryVolume = 1.0f;
        SetVoice(Context.Defender, TEXT("React"), TEXT("Hurt"), 3, Tick, 25);
        Plan.VoiceVolume = 0.75f;
        Plan.Reason = TEXT("clean hit: attacker's character impact plus the defender's hurt reaction");
        break;

    case RKC::ECombatEvent::AttackBlocked:
        Plan.PrimaryFolder = TEXT("Combat");
        Plan.PrimaryStem = Numbered(TEXT("Block"), PickVariant(Tick, 26, 3) + 1);
        Plan.PrimaryVolume = 0.95f;
        SetVoice(Context.Defender, TEXT("ExertLight"), TEXT("ExertLight"), 3, Tick, 27);
        Plan.VoiceVolume = 0.6f;
        Plan.Reason = TEXT("blocked hit: dull glove-on-glove smack, measurably lower brightness than a clean hit");
        break;

    case RKC::ECombatEvent::GuardBroken:
        Plan.PrimaryFolder = TEXT("Combat");
        Plan.PrimaryStem = Numbered(TEXT("Impact_Heavy"), PickVariant(Tick, 28, 4) + 1);
        Plan.PrimaryVolume = 1.0f;
        SetVoice(Context.Defender, TEXT("React"), TEXT("Hurt"), 3, Tick, 29);
        Plan.VoiceVolume = 0.85f;
        Plan.Reason = TEXT("guard break: heavy neutral impact (weight and sub layer) plus a pained reaction");
        break;

    case RKC::ECombatEvent::DodgeStarted:
        Plan.PrimaryFolder = TEXT("Combat");
        Plan.PrimaryStem = Numbered(TEXT("Dodge"), PickVariant(Tick, 30, 3) + 1);
        Plan.PrimaryVolume = 0.8f;
        SetVoice(Context.Attacker, TEXT("ExertLight"), TEXT("ExertLight"), 3, Tick, 31);
        Plan.VoiceVolume = 0.4f;
        Plan.Reason = TEXT("dodge: cloth swish, quieter than an impact so a whiffed attack stays readable");
        break;

    case RKC::ECombatEvent::Knockdown:
        Plan.PrimaryFolder = TEXT("Combat");
        Plan.PrimaryStem = Numbered(TEXT("KO"), PickVariant(Tick, 32, 2) + 1);
        Plan.PrimaryVolume = 1.0f;
        SetVoice(Context.Defender, TEXT("React"), TEXT("Hurt"), 3, Tick, 33);
        Plan.VoiceVolume = 0.9f;
        Plan.Reason = TEXT("knockdown: canvas body fall plus a heavy reaction");
        break;

    case RKC::ECombatEvent::GetUp:
        Plan.PrimaryFolder = TEXT("Combat");
        Plan.PrimaryStem = Numbered(TEXT("Land"), PickVariant(Tick, 34, 2) + 1);
        Plan.PrimaryVolume = 0.7f;
        Plan.Reason = TEXT("getting up: canvas rustle, quieter than the fall itself");
        break;

    case RKC::ECombatEvent::Knockout:
        Plan.PrimaryFolder = TEXT("Combat");
        Plan.PrimaryStem = Numbered(TEXT("KO"), PickVariant(Tick, 35, 2) + 1);
        Plan.PrimaryVolume = 1.0f;
        SetVoice(Context.Defender, TEXT("React"), TEXT("Hurt"), 3, Tick, 36);
        Plan.VoiceVolume = 1.0f;
        Plan.Reason = TEXT("knockout: body fall plus the losing fighter's reaction; the KO stinger is a separate cue");
        break;

    case RKC::ECombatEvent::RoundStarted:
        Plan.PrimaryFolder = TEXT("Stingers");
        Plan.PrimaryStem = TEXT("STG_RoundStart");
        Plan.Reason = TEXT("round start cue");
        break;

    case RKC::ECombatEvent::RoundEnded:
        Plan.PrimaryFolder = TEXT("Stingers");
        Plan.PrimaryStem = TEXT("STG_RoundEnd");
        Plan.Reason = TEXT("round end cue");
        break;

    case RKC::ECombatEvent::MatchEnded:
        Plan.PrimaryFolder = TEXT("Stingers");
        Plan.PrimaryStem = Context.Value > 0.5f ? TEXT("STG_Victory") : TEXT("STG_Defeat");
        // The victory call stays on the synthesised voice on purpose. The only
        // recorded material supplied is a take of fight grunts; a grunt is not a
        // victory cry, and pitching one up into a triumph would be inventing a
        // performance and then calling it recorded. RecordedCount is 0 for the
        // Victory role, so SetVoice uses the synthetic voice and plans no fallback.
        if (Context.Value > 0.5f)
        {
            SetVoice(Context.Attacker, TEXT("Victory"), TEXT("Victory"), 2, Tick, 37);
        }
        Plan.VoiceVolume = 0.9f;
        Plan.Reason = TEXT("match end: victory or defeat cue, plus the winner's victory call");
        break;

    case RKC::ECombatEvent::StaminaExhausted:
        SetVoice(Context.Attacker, TEXT("ExertHeavy"), TEXT("ExertHeavy"), 3, Tick, 38);
        Plan.VoiceVolume = 0.7f;
        Plan.Reason = TEXT("stamina exhausted: a heavy breath and nothing else, so it cannot read as a hit");
        break;

    case RKC::ECombatEvent::AttackStarted:
        // Deliberately silent: the swing itself is heard when it whiffs or
        // connects, so a swing cannot produce two sounds for one action.
        Plan.Reason = TEXT("silent by design: the swing is voiced by its whiff or its impact");
        break;

    default:
        Plan.Reason = TEXT("no audio mapping");
        break;
    }

    return Plan;
}

bool URKCAudioSystem::WouldDuplicate(int32 Tick, const FString& SoundKey)
{
    const FString Key = FString::Printf(TEXT("%d|%s"), Tick, *SoundKey);
    if (RecentKeys.Contains(Key))
    {
        return true;
    }
    if (RecentKeys.Num() == 0)
    {
        RecentKeys.Init(FString(), DuplicateGuardDepth);
    }
    RecentKeys[RecentKeyCursor] = Key;
    RecentKeyCursor = (RecentKeyCursor + 1) % RecentKeys.Num();
    return false;
}

FRKCSoundPlan URKCAudioSystem::PlayEvent(const FRKCEventContext& Context)
{
    FRKCSoundPlan Plan = PlanEvent(Context);
    ++Diagnostics.EventsPlanned;

    int32 IssuedForEvent = 0;

    auto Issue = [&](const FString& Folder, const FString& Stem, float CategoryVolume, float Scale, int32 Salt)
    {
        if (Stem.IsEmpty())
        {
            return;
        }
        USoundBase* Sound = Resolve(Folder, Stem);
        if (!Sound)
        {
            return;
        }
        // The side is part of the key, so two fighters landing a hit on the same
        // tick both sound, while a repeated event for the same fighter does not.
        const FString Key = FString::Printf(TEXT("%d|%s/%s"),
                                            static_cast<int32>(Context.Side), *Folder, *Stem);
        if (WouldDuplicate(Context.Tick, Key))
        {
            ++Diagnostics.PlaysSuppressedAsDuplicate;
            return;
        }
        if (IsMuted() || CategoryVolume <= 0.0001f)
        {
            ++Diagnostics.PlaysSuppressedByMute;
            return;
        }
        IssueOneShot(Sound, CategoryVolume * Scale, PitchFor(Context.Tick, Salt), TEXT("event"));
        ++IssuedForEvent;
    };

    if (Plan.HasPrimary())
    {
        Issue(Plan.PrimaryFolder, Plan.PrimaryStem, GetSfxVolume(), Plan.PrimaryVolume, 41);
        ++Diagnostics.EventsWithPrimary;
    }

    // The voice goes through its own path because the preferred asset is a
    // recorded take that may not be in this cook. Three outcomes, all counted:
    // the recorded take played, the synthesised voice of the same role stood in,
    // or nothing resolved and the event is a real hole.
    if (Plan.HasVoice())
    {
        ++Diagnostics.EventsWithVoice;
        const FString Key = FString::Printf(TEXT("%d|%s/%s"),
                                            static_cast<int32>(Context.Side),
                                            *Plan.VoiceFolder, *Plan.VoiceStem);
        if (WouldDuplicate(Context.Tick, Key))
        {
            ++Diagnostics.PlaysSuppressedAsDuplicate;
        }
        else if (IsMuted() || GetVoiceVolume() <= 0.0001f)
        {
            ++Diagnostics.PlaysSuppressedByMute;
        }
        else
        {
            const bool bPrefersRecorded = Plan.VoiceFolder.StartsWith(TEXT("Recorded/"));
            USoundBase* Voice = Resolve(Plan.VoiceFolder, Plan.VoiceStem);
            if (!Voice && Plan.HasVoiceFallback())
            {
                Voice = Resolve(Plan.VoiceFallbackFolder, Plan.VoiceFallbackStem);
                if (Voice)
                {
                    ++Diagnostics.VoiceFallbacksToSynthetic;
                }
            }
            if (!Voice)
            {
                ++Diagnostics.VoicePlansUnresolved;
                UE_LOG(LogRockyKombat, Warning,
                       TEXT("Audio: no voice resolved for %s/%s and no fallback played (%s/%s)."),
                       *Plan.VoiceFolder, *Plan.VoiceStem,
                       *Plan.VoiceFallbackFolder, *Plan.VoiceFallbackStem);
            }
            else
            {
                IssueOneShot(Voice, GetVoiceVolume() * Plan.VoiceVolume,
                             PitchFor(Context.Tick, 42), TEXT("voice"));
                ++IssuedForEvent;
                if (bPrefersRecorded)
                {
                    // Only the recorded branch is counted here. If the fallback
                    // ran, bPrefersRecorded is still true but the recorded asset
                    // did not play, and a recorded play is what this counts.
                    if (Voice != nullptr
                        && Resolve(Plan.VoiceFolder, Plan.VoiceStem) == Voice)
                    {
                        ++Diagnostics.RecordedVoicePlays;
                    }
                }
            }
        }
    }

    Diagnostics.SoundsIssuedPerEventMax = FMath::Max(Diagnostics.SoundsIssuedPerEventMax, IssuedForEvent);
    Diagnostics.PlaysByEventType.FindOrAdd(EventTypeName(Context.Type)) += IssuedForEvent;

    return Plan;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void URKCAudioSystem::OnGamePaused()
{
    if (bPaused)
    {
        return;
    }
    bPaused = true;
    ++Diagnostics.PauseRequests;
    bMusicFrozenByPause = false;
    if (MusicComponent && MusicComponent->IsPlaying())
    {
        MusicComponent->SetPaused(true);
        bMusicFrozenByPause = true;
        ++Diagnostics.MusicPauses;
    }
    if (AmbienceComponent && AmbienceComponent->IsPlaying())
    {
        AmbienceComponent->SetPaused(true);
    }
}

void URKCAudioSystem::OnGameResumed()
{
    if (!bPaused)
    {
        return;
    }
    bPaused = false;
    ++Diagnostics.ResumeRequests;
    if (bMusicFrozenByPause && MusicComponent)
    {
        MusicComponent->SetPaused(false);
        ++Diagnostics.MusicResumes;
    }
    if (AmbienceComponent)
    {
        AmbienceComponent->SetPaused(false);
    }
    bMusicFrozenByPause = false;
}

void URKCAudioSystem::OnLeftGameplayToFrontEnd()
{
    StopAmbience();
    if (CurrentMusic == ERKCMusic::Arena)
    {
        PlayMusic(ERKCMusic::Menu, 0.4f);
    }
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

TArray<FString> URKCAudioSystem::DescribeCatalog() const
{
    TArray<FString> Lines;
    for (const FRKCAudioCatalogEntry& Entry : RKCAudioCatalog())
    {
        const FString Path = BuildPath(Entry.Folder, Entry.Stem);
        const TObjectPtr<USoundBase>* Found = SoundCache.Find(FName(*Path));
        const bool bLoaded = (Found != nullptr) && (Found->Get() != nullptr);
        Lines.Add(FString::Printf(TEXT("%s/%s%s -> %s [%s]"),
                                  *Entry.Folder, *Entry.Stem,
                                  Entry.bLooping ? TEXT(" (looping)") : TEXT(""),
                                  *Path,
                                  bLoaded ? TEXT("loaded") : TEXT("MISSING")));
    }
    return Lines;
}
