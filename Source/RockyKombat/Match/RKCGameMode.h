// RKCGameMode.h
//
// The match director: the only place in the project that owns both a live
// RKC::FCombatSim and the actors that present it.
//
// WHAT THIS CLASS IS FOR
// Rocky Kombat separates rules from presentation on purpose. RKC::FCombatSim is
// the truth; ARKCFighter and ARKCArena are pictures of it. Something has to sit
// in the middle, own the simulation, feed it input, and tell the pictures who
// they are looking at. That is this class, and nothing else.
//
// FRAME ORDER (fixed, and the same every frame):
//   1. gather one FInputState per side  (human controller, or FBotBrain)
//   2. Sim.Advance(DeltaSeconds, InA, InB)   -> whole fixed ticks only
//   3. drain simulation events into the HUD event feed and the evidence record
//   4. SyncFromSim() on both fighters        -> presentation follows the rules
//   5. update the centre banner from the match phase
// The ordering matters: presentation is always read AFTER the simulation has
// advanced, so a fighter can never be drawn in a state the rules are not in.
//
// NO MAP ASSET. The world is built procedurally here (ring, lights, camera) so
// that a clean checkout of the source plays with zero binary content. A hand-made
// .umap can replace BuildWorld() later without touching the simulation.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Combat/RKCCombatCore.h"
#include "Combat/RKCRoster.h"
#include "AI/RKCAI.h"
#include "UI/RKCFrontEnd.h"
#include "Misc/CoreDelegates.h"
#include "Match/RKCScriptedInput.h"
#include "RKCGameMode.generated.h"

class ARKCArena;
class ARKCFighter;
class ARKCPlayerController;
class ACameraActor;

/** One line in the on-screen event feed. */
USTRUCT()
struct FRKCEventFeedLine
{
    GENERATED_BODY()

    UPROPERTY()
    FString Text;

    /** Seconds since the event was raised; the feed fades lines out. */
    UPROPERTY()
    float AgeSeconds = 0.0f;
};

/** Everything that configures one match. Deliberately a plain C++ struct with no
 *  reflection: it is read from console variables and from test code, and the
 *  enums it carries are the simulation's own, not UENUMs. */
struct FRKCMatchSetup
{
    RKC::ERoster FighterA = RKC::ERoster::Rocky;
    RKC::ERoster FighterB = RKC::ERoster::Shadow;

    /** One round instead of three, and a short round clock. Used by smoke runs. */
    bool bUseQuickRules = false;

    /** Which sides the offline opponent drives. A human can drive the other. */
    bool bBotControlsA = false;
    bool bBotControlsB = true;

    /** Arbitrary above-rookie grades that raise the bot's REFLEXES AND DISCIPLINE
     *  only. Difficulty never grants extra damage, health, stamina or information;
     *  see RKCAI.h. */
    /** Champion by default: the offline opponent is the only opponent the build
     *  has, and the brief for it is a fighter that is hard to beat. Difficulty
     *  raises reflexes and discipline only, never damage, health or stamina. */
    RKC::EBotDifficulty BotDifficulty = RKC::EBotDifficulty::Champion;
    RKC::EBotArchetype BotArchetypeA = RKC::EBotArchetype::Balanced;
    RKC::EBotArchetype BotArchetypeB = RKC::EBotArchetype::Balanced;

    RKC::ETrainingMode TrainingA = RKC::ETrainingMode::Off;
    RKC::ETrainingMode TrainingB = RKC::ETrainingMode::Off;

    /** Bot RNG seeds. Identical seeds reproduce an identical match exactly. */
    uint32_t BotSeedA = 20261017u;
    uint32_t BotSeedB = 911042u;

    /** Write a match record to Saved/Evidence on match end. */
    bool bCaptureEvidence = true;

    /** Camera: a fixed, slightly distant view so the whole ring is readable. */
    float CameraFOV = 45.0f;
    float CameraDistance = 1500.0f;
    float CameraHeight = 300.0f;
    float CameraPitch = -5.0f;
};

