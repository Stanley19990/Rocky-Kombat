// RKCFrontEnd.h
//
// Rocky Kombat - the branded startup, loading and menu experience.
//
// WHY A STATE MACHINE AND NOT UMG: the whole game is drawn on a canvas by
// ARKCHUD, the front end has to be usable with a keyboard, a gamepad and a
// thumb on a phone, and it has to be verifiable without a human clicking. A
// small explicit machine with a normalised layout table gives all three, and it
// keeps the menu out of gameplay state: the machine only ever says "a match may
// run" or "a match may not run".
//
// THE LOADING SCREEN DOES A REAL JOB: the arena, the lights and the sprite
// materials need a few rendered frames before the first correctly lit frame
// exists (see ARKCGameMode::IsRenderWarmupComplete). The loading screen is what
// the player looks at during that window, so no black frame is ever presented
// as gameplay, and the progress it shows is derived from work that actually
// happened rather than from a timer.
//
// OFFLINE BY CONSTRUCTION: there is no network call anywhere in this file and no
// screen depends on connectivity. Private online versus is listed as unavailable
// rather than faked, and the menu says so out loud.
//
// RETURNING PLAYERS: the only difference between a first run and a later one is
// a shorter studio sting and an auto-advancing title. A returning player is on
// the menu about one second after launch without touching anything.
//
// TWO DECISIONS, KEPT APART (requirement 3): the combat EXPERIENCE and the play
// ARRANGEMENT are chosen on separate screens, in that order, and both are kept
// as the player steps back and forth so a wrong turn never loses a choice.

#pragma once

#include "CoreMinimal.h"
#include "Audio/RKCAudio.h"
#include "Combat/RKCRoster.h"
#include "AI/RKCAI.h"

class URKCAudioSystem;

namespace RKCFrontEnd
{
/** One screen. Match and Paused are states of the same machine, which is what
 *  guarantees the menu can never be drawn over a running match by accident. */
enum class EScreen : uint8
{
    Boot = 0,
    Title = 1,
    Menu = 2,
    Experience = 3,
    Arrangement = 4,
    LocalVersusJoin = 5,
    CharacterSelect = 6,
    Difficulty = 7,
    TrainingSetup = 8,
    Controls = 9,
    Options = 10,
    Confirm = 11,
    Loading = 12,
    Versus = 13,
    Match = 14,
    Results = 15,
    Paused = 16,
    Quit = 17
};

/** Requirement 3A - the combat experience. */
enum class EExperience : uint8
{
    RockyBoxingArena = 0,
    StreetFights = 1,
    RockyUniverse = 2
};

/** Requirement 3B - who is playing whom. */
enum class EArrangement : uint8
{
    VsComputer = 0,
    LocalVersus = 1,
    Training = 2,
    PrivateOnline = 3
};

/** Everything the director needs to configure one match. The front end never
 *  touches RKC::FCombatSim itself; it fills this in and the director applies it. */
struct FRKCMatchRequest
{
    EExperience Experience = EExperience::RockyBoxingArena;
    EArrangement Arrangement = EArrangement::VsComputer;

    RKC::ERoster FighterA = RKC::ERoster::Rocky;
    RKC::ERoster FighterB = RKC::ERoster::Shadow;

    /** Side A is the fighter the first player (or the only player) controls.
     *  Character identity never decides ownership: playing as Shadow puts the
     *  human on side A and the CPU on side B. */
    bool bHumanA = true;
    bool bHumanB = false;

    /** Champion by default. The offline opponent is the only opponent the build
     *  has, and the brief for it is a fighter that is hard to beat; the difficulty
     *  screen still offers Rookie and Contender. This changes reflexes and
     *  discipline only, never damage, health, stamina or information. */
    RKC::EBotDifficulty Difficulty = RKC::EBotDifficulty::Champion;

    /** Training aids, carried by the side they belong to. NoOpponentInput means
     *  "the fighter on the other side does not act", so the stationary dummy is
     *  owned by the PLAYER's side (A) and silences the CPU. It used to sit on side
     *  B, which silenced the player instead and left the CPU boxing an opponent
     *  that could not answer. */
    RKC::ETrainingMode TrainingA = RKC::ETrainingMode::NoOpponentInput;
    RKC::ETrainingMode TrainingB = RKC::ETrainingMode::Off;

