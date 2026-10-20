// RKCScriptedInput.h
//
// DEV-ONLY scripted input driver: a timed key timeline fed through the engine's
// own input path, so a complete playthrough can be driven and recorded without a
// hand on the keyboard.
//
// WHY THIS EXISTS
// The packaged build does not consume operating-system-injected keyboard input.
// Three methods were tried on the shipped binary (keybd_event with a virtual key,
// keybd_event with a scan code, and PostMessage(WM_KEYDOWN) at the window), each
// after activating the window with a real click, and the menu selection did not
// move for any of them, while the same injection into Notepad arrived. The
// front end's own diagnostics also record every claimed "human" run as
// "0 accept(s), 0 navigate(s)", because those runs were actually the autopilot,
// which calls the state machine's ActivateItem directly and never touches the
// input path at all. So neither an injection tool nor the autopilot can produce
// the acceptance evidence the checklist asks for.
//
// WHAT IT IS
// A driver that synthesises key EVENTS and hands them to
// APlayerController::InputKey, the same entry point the engine's own functional
// tests use for simulated player input. From there the events travel the ordinary
// path: the binding table in Config/DefaultInput.ini, UInputComponent, and the
// controller's latch setters - exactly the functions a real keyboard would call.
// Nothing in this file reaches into the front end or the simulation directly.
//
// WHAT MAKES IT EVIDENCE RATHER THAN A CLAIM
// Every press is verified inside the same call that delivers it. InputKey only
// records the event; the bound handlers run later, when the controller processes
// its input stack - so this driver asks the controller to process the stack in
// the same call, the way the controller's own tick does, and only then reads the
// front end's accept/navigate/back counters and the controller's latched press
// count and movement axis. The run record says CONSUMED or NOT CONSUMED for each
// press, with the value of each counter beside it, so a timeline that "ran" but
// was not consumed cannot pass quietly, and no verdict depends on guesswork about
// which of two actors happened to tick first.
//
// GATING
// It is inert unless a run asks for it explicitly:
//     -rkc.Script=<name>          a built-in timeline (playthrough)
//     -rkc.ScriptFile=<path>      a timeline file, one step per line
// A normal launch passes neither, so normal play is untouched.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class ARKCGameMode;
class ARKCPlayerController;

namespace RKCScript
{
/** One synthesised key event in a timeline. */
struct FStep
{
    /** Front-end screen to wait for before pressing, as named by
     *  RKCFrontEnd::FMachine::ScreenName ("Menu", "CharacterSelect", "Match",
     *  "Results", ...). "*" (or an empty string) means "press now". */
    FString Screen;

    /** Key as FKey knows it: "Enter", "Down", "Up", "Z", "X", "C", "S", "D", "A". */
    FString Key;

    /** How long the key is held down before being released. */
    float HoldSeconds = 0.08f;

    /** Gap after the release before the next step may fire. */
    float DelayAfter = 0.35f;

    /** Loop support: if, at the end of the gap, the live screen is still this
     *  string, execution jumps back to JumpTo instead of advancing. Used to keep
     *  a match block repeating while the match runs and to leave it the instant
     *  the result card appears. */
    FString LoopWhileScreen;
    int32 JumpTo = INDEX_NONE;

    /** A step that waits for a screen it never sees is not allowed to hang the
     *  run: after this long it is skipped, along with the rest of its screen
     *  block, so a match that ends mid-block moves on to the result card. */
    float MaxWaitSeconds = 6.0f;
};

/** What the controller and the front end looked like at one instant. Read before
 *  and after every dispatched event, so "the key arrived" is measured. */
struct FProbe
{
    int32 Accepts = 0;
    int32 Navigates = 0;
    int32 Backs = 0;
    int32 Pauses = 0;
    int32 LatchedPresses = 0;
    float AxisA = 0.0f;
    float AxisB = 0.0f;
    FString Screen;
    FString Selection;
};

class FDriver
{
public:
    /** Reads -rkc.Script / -rkc.ScriptFile. Returns the number of steps loaded
     *  (0 means the driver stays inactive and normal play is unaffected). */
    int32 InitFromCommandLine();

    bool IsActive() const { return bActive; }
    int32 GetStepCount() const { return Steps.Num(); }
    const FString& GetSource() const { return Source; }

    /** One frame: waits for the step's screen, presses, holds, releases, gaps,
     *  then verifies that the event reached the latches. */
    void Tick(ARKCGameMode& GameMode, float DeltaSeconds);

    /** Writes the run record under Saved/Evidence/Scripted/. Returns its path. */
    FString WriteRecord(const TCHAR* Reason);

    /** One-line state, for the log and the record header. */
    FString Describe() const;

private:
    enum class EPhase : uint8
    {
        WaitScreen,
        Hold,
        Gap,
        Done
    };

    /** Appends one timeline step. */
    void AddStep(const TCHAR* Screen, const TCHAR* Key, float HoldSeconds, float DelayAfter);

    /** One pass of the match block: the five combat verbs and movement, in the
     *  order the acceptance checklist lists them. */
    void AppendMatchBlock();

    bool AppendBuiltIn(const FString& Name);
    bool AppendFile(const FString& Path);
    void AppendPlaythrough();

    /** Delivers one key event through APlayerController::InputKey and then asks
     *  the controller to process its input stack, which is where the engine runs
     *  the bound handlers. Doing both here is what makes the probe after it exact.
     *  The return value is the engine's own answer to "is this key mapped to
     *  something the controller handles?", which is a different fact from "did the
     *  game act on it" and is recorded beside it. */
    bool DispatchEvent(ARKCPlayerController& PC, const FKey& Key, EInputEvent Event,
                       const FString& KeyName);

    /** Presses the step's key, probes the game immediately before and after, and
     *  appends the record line. True means the press was consumed. */
    bool PressAndVerify(ARKCGameMode& GameMode, ARKCPlayerController& PC, const FStep& Step);

    static FProbe Probe(const ARKCGameMode& GameMode);
    static FString DescribeProbe(const FProbe& In);
    static bool ProbeDiffers(const FProbe& Before, const FProbe& After);

    TArray<FStep> Steps;
    FString Source;
    FString ScriptName;
    /** Where the run record was written, kept so the two exits (a completed
     *  timeline and an early teardown) cannot write it twice. */
    FString RecordPath;

    EPhase Phase = EPhase::WaitScreen;
    float PhaseSeconds = 0.0f;
    float ElapsedSeconds = 0.0f;
    int32 StepIndex = 0;
    int32 Dispatches = 0;
    int32 ConsumedEvents = 0;
    int32 CheckedEvents = 0;
    int32 SkippedBlocks = 0;
    int32 Loops = 0;
    bool bActive = false;
    bool bRecordWritten = false;

    TArray<FString> Record;
};
} // namespace RKCScript