UCLASS()
class ROCKYKOMBAT_API ARKCGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ARKCGameMode();

    virtual void StartPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // ---- Read-only access for the HUD and for tests ------------------------
    const RKC::FCombatSim& GetSim() const { return Sim; }
    const FRKCMatchSetup& GetSetup() const { return Setup; }
    const RKC::FBotBrain* GetBot(RKC::ESide Side) const;
    bool IsWorldBuilt() const { return bWorldBuilt; }

    ARKCFighter* GetFighterActor(RKC::ESide Side) const;
    ARKCArena* GetArena() const { return Arena; }
    ARKCPlayerController* GetRKCPlayerController() const;

    // ---- Front end, audio and render warm-up --------------------------------
    /** The branded startup, menu and result machine. The director drives it and
     *  applies its requests; the HUD only reads it, so the screen and the rules
     *  can never disagree about which screen is up. */
    RKCFrontEnd::FMachine& GetFrontEnd() { return FrontEnd; }
    const RKCFrontEnd::FMachine& GetFrontEnd() const { return FrontEnd; }

    /** True while a menu, loading, versus or result card owns the screen. While
     *  it is set the simulation does not advance and human input cannot reach a
     *  fighter, which makes "no match runs behind the menu" a property of the
     *  code rather than a promise. */
    bool IsFrontEndActive() const { return bFrontEndActive; }

    /** False for a self-test run and for -rkc.FrontEnd=0, where the director
     *  starts a match directly as it always did. */
    bool IsFrontEndEnabled() const { return bFrontEndEnabled; }

    /** The presentation audio layer. Null when the run asked for no audio at all
     *  (-rkc.Audio=0), which never changes a match result. */
    URKCAudioSystem* GetAudioSystem() const { return AudioSystem; }

    /** True once the world has produced enough rendered frames for the first
     *  correctly lit frame to exist. The loading card waits for this, so a black
     *  first frame is shown as loading rather than presented as gameplay. A run
     *  that cannot render at all is reported as warm immediately rather than
     *  hung on a screen that can never fill in. */
    bool IsRenderWarmupComplete() const;

    /** 0..1, derived from work that actually happened (world built, arena built,
     *  fighters spawned, frames drawn), never from a timer. */
    float GetLoadingProgress() const { return LoadingProgress; }

    /** Rendered frames counted since the world was built. */
    int32 GetWarmupFrameCount() const { return WarmupFramesObserved; }

    /** True once the HUD has reported a completed draw pass. A headless run never
     *  sets it, which is how the warm-up gate tells "no renderer at all" from
     *  "renderer not ready yet" instead of waiting forever. */
    bool HasSeenHudDraw() const { return bSawAnyHudDraw; }

    /** Called by ARKCHUD at the end of every completed draw pass. One line, and
     *  the only thing the HUD ever tells this class. It both records that a
     *  renderer exists and advances the warm-up clock, so the loading card is
     *  released by frames that were really drawn. */
    void NotifyHudDrew();

    /** Arena component count, reported so the HUD can prove the ring exists. */
    int32 GetArenaPieceCount() const;
    /** Greybox part count of one fighter's rig. */
    int32 GetFighterPartCount(RKC::ESide Side) const;

    bool IsFrameDataVisible() const { return bFrameDataVisible; }
    void SetFrameDataVisible(bool bVisible);

    const TArray<FRKCEventFeedLine>& GetEventFeed() const { return EventFeed; }

    /** Large text drawn across the middle of the screen (countdown, KO, result).
     *  Chosen by the director from the match phase so the HUD stays a renderer. */
    const FString& GetBannerText() const { return BannerText; }
    float GetBannerAge() const { return BannerAgeSeconds; }
    bool IsBannerProminent() const { return bBannerProminent; }

    /** Seconds since MatchEnd, or -1 while the match is still running. */
    float GetSecondsSinceMatchEnd() const;
    bool IsRematchOffered() const;

    // ---- Actions -----------------------------------------------------------
    /** Starts a brand-new match with the same setup (used by the rematch key and
     *  by the console command). Fighters and ring are reused, not respawned. */
    void RestartMatch();

    /** Re-reads the rkc.* console variables into Setup, then restarts. */
    void ApplyConsoleOverridesAndRestart();

    /** Requests the match be abandoned. In a packaged game this exits; in the
     *  editor it is logged and ignored so a running PIE session is not killed. */
    void RequestQuitMatch();

    /** Starts the match the front end has just configured: reads its request into
     *  Setup, applies it, resets the input scheme and restarts the simulation.
     *  Called once per accepted request, so a request the machine has already
     *  consumed cannot be applied twice. */
    void BeginMatchFromFrontEnd();

    /** Leaves gameplay for the menu. Drops every latched human input so nothing
     *  can fire on the way back in, releases the audio bed, and hands the screen
     *  back to the front end. */
    void ReturnToFrontEnd();

    /** Outcome summary of the most recently completed match, or an explanatory
     *  string while one is still in progress. Used by the evidence record and by
     *  the console dump. */
    FString DescribeCurrentMatch() const;

    /** Deterministic fingerprint of the live simulation state. Two runs of the
     *  same match with the same seeds must produce the same number; this is how
     *  the evidence record proves determinism rather than asserting it. */
    static uint32 ComputeStateChecksum(const RKC::FCombatSim& InSim);

    /** Writes the evidence record for a finished match. Returns the file path, or
     *  an empty string when nothing was written. */
    FString WriteEvidenceRecord(const TCHAR* Reason);

    // ---- In-engine self-test -----------------------------------------------
    // Enabled from the command line, never from gameplay:
    //     UnrealEditor-Cmd.exe RockyKombat.uproject -game -nullrhi -RKCSelfTest=3
    //
    // WHAT IT IS FOR: the standalone harness in Tools/AITests proves the rules in
    // plain C++, but it cannot prove that the ENGINE build - the shipped
    // UnrealEditor-RockyKombat.dll, the world this class builds, the fighter and
    // arena actors, and the HUD - behaves the same way. This mode closes that gap
    // by driving complete matches through the real per-frame director loop
    // (bots -> Sim.Advance -> DrainEvents -> SyncFromSim -> UpdateBanner), then
    // cross-checking the identical match against the pure-C++ harness in the same
    // process, re-running it to prove repeatability, and finally asking the HUD
    // how many frames and regions it actually drew.
    //
    // It writes Saved/Evidence/RKC_SELFTEST_*.txt and exits the process with 0 on
    // success or 1 on failure, so a CI job or a Gate check can read the result
    // without a human looking at a screen.
    void RunInEngineSelfTest(int32 MatchCount);

    /** True once the tick-driven half of the self-test has finished reporting. */
    bool IsSelfTestFinished() const { return bSelfTestFinished; }

    int32 GetSelfTestFailureCount() const { return SelfTestFailures; }