    /** Human-readable rule summary shown on the confirm and versus cards. */
    FString RulesSummary;
};

/** One selectable row. Y is normalised, so the layout scales with any screen. */
struct FMenuItem
{
    FString Id;
    FString Label;
    FString Hint;
    double Y = 0.5;
    bool bAdjustable = false;
    bool bEnabled = true;
};

/** Navigation input, already merged from keyboard, gamepad and touch by the
 *  controller. The machine never reads a device itself. */
struct FInput
{
    int32 Navigate = 0;   // -1 up, +1 down
    int32 Adjust = 0;     // -1 left, +1 right (options)
    bool bAccept = false;
    bool bBack = false;
    bool bPause = false;
    /** Taps in PIXEL viewport space, used for touch/mouse menu selection. */
    TArray<FVector2D> Taps;
    /** Viewport size the taps were measured in. */
    FVector2D ViewportSize = FVector2D(1280.0, 720.0);
};

struct FDiagnostics
{
    int32 ScreenChanges = 0;
    int32 Accepts = 0;
    int32 Backs = 0;
    int32 Navigates = 0;
    int32 OptionsChanged = 0;
    int32 TapSelections = 0;
    int32 RejectedAccepts = 0;
    int32 DisabledSelectionsBlocked = 0;
    int32 Pauses = 0;
    int32 Resumes = 0;
    /** Selections that survived a back/forward trip, proving nothing resets. */
    int32 SelectionsPreserved = 0;
    TArray<FString> TransitionLog;
};

class FMachine
{
public:
    /** Branding durations. A first run watches the studio sting; a returning
     *  player sees a short fade and lands on the title. */
    static constexpr float BootSecondsFirstRun = 2.0f;
    static constexpr float BootSecondsReturning = 0.35f;
    static constexpr float BootSkippableAfter = 0.4f;
    static constexpr float TitleAutoAdvanceReturning = 0.8f;
    /** A first run waits for the player, but not forever: an unattended or
     *  kiosk run still reaches the menu. */
    static constexpr float TitleAutoAdvanceFirstRun = 6.0f;
    /** How long the versus card is held before the round begins. */
    static constexpr float VersusSeconds = 1.8f;
    /** The loading screen is held at least this long, so a fast machine cannot
     *  flash a progress bar for two frames. */
    static constexpr float MinimumLoadingSeconds = 0.45f;
    /** Two accepts inside this window count as both local players agreeing. */
    static constexpr float LocalRematchWindowSeconds = 5.0f;

    FMachine();

    void Init(bool bReturningPlayer);
    void SetAudio(URKCAudioSystem* InAudio) { Audio = InAudio; }
    URKCAudioSystem* GetAudio() const { return Audio; }

    /** Set by the director for a scripted demonstration run (rkc.FrontEndAuto).
     *  It is an explicitly selected development path, never normal play. */
    void SetAutoPilot(bool bInAutoPilot, float SecondsPerScreen = 1.2f);
    bool IsAutoPilot() const { return bAutoPilot; }

    /** One frame of menu logic. bWarmupComplete comes from the game mode and is
     *  the only thing besides real progress that can release Loading into the
     *  versus card. */
    void Tick(float DeltaSeconds, const FInput& Input, bool bWarmupComplete);

    EScreen GetScreen() const { return Screen; }
    bool IsMatchPlayable() const { return Screen == EScreen::Match; }
    bool IsFrontEndScreen() const { return Screen != EScreen::Match; }
    bool IsGamePaused() const { return Screen == EScreen::Paused; }

    /** The chosen match configuration, applied by the director. */
    const FRKCMatchRequest& GetMatchRequest() const { return Request; }
    /** Set when the versus card has finished and the round may begin. */
    bool ConsumeMatchStartRequest();
    bool ConsumeRematchRequest();
    bool ConsumeQuitRequest();
    /** True when the director should stop simulating entirely (front end or
     *  pause). Requirement 2: no match may run behind the menu. */
    bool ShouldSimulationRun() const { return Screen == EScreen::Match || Screen == EScreen::Results; }

    /** Reports the transition into gameplay so the machine can log it. */
    void NotifyMatchStarted();
    /** Reports the result; the machine moves to Results and remembers who won. */
    void NotifyMatchEnded(bool bWinnerIsSideA, const FString& WinnerName);

    /** Pause is orthogonal to the screen stack: entering Paused remembers the
     *  previous screen and leaving it restores it. */
    void OnPauseRequested();
    void OnResumeRequested();
    bool IsPaused() const { return Screen == EScreen::Paused; }

