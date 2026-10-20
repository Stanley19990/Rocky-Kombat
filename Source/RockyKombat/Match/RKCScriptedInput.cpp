// RKCScriptedInput.cpp
//
// The implementation. Read the header first: it explains why this exists, what
// it is not (it is not the front-end autopilot), and why every press is verified
// after delivery rather than assumed.

#include "Match/RKCScriptedInput.h"

#include "Combat/RKCCombatCore.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Match/RKCGameMode.h"
#include "Match/RKCPlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RockyKombat.h"
#include "UI/RKCFrontEnd.h"

namespace
{
/** The two documented switches.
 *
 *  They are registered as console variables for one reason beyond convenience:
 *  the game module applies every  -rkc.<Name>=<Value>  switch to a console
 *  variable at startup and reports an unknown name as a warning. Registering
 *  them means a scripted run is applied and logged as an override, rather than
 *  looking like a typo - and the value the run actually used is then in the log
 *  next to the run record. */
FString GRKCScriptName;
FString GRKCScriptFile;

FAutoConsoleVariableRef CVarRKCScript(
    TEXT("rkc.Script"), GRKCScriptName,
    TEXT("DEV-ONLY. Name of a built-in scripted-input timeline that drives a real ")
    TEXT("playthrough through the engine's own input path (playthrough). Empty means ")
    TEXT("no script runs, which is the normal case."),
    ECVF_Default);

FAutoConsoleVariableRef CVarRKCScriptFile(
    TEXT("rkc.ScriptFile"), GRKCScriptFile,
    TEXT("DEV-ONLY. Path to a timeline file: one step per line as ")
    TEXT("'<screen> <key> [holdSeconds] [delaySeconds]'. '#' starts a comment and '*' ")
    TEXT("as the screen means 'do not wait for a screen before pressing'."),
    ECVF_Default);

/** True for the wildcard screen, meaning "press without waiting". */
bool IsAnyScreen(const FString& Screen)
{
    return Screen.IsEmpty() || Screen == TEXT("*");
}
} // namespace