private:
    /** One frame of match direction, from input gathering to presentation.
     *  Tick() is a thin caller so the self-test can step the real loop at the
     *  simulation's fixed timestep instead of a test-only reimplementation. */
    void AdvanceMatchFrame(float DeltaSeconds);

    /** Enables the self-test when the command line asks for it. */
    void StartSelfTestIfRequested();

    /** Records one self-test assertion, raising it to a log line and to a counter. */
    void SelfTestCheck(bool bCondition, const FString& Message);

    /** Writes the self-test evidence record. Returns the path written. */
    FString WriteSelfTestRecord();

    /** Runs the match half of the self-test. Called once from StartPlay. */
    void RunSelfTestMatches();

    /** Runs the presentation half of the self-test: audio catalog, event planner,
     *  front-end state machine and touch controller. Called once from StartPlay,
     *  directly after RunSelfTestMatches(). */
    void RunSelfTestPresentationSystems();

    /** Reads the HUD's draw counters, writes the record and exits the process
     *  with a status code a CI job can read. Called from Tick, a few rendering
     *  frames after the matches finish, so the HUD has had a chance to draw. */
    void FinishSelfTestAndExit();

    /** Frame counts awaited after the match half, so the HUD gets a chance to
     *  draw before its counters are read. Zero rendering frames means headless. */
    static constexpr int32 SelfTestFramesToAwaitHud = 4;

    /** Frames the loading card waits for before it releases gameplay. Eight is
     *  enough for the arena materials, the lighting and the sprite materials to
     *  have been prepared by the renderer on the hardware this was built for, and
     *  short enough that a player does not notice it. */
    static constexpr int32 RenderWarmupFramesRequired = 8;

    /** Upper bound on the warm-up wait when nothing renders at all, so a CI run
     *  or a -nullrhi session cannot hang on a screen that will never fill in.
     *  Roughly ten seconds at 60 Hz; the loading card says so out loud. */
    static constexpr int32 RenderWarmupHeadlessTimeoutFrames = 600;

    void BuildWorld();
    void BuildLighting();
    void BuildCamera();
    void SpawnFighters();

    void GatherInputs(float DeltaSeconds, RKC::FInputState& OutA, RKC::FInputState& OutB);
    void DrainEvents();

    /** Presentation-only audio that has no simulation event behind it: footfalls
     *  measured from how far each fighter actually moved this frame, and the
     *  pre-round bell as the countdown reaches each second. Runs after the
     *  simulation has advanced and writes no simulation state, so a sound can
     *  never change a match. */
    void DrivePresentationAudio();
    void UpdateBanner();
    void PushFeed(const FString& Line);

    void OnMatchEnded();

    // ---- Front-end and lifecycle plumbing ----------------------------------
    /** Starts the audio layer and puts the front end on screen, unless this run
     *  is a self-test or asked for -rkc.FrontEnd=0. */
    void InitFrontEndAndAudio();

    /** One frame of menu logic: collects merged menu input from the human
     *  controller, ticks the machine, and acts on whatever it asked for. */
    void TickFrontEnd(float DeltaSeconds);

    /** Merges the controller's menu input into the form the machine consumes. */
    RKCFrontEnd::FInput CollectFrontEndInput();

    /** Recomputes the loading progress from real work and reports it to the
     *  machine. */
    void RefreshLoadingProgress();

    // ---- Stage capture (rkc.CaptureStages; evidence runs only) --------------
    /** One frame of an evidence run: asks the renderer for a screenshot when the
     *  front end reaches each stage of the plan in the .cpp, and records what the
     *  HUD had drawn at that moment. Does nothing unless rkc.CaptureStages is
     *  set, so normal play never takes a picture. Called once per frame from Tick,
     *  after the front end has decided which screen is up. */
    void MaybeCaptureStageShots();

    /** Queues one stage's screenshot and appends its row to the manifest. */
    void RequestStageShot(int32 StepIndex);

    /** Rewrites the manifest beside the screenshots. Called after every stage, so
     *  a run that is killed still leaves a reviewable record of what it got. */
    void WriteStageCaptureManifest();

    /** True while the on-screen pad is actually being drawn, which is what the
     *  touch-pad stage is evidence of. */
    bool IsTouchPadOnScreen() const;

    /** Feeds one simulation event to the audio layer. Presentation only: the
     *  simulation has already been advanced by the time this is called, so a
     *  sound can never influence a match. */
    void PlayAudioForEvent(const RKC::FCombatEventRecord& Event);

    /** Registered with FCoreDelegates so backgrounding a phone or losing focus
     *  cannot leave a fighter walking into the ropes or a music loop playing
     *  into an empty room. Both are removed again in EndPlay. */
    void HandleApplicationBackgrounded();
    void HandleApplicationForegrounded();

    /** Slate's activation-state hook. Some platforms never raise the core
     *  lifecycle delegates above, and a window that loses the keyboard is still
     *  a window that must stop taking fighter input, so both routes end in the
     *  same two handlers. */
    void HandleSlateActivationStateChanged(bool bIsActive);

    /** The one action both lifecycle hooks share: discard every latched human
     *  input, cancel every tracked touch, and freeze the audio without losing its
     *  position. */
    void DropHumanInputAndFreezeAudio(const TCHAR* Reason);

    /** Keeps the arena picture filling the match camera's view: gives the arena the
     *  rendered aspect ratio, the camera that is really in the world and the axis the
     *  engine applies its field of view to, and applies the 4:3 letterbox for a window
     *  narrower than that. Called once per frame from Tick and once from BuildCamera,
     *  so no screen ever shows an unframed picture. Presentation only: it writes a
     *  camera property and a backdrop transform, never a rule. */
    void UpdateArenaFraming(float DeltaSeconds);

    /** Runs the same match in the pure-C++ harness with the same seeds, to prove
     *  the engine build and the standalone build agree. Only meaningful when both
     *  sides are bots; returns false when it could not be run. */
    bool CrossCheckAgainstStandalone(FString& OutReport) const;

    void LogSetup() const;

    RKC::FCombatSim Sim;
    FRKCMatchSetup Setup;

    /** The startup, loading, menu and result machine. It decides nothing about a
     *  match itself; it decides who may start one, and with what. */
    RKCFrontEnd::FMachine FrontEnd;

    UPROPERTY(Transient)
    TObjectPtr<URKCAudioSystem> AudioSystem;

    /** Set once at startup. False means the director behaves exactly as the
     *  validated build did: a match exists from the first frame. */
    bool bFrontEndEnabled = false;

    /** Recomputed from the machine every frame, so the screen and the rules can
     *  never disagree about whether a match is live. */
    bool bFrontEndActive = false;

    /** Rendered frames counted since the world was built, for the warm-up gate
     *  and for the evidence record. */
    int32 WarmupFramesObserved = 0;
    bool bSawAnyHudDraw = false;
    float LoadingProgress = 0.0f;

    /** Front-end frames ticked since startup. Separate from the HUD draw count
     *  above because a run with no renderer never reports a draw, and the
     *  warm-up gate has to be able to give up on a screen that can never fill
     *  in rather than hang on it. */
    int32 FrontEndFramesTicked = 0;

    /** Lifecycle hooks, removed in EndPlay. Held so a torn-down world cannot
     *  leave a delegate pointing at a dead director. Backgrounded and
     *  Deactivated cover a phone going to the home screen or being interrupted;
     *  the focus pair covers a desktop window losing the keyboard; all four
     *  route to the same two handlers. */
    FDelegateHandle BackgroundedDelegateHandle;
    FDelegateHandle ForegroundedDelegateHandle;
    FDelegateHandle DeactivatedDelegateHandle;
    FDelegateHandle ReactivatedDelegateHandle;

    /** Registered on the Slate application's activation-state delegate, so a
     *  window that loses focus still drops input on platforms where the core
     *  lifecycle delegates never fire. */
    FDelegateHandle ActivationStateDelegateHandle;

    /** Last frame's pause state, so the single transition into Paused can drop
     *  every latched input exactly once. */
    bool bWasPaused = false;

    /** True while the machine was on a screen that lets the simulation run last
     *  frame. The transition out of gameplay is what releases the match, and it
     *  must happen exactly once. */
    bool bWasSimulating = false;

    /** Set the first time a finished match is reported to the front end, so the
     *  result card and its stinger are raised exactly once even when the
     *  evidence record could not be written. */
    bool bMatchEndReported = false;

    UPROPERTY(Transient)
    TObjectPtr<ARKCArena> Arena;

    UPROPERTY(Transient)
    TObjectPtr<ARKCFighter> FighterA;

    UPROPERTY(Transient)
    TObjectPtr<ARKCFighter> FighterB;

    UPROPERTY(Transient)
    TObjectPtr<ACameraActor> MatchCamera;

    /** The rendered aspect ratio and the letterbox state last applied to the match
     *  camera, so the per-frame framing pass can tell a change from a repeat. */
    float RenderAspectApplied = 0.0f;
    bool bLetterboxApplied = false;

    /** Offline opponents, one per bot-driven side. Plain C++ objects: the AI has
     *  no engine dependencies by design and must stay runnable in the standalone
     *  harness. */
    RKC::FBotBrain BotBrains[2];
    bool bBotActive[2] = {false, false};

    /** Fixed-step accumulator for the simulation, independent of frame rate. */
    float SimAccumulator = 0.0f;

    bool bWorldBuilt = false;
    bool bFrameDataVisible = false;
    bool bEvidenceWritten = false;
    bool bMatchWasRunning = false;

    /** Match-clock seconds elapsed when MatchEnd was reached, or -1. */
    float MatchEndedAtSimTime = -1.0f;
    /** Wall-clock seconds since MatchEnd was detected, for the rematch prompt. */
    float SecondsSinceMatchEnd = -1.0f;

    FString BannerText;
    float BannerAgeSeconds = 0.0f;
    bool bBannerProminent = false;

    /** Set when the round was still in Countdown at the top of this frame, so the
     *  "FIGHT!" banner can be raised exactly once. */
    bool bWasCountingDown = false;

    /** Presentation-only audio drivers that the simulation has no event for. */
    float FootstepAccumA = 0.0f;
    float FootstepAccumB = 0.0f;
    float FootstepLastPosA = 0.0f;
    float FootstepLastPosB = 0.0f;
    bool bFootstepAnchored = false;
    int32 LastCountdownSecond = -1;

    TArray<FRKCEventFeedLine> EventFeed;

    /** Integrity counters for the evidence record. */
    int32 TotalHitsA = 0;
    int32 TotalHitsB = 0;
    int32 TotalBlocksA = 0;
    int32 TotalBlocksB = 0;
    int32 TotalDodgesA = 0;
    int32 TotalDodgesB = 0;
    int32 TotalAttacksA = 0;
    int32 TotalAttacksB = 0;
    int32 TotalKnockdownsA = 0;
    int32 TotalKnockdownsB = 0;
    /** Largest breach of Rules.MinSeparation seen at any simulated tick. Must be
     *  zero for a match to be physically legal. */
    float WorstSeparationViolation = 0.0f;
    int32 TicksSimulated = 0;
    /** Countdown for the optional rkc.LogPositions trace, in seconds. */
    float PositionTraceCountdown = 0.0f;

    // ---- Stage capture state (inert unless rkc.CaptureStages is set) --------
    /** Set on the first frame of an evidence run, when the timestamped folder is
     *  fixed: one run's pictures land together and a later run cannot overwrite
     *  them. */
    bool bStageCaptureStarted = false;
    /** Set once the plan is done and the process has been asked to leave. */
    bool bStageCaptureExiting = false;
    /** Position in the plan table, which lives in the .cpp. */
    int32 StageCaptureStep = 0;
    int32 StageCaptureRequested = 0;
    /** Stages whose screenshot was asked for while the HUD was drawing. */
    int32 StageCaptureHudDrew = 0;
    /** Stages the run never reached; recorded rather than passed over. */
    int32 StageCaptureMissed = 0;
    /** HUD draw count at the previous stage, so each stage asserts that the HUD
     *  drew again during it rather than trusting a counter that only grows. */
    int32 StageCaptureLastHudFrames = 0;
    /** Frames and wall-clock seconds since the last request. The engine writes the
     *  image on a later frame, so leaving immediately after asking would lose it,
     *  and a frame count alone means nothing on a fast machine. */
    int32 StageCaptureFramesSinceShot = 0;
    double StageCaptureLastShotSeconds = 0.0;
    /** A stage that never arrives must not hang an unattended run. */
    double StageCaptureStepDeadlineSeconds = 0.0;
    FString StageCaptureDirectory;
    FString StageCaptureStartedAt;
    /** One line per stage: what was asked for and what the HUD reported. */
    TArray<FString> StageCaptureRows;

    // ---- Self-test state (inert unless -RKCSelfTest was passed) -------------
    bool bSelfTestActive = false;
    /** Set when the match half is done and the exit has been scheduled. */
    bool bSelfTestFinished = false;
    int32 SelfTestMatchesRequested = 0;
    /** Rounds the match half actually started, including the determinism re-run.
     *  Each one owes exactly one round-start event and therefore one round-start
     *  stinger, so the audio check can compare against a real count instead of
     *  against an assumption about how many rounds a match takes. */
    int32 SelfTestRoundsStarted = 0;
    /** Rounds that ended as a round break, which is the only thing that produces a
     *  round-end event. A knockout stops the contest outright (the simulation calls
     *  EnterMatchEnd, not EnterRoundEnd), so that round never breaks and correctly
     *  plays no round-end cue. Counting the breaks separately is what lets the audio
     *  check be an exact equality on a card decided entirely by knockout. */
    int32 SelfTestRoundsThatBroke = 0;
    int32 SelfTestFailures = 0;
    /** Rendering frames observed since the match half finished. */
    int32 SelfTestFramesSinceMatches = 0;
    bool bSelfTestExitScheduled = false;

    /** The assembled record. Built incrementally so a crash mid-test still leaves
     *  everything up to the crash in the log file. */
    FString SelfTestLog;
    /** One line per match: seeds, ticks, result and checksum. */
    TArray<FString> SelfTestMatchLines;
    TArray<uint32> SelfTestChecksums;

    /** HUD self-report captured before exit. -1 means no HUD object was found. */
    int32 SelfTestHudFrames = -1;
    int32 SelfTestHudRegions = -1;
    FIntPoint SelfTestHudCanvas = FIntPoint::ZeroValue;

    // ---- Scripted input driver (inert unless -rkc.Script / -rkc.ScriptFile) --
    // A dev-only timeline fed through APlayerController::InputKey, which is the
    // engine's own entry point for simulated player input. It exists because the
    // packaged build does not consume operating-system-injected keys, so the
    // only way to drive a hands-off playthrough through the REAL input path is
    // from inside the process. The driver reports, per event, whether the key
    // actually reached the controller's latches and the front end's counters, so
    // a timeline that ran without being consumed cannot pass quietly.
    //
    // A normal launch passes neither switch, so the driver never becomes active
    // and normal play is bit-for-bit what it was before.
    RKCScript::FDriver ScriptDriver;
};