    /** Returns to the main menu from anywhere and drops any pending request. */
    void ReturnToMainMenu();

    void SetLoadingProgress(float InProgress);
    float GetLoadingProgress() const { return LoadingProgress; }
    void SetLoadDetail(const FString& InDetail) { LoadDetail = InDetail; }

    // ---- Rendering inputs ---------------------------------------------------
    const TArray<FMenuItem>& GetItems() const { return Items; }
    int32 GetSelection() const { return Selection; }
    FString GetHeading() const;
    FString GetSubheading() const;
    const TArray<FString>& GetBodyLines() const { return BodyLines; }
    const TArray<FString>& GetFooterLines() const { return FooterLines; }
    FString GetFooter() const;
    float GetScreenAge() const { return ScreenAgeSeconds; }
    const FString& GetResultText() const { return ResultText; }
    const FString& GetLoadDetail() const { return LoadDetail; }
    /** Portrait/identity block drawn on the character-select and versus cards.
     *  Empty on screens that do not use it. */
    const FString& GetLeftCaption() const { return LeftCaption; }
    const FString& GetRightCaption() const { return RightCaption; }
    /** True while a local-versus rematch is waiting for the second player. */
    bool IsAwaitingSecondReady() const { return LocalReadyPresses == 1; }
    int32 GetLocalReadyPresses() const { return LocalReadyPresses; }

    // ---- Diagnostics --------------------------------------------------------
    const FDiagnostics& GetDiagnostics() const { return Diag; }
    void ResetDiagnostics() { Diag = FDiagnostics(); }
    FString DescribeState() const;
    static FString ScreenName(EScreen InScreen);

    /** Human-readable binding list, shared by the Controls screen and by the
     *  delivered test checklist so they cannot disagree. */
    static const TArray<FString>& ControlReference();

private:
    void SetScreen(EScreen NewScreen, const TCHAR* Reason);
    void RebuildItems();
    void ActivateItem(const FMenuItem& Item);
    void AdjustItem(const FMenuItem& Item, int32 Direction);
    void ApplyNavigate(int32 Direction);
    void HandleTap(const FVector2D& Tap, const FVector2D& ViewportSize);
    void PlayUi(ERKCUiSound Sound);
    void RefreshVolumeLabels();
    void ApplyScreenAudio();
    void TickAutoPilot(float DeltaSeconds);
    void SelectById(const TCHAR* Id);
    int32 IndexOfId(const TCHAR* Id) const;
    FString SideLabel(bool bHuman, int32 PlayerIndex, bool bIsCpu) const;
    FString CharacterBlurb(RKC::ERoster Who) const;
    /** Fills in the human/CPU flags and the rule summary from the arrangement. */
    void FinaliseRequest();

    URKCAudioSystem* Audio = nullptr;

    EScreen Screen = EScreen::Boot;
    EScreen ScreenBeforePause = EScreen::Menu;
    float ScreenAgeSeconds = 0.0f;
    float BootSeconds = BootSecondsFirstRun;
    int32 Selection = 0;
    float LoadingProgress = 0.0f;
    float LoadingSeconds = 0.0f;
    FString LoadDetail;
    FString ResultText;
    FString WinnerName;
    bool bWinnerIsSideA = false;
    bool bReturningPlayer = false;

    bool bAutoPilot = false;
    float AutoPilotSecondsPerScreen = 1.2f;
    float AutoPilotAccumulator = 0.0f;

    bool bMatchStartRequested = false;
    bool bRematchRequested = false;
    bool bQuitRequested = false;

    /** Which fighter the character-select screen is choosing right now. */
    int32 CharStage = 0;
    /** Local-versus readiness: 0 nobody, 1 first player, 2 both. */
    int32 LocalReadyPresses = 0;
    float LocalReadySeconds = 0.0f;
    bool bVersusChosen = false;

    FRKCMatchRequest Request;

    TArray<FMenuItem> Items;
    TArray<FString> BodyLines;
    TArray<FString> FooterLines;
    FString LeftCaption;
    FString RightCaption;
    FDiagnostics Diag;

    /** Normalised layout handed to the renderer. One table, one place to
     *  change, so the HUD cannot drift from the machine's row positions. */
    static constexpr double FirstItemY = 0.42;
    static constexpr double ItemSpacingY = 0.072;
};
}