namespace RKCScript
{
// ---------------------------------------------------------------------------
// Building a timeline
// ---------------------------------------------------------------------------

void FDriver::AddStep(const TCHAR* Screen, const TCHAR* Key, float Hold, float Delay)
{
    FStep Step;
    Step.Screen = Screen;
    Step.Key = Key;
    Step.HoldSeconds = Hold;
    Step.DelayAfter = Delay;
    Steps.Add(Step);
}

/** One pass of the match block: the five combat verbs and movement, in the order
 *  the acceptance checklist lists them. Side A starts on the left of the ring
 *  (RKC::FCombatSim places fighter A at ArenaMinX + 120), so D walks the human
 *  towards the CPU and A walks away from it. */
void FDriver::AppendMatchBlock()
{
    AddStep(TEXT("Match"), TEXT("D"), 0.90f, 0.20f); // walk forward, closing distance
    AddStep(TEXT("Match"), TEXT("Z"), 0.06f, 0.45f); // jab
    AddStep(TEXT("Match"), TEXT("Z"), 0.06f, 0.45f); // jab
    AddStep(TEXT("Match"), TEXT("X"), 0.06f, 0.60f); // heavy punch
    AddStep(TEXT("Match"), TEXT("C"), 0.06f, 0.50f); // dodge
    AddStep(TEXT("Match"), TEXT("S"), 0.90f, 0.25f); // guard, held
    AddStep(TEXT("Match"), TEXT("A"), 0.55f, 0.15f); // walk back out
    AddStep(TEXT("Match"), TEXT("D"), 0.55f, 0.15f); // walk back in: close range
    AddStep(TEXT("Match"), TEXT("Z"), 0.06f, 0.30f); // jab
    AddStep(TEXT("Match"), TEXT("X"), 0.06f, 0.70f); // heavy punch
}

void FDriver::AppendPlaythrough()
{
    // ---- The front end, navigated entirely by key events -------------------
    // Each step waits for the screen the previous accept produced, so the run
    // does not depend on how long a load takes.
    AddStep(TEXT("Menu"), TEXT("Enter"), 0.08f, 1.00f);           // PLAY
    AddStep(TEXT("Experience"), TEXT("Enter"), 0.08f, 1.00f);     // ROCKY BOXING ARENA
    AddStep(TEXT("Arrangement"), TEXT("Enter"), 0.08f, 1.00f);    // PLAY AGAINST COMPUTER

    // Fighter selection, first stage: step off ROCKY onto SHADOW and take it, so
    // the HUMAN is Shadow. (The defect under test was that choosing Shadow still
    // left the human on Rocky.)
    AddStep(TEXT("CharacterSelect"), TEXT("Down"), 0.08f, 0.50f);
    AddStep(TEXT("CharacterSelect"), TEXT("Enter"), 0.08f, 0.80f);

    // Fighter selection, second stage: back onto ROCKY, who becomes the CPU
    // opponent, so the two fighters are visually distinct on the card and in the
    // ring.
    AddStep(TEXT("CharacterSelect"), TEXT("Up"), 0.08f, 0.50f);
    AddStep(TEXT("CharacterSelect"), TEXT("Enter"), 0.08f, 0.80f);

    AddStep(TEXT("Difficulty"), TEXT("Enter"), 0.08f, 1.00f);     // CHAMPION is preselected
    AddStep(TEXT("Confirm"), TEXT("Enter"), 0.08f, 0.50f);        // FIGHT

    // ---- Match one ---------------------------------------------------------
    const int32 FirstMatch = Steps.Num();
    AppendMatchBlock();
    // The block repeats for as long as the match runs, and leaves the moment the
    // result card is up, because the loop step's own screen no longer matches.
    Steps.Last().LoopWhileScreen = TEXT("Match");
    Steps.Last().JumpTo = FirstMatch;

    AddStep(TEXT("Results"), TEXT("Enter"), 0.08f, 0.50f);        // REMATCH

    // ---- Match two: the same block, reached through the result card ---------
    const int32 SecondMatch = Steps.Num();
    AppendMatchBlock();
    Steps.Last().LoopWhileScreen = TEXT("Match");
    Steps.Last().JumpTo = SecondMatch;

    // ---- Back out through the menu ----------------------------------------
    // Included deliberately: it proves the front end walks backwards as well as
    // forwards and that the quit path really does end the process, which no
    // injected-key run was ever able to show.
    AddStep(TEXT("Results"), TEXT("Down"), 0.08f, 0.40f);         // -> CHANGE FIGHTERS
    AddStep(TEXT("Results"), TEXT("Down"), 0.08f, 0.40f);         // -> MAIN MENU
    AddStep(TEXT("Results"), TEXT("Enter"), 0.08f, 1.00f);
    AddStep(TEXT("Menu"), TEXT("Down"), 0.08f, 0.35f);            // -> TRAINING
    AddStep(TEXT("Menu"), TEXT("Down"), 0.08f, 0.35f);            // -> CONTROLS
    AddStep(TEXT("Menu"), TEXT("Down"), 0.08f, 0.35f);            // -> SETTINGS
    AddStep(TEXT("Menu"), TEXT("Down"), 0.08f, 0.80f);            // -> EXIT GAME
    AddStep(TEXT("Menu"), TEXT("Enter"), 0.08f, 1.00f);           // exit confirmation
    AddStep(TEXT("Quit"), TEXT("Down"), 0.08f, 0.50f);            // -> EXIT GAME
    AddStep(TEXT("Quit"), TEXT("Enter"), 0.08f, 1.00f);           // confirm, and the process ends
}

bool FDriver::AppendBuiltIn(const FString& Name)
{
    if (Name.Equals(TEXT("playthrough"), ESearchCase::IgnoreCase) ||
        Name.Equals(TEXT("acceptance"), ESearchCase::IgnoreCase))
    {
        AppendPlaythrough();
        return true;
    }
    UE_LOG(LogRockyKombat, Error,
           TEXT("scripted input: no built-in timeline is named '%s' (known: playthrough)"), *Name);
    return false;
}

bool FDriver::AppendFile(const FString& Path)
{
    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
    {
        UE_LOG(LogRockyKombat, Error, TEXT("scripted input: could not read '%s'"), *Path);
        return false;
    }

    for (int32 Index = 0; Index < Lines.Num(); ++Index)
    {
        FString Line = Lines[Index];
        // A '#' starts a comment, and a blank line is nothing at all.
        int32 Hash = INDEX_NONE;
        if (Line.FindChar(TEXT('#'), Hash))
        {
            Line.LeftInline(Hash);
        }
        Line.TrimStartAndEndInline();
        if (Line.IsEmpty())
        {
            continue;
        }

        TArray<FString> Parts;
        Line.ParseIntoArrayWS(Parts);
        if (Parts.Num() < 2)
        {
            UE_LOG(LogRockyKombat, Warning,
                   TEXT("scripted input: line %d ('%s') is not '<screen> <key> [hold] [delay]' "
                        "and was ignored"),
                   Index + 1, *Line);
            continue;
        }

        FStep Step;
        Step.Screen = Parts[0];
        Step.Key = Parts[1];
        if (Parts.IsValidIndex(2))
        {
            Step.HoldSeconds = FCString::Atof(*Parts[2]);
        }
        if (Parts.IsValidIndex(3))
        {
            Step.DelayAfter = FCString::Atof(*Parts[3]);
        }
        Steps.Add(Step);
    }
    return Steps.Num() > 0;
}

int32 FDriver::InitFromCommandLine()
{
    // The switches are applied to the console variables by the game module before
    // any actor exists, so reading them here sees the run's requested values
    // whether they arrived on the command line or from the console.
    FString Name = GRKCScriptName;
    FString File = GRKCScriptFile;
    Name.TrimStartAndEndInline();
    File.TrimStartAndEndInline();

    if (Name.IsEmpty() && File.IsEmpty())
    {
        return 0; // Normal play: the driver stays out of the way entirely.
    }

    ScriptName = Name.IsEmpty() ? TEXT("file") : Name;

    bool bLoaded = false;
    if (!File.IsEmpty())
    {
        Source = FString::Printf(TEXT("file:%s"), *File);
        bLoaded = AppendFile(File);
    }
    else
    {
        Source = FString::Printf(TEXT("built-in:%s"), *Name);
        bLoaded = AppendBuiltIn(Name);
    }

    if (!bLoaded || Steps.Num() == 0)
    {
        UE_LOG(LogRockyKombat, Error,
               TEXT("scripted input: '%s' produced no steps, so nothing will be driven"),
               *Source);
        Steps.Reset();
        bActive = false;
        return 0;
    }

    bActive = true;
    Record.Reserve(Steps.Num() * 3 + 16);
    Record.Add(TEXT("Rocky Kombat - scripted input run record"));
    Record.Add(TEXT("========================================="));
    Record.Add(FString::Printf(TEXT("script:       %s"), *ScriptName));
    Record.Add(FString::Printf(TEXT("source:       %s"), *Source));
    Record.Add(FString::Printf(TEXT("steps:        %d"), Steps.Num()));
    Record.Add(FString::Printf(TEXT("run started:  %s"), *FDateTime::Now().ToString()));
    Record.Add(FString::Printf(TEXT("command line: %s"), FCommandLine::Get()));
    Record.Add(TEXT(""));
    Record.Add(TEXT("Every line is one synthesised key PRESS handed to the player controller's own"));
    Record.Add(TEXT("input path (APlayerController::InputKey), and then processed immediately, which"));
    Record.Add(TEXT("is the point at which the engine runs the controller's bound handlers. The probe"));
    Record.Add(TEXT("beside it is read before that call and again straight after it."));
    Record.Add(TEXT(""));
    Record.Add(TEXT("  latches    the controller's latched edge-press count (menu and attack keys)"));
    Record.Add(TEXT("  accepts/navigates   the front end's own cumulative counters"));
    Record.Add(TEXT("  axisA/axisB   the movement axis the controller is reporting for each side"));
    Record.Add(TEXT("  mapped/UNMAPPED   the engine's own answer to whether the key has a binding"));
    Record.Add(TEXT("  CONSUMED   at least one of those changed: the game acted on the key, not"));
    Record.Add(TEXT("             merely that the key was sent"));
    Record.Add(TEXT("  NOT CONSUMED   the key arrived but nothing in the game moved"));
    Record.Add(TEXT(""));

    UE_LOG(LogRockyKombat, Display,
           TEXT("scripted input: '%s' loaded from %s with %d step(s); driving the engine's own "
                "input path"),
           *ScriptName, *Source, Steps.Num());
    return Steps.Num();
}

// ---------------------------------------------------------------------------
// Probing and dispatch
// ---------------------------------------------------------------------------

FProbe FDriver::Probe(const ARKCGameMode& GameMode)
{
    FProbe Out;

    const RKCFrontEnd::FMachine& Machine = GameMode.GetFrontEnd();
    const RKCFrontEnd::FDiagnostics& Diag = Machine.GetDiagnostics();
    Out.Accepts = Diag.Accepts;
    Out.Navigates = Diag.Navigates;
    Out.Backs = Diag.Backs;
    Out.Pauses = Diag.Pauses;
    Out.Screen = GameMode.IsFrontEndEnabled() ? RKCFrontEnd::FMachine::ScreenName(Machine.GetScreen())
                                              : TEXT("Match");

    const TArray<RKCFrontEnd::FMenuItem>& Items = Machine.GetItems();
    const int32 Selection = Machine.GetSelection();
    if (Items.IsValidIndex(Selection))
    {
        Out.Selection = Items[Selection].Label;
    }

    if (const ARKCPlayerController* PC = GameMode.GetRKCPlayerController())
    {
        Out.LatchedPresses = PC->GetLatchedPressCount();
        Out.AxisA = PC->GetRawMoveAxis(RKC::ESide::A);
        Out.AxisB = PC->GetRawMoveAxis(RKC::ESide::B);
    }
    return Out;
}

bool FDriver::ProbeDiffers(const FProbe& Before, const FProbe& After)
{
    return Before.Accepts != After.Accepts || Before.Navigates != After.Navigates ||
           Before.Backs != After.Backs || Before.Pauses != After.Pauses ||
           Before.LatchedPresses != After.LatchedPresses ||
           !FMath::IsNearlyEqual(Before.AxisA, After.AxisA, 0.001f) ||
           !FMath::IsNearlyEqual(Before.AxisB, After.AxisB, 0.001f);
}

FString FDriver::DescribeProbe(const FProbe& In)
{
    return FString::Printf(TEXT("screen=%-15s sel=\"%s\" latches=%d accepts=%d navigates=%d"),
                           *In.Screen, *In.Selection, In.LatchedPresses, In.Accepts, In.Navigates);
}

bool FDriver::DispatchEvent(ARKCPlayerController& PC, const FKey& Key, EInputEvent Event,
                            const FString& KeyName)
{
    FViewport* Viewport = nullptr;
    if (GEngine != nullptr && GEngine->GameViewport != nullptr)
    {
        Viewport = GEngine->GameViewport->Viewport;
    }

    // The engine's own factory for simulated input: the same call the functional
    // tests use, so this is the supported path rather than a private one.
    const FInputKeyEventArgs Args = FInputKeyEventArgs::CreateSimulated(
        Key, Event, /*AmountDepressed*/ 1.0f, /*InNumSamplesOverride*/ -1,
        /*InputDevice*/ INPUTDEVICEID_NONE, /*bIsTouchEvent*/ false, Viewport);

    // One: hand the event to the controller, which records it in the input stack.
    const bool bMapped = PC.InputKey(Args);

    // Two: make the controller process that stack here, which is the step that
    // runs the bound handlers (OnMenuDown, AxisMoveP1, OnP1JabPressed, ...). The
    // controller does this itself in its own tick; asking it to do it now means
    // the handler has run before this function returns, so the caller's "after"
    // probe reads a state the press has already been through - no dependency on
    // whether the director or the controller happens to tick first, and no
    // second frame of latency to guess about. Processing an empty stack on the
    // controller's own tick immediately afterwards is harmless. The step itself
    // is protected on APlayerController, so it is reached through the project's
    // own controller, which exposes it for diagnostics.
    PC.FlushInputStackNow();

    UE_LOG(LogRockyKombat, Verbose, TEXT("scripted input: %s %s -> %s"), *KeyName,
           Event == IE_Pressed ? TEXT("down") : TEXT("up"),
           bMapped ? TEXT("mapped to a controller binding") : TEXT("not mapped"));
    return bMapped;
}

bool FDriver::PressAndVerify(ARKCGameMode& GameMode, ARKCPlayerController& PC, const FStep& Step)
{
    const FKey Key(*Step.Key);
    const FProbe Before = Probe(GameMode);

    const bool bMapped = DispatchEvent(PC, Key, IE_Pressed, Step.Key);

    const FProbe After = Probe(GameMode);
    const bool bConsumed = ProbeDiffers(Before, After);

    ++Dispatches;
    ++CheckedEvents;
    if (bConsumed)
    {
        ++ConsumedEvents;
    }

    Record.Add(FString::Printf(TEXT("[%8.2f] step %-3d %-15s %-9s %s  %s  %s"), ElapsedSeconds,
                               StepIndex + 1, *Step.Screen, *Step.Key,
                               bMapped ? TEXT("mapped") : TEXT("UNMAPPED"), *DescribeProbe(After),
                               bConsumed ? TEXT("CONSUMED") : TEXT("NOT CONSUMED")));

    return bConsumed;
}

// ---------------------------------------------------------------------------
// The per-frame loop
// ---------------------------------------------------------------------------

void FDriver::Tick(ARKCGameMode& GameMode, float DeltaSeconds)
{
    if (!bActive)
    {
        return;
    }

    ElapsedSeconds += DeltaSeconds;

    ARKCPlayerController* PC = GameMode.GetRKCPlayerController();
    if (PC == nullptr)
    {
        return;
    }

    PhaseSeconds += DeltaSeconds;

    switch (Phase)
    {
    case EPhase::WaitScreen:
    {
        if (!Steps.IsValidIndex(StepIndex))
        {
            WriteRecord(TEXT("script complete"));
            bActive = false;
            Phase = EPhase::Done;
            return;
        }

        const FStep& Step = Steps[StepIndex];
        const FString Live = GameMode.IsFrontEndEnabled()
                                 ? RKCFrontEnd::FMachine::ScreenName(GameMode.GetFrontEnd().GetScreen())
                                 : TEXT("Match");

        if (IsAnyScreen(Step.Screen) || Step.Screen == Live)
        {
            const FKey Key(*Step.Key);
            if (!Key.IsValid())
            {
                UE_LOG(LogRockyKombat, Warning,
                       TEXT("scripted input: step %d names key '%s', which is not a known key; "
                            "the step was skipped"),
                       StepIndex + 1, *Step.Key);
                ++StepIndex;
                PhaseSeconds = 0.0f;
                return;
            }

            // The press is delivered and judged here, in this call: the record line
            // written by PressAndVerify already says whether the game acted on it.
            PressAndVerify(GameMode, *PC, Step);

            Phase = EPhase::Hold;
            PhaseSeconds = 0.0f;
            return;
        }

        // A step that waits for a screen it will never see must not hang the run.
        // The whole consecutive run of steps for that screen is dropped at once,
        // which is what lets a match block whose match just ended walk on to the
        // result card instead of waiting out every remaining step.
        if (PhaseSeconds >= Step.MaxWaitSeconds)
        {
            const int32 SkippedFrom = StepIndex;
            while (Steps.IsValidIndex(StepIndex) && Steps[StepIndex].Screen == Step.Screen)
            {
                ++StepIndex;
            }
            ++SkippedBlocks;
            Record.Add(FString::Printf(
                TEXT("[%8.2f] skipped   steps %d-%d: '%s' never came up (live screen '%s')"),
                ElapsedSeconds, SkippedFrom + 1, StepIndex, *Step.Screen, *Live));
            UE_LOG(LogRockyKombat, Log,
                   TEXT("scripted input: '%s' did not appear within %.1fs, so steps %d-%d were "
                        "skipped"),
                   *Step.Screen, Step.MaxWaitSeconds, SkippedFrom + 1, StepIndex);
            PhaseSeconds = 0.0f;
        }
        return;
    }

    case EPhase::Hold:
    {
        const FStep& Step = Steps[StepIndex];
        if (PhaseSeconds >= Step.HoldSeconds)
        {
            const FKey Key(*Step.Key);
            if (Key.IsValid())
            {
                DispatchEvent(*PC, Key, IE_Released, Step.Key);
            }
            Phase = EPhase::Gap;
            PhaseSeconds = 0.0f;
        }
        return;
    }

    case EPhase::Gap:
    {
        const FStep& Step = Steps[StepIndex];
        if (PhaseSeconds < Step.DelayAfter)
        {
            return;
        }

        // A loop step returns to the start of its block while the screen it names
        // is still up, and falls through to the next step the moment it is not.
        if (Step.JumpTo != INDEX_NONE && !Step.LoopWhileScreen.IsEmpty())
        {
            const FString Live = GameMode.IsFrontEndEnabled()
                                     ? RKCFrontEnd::FMachine::ScreenName(GameMode.GetFrontEnd().GetScreen())
                                     : TEXT("Match");
            if (Live == Step.LoopWhileScreen)
            {
                ++Loops;
                StepIndex = Step.JumpTo;
                Phase = EPhase::WaitScreen;
                PhaseSeconds = 0.0f;
                return;
            }
        }

        ++StepIndex;
        Phase = Steps.IsValidIndex(StepIndex) ? EPhase::WaitScreen : EPhase::Done;
        PhaseSeconds = 0.0f;
        return;
    }

    case EPhase::Done:
    default:
        bActive = false;
        return;
    }
}

// ---------------------------------------------------------------------------
// The run record
// ---------------------------------------------------------------------------

FString FDriver::Describe() const
{
    return FString::Printf(
        TEXT("script '%s' (%s): step %d of %d, %d event(s) dispatched, %d of %d verified "
             "delivered, %d loop(s), %d skipped block(s)"),
        *ScriptName, *Source, FMath::Min(StepIndex + 1, Steps.Num()), Steps.Num(), Dispatches,
        ConsumedEvents, CheckedEvents, Loops, SkippedBlocks);
}

FString FDriver::WriteRecord(const TCHAR* Reason)
{
    if (bRecordWritten)
    {
        return RecordPath;
    }

    Record.Add(TEXT(""));
    Record.Add(FString::Printf(TEXT("--- %s ---"), Reason));
    Record.Add(Describe());
    Record.Add(FString::Printf(TEXT("events dispatched:  %d"), Dispatches));
    Record.Add(FString::Printf(TEXT("deliveries checked: %d (presses only; a release carries no "
                                    "edge of its own)"),
                               CheckedEvents));
    Record.Add(FString::Printf(TEXT("delivered:          %d"), ConsumedEvents));
    Record.Add(FString::Printf(TEXT("not delivered:      %d"), CheckedEvents - ConsumedEvents));
    Record.Add(FString::Printf(TEXT("match-block loops:  %d"), Loops));
    Record.Add(FString::Printf(TEXT("screen-block skips: %d"), SkippedBlocks));

    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Evidence/Scripted");
    const FString File = Directory /
                         FString::Printf(TEXT("%s_script-%s.txt"),
                                         *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")),
                                         *ScriptName);
    if (!FFileHelper::SaveStringArrayToFile(Record, *File))
    {
        UE_LOG(LogRockyKombat, Warning, TEXT("scripted input: could not write the run record to %s"),
               *File);
        return FString();
    }

    bRecordWritten = true;
    RecordPath = File;
    UE_LOG(LogRockyKombat, Display, TEXT("scripted input: run record written to %s"), *File);
    return File;
}
} // namespace RKCScript
