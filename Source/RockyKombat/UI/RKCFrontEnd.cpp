// RKCFrontEnd.cpp
//
// The front end. Pure logic: it decides, it never draws (ARKCHUD draws) and it
// never simulates (ARKCGameMode simulates). That split is why the whole menu can
// be exercised in a headless run - see the front-end section of the in-engine
// self-test - and why a menu bug can never change a match result.
//
// SCREEN GRAPH
//   Boot -> Title -> Menu
//   Menu -> Experience -> Arrangement -> [LocalVersusJoin]
//                                     -> CharacterSelect (A, then B)
//                                     -> [Difficulty | TrainingSetup]
//                                     -> Confirm -> Loading -> Versus -> Match
//   Match -> (Pause) or (Results) -> Rematch / Change fighters / Menu
//
// Every Back goes to the previous decision, and every choice made so far is
// kept: stepping back into CharacterSelect shows the fighter you already picked.

#include "RKCFrontEnd.h"
#include "Online/RKCOnlineClient.h"

namespace RKCFrontEnd
{
namespace
{
/** Item ids. String ids rather than indices, so inserting a row cannot silently
 *  re-point an existing handler. */
const TCHAR* const IdPlay = TEXT("play");
const TCHAR* const IdTraining = TEXT("training");
const TCHAR* const IdControls = TEXT("controls");
const TCHAR* const IdOptions = TEXT("options");
const TCHAR* const IdQuit = TEXT("quit");
const TCHAR* const IdBack = TEXT("back");

const TCHAR* const IdExpRba = TEXT("exp_rba");
const TCHAR* const IdExpStreet = TEXT("exp_street");
const TCHAR* const IdExpUniverse = TEXT("exp_universe");

const TCHAR* const IdArrCpu = TEXT("arr_cpu");
const TCHAR* const IdArrLocal = TEXT("arr_local");
const TCHAR* const IdArrTraining = TEXT("arr_training");
const TCHAR* const IdArrOnline = TEXT("arr_online");

const TCHAR* const IdRocky = TEXT("fighter_rocky");
const TCHAR* const IdShadow = TEXT("fighter_shadow");

const TCHAR* const IdDiffRookie = TEXT("diff_rookie");
const TCHAR* const IdDiffContender = TEXT("diff_contender");
const TCHAR* const IdDiffChampion = TEXT("diff_champion");

const TCHAR* const IdDummyStationary = TEXT("dummy_stationary");
const TCHAR* const IdDummyBlocking = TEXT("dummy_blocking");
const TCHAR* const IdTrainingHitboxes = TEXT("training_hitboxes");
const TCHAR* const IdTrainingFrameData = TEXT("training_framedata");
const TCHAR* const IdTrainingInfiniteHealth = TEXT("training_infhealth");
const TCHAR* const IdTrainingInfiniteStamina = TEXT("training_infstamina");

const TCHAR* const IdFight = TEXT("fight");
const TCHAR* const IdChangeFighters = TEXT("change_fighters");

const TCHAR* const IdRematch = TEXT("rematch");
const TCHAR* const IdMenu = TEXT("main_menu");

const TCHAR* const IdResume = TEXT("resume");
const TCHAR* const IdQuitToMenu = TEXT("quit_to_menu");
const TCHAR* const IdRestartMatch = TEXT("restart_match");

const TCHAR* const IdMute = TEXT("opt_mute");
const TCHAR* const IdMusicVol = TEXT("opt_music");
const TCHAR* const IdSfxVol = TEXT("opt_sfx");
const TCHAR* const IdVoiceVol = TEXT("opt_voice");
const TCHAR* const IdUiVol = TEXT("opt_ui");
const TCHAR* const IdTouch = TEXT("opt_touch");

const TCHAR* const IdConfirmQuit = TEXT("confirm_quit");
const TCHAR* const IdCancelQuit = TEXT("cancel_quit");

/** The honest names of the two experiences that are not built yet. */
const TCHAR* const NotAvailableStreet = TEXT("Not available in this build (Street Fights rules are not implemented)");
const TCHAR* const NotAvailableUniverse = TEXT("Not available in this build (Rocky Universe is not implemented)");
const TCHAR* const NotAvailableOnline = TEXT("Not available in this build (no networking)");

FString RosterName(RKC::ERoster Who)
{
    return RKC::GetRosterDisplayName(Who);
}

FString DifficultyName(RKC::EBotDifficulty Difficulty)
{
    switch (Difficulty)
    {
    case RKC::EBotDifficulty::Rookie:
        return TEXT("ROOKIE");
    case RKC::EBotDifficulty::Champion:
        return TEXT("CHAMPION");
    default:
        return TEXT("CONTENDER");
    }
}

FString DifficultyBlurb(RKC::EBotDifficulty Difficulty)
{
    switch (Difficulty)
    {
    case RKC::EBotDifficulty::Rookie:
        return TEXT("Slower reactions, wider spacing, punishes nothing. Good for learning the controls.");
    case RKC::EBotDifficulty::Champion:
        return TEXT("Fast reactions, tight spacing, punishes whiffs immediately.");
    default:
        return TEXT("Balanced reactions and spacing. Blocks most one-shot pokes.");
    }
}
}

// ---------------------------------------------------------------------------
// Construction and configuration
// ---------------------------------------------------------------------------

FMachine::FMachine()
{
    Request.RulesSummary = TEXT("3 rounds - 30 s - 3-knockdown rule");
    RebuildItems();
}

void FMachine::Init(bool bInReturningPlayer)
{
    bReturningPlayer = bInReturningPlayer;
    BootSeconds = bReturningPlayer ? BootSecondsReturning : BootSecondsFirstRun;
    Screen = EScreen::Boot;
    ScreenAgeSeconds = 0.0f;
    Selection = 0;
    LocalReadyPresses = 0;
    LocalReadySeconds = 0.0f;
    bVersusChosen = false;
    RebuildItems();
    ApplyScreenAudio();
}

void FMachine::SetAutoPilot(bool bInAutoPilot, float SecondsPerScreen)
{
    bAutoPilot = bInAutoPilot;
    AutoPilotSecondsPerScreen = FMath::Max(0.3f, SecondsPerScreen);
    AutoPilotAccumulator = 0.0f;
}

// ---------------------------------------------------------------------------
// The one frame tick
// ---------------------------------------------------------------------------

void FMachine::Tick(float DeltaSeconds, const FInput& Input, bool bWarmupComplete)
{
    DeltaSeconds = FMath::Clamp(DeltaSeconds, 0.0f, 0.25f);
    ScreenAgeSeconds += DeltaSeconds;

    if (LocalReadyPresses == 1)
    {
        LocalReadySeconds += DeltaSeconds;
        if (LocalReadySeconds > LocalRematchWindowSeconds)
        {
            // The other local player never confirmed; drop the half-press rather
            // than starting a match one person did not agree to.
            LocalReadyPresses = 0;
            LocalReadySeconds = 0.0f;
            RebuildItems();
        }
    }

    // ---- pause is available on every gameplay screen -----------------------
    if (Input.bPause)
    {
        if (Screen == EScreen::Match || Screen == EScreen::Results)
        {
            OnPauseRequested();
        }
        else if (Screen == EScreen::Paused)
        {
            OnResumeRequested();
        }
    }

    // One press must produce at most one screen action. The splash cards below
    // consume accept/back to move on; the same press must not then be handed to
    // the screen they move to, or a single keypress on the title card would
    // activate the main menu's first row and skip the menu entirely.
    bool bAcceptSpent = false;
    bool bBackSpent = false;

    if (Online) {
        if (Online->ConsumeEnded() && Request.Arrangement == EArrangement::PrivateOnline) SetScreen(EScreen::OnlineLobby, TEXT("online match ended"));
        if (Online->ConsumeMatch()) {
            Request.Arrangement = EArrangement::PrivateOnline;
            Request.FighterA = static_cast<RKC::ERoster>(Online->Fighters[0]);
            Request.FighterB = static_cast<RKC::ERoster>(Online->Fighters[1]);
            Request.bHumanA = Request.bHumanB = true;
            Request.TrainingA = Request.TrainingB = RKC::ETrainingMode::Off;
            Request.RulesSummary = TEXT("Unranked online - 3 rounds - 30 seconds");
            LoadingProgress = LoadingSeconds = 0;
            SetScreen(EScreen::Loading, TEXT("online invitation accepted"));
        }
        if (Screen == EScreen::OnlineLobby && OnlineRevision != Online->GetRevision()) { OnlineRevision = Online->GetRevision(); RebuildItems(); }
    }
    switch (Screen)
    {
    case EScreen::Boot:
        // Skippable, and never held on a first run longer than its sting.
        if (ScreenAgeSeconds >= BootSeconds ||
            (ScreenAgeSeconds >= BootSkippableAfter && (Input.bAccept || Input.bBack)))
        {
            bAcceptSpent = bAcceptSpent || Input.bAccept;
            bBackSpent = bBackSpent || Input.bBack;
            SetScreen(EScreen::Title, TEXT("startup sting finished"));
        }
        break;

    case EScreen::Title:
    {
        const float Auto = bReturningPlayer ? TitleAutoAdvanceReturning : TitleAutoAdvanceFirstRun;
        if (ScreenAgeSeconds >= Auto || Input.bAccept || Input.bBack)
        {
            bAcceptSpent = bAcceptSpent || Input.bAccept;
            bBackSpent = bBackSpent || Input.bBack;
            SetScreen(EScreen::Menu, bReturningPlayer ? TEXT("returning player, skipping title")
                                                      : TEXT("title acknowledged"));
        }
        break;
    }

    case EScreen::Loading:
    {
        LoadingSeconds += DeltaSeconds;
        const bool bWorkDone = LoadingProgress >= 1.0f && bWarmupComplete;
        if (bWorkDone && LoadingSeconds >= MinimumLoadingSeconds)
        {
            SetScreen(EScreen::Versus, TEXT("loading complete and render warm"));
        }
        break;
    }

    case EScreen::Versus:
        if (ScreenAgeSeconds >= VersusSeconds)
        {
            bMatchStartRequested = true;
            SetScreen(EScreen::Match, TEXT("versus card finished"));
        }
        break;

    default:
        break;
    }

    // ---- navigation --------------------------------------------------------
    // Loading and Versus are timed cards: they ignore navigation so a held key
    // during the countdown cannot skip the introductions.
    if (Screen != EScreen::Loading && Screen != EScreen::Versus && Screen != EScreen::Boot)
    {
        ApplyNavigate(Input.Navigate);

        if (Input.Adjust != 0)
        {
            if (Selection >= 0 && Selection < Items.Num())
            {
                AdjustItem(Items[Selection], Input.Adjust);
            }
        }

        for (const FVector2D& Tap : Input.Taps)
        {
            HandleTap(Tap, Input.ViewportSize);
        }

        if (Input.bAccept && !bAcceptSpent)
        {
            ++Diag.Accepts;
            if (Selection >= 0 && Selection < Items.Num())
            {
                const FMenuItem Chosen = Items[Selection];
        ActivateItem(Chosen);
            }
            else
            {
                ++Diag.RejectedAccepts;
            }
        }
        if (Input.bBack && !bBackSpent)
        {
            ++Diag.Backs;
            switch (Screen)
            {
            case EScreen::Paused:
                OnResumeRequested();
                break;
            case EScreen::Results:
                ReturnToMainMenu();
                break;
            case EScreen::Menu:
                SetScreen(EScreen::Quit, TEXT("exit requested from the main menu"));
                break;
            case EScreen::Options:
            case EScreen::Controls:
                SetScreen(EScreen::Menu, TEXT("back to main menu"));
                break;
            case EScreen::Experience:
                SetScreen(EScreen::Menu, TEXT("back to main menu"));
                break;
            case EScreen::Arrangement:
                SetScreen(EScreen::Experience, TEXT("back to experience selection"));
                break;
            case EScreen::OnlineLobby:
                if (Online) Online->Leave();
                SetScreen(EScreen::Arrangement, TEXT("back from online lobby"));
                break;
            case EScreen::LocalVersusJoin:
                SetScreen(EScreen::Arrangement, TEXT("back to arrangement selection"));
                break;
            case EScreen::CharacterSelect:
                if (CharStage > 0)
                {
                    --CharStage;
                    RebuildItems();
                    ++Diag.SelectionsPreserved;
                }
                else
                {
                    SetScreen(EScreen::Arrangement, TEXT("back to arrangement selection"));
                }
                break;
            case EScreen::Difficulty:
                CharStage = 1;
                SetScreen(EScreen::CharacterSelect, TEXT("back to second fighter"));
                break;
            case EScreen::TrainingSetup:
                SetScreen(EScreen::Arrangement, TEXT("back to arrangement selection"));
                break;
            case EScreen::Confirm:
                if (Request.Arrangement == EArrangement::Training)
                {
                    SetScreen(EScreen::TrainingSetup, TEXT("back to training setup"));
                }
                else
                {
                    SetScreen(EScreen::Difficulty, TEXT("back to difficulty"));
                }
                break;
            case EScreen::Quit:
                SetScreen(EScreen::Menu, TEXT("exit cancelled"));
                break;
            default:
                break;
            }
        }
    }

    TickAutoPilot(DeltaSeconds);
}

void FMachine::TickAutoPilot(float DeltaSeconds)
{
    if (!bAutoPilot || Screen == EScreen::Match || Screen == EScreen::Quit)
    {
        return;
    }
    // Timed cards advance themselves; the autopilot only has to answer screens
    // that wait for a person.
    if (Screen == EScreen::Boot || Screen == EScreen::Loading || Screen == EScreen::Versus)
    {
        AutoPilotAccumulator = 0.0f;
        return;
    }

    AutoPilotAccumulator += DeltaSeconds;
    if (AutoPilotAccumulator < AutoPilotSecondsPerScreen)
    {
        return;
    }
    AutoPilotAccumulator = 0.0f;

    // On the results screen the demonstration run goes back to the menu rather
    // than looping matches forever.
    if (Screen == EScreen::Results)
    {
        ReturnToMainMenu();
        return;
    }
    if (Screen == EScreen::Paused)
    {
        OnResumeRequested();
        return;
    }

    if (Selection >= 0 && Selection < Items.Num())
    {
        const FMenuItem Chosen = Items[Selection];
        ActivateItem(Chosen);
    }
}

// ---------------------------------------------------------------------------
// Screen changes
// ---------------------------------------------------------------------------

void FMachine::SetScreen(EScreen NewScreen, const TCHAR* Reason)
{
    if (Screen == NewScreen)
    {
        return;
    }

    if (Online && Online->IsInMatch() && (NewScreen == EScreen::Menu || NewScreen == EScreen::Arrangement || NewScreen == EScreen::CharacterSelect)) Online->Leave();
    Screen = NewScreen;
    ScreenAgeSeconds = 0.0f;
    ++Diag.ScreenChanges;

    if (Diag.TransitionLog.Num() < 256)
    {
        Diag.TransitionLog.Add(FString::Printf(TEXT("%s -> %s (%s)"), *ScreenName(Screen),
                                               *ScreenName(NewScreen), Reason));
    }

    Selection = 0;
    if (NewScreen == EScreen::Loading)
    {
        LoadingSeconds = 0.0f;
    }
    if (NewScreen == EScreen::CharacterSelect && CharStage == 0)
    {
        // Entering character select for the first person: keep whatever the
        // player already chose rather than resetting to the leftmost fighter.
    }
    if (NewScreen == EScreen::Arrangement)
    {
        LocalReadyPresses = 0;
        LocalReadySeconds = 0.0f;
    }

    RebuildItems();
    ApplyScreenAudio();
}

void FMachine::ApplyScreenAudio()
{
    if (!Audio)
    {
        return;
    }

    switch (Screen)
    {
    case EScreen::Boot:
        Audio->StopMusic(0.2f);
        break;
    case EScreen::Title:
        Audio->PlayMusic(ERKCMusic::Title, 0.6f);
        break;
    case EScreen::Match:
        Audio->PlayMusic(ERKCMusic::Arena, 0.8f);
        Audio->PlayAmbience();
        break;
    case EScreen::Results:
        Audio->StopMusic(0.6f);
        Audio->StopAmbience();
        break;
    case EScreen::Versus:
        Audio->PlayStinger(ERKCStinger::RoundStart);
        break;
    case EScreen::Quit:
        Audio->StopMusic(0.4f);
        break;
    default:
        Audio->PlayMusic(ERKCMusic::Menu, 0.5f);
        break;
    }
}

void FMachine::OnPauseRequested()
{
    // A peer cannot freeze the other player's simulation. Back leaves the match.
    if (Request.Arrangement == EArrangement::PrivateOnline && Online && Online->IsInMatch()) { Online->Leave(); SetScreen(EScreen::OnlineLobby, TEXT("left online match")); return; }
    if (Screen == EScreen::Paused)
    {
        return;
    }
    ScreenBeforePause = Screen;
    ++Diag.Pauses;
    // Pausing intentionally does not clear the chosen setup: resuming must feel
    // like the same match, not a new decision.
    Screen = EScreen::Paused;
    ScreenAgeSeconds = 0.0f;
    RebuildItems();
    if (Audio)
    {
        Audio->PlayUi(ERKCUiSound::Select);
        Audio->OnGamePaused();
    }
}

void FMachine::OnResumeRequested()
{
    if (Screen != EScreen::Paused)
    {
        return;
    }
    ++Diag.Resumes;
    const EScreen Target = ScreenBeforePause;
    Screen = Target;
    ScreenAgeSeconds = 0.0f;
    Selection = 0;
    RebuildItems();
    if (Audio)
    {
        Audio->OnGameResumed();
        Audio->PlayUi(ERKCUiSound::Back);
    }
}

void FMachine::ReturnToMainMenu()
{
    LocalReadyPresses = 0;
    LocalReadySeconds = 0.0f;
    bMatchStartRequested = false;
    bRematchRequested = false;
    ResultText.Reset();
    CharStage = 0;
    if (Audio)
    {
        Audio->OnLeftGameplayToFrontEnd();
    }
    SetScreen(EScreen::Menu, TEXT("returned to main menu"));
}

void FMachine::NotifyMatchStarted()
{
    if (Diag.TransitionLog.Num() < 256)
    {
        Diag.TransitionLog.Add(TEXT("match started: gameplay input and AI are enabled"));
    }
}

void FMachine::NotifyMatchEnded(bool bWinnerIsSideAIn, const FString& InWinnerName)
{
    bWinnerIsSideA = bWinnerIsSideAIn;
    WinnerName = InWinnerName;
    ResultText = InWinnerName.IsEmpty() ? TEXT("DRAW") : InWinnerName;

    if (Audio)
    {
        Audio->PlayStinger(bWinnerIsSideAIn ? ERKCStinger::Victory : ERKCStinger::Defeat);
    }

    LocalReadyPresses = 0;
    LocalReadySeconds = 0.0f;
    SetScreen(EScreen::Results, TEXT("match resolved"));
}

// ---------------------------------------------------------------------------
// Requests consumed by the director
// ---------------------------------------------------------------------------

bool FMachine::ConsumeMatchStartRequest()
{
    const bool bValue = bMatchStartRequested;
    bMatchStartRequested = false;
    return bValue;
}

bool FMachine::ConsumeRematchRequest()
{
    const bool bValue = bRematchRequested;
    bRematchRequested = false;
    return bValue;
}

bool FMachine::ConsumeQuitRequest()
{
    const bool bValue = bQuitRequested;
    bQuitRequested = false;
    return bValue;
}

void FMachine::SetLoadingProgress(float InProgress)
{
    LoadingProgress = FMath::Clamp(InProgress, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Item construction
// ---------------------------------------------------------------------------

namespace
{
FMenuItem MakeItem(const FString& Id, const FString& Label, const FString& Hint, double Y,
                   bool bEnabled = true, bool bAdjustable = false)
{
    FMenuItem Item;
    Item.Id = Id;
    Item.Label = Label;
    Item.Hint = Hint;
    Item.Y = Y;
    Item.bEnabled = bEnabled;
    Item.bAdjustable = bAdjustable;
    return Item;
}
}

void FMachine::RebuildItems()
{
    Items.Reset();
    BodyLines.Reset();
    FooterLines.Reset();
    LeftCaption.Reset();
    RightCaption.Reset();

    double Y = FirstItemY;
    auto NextY = [&Y]()
    {
        const double Value = Y;
        Y += ItemSpacingY;
        return Value;
    };

    switch (Screen)
    {
    case EScreen::OnlineLobby:
        if (Online) {
            BodyLines.Add(Online->Status);
            if (Online->Status.StartsWith(TEXT("Invitation sent"))) Items.Add(MakeItem(TEXT("online_decline"), TEXT("CANCEL INVITATION"), TEXT("Choose another player"), NextY()));
            if (!Online->IncomingId.IsEmpty()) {
                Items.Add(MakeItem(TEXT("online_accept"), TEXT("ACCEPT: ") + Online->IncomingName, TEXT("Start an unranked match"), NextY()));
                Items.Add(MakeItem(TEXT("online_decline"), TEXT("DECLINE INVITATION"), TEXT("Stay in the lobby"), NextY()));
            }
            for (const auto& P : Online->Players) Items.Add(MakeItem(TEXT("peer:") + P.Id, P.Name + (P.bBusy ? TEXT("  / BUSY") : TEXT("  / AVAILABLE")), P.Fighter == 0 ? TEXT("Rocky") : TEXT("Shadow"), NextY(), !P.bBusy));
            if (Online->IsConnected() && Online->Players.IsEmpty()) BodyLines.Add(TEXT("No other players connected. Ask your opponent to open Online Versus."));
            if (!Online->IsConnected()) Items.Add(MakeItem(TEXT("online_connect"), TEXT("CONNECT"), TEXT("Refresh the connection"), NextY(), Online->IsConfigured()));
        }
        Items.Add(MakeItem(IdBack, TEXT("BACK"), TEXT("Return to play selection"), NextY()));
        break;
    case EScreen::Menu:
        Items.Add(MakeItem(IdPlay, TEXT("PLAY"), TEXT("Choose how you want to fight"), NextY()));
        Items.Add(MakeItem(IdTraining, TEXT("TRAINING"), TEXT("Learn the controls against a stationary dummy"), NextY()));
        Items.Add(MakeItem(IdControls, TEXT("CONTROLS / HOW TO PLAY"), TEXT("Every binding, on keyboard and on touch"), NextY()));
        Items.Add(MakeItem(IdOptions, TEXT("SETTINGS"), TEXT("Volume, mute and touch controls"), NextY()));
        Items.Add(MakeItem(IdQuit, TEXT("EXIT GAME"), TEXT("Close Rocky Kombat"), NextY()));
        FooterLines.Add(TEXT("RBA / UNRANKED ALPHA"));
        FooterLines.Add(TEXT("Navigate W/S or Arrows - Confirm ENTER - Back ESC"));
        break;

    case EScreen::Experience:
        Items.Add(MakeItem(IdExpRba, TEXT("ROCKY BOXING ARENA"), TEXT("Boxing rules in the Midnight Rooftop Ring"), NextY()));
        Items.Add(MakeItem(IdExpStreet, TEXT("STREET FIGHTS"), NotAvailableStreet, NextY(), false));
        Items.Add(MakeItem(IdExpUniverse, TEXT("ROCKY UNIVERSE"), NotAvailableUniverse, NextY(), false));
        Items.Add(MakeItem(IdBack, TEXT("BACK"), TEXT("Return to the main menu"), NextY()));
        BodyLines.Add(TEXT("What kind of fight? This stays boxing-only in this build."));
        break;

    case EScreen::Arrangement:
        Items.Add(MakeItem(IdArrCpu, TEXT("PLAY AGAINST COMPUTER"), TEXT("One player, one CPU opponent"), NextY()));
        Items.Add(MakeItem(IdArrLocal, TEXT("PLAY WITH ANOTHER PLAYER"), TEXT("Local versus on this device - two humans, no AI"), NextY()));
        Items.Add(MakeItem(IdArrTraining, TEXT("TRAINING"), TEXT("Stationary dummy, no damage to you"), NextY()));
        Items.Add(MakeItem(IdArrOnline, TEXT("ONLINE VERSUS"), TEXT("Invite a connected player over the internet"), NextY()));
        Items.Add(MakeItem(IdBack, TEXT("BACK"), TEXT("Return to experience selection"), NextY()));
        break;

    case EScreen::LocalVersusJoin:
    {
        const bool bNumpad = true;
        Items.Add(MakeItem(IdBack, TEXT("CONTINUE"), TEXT("Both players confirmed on this keyboard"), NextY()));
        BodyLines.Add(TEXT("LOCAL VERSUS - device assignment"));
        BodyLines.Add(TEXT("PLAYER 1   Keyboard, left hand   A / D move, S block, Z jab, X heavy, C dodge"));
        BodyLines.Add(bNumpad
                          ? TEXT("PLAYER 2   Keyboard, right hand  Left / Right move, Down block, Numpad 1/2/3 jab/heavy/dodge")
                          : TEXT("PLAYER 2   Keyboard, right hand  Left / Right move, Down block, , . / jab/heavy/dodge"));
        BodyLines.Add(TEXT("No numpad? Use the comma, period and slash keys for Player 2 attacks."));
        BodyLines.Add(TEXT("Neither fighter is controlled by AI in this mode, and neither moves until its own keys are pressed."));
        BodyLines.Add(TEXT("One keyboard cannot be split across two PCs or two phones. On a phone, two players sharing one"));
        BodyLines.Add(TEXT("small screen is not a supported default: use Play Against Computer, or a controller per player."));
        BodyLines.Add(TEXT("Local controllers: not implemented in this build, so it is not offered as a choice."));
        break;
    }

    case EScreen::CharacterSelect:
    {
        Items.Add(MakeItem(IdRocky, RosterName(RKC::ERoster::Rocky).ToUpper(), CharacterBlurb(RKC::ERoster::Rocky), NextY()));
        Items.Add(MakeItem(IdShadow, RosterName(RKC::ERoster::Shadow).ToUpper(), CharacterBlurb(RKC::ERoster::Shadow), NextY()));
        Items.Add(MakeItem(IdBack, TEXT("BACK"), TEXT("Return to the previous choice"), NextY()));

        const bool bChoosingHuman = (CharStage == 0);
        const FString Who = bChoosingHuman ? SideLabel(Request.bHumanA, 1, false)
                                           : SideLabel(Request.bHumanB, 2, !Request.bHumanB);
        BodyLines.Add(FString::Printf(TEXT("Choose the fighter for %s"), *Who));
        if (!bChoosingHuman && Request.Arrangement == EArrangement::VsComputer)
        {
            BodyLines.Add(FString::Printf(TEXT("CPU difficulty: %s (chosen next)"),
                                          *DifficultyName(Request.Difficulty)));
        }
        if (Request.FighterA == Request.FighterB)
        {
            BodyLines.Add(TEXT("MIRROR MATCH: both fighters are the same character. Side markers,"));
            BodyLines.Add(TEXT("a red/blue health frame and the YOU/CPU labels keep them apart."));
        }
        else
        {
            BodyLines.Add(TEXT("RBA is a boxing ruleset: both fighters punch, block and dodge only."));
        }

        LeftCaption = FString::Printf(TEXT("%s (%s)"), *RosterName(Request.FighterA),
                                      Request.bHumanA ? TEXT("HUMAN") : TEXT("CPU"));
        RightCaption = FString::Printf(TEXT("%s (%s)"), *RosterName(Request.FighterB),
                                       Request.bHumanB ? TEXT("HUMAN") : TEXT("CPU"));
        break;
    }

    case EScreen::Difficulty:
        Items.Add(MakeItem(IdDiffRookie, TEXT("ROOKIE"), DifficultyBlurb(RKC::EBotDifficulty::Rookie), NextY(),
                           true, true));
        Items.Add(MakeItem(IdDiffContender, TEXT("CONTENDER"), DifficultyBlurb(RKC::EBotDifficulty::Contender), NextY(),
                           true, true));
        Items.Add(MakeItem(IdDiffChampion, TEXT("CHAMPION"), DifficultyBlurb(RKC::EBotDifficulty::Champion), NextY(),
                           true, true));
        Items.Add(MakeItem(IdBack, TEXT("BACK"), TEXT("Return to fighter selection"), NextY()));
        BodyLines.Add(TEXT("Difficulty changes reaction speed, spacing and how often the CPU makes mistakes."));
        BodyLines.Add(TEXT("It never grants extra health, damage, stamina or knowledge of your inputs."));
        break;

    case EScreen::TrainingSetup:
    {
        Items.Add(MakeItem(IdDummyStationary, TEXT("DUMMY: STATIONARY"), TEXT("Default. Never attacks, never walks, never blocks"), NextY()));
        Items.Add(MakeItem(IdDummyBlocking, TEXT("DUMMY: ALWAYS BLOCKS"),
                           TEXT("Not available in this build: the dummy cannot yet hold guard on request"),
                           NextY(), false));
        auto Toggle = [this](RKC::ETrainingMode Mode)
        {
            return Request.TrainingA == Mode ? TEXT("ON") : TEXT("OFF");
        };
        Items.Add(MakeItem(IdTrainingHitboxes, FString::Printf(TEXT("SHOW HITBOXES  [%s]"), Toggle(RKC::ETrainingMode::ShowHitboxes)),
                           TEXT("Draw attack and hurt boxes"), NextY(), true, true));
        Items.Add(MakeItem(IdTrainingFrameData, FString::Printf(TEXT("FRAME DATA OVERLAY  [%s]"), Toggle(RKC::ETrainingMode::DisplayFrameData)),
                           TEXT("Tick-accurate windows for every attack"), NextY(), true, true));
        Items.Add(MakeItem(IdTrainingInfiniteHealth, FString::Printf(TEXT("INFINITE HEALTH  [%s]"), Toggle(RKC::ETrainingMode::InfiniteHealth)),
                           TEXT("You cannot be knocked out; the dummy still takes damage"), NextY(), true, true));
        Items.Add(MakeItem(IdTrainingInfiniteStamina, FString::Printf(TEXT("INFINITE STAMINA  [%s]"), Toggle(RKC::ETrainingMode::InfiniteStamina)),
                           TEXT("Test full move lists without stamina limits"), NextY(), true, true));
        Items.Add(MakeItem(IdBack, TEXT("BACK"), TEXT("Return to arrangement selection"), NextY()));
        BodyLines.Add(TEXT("Training isolates your own inputs: the dummy is stationary until you turn the CPU on."));
        BodyLines.Add(TEXT("A freely attacking CPU is never the default here."));
        BodyLines.Add(TEXT("The simulation carries one training aid per side, so turning one of these on"));
        BodyLines.Add(TEXT("turns the previous one off. The dummy block option is the only row that cannot"));
        BodyLines.Add(TEXT("be switched on yet, and it says so instead of pretending."));
        break;
    }

    case EScreen::Confirm:
    {
        Items.Add(MakeItem(IdFight, TEXT("FIGHT"), TEXT("Start the match"), NextY()));
        Items.Add(MakeItem(IdChangeFighters, TEXT("CHANGE FIGHTERS"), TEXT("Pick a different fighter"), NextY()));
        Items.Add(MakeItem(IdBack, TEXT("BACK"), TEXT("Change difficulty or training options"), NextY()));

        BodyLines.Add(TEXT("MIDNIGHT ROOFTOP RING - the only playable arena in this build"));
        BodyLines.Add(FString::Printf(TEXT("%s  vs  %s"), *RosterName(Request.FighterA), *RosterName(Request.FighterB)));
        BodyLines.Add(FString::Printf(TEXT("%s      %s"), *SideLabel(Request.bHumanA, 1, !Request.bHumanA),
                                      *SideLabel(Request.bHumanB, 2, !Request.bHumanB)));
        BodyLines.Add(FString::Printf(TEXT("Rules: %s"), *Request.RulesSummary));
        if (Request.Arrangement == EArrangement::VsComputer)
        {
            BodyLines.Add(FString::Printf(TEXT("CPU difficulty: %s"), *DifficultyName(Request.Difficulty)));
        }
        else if (Request.Arrangement == EArrangement::LocalVersus)
        {
            BodyLines.Add(TEXT("Local versus: no AI controls either fighter. Two humans, one keyboard."));
        }
        else
        {
            BodyLines.Add(TEXT("Training: your fighter is the only one that can act."));
        }
        FooterLines.Add(TEXT("ENTER on FIGHT starts the versus card, the countdown and then the round."));
        break;
    }

    case EScreen::Controls:
    {
        Items.Add(MakeItem(IdBack, TEXT("BACK"), TEXT("Return to the main menu"), NextY()));
        for (const FString& Line : ControlReference())
        {
            BodyLines.Add(Line);
        }
        break;
    }

    case EScreen::Options:
    {
        Items.Add(MakeItem(IdMute, TEXT("MUTE"), Audio && Audio->IsMuted() ? TEXT("ON") : TEXT("OFF"), NextY(), true, true));
        Items.Add(MakeItem(IdMusicVol, TEXT("MUSIC"), TEXT(""), NextY(), true, true));
        Items.Add(MakeItem(IdSfxVol, TEXT("SOUND EFFECTS"), TEXT(""), NextY(), true, true));
        Items.Add(MakeItem(IdVoiceVol, TEXT("FIGHTER VOICES"), TEXT(""), NextY(), true, true));
        Items.Add(MakeItem(IdUiVol, TEXT("MENU SOUNDS"), TEXT(""), NextY(), true, true));
        Items.Add(MakeItem(IdTouch, TEXT("TOUCH CONTROLS"),
                           Audio && Audio->GetTouchControlsEnabled() ? TEXT("ON") : TEXT("OFF"),
                           NextY(), true, true));
        Items.Add(MakeItem(IdBack, TEXT("BACK"), TEXT("Return to the main menu"), NextY()));
        RefreshVolumeLabels();
        if (!Audio)
        {
            BodyLines.Add(TEXT("No audio system in this run: volume changes are not applied."));
        }
        BodyLines.Add(TEXT("Every change here is saved immediately and survives a restart."));
        BodyLines.Add(TEXT("Screen shake is not implemented in this build, so there is no reduced-shake"));
        BodyLines.Add(TEXT("option to offer; impact feedback is audio and animation only."));
        break;
    }

    case EScreen::Paused:
        Items.Add(MakeItem(IdResume, TEXT("RESUME"), TEXT("Continue the match"), NextY()));
        Items.Add(MakeItem(IdControls, TEXT("CONTROLS"), TEXT("Bindings while you are mid-match"), NextY()));
        Items.Add(MakeItem(IdOptions, TEXT("SETTINGS"), TEXT("Volume and touch controls"), NextY()));
        Items.Add(MakeItem(IdRestartMatch, TEXT("RESTART MATCH"), TEXT("Reset this match from the opening bell"), NextY()));
        Items.Add(MakeItem(IdQuitToMenu, TEXT("QUIT TO MAIN MENU"), TEXT("Abandon the match and return to the menu"), NextY()));
        BodyLines.Add(TEXT("PAUSED - the match clock and both fighters are frozen."));
        BodyLines.Add(TEXT("Held buttons and queued attacks are cleared, so resuming cannot fire a stray punch."));
        break;

    case EScreen::Results:
    {
        Items.Add(MakeItem(IdRematch, TEXT("REMATCH"), TEXT("Same fighters, same rules, fresh match"), NextY()));
        Items.Add(MakeItem(IdChangeFighters, TEXT("CHANGE FIGHTERS"), TEXT("Back to fighter selection"), NextY()));
        Items.Add(MakeItem(IdMenu, TEXT("MAIN MENU"), TEXT("Return to the main menu"), NextY()));
        Items.Add(MakeItem(IdQuit, TEXT("EXIT GAME"), TEXT("Close Rocky Kombat"), NextY()));

        BodyLines.Add(ResultText.IsEmpty() ? TEXT("MATCH OVER") : FString::Printf(TEXT("WINNER: %s"), *ResultText));
        if (Request.Arrangement == EArrangement::LocalVersus)
        {
            if (LocalReadyPresses == 1)
            {
                BodyLines.Add(TEXT("WAITING FOR THE OTHER PLAYER to press ENTER on REMATCH."));
            }
            else
            {
                BodyLines.Add(TEXT("Local versus rematch needs both players: press ENTER on REMATCH once each."));
            }
        }
        else if (Request.Arrangement == EArrangement::VsComputer)
        {
            BodyLines.Add(TEXT("The computer does not need to confirm a rematch."));
        }
        break;
    }

    case EScreen::Quit:
    {
        Items.Add(MakeItem(IdCancelQuit, TEXT("CANCEL"), TEXT("Back to the main menu"), NextY()));
        Items.Add(MakeItem(IdConfirmQuit, TEXT("EXIT GAME"), TEXT("Close Rocky Kombat now"), NextY()));
        BodyLines.Add(TEXT("Exit Rocky Kombat?"));
        break;
    }

    case EScreen::Boot:
    case EScreen::Title:
    case EScreen::Loading:
    case EScreen::Versus:
    case EScreen::Match:
    default:
        break;
    }

    Selection = FMath::Clamp(Selection, 0, FMath::Max(0, Items.Num() - 1));
}

void FMachine::RefreshVolumeLabels()
{
    if (!Audio)
    {
        return;
    }
    auto Percent = [](float Value)
    {
        return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.0f));
    };

    for (FMenuItem& Item : Items)
    {
        if (Item.Id == IdMusicVol)
        {
            Item.Hint = Percent(Audio->GetMusicVolume());
        }
        else if (Item.Id == IdSfxVol)
        {
            Item.Hint = Percent(Audio->GetSfxVolume());
        }
        else if (Item.Id == IdVoiceVol)
        {
            Item.Hint = Percent(Audio->GetVoiceVolume());
        }
        else if (Item.Id == IdUiVol)
        {
            Item.Hint = Percent(Audio->GetUiVolume());
        }
    }
}

// ---------------------------------------------------------------------------
// Selection and activation
// ---------------------------------------------------------------------------

void FMachine::ApplyNavigate(int32 Direction)
{
    if (Direction == 0 || Items.Num() == 0)
    {
        return;
    }
    ++Diag.Navigates;

    int32 Index = Selection;
    for (int32 Step = 0; Step < Items.Num(); ++Step)
    {
        Index = (Index + (Direction > 0 ? 1 : Items.Num() - 1)) % Items.Num();
        if (Items[Index].bEnabled)
        {
            break;
        }
    }
    if (Items.IsValidIndex(Index) && Index != Selection)
    {
        Selection = Index;
        PlayUi(ERKCUiSound::Focus);
    }
}

void FMachine::SelectById(const TCHAR* Id)
{
    for (int32 Index = 0; Index < Items.Num(); ++Index)
    {
        if (Items[Index].Id == Id)
        {
            Selection = Index;
            return;
        }
    }
}

int32 FMachine::IndexOfId(const TCHAR* Id) const
{
    for (int32 Index = 0; Index < Items.Num(); ++Index)
    {
        if (Items[Index].Id == Id)
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

FBox2D FMachine::GetItemRect(int32 Index, const FVector2D& V) const
{
    const float S = FMath::Min(V.X / 1280.0f, V.Y / 720.0f);
    const int32 Visible = 7;
    const int32 First = FMath::Clamp(Selection - Visible / 2, 0, FMath::Max(0, Items.Num() - Visible));
    if (Index < First || Index >= First + Visible) return FBox2D(ForceInit);
    const bool Portrait = Screen == EScreen::CharacterSelect;
    const bool Poster = Screen == EScreen::Menu;
    float X = Portrait || Poster ? V.X * .52f : V.X * .14f;
    float W = Portrait || Poster ? V.X * .43f : V.X * .72f;
    float Y = (Screen == EScreen::LocalVersusJoin || Screen == EScreen::Controls) ? V.Y * .76f : V.Y * .33f;
    if (Screen == EScreen::Results) Y = V.Y * .49f;
    Y += (Index - First) * 46.0f * S;
    return FBox2D(FVector2D(X,Y), FVector2D(X+W,Y+40.0f*S));
}
FString FMachine::GetCornerLabel(int32 Side) const
{
    if (Request.Arrangement == EArrangement::PrivateOnline && Online) return Online->Names[Side];
    if (Side == 0) return TEXT("PLAYER 1 / RED CORNER");
    return Request.bHumanB ? TEXT("PLAYER 2 / BLUE CORNER") : TEXT("CPU / BLUE CORNER");
}
void FMachine::HandleTap(const FVector2D& Tap, const FVector2D& V)
{
    if (Items.Num()>7 && Tap.X>V.X*.96f) { ApplyNavigate(Tap.Y<V.Y*.5f?-1:1); return; }
    for (int32 I = 0; I < Items.Num(); ++I) {
        const FBox2D Rect = GetItemRect(I,V);
        if (Rect.bIsValid && Rect.IsInside(Tap)) { const FMenuItem Item=Items[I]; SelectById(*Item.Id); ++Diag.TapSelections; ActivateItem(Item); return; }
    }
}

void FMachine::AdjustItem(const FMenuItem& Item, int32 Direction)
{
    if (!Item.bAdjustable || !Item.bEnabled)
    {
        return;
    }
    bool bChanged = false;

    if (Item.Id == IdMute)
    {
        if (Audio)
        {
            bChanged = Audio->SetMuted(!Audio->IsMuted());
        }
    }
    else if (Item.Id == IdTouch)
    {
        if (Audio)
        {
            bChanged = Audio->SetTouchControlsEnabled(!Audio->GetTouchControlsEnabled());
        }
    }
    else if (Item.Id == IdMusicVol || Item.Id == IdSfxVol || Item.Id == IdVoiceVol || Item.Id == IdUiVol)
    {
        if (Audio)
        {
            const float Step = 0.05f * static_cast<float>(Direction);
            float Value = 0.0f;
            if (Item.Id == IdMusicVol)
            {
                Value = Audio->GetMusicVolume();
            }
            else if (Item.Id == IdSfxVol)
            {
                Value = Audio->GetSfxVolume();
            }
            else if (Item.Id == IdVoiceVol)
            {
                Value = Audio->GetVoiceVolume();
            }
            else
            {
                Value = Audio->GetUiVolume();
            }
            Value = FMath::Clamp(Value + Step, 0.0f, 1.0f);

            if (Item.Id == IdMusicVol)
            {
                bChanged = Audio->SetMusicVolume(Value);
            }
            else if (Item.Id == IdSfxVol)
            {
                bChanged = Audio->SetSfxVolume(Value);
            }
            else if (Item.Id == IdVoiceVol)
            {
                bChanged = Audio->SetVoiceVolume(Value);
            }
            else
            {
                bChanged = Audio->SetUiVolume(Value);
            }
        }
    }
    else if (Item.Id == IdDiffRookie || Item.Id == IdDiffContender || Item.Id == IdDiffChampion)
    {
        // Adjusting left/right on the difficulty list moves the highlighted grade
        // rather than toggling the row, which is what a player expects here.
        Selection = FMath::Clamp(Selection + Direction, 0, FMath::Max(0, Items.Num() - 2));
        PlayUi(ERKCUiSound::Focus);
        return;
    }
    else if (Item.Id == IdTrainingHitboxes)
    {
        Request.TrainingA = (Request.TrainingA == RKC::ETrainingMode::ShowHitboxes)
                                ? RKC::ETrainingMode::Off
                                : RKC::ETrainingMode::ShowHitboxes;
        bChanged = true;
    }
    else if (Item.Id == IdTrainingFrameData)
    {
        Request.TrainingA = (Request.TrainingA == RKC::ETrainingMode::DisplayFrameData)
                                ? RKC::ETrainingMode::Off
                                : RKC::ETrainingMode::DisplayFrameData;
        bChanged = true;
    }
    else if (Item.Id == IdTrainingInfiniteHealth)
    {
        Request.TrainingA = (Request.TrainingA == RKC::ETrainingMode::InfiniteHealth)
                                ? RKC::ETrainingMode::Off
                                : RKC::ETrainingMode::InfiniteHealth;
        bChanged = true;
    }
    else if (Item.Id == IdTrainingInfiniteStamina)
    {
        Request.TrainingA = (Request.TrainingA == RKC::ETrainingMode::InfiniteStamina)
                                ? RKC::ETrainingMode::Off
                                : RKC::ETrainingMode::InfiniteStamina;
        bChanged = true;
    }
    else if (Item.Id == IdDummyStationary)
    {
        // A training aid belongs to the fighter it is FOR, and NoOpponentInput
        // means "the fighter on the other side does not act". The stationary dummy
        // is therefore owned by the player's own side: putting it on side B
        // silenced side A, the player, and left the CPU boxing an unresponsive
        // opponent. Toggling turns the dummy off, which hands the CPU its input
        // back.
        Request.TrainingA = (Request.TrainingA == RKC::ETrainingMode::NoOpponentInput)
                                ? RKC::ETrainingMode::Off
                                : RKC::ETrainingMode::NoOpponentInput;
        Request.TrainingB = RKC::ETrainingMode::Off;
        bChanged = true;
    }

    if (bChanged)
    {
        ++Diag.OptionsChanged;
        PlayUi(ERKCUiSound::Select);
        // Rebuilding preserves the highlighted row by id and refreshes the
        // value labels, so the screen always shows what was actually stored.
        const int32 KeepIndex = Selection;
        RebuildItems();
        Selection = FMath::Clamp(KeepIndex, 0, FMath::Max(0, Items.Num() - 1));
        RefreshVolumeLabels();
    }
    else
    {
        PlayUi(ERKCUiSound::Disabled);
    }
}

void FMachine::PlayUi(ERKCUiSound Sound)
{
    if (Audio)
    {
        Audio->PlayUi(Sound);
    }
}

void FMachine::ActivateItem(const FMenuItem& Item)
{
    if (!Item.bEnabled)
    {
        ++Diag.DisabledSelectionsBlocked;
        PlayUi(ERKCUiSound::Disabled);
        return;
    }

    PlayUi(ERKCUiSound::Select);

    // ---- main menu ---------------------------------------------------------
    if (Item.Id == IdPlay)
    {
        SetScreen(EScreen::Experience, TEXT("play chosen"));
        SelectById(IdExpRba);
        return;
    }
    if (Item.Id == IdTraining)
    {
        Request.Arrangement = EArrangement::Training;
        Request.bHumanA = true;
        Request.bHumanB = false;
        Request.TrainingA = RKC::ETrainingMode::NoOpponentInput;
        Request.TrainingB = RKC::ETrainingMode::Off;
        CharStage = 0;
        SetScreen(EScreen::CharacterSelect, TEXT("training chosen"));
        return;
    }
    if (Item.Id == IdControls)
    {
        SetScreen(Screen == EScreen::Paused ? EScreen::Controls : EScreen::Controls, TEXT("controls opened"));
        return;
    }
    if (Item.Id == IdOptions)
    {
        SetScreen(EScreen::Options, TEXT("settings opened"));
        return;
    }
    if (Item.Id == IdQuit)
    {
        SetScreen(EScreen::Quit, TEXT("exit confirmation"));
        SelectById(IdCancelQuit);
        return;
    }
    if (Item.Id == IdCancelQuit)
    {
        SetScreen(EScreen::Menu, TEXT("exit cancelled"));
        return;
    }
    if (Item.Id == IdConfirmQuit)
    {
        bQuitRequested = true;
        return;
    }

    // ---- experience --------------------------------------------------------
    if (Item.Id == IdExpRba)
    {
        Request.Experience = EExperience::RockyBoxingArena;
        SetScreen(EScreen::Arrangement, TEXT("Rocky Boxing Arena chosen"));
        return;
    }

    if (Item.Id == IdArrOnline) {
        Request.Arrangement = EArrangement::PrivateOnline; Request.bHumanA = Request.bHumanB = true;
        CharStage = 0; SetScreen(EScreen::CharacterSelect, TEXT("online fighter selection")); return;
    }
    if (Screen == EScreen::OnlineLobby && Online) {
        if (Item.Id == TEXT("online_connect")) { Online->Connect(); return; }
        if (Item.Id == TEXT("online_accept")) { Online->Accept(); return; }
        if (Item.Id == TEXT("online_decline")) { Online->Decline(); return; }
        if (Item.Id.StartsWith(TEXT("peer:"))) { Online->Invite(Item.Id.Mid(5)); return; }
        if (Item.Id == IdBack) { Online->Leave(); SetScreen(EScreen::Arrangement, TEXT("leave online lobby")); return; }
    }
    // ---- arrangement -------------------------------------------------------
    if (Item.Id == IdArrCpu)
    {
        Request.Arrangement = EArrangement::VsComputer;
        Request.bHumanA = true;
        Request.bHumanB = false;
        CharStage = 0;
        SetScreen(EScreen::CharacterSelect, TEXT("versus computer chosen"));
        return;
    }
    if (Item.Id == IdArrLocal)
    {
        Request.Arrangement = EArrangement::LocalVersus;
        Request.bHumanA = true;
        Request.bHumanB = true;
        SetScreen(EScreen::LocalVersusJoin, TEXT("local versus chosen - device assignment"));
        return;
    }
    if (Item.Id == IdArrTraining)
    {
        Request.Arrangement = EArrangement::Training;
        Request.bHumanA = true;
        Request.bHumanB = false;
        Request.TrainingA = RKC::ETrainingMode::NoOpponentInput;
        Request.TrainingB = RKC::ETrainingMode::Off;
        CharStage = 0;
        SetScreen(EScreen::CharacterSelect, TEXT("training chosen"));
        return;
    }
    if (Item.Id == IdBack && Screen == EScreen::LocalVersusJoin)
    {
        // The join screen's only enabled row is CONTINUE.
        CharStage = 0;
        SetScreen(EScreen::CharacterSelect, TEXT("both players confirmed - fighter selection"));
        return;
    }

    // ---- fighter selection -------------------------------------------------
    if (Item.Id == IdRocky || Item.Id == IdShadow)
    {
        const RKC::ERoster Chosen = (Item.Id == IdRocky) ? RKC::ERoster::Rocky : RKC::ERoster::Shadow;
        if (CharStage == 0)
        {
            Request.FighterA = Chosen;
            if (Request.Arrangement == EArrangement::PrivateOnline && Online) {
                Online->Connect(); Online->SelectFighter(static_cast<int32>(Chosen));
                SetScreen(EScreen::OnlineLobby, TEXT("online fighter chosen")); return;
            }
            CharStage = 1;
            RebuildItems();
            return;
        }

        Request.FighterB = Chosen;

        if (Request.Arrangement == EArrangement::VsComputer)
        {
            SetScreen(EScreen::Difficulty, TEXT("both fighters chosen - difficulty"));
            SelectById(Request.Difficulty == RKC::EBotDifficulty::Rookie
                           ? IdDiffRookie
                           : (Request.Difficulty == RKC::EBotDifficulty::Champion ? IdDiffChampion
                                                                                 : IdDiffContender));
            return;
        }
        if (Request.Arrangement == EArrangement::Training)
        {
            SetScreen(EScreen::TrainingSetup, TEXT("both fighters chosen - training options"));
            return;
        }
        FinaliseRequest();
        SetScreen(EScreen::Confirm, TEXT("both fighters chosen - confirm"));
        return;
    }

    // ---- difficulty --------------------------------------------------------
    if (Item.Id == IdDiffRookie || Item.Id == IdDiffContender || Item.Id == IdDiffChampion)
    {
        if (Item.Id == IdDiffRookie)
        {
            Request.Difficulty = RKC::EBotDifficulty::Rookie;
        }
        else if (Item.Id == IdDiffChampion)
        {
            Request.Difficulty = RKC::EBotDifficulty::Champion;
        }
        else
        {
            Request.Difficulty = RKC::EBotDifficulty::Contender;
        }
        FinaliseRequest();
        SetScreen(EScreen::Confirm, TEXT("difficulty chosen - confirm"));
        return;
    }

    // ---- training ----------------------------------------------------------
    if (Screen == EScreen::TrainingSetup)
    {
        if (Item.Id == IdBack)
        {
            CharStage = 0;
            SetScreen(EScreen::CharacterSelect, TEXT("back to fighter selection"));
            return;
        }
        if (Item.bAdjustable)
        {
            AdjustItem(Item, 1);
            return;
        }
    }

    // ---- confirm -----------------------------------------------------------
    if (Item.Id == IdFight)
    {
        FinaliseRequest();
        if (Request.Arrangement == EArrangement::Training)
        {
            Request.RulesSummary = TEXT("Training - no round clock pressure");
        }
        LoadingProgress = 0.0f;
        LoadingSeconds = 0.0f;
        SetScreen(EScreen::Loading, TEXT("confirmed - loading presentation assets"));
        return;
    }
    if (Item.Id == IdChangeFighters)
    {
        CharStage = 0;
        SetScreen(EScreen::CharacterSelect, TEXT("change fighters"));
        return;
    }

    // ---- results -----------------------------------------------------------
    if (Item.Id == IdRematch)
    {
        if (Request.Arrangement == EArrangement::PrivateOnline && Online) { Online->Rematch(); return; }
        if (Request.Arrangement == EArrangement::LocalVersus)
        {
            if (LocalReadyPresses < 1)
            {
                LocalReadyPresses = 1;
                LocalReadySeconds = 0.0f;
                RebuildItems();
                PlayUi(ERKCUiSound::Focus);
                return;
            }
            LocalReadyPresses = 0;
            LocalReadySeconds = 0.0f;
        }
        bRematchRequested = true;
        LoadingProgress = 0.0f;
        LoadingSeconds = 0.0f;
        SetScreen(EScreen::Loading, TEXT("rematch confirmed"));
        return;
    }

    // ---- pause -------------------------------------------------------------
    if (Item.Id == IdResume)
    {
        OnResumeRequested();
        return;
    }
    if (Item.Id == IdRestartMatch)
    {
        bRematchRequested = true;
        SetScreen(EScreen::Match, TEXT("match restarted from pause"));
        return;
    }
    if (Item.Id == IdQuitToMenu)
    {
        ReturnToMainMenu();
        return;
    }

    // ---- shared ------------------------------------------------------------
    if (Item.Id == IdBack)
    {
        switch (Screen)
        {
        case EScreen::Experience:
            SetScreen(EScreen::Menu, TEXT("back to main menu"));
            break;
        case EScreen::Arrangement:
            SetScreen(EScreen::Experience, TEXT("back to experience selection"));
            break;
        case EScreen::CharacterSelect:
            if (CharStage > 0)
            {
                --CharStage;
                RebuildItems();
                ++Diag.SelectionsPreserved;
            }
            else
            {
                SetScreen(EScreen::Arrangement, TEXT("back to arrangement selection"));
            }
            break;
        case EScreen::Difficulty:
            CharStage = 1;
            SetScreen(EScreen::CharacterSelect, TEXT("back to second fighter"));
            break;
        case EScreen::TrainingSetup:
            CharStage = 1;
            SetScreen(EScreen::CharacterSelect, TEXT("back to second fighter"));
            break;
        case EScreen::Confirm:
            if (Request.Arrangement == EArrangement::Training)
            {
                SetScreen(EScreen::TrainingSetup, TEXT("back to training options"));
            }
            else if (Request.Arrangement == EArrangement::VsComputer)
            {
                SetScreen(EScreen::Difficulty, TEXT("back to difficulty"));
            }
            else
            {
                CharStage = 0;
                SetScreen(EScreen::CharacterSelect, TEXT("back to fighter selection"));
            }
            break;
        case EScreen::Controls:
        case EScreen::Options:
            SetScreen(ScreenBeforePause == EScreen::Paused && Screen == EScreen::Options ? EScreen::Paused
                                                                                        : EScreen::Menu,
                      TEXT("back"));
            break;
        default:
            SetScreen(EScreen::Menu, TEXT("back to main menu"));
            break;
        }
        return;
    }

    if (Item.Id == IdMenu)
    {
        ReturnToMainMenu();
        return;
    }
}

void FMachine::FinaliseRequest()
{
    if (Request.Arrangement == EArrangement::VsComputer)
    {
        Request.bHumanA = true;
        Request.bHumanB = false;
        Request.RulesSummary = TEXT("3 rounds - 30 s - 3-knockdown rule");
    }
    else if (Request.Arrangement == EArrangement::LocalVersus)
    {
        Request.bHumanA = true;
        Request.bHumanB = true;
        Request.RulesSummary = TEXT("3 rounds - 30 s - 3-knockdown rule - no AI");
    }
    else
    {
        Request.bHumanA = true;
        Request.bHumanB = false;
        Request.RulesSummary = TEXT("Training - stationary dummy");
    }
}

// ---------------------------------------------------------------------------
// Text for the renderer
// ---------------------------------------------------------------------------

FString FMachine::SideLabel(bool bHuman, int32 PlayerIndex, bool bIsCpu) const
{
    if (bIsCpu)
    {
        return TEXT("CPU");
    }
    if (Request.Arrangement == EArrangement::LocalVersus)
    {
        return FString::Printf(TEXT("PLAYER %d"), PlayerIndex);
    }
    return bHuman ? TEXT("HUMAN (YOU)") : TEXT("CPU");
}

FString FMachine::CharacterBlurb(RKC::ERoster Who) const
{
    if (Who == RKC::ERoster::Rocky)
    {
        return TEXT("Orange tabby boxer. Red gloves, boxing stance, straight and heavy hands. Walks you down.");
    }
    return TEXT("Black panther martial artist. Quicker, lighter hands, more fragile. Punishes and slips away.");
}

FString FMachine::GetHeading() const
{
    switch (Screen)
    {
    case EScreen::Boot:
        return TEXT("ROCKYCAT ECOSYSTEM");
    case EScreen::Title:
        return TEXT("ROCKY KOMBAT");
    case EScreen::OnlineLobby:
        return TEXT("CONNECTED PLAYERS");
    case EScreen::Menu:
        return TEXT("ROCKY KOMBAT");
    case EScreen::Experience:
        return TEXT("COMBAT EXPERIENCE");
    case EScreen::Arrangement:
        return TEXT("PLAY ARRANGEMENT");
    case EScreen::LocalVersusJoin:
        return TEXT("LOCAL VERSUS");
    case EScreen::CharacterSelect:
        return CharStage == 0 ? TEXT("CHOOSE YOUR FIGHTER") : TEXT("CHOOSE THE OPPONENT");
    case EScreen::Difficulty:
        return TEXT("DIFFICULTY");
    case EScreen::TrainingSetup:
        return TEXT("TRAINING SETUP");
    case EScreen::Controls:
        return TEXT("CONTROLS");
    case EScreen::Options:
        return TEXT("SETTINGS");
    case EScreen::Confirm:
        return TEXT("CONFIRM MATCH");
    case EScreen::Loading:
        return TEXT("LOADING");
    case EScreen::Versus:
        return TEXT("MIDNIGHT ROOFTOP RING");
    case EScreen::Match:
        return TEXT("");
    case EScreen::Results:
        return TEXT("RESULT");
    case EScreen::Paused:
        return TEXT("PAUSED");
    case EScreen::Quit:
        return TEXT("EXIT");
    default:
        return TEXT("");
    }
}

FString FMachine::GetSubheading() const
{
    switch (Screen)
    {
    case EScreen::Boot:
        return TEXT("presents");
    case EScreen::Title:
        return TEXT("THE FIGHT BEGINS.");
    case EScreen::CharacterSelect:
        return CharStage == 0 ? TEXT("PLAYER 1 / HUMAN") : TEXT("PLAYER 2 / OPPONENT");
    case EScreen::Versus:
        return FString::Printf(TEXT("%s  vs  %s"), *RosterName(Request.FighterA), *RosterName(Request.FighterB));
    case EScreen::Menu:
        return TEXT("ROCKY BOXING ARENA / THE UNDERDOGS ARENA");
    default:
        return TEXT("");
    }
}

FString FMachine::GetFooter() const
{
    if (Screen == EScreen::Results)
    {
        return FString::Printf(TEXT("Winner: %s"), *ResultText);
    }
    return FString();
}

FString FMachine::DescribeState() const
{
    return FString::Printf(
        TEXT("screen=%s age=%.2fs sel=%d items=%d charStage=%d arrangement=%d fighters=%s/%s humans=%d%c%d "
             "difficulty=%s loading=%.0f%% ready=%d"),
        *ScreenName(Screen), ScreenAgeSeconds, Selection, Items.Num(), CharStage,
        static_cast<int32>(Request.Arrangement),
        *RosterName(Request.FighterA), *RosterName(Request.FighterB),
        Request.bHumanA ? 1 : 0, '/', Request.bHumanB ? 1 : 0,
        *DifficultyName(Request.Difficulty), LoadingProgress * 100.0f, LocalReadyPresses);
}

FString FMachine::ScreenName(EScreen InScreen)
{
    switch (InScreen)
    {
    case EScreen::OnlineLobby:
        return TEXT("OnlineLobby");
    case EScreen::Boot:
        return TEXT("Boot");
    case EScreen::Title:
        return TEXT("Title");
    case EScreen::Menu:
        return TEXT("Menu");
    case EScreen::Experience:
        return TEXT("Experience");
    case EScreen::Arrangement:
        return TEXT("Arrangement");
    case EScreen::LocalVersusJoin:
        return TEXT("LocalVersusJoin");
    case EScreen::CharacterSelect:
        return TEXT("CharacterSelect");
    case EScreen::Difficulty:
        return TEXT("Difficulty");
    case EScreen::TrainingSetup:
        return TEXT("TrainingSetup");
    case EScreen::Controls:
        return TEXT("Controls");
    case EScreen::Options:
        return TEXT("Options");
    case EScreen::Confirm:
        return TEXT("Confirm");
    case EScreen::Loading:
        return TEXT("Loading");
    case EScreen::Versus:
        return TEXT("Versus");
    case EScreen::Match:
        return TEXT("Match");
    case EScreen::Results:
        return TEXT("Results");
    case EScreen::Paused:
        return TEXT("Paused");
    case EScreen::Quit:
        return TEXT("Quit");
    default:
        return TEXT("?");
    }
}

const TArray<FString>& FMachine::ControlReference()
{
    // The single source of truth for bindings, shared with the delivered test
    // checklist. DefaultInput.ini must agree with this list; that is asserted by
    // the in-engine self-test rather than assumed here.
    static const TArray<FString> Lines = {
        TEXT("KEYBOARD - PLAYER 1 (left hand)"),
        TEXT("   Move left / right .......... A / D"),
        TEXT("   Block (hold) ............... S"),
        TEXT("   Jab ........................ Z"),
        TEXT("   Heavy punch ................ X"),
        TEXT("   Dodge ...................... C"),
        TEXT(""),
        TEXT("KEYBOARD - PLAYER 2 (right hand, local versus only)"),
        TEXT("   Move left / right .......... LEFT / RIGHT arrow"),
        TEXT("   Block (hold) ............... DOWN arrow"),
        TEXT("   Jab ........................ Numpad 1   (no numpad: comma)"),
        TEXT("   Heavy punch ................ Numpad 2   (no numpad: period)"),
        TEXT("   Dodge ...................... Numpad 3   (no numpad: slash)"),
        TEXT(""),
        TEXT("SHARED"),
        TEXT("   Pause / resume a match ..... P   (gamepad: right special)"),
        TEXT("   Back / cancel a screen ..... ESCAPE   (gamepad: B)"),
        TEXT("   Rematch after a match ...... R"),
        TEXT("   Leave the match ............ F10"),
        TEXT("   Frame-data overlay (dev) ... F1"),
        TEXT(""),
        TEXT("MENU"),
        TEXT("   Navigate ................... W / S or UP / DOWN arrows"),
        TEXT("   Change a setting ........... A / D or LEFT / RIGHT arrows"),
        TEXT("   Confirm .................... ENTER or SPACE"),
        TEXT("   Back ....................... ESCAPE"),
        TEXT(""),
        TEXT("TOUCH (mobile)"),
        TEXT("   Left thumb ................. virtual stick left / right"),
        TEXT("   Right thumb ................ JAB, HEAVY, BLOCK (hold), DODGE"),
        TEXT("   On-screen .................. PAUSE button, top right"),
        TEXT("A punch only lands inside its active hit window. Blocking absorbs it; an out-of-range"),
        TEXT("punch still animates and misses. A rejected input is shown in the input display (F1).")};

    return Lines;
}
}
