// RKCPlayerController.cpp

#include "Match/RKCPlayerController.h"

#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/InputSettings.h"
#include "Misc/OutputDeviceNull.h"
#include "RockyKombat.h"

namespace
{
/** Movement axis magnitude below which a stick/key is treated as neutral. The
 *  keyboard axis is only ever -1/0/+1, so this exists for the eventual gamepad
 *  mapping rather than for the current keys. */
constexpr float MoveDeadZone = 0.20f;

/** Maps an ESide onto the two latch slots. */
int32 LatchIndex(RKC::ESide Side)
{
    return (Side == RKC::ESide::B) ? 1 : 0;
}
}

// ---------------------------------------------------------------------------
// The binding table
// ---------------------------------------------------------------------------
//
// This is the list the game PROMISES. Config/DefaultInput.ini must supply it and
// the on-screen Controls screen must describe it; the in-engine self-test reads
// this table and checks both against the loaded input settings, so the three
// cannot drift apart silently. Add a binding here first, then in the ini, then
// to the control reference.

const TArray<FRKCBindingExpectation>& RKCBindingTable()
{
    static const TArray<FRKCBindingExpectation> Table = {
        // Movement: one axis per player, and the two players deliberately share
        // no key, because in a hot-seat match both hands are live at once.
        {TEXT("RKC_MoveX"), TEXT("A"), true, -1.0f},
        {TEXT("RKC_MoveX"), TEXT("D"), true, 1.0f},
        {TEXT("RKC_MoveX2"), TEXT("Left"), true, -1.0f},
        {TEXT("RKC_MoveX2"), TEXT("Right"), true, 1.0f},

        // Player 1.
        {TEXT("RKC_P1Block"), TEXT("S"), false, 0.0f},
        {TEXT("RKC_P1Jab"), TEXT("Z"), false, 0.0f},
        {TEXT("RKC_P1Heavy"), TEXT("X"), false, 0.0f},
        {TEXT("RKC_P1Dodge"), TEXT("C"), false, 0.0f},

        // Player 2, with the laptop-friendly alternates alongside the numpad.
        {TEXT("RKC_P2Block"), TEXT("Down"), false, 0.0f},
        {TEXT("RKC_P2Jab"), TEXT("NumPadOne"), false, 0.0f},
        {TEXT("RKC_P2Jab"), TEXT("Comma"), false, 0.0f},
        {TEXT("RKC_P2Heavy"), TEXT("NumPadTwo"), false, 0.0f},
        {TEXT("RKC_P2Heavy"), TEXT("Period"), false, 0.0f},
        {TEXT("RKC_P2Dodge"), TEXT("NumPadThree"), false, 0.0f},
        {TEXT("RKC_P2Dodge"), TEXT("Slash"), false, 0.0f},

        // Shared match controls.
        {TEXT("RKC_Rematch"), TEXT("R"), false, 0.0f},
        {TEXT("RKC_ToggleFrameData"), TEXT("F1"), false, 0.0f},
        {TEXT("RKC_QuitMatch"), TEXT("F10"), false, 0.0f},
        {TEXT("RKC_Pause"), TEXT("P"), false, 0.0f},
        {TEXT("RKC_Pause"), TEXT("Gamepad_Special_Right"), false, 0.0f},

        // Menu navigation.
        {TEXT("RKC_MenuUp"), TEXT("W"), false, 0.0f},
        {TEXT("RKC_MenuUp"), TEXT("Up"), false, 0.0f},
        {TEXT("RKC_MenuUp"), TEXT("Gamepad_DPad_Up"), false, 0.0f},
        {TEXT("RKC_MenuDown"), TEXT("S"), false, 0.0f},
        {TEXT("RKC_MenuDown"), TEXT("Down"), false, 0.0f},
        {TEXT("RKC_MenuDown"), TEXT("Gamepad_DPad_Down"), false, 0.0f},
        {TEXT("RKC_MenuLeft"), TEXT("A"), false, 0.0f},
        {TEXT("RKC_MenuLeft"), TEXT("Left"), false, 0.0f},
        {TEXT("RKC_MenuLeft"), TEXT("Gamepad_DPad_Left"), false, 0.0f},
        {TEXT("RKC_MenuRight"), TEXT("D"), false, 0.0f},
        {TEXT("RKC_MenuRight"), TEXT("Right"), false, 0.0f},
        {TEXT("RKC_MenuRight"), TEXT("Gamepad_DPad_Right"), false, 0.0f},
        {TEXT("RKC_MenuAccept"), TEXT("Enter"), false, 0.0f},
        {TEXT("RKC_MenuAccept"), TEXT("SpaceBar"), false, 0.0f},
        {TEXT("RKC_MenuAccept"), TEXT("Gamepad_FaceButton_Bottom"), false, 0.0f},
        {TEXT("RKC_MenuBack"), TEXT("Escape"), false, 0.0f},
        {TEXT("RKC_MenuBack"), TEXT("BackSpace"), false, 0.0f},
        {TEXT("RKC_MenuBack"), TEXT("Gamepad_FaceButton_Right"), false, 0.0f}};
    return Table;
}

FString RKCKeyDisplayName(const FString& KeyName)
{
    // Gamepad aliases are covered by one sentence on the Controls screen rather
    // than by repeating each button, so they map to nothing here.
    if (KeyName.StartsWith(TEXT("Gamepad_")) || KeyName.StartsWith(TEXT("Oculus")) ||
        KeyName.StartsWith(TEXT("Valve")) || KeyName.StartsWith(TEXT("Vive")) ||
        KeyName.StartsWith(TEXT("MixedReality")))
    {
        return FString();
    }

    static const TMap<FString, FString> Names = {
        {TEXT("Escape"), TEXT("ESCAPE")},     {TEXT("BackSpace"), TEXT("BACKSPACE")},
        {TEXT("Enter"), TEXT("ENTER")},       {TEXT("SpaceBar"), TEXT("SPACE")},
        {TEXT("Up"), TEXT("UP")},             {TEXT("Down"), TEXT("DOWN")},
        {TEXT("Left"), TEXT("LEFT")},         {TEXT("Right"), TEXT("RIGHT")},
        {TEXT("NumPadOne"), TEXT("Numpad 1")}, {TEXT("NumPadTwo"), TEXT("Numpad 2")},
        {TEXT("NumPadThree"), TEXT("Numpad 3")}, {TEXT("Comma"), TEXT("comma")},
        {TEXT("Period"), TEXT("period")},     {TEXT("Slash"), TEXT("slash")},
        {TEXT("A"), TEXT("A")},               {TEXT("D"), TEXT("D")},
        {TEXT("W"), TEXT("W")},               {TEXT("S"), TEXT("S")},
        {TEXT("Z"), TEXT("Z")},               {TEXT("X"), TEXT("X")},
        {TEXT("C"), TEXT("C")},               {TEXT("P"), TEXT("P")},
        {TEXT("R"), TEXT("R")},               {TEXT("F1"), TEXT("F1")},
        {TEXT("F10"), TEXT("F10")}};

    if (const FString* Found = Names.Find(KeyName))
    {
        return *Found;
    }
    return KeyName;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ARKCPlayerController::ARKCPlayerController()
{
    // The controller is the input source for the whole session; it must keep
    // ticking even though it never possesses a pawn (the simulation moves the
    // fighters, not the engine).
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = false;

    // The pad is off where there is no touch screen; the director turns it on
    // from the saved setting or from rkc.TouchControls.
    Touch.SetEnabled(false);
}

// ---------------------------------------------------------------------------
// Scheme
// ---------------------------------------------------------------------------

void ARKCPlayerController::SetInputScheme(ERKCInputScheme InScheme, RKC::ESide InSingleSide)
{
    Scheme = InScheme;
    SingleSide = (InSingleSide == RKC::ESide::B) ? RKC::ESide::B : RKC::ESide::A;
}

bool ARKCPlayerController::DrivesSide(RKC::ESide Side) const
{
    if (Side != RKC::ESide::A && Side != RKC::ESide::B)
    {
        return false;
    }
    return (Scheme == ERKCInputScheme::HotSeat) || (Side == SingleSide);
}

// ---------------------------------------------------------------------------
// Fighter input
// ---------------------------------------------------------------------------

RKC::FInputState ARKCPlayerController::ConsumeInputForSide(RKC::ESide Side)
{
    RKC::FInputState Out;

    if (Side != RKC::ESide::A && Side != RKC::ESide::B)
    {
        return Out;
    }

    FSideLatch& Latch = Latches[LatchIndex(Side)];

    // Always clear the latches. If the controller does not currently drive this
    // side, clearing is still the correct behaviour: a key pressed while the
    // side was not being driven must not be replayed later.
    const float MoveAxis = Latch.MoveAxis;
    const bool bBlock = Latch.bBlockHeld;
    const int32 JabPresses = Latch.JabPresses;
    const int32 HeavyPresses = Latch.HeavyPresses;
    const int32 DodgePresses = Latch.DodgePresses;
    Latch.JabPresses = 0;
    Latch.HeavyPresses = 0;
    Latch.DodgePresses = 0;

    if (!bInputEnabled || !DrivesSide(Side))
    {
        return Out;
    }

    Out.bMoveLeft = (MoveAxis < -MoveDeadZone);
    Out.bMoveRight = (MoveAxis > MoveDeadZone);
    Out.bHoldBlock = bBlock;

    // A press counts even if the key was tapped and released between two frames.
    Out.bJabPressed = (JabPresses > 0);
    Out.bHeavyPressed = (HeavyPresses > 0);
    Out.bDodgePressed = (DodgePresses > 0);

    // The virtual pad writes over movement only while its stick is engaged, and
    // merges its own edge latches (which it consumes, so one tap is one edge).
    // The virtual pad is a one-player device, so it drives this controller's
    // primary side only: in hot-seat that is side A. Without this, one thumb on
    // the pad would walk and punch both fighters at once, which is the input leak
    // this layer exists to prevent.
    const RKC::ESide PadSide =
        (Scheme == ERKCInputScheme::SingleSide) ? SingleSide : RKC::ESide::A;
    if (Touch.IsEnabled() && Side == PadSide)
    {
        Touch.ApplyTo(Out);
    }

    return Out;
}

void ARKCPlayerController::SetInputEnabled(bool bEnabled)
{
    if (bInputEnabled == bEnabled)
    {
        return;
    }
    bInputEnabled = bEnabled;

    if (!bInputEnabled)
    {
        // Drop everything, including held keys and every tracked finger, so a
        // key or a thumb held across the disabled period does not resume as a
        // stale press. Held movement will be re-reported by the axis binding on
        // the next frame in which the key state is polled, so no legitimate
        // input is lost.
        for (FSideLatch& Latch : Latches)
        {
            Latch = FSideLatch();
        }
        Touch.CancelAllTouches(TEXT("input-disabled"));
    }
}

void ARKCPlayerController::SetGameplayRouting(bool bInGameplay)
{
    if (bGameplayRouting == bInGameplay)
    {
        return;
    }
    bGameplayRouting = bInGameplay;

    // Changing mode drops everything from the old mode: a thumb held on the pad
    // when the match ends must not keep the next menu scrolling, and a menu tap
    // must not become a punch.
    ClearAllHumanInput(bInGameplay ? TEXT("entering-gameplay") : TEXT("leaving-gameplay"));
}

int32 ARKCPlayerController::ClearAllHumanInput(const TCHAR* Reason)
{
    for (FSideLatch& Latch : Latches)
    {
        Latch = FSideLatch();
    }
    MenuUpPresses = 0;
    MenuDownPresses = 0;
    MenuLeftPresses = 0;
    MenuRightPresses = 0;
    bMenuAccept = false;
    bMenuBack = false;
    bMenuPause = false;
    PendingMenuTaps.Reset();

    const int32 Dropped = Touch.CancelAllTouches(Reason);
    UE_LOG(LogRockyKombat, Verbose, TEXT("Human input cleared (%s); %d touch(es) cancelled."),
           Reason ? Reason : TEXT("unspecified"), Dropped);
    return Dropped;
}

int32 ARKCPlayerController::GetLatchedPressCount() const
{
    int32 Total = MenuUpPresses + MenuDownPresses + MenuLeftPresses + MenuRightPresses;
    for (const FSideLatch& Latch : Latches)
    {
        Total += Latch.JabPresses + Latch.HeavyPresses + Latch.DodgePresses;
    }
    return Total;
}

void ARKCPlayerController::FlushInputStackNow()
{
    // The engine's own processing step, run early. APlayerController declares it
    // protected and calls it from its tick; calling it from this subclass is the
    // supported way to run it out of band. It walks the input stack through the
    // real chain (PlayerInput -> UInputComponent -> the bindings below) exactly as
    // a tick would, and an empty stack is a no-op, so the tick that follows a
    // dispatched event costs nothing extra.
    ProcessPlayerInput(0.0f, /*bGamePaused*/ false);
}

bool ARKCPlayerController::IsAnyHumanInputHeld() const
{
    // A held guard and a movement axis are continuous states, so they are checked
    // here as well as the edge counts: a key held down across a focus change
    // produces no new edge, which is exactly why it has to be dropped explicitly.
    for (const FSideLatch& Latch : Latches)
    {
        if (Latch.bBlockHeld || FMath::Abs(Latch.MoveAxis) > MoveDeadZone || Latch.JabPresses > 0 ||
            Latch.HeavyPresses > 0 || Latch.DodgePresses > 0)
        {
            return true;
        }
    }

    if (MenuUpPresses > 0 || MenuDownPresses > 0 || MenuLeftPresses > 0 || MenuRightPresses > 0 ||
        bMenuAccept || bMenuBack || bMenuPause || PendingMenuTaps.Num() > 0)
    {
        return true;
    }

    return Touch.GetTrackedFingerCount() > 0 || Touch.IsStickActive() || Touch.IsBlockHeld() ||
           FMath::Abs(Touch.GetStickAxis()) > MoveDeadZone;
}

void ARKCPlayerController::SimulateHeldKeyboardForTest()
{
    FSideLatch& Latch = LatchFor(RKC::ESide::A);
    Latch.MoveAxis = 1.0f;
    Latch.bBlockHeld = true;
    Latch.JabPresses = 1;
}

void ARKCPlayerController::SetTouchControlsEnabled(bool bEnabled)
{
    Touch.SetEnabled(bEnabled);
    if (!bEnabled)
    {
        Touch.CancelAllTouches(TEXT("touch-disabled"));
    }
}

void ARKCPlayerController::SetTouchForcedVisible(bool bForced)
{
    Touch.SetForcedVisible(bForced);
}

// ---------------------------------------------------------------------------
// Menu input
// ---------------------------------------------------------------------------

FRKCMenuInput ARKCPlayerController::ConsumeMenuInput(const FVector2D& ViewportSize)
{
    FRKCMenuInput Out;
    Out.ViewportSize = ViewportSize;

    // A press is one step. Holding a direction does not repeat: repeat is a UI
    // behaviour, and the front end can add one without the input layer knowing.
    if (MenuUpPresses > 0)
    {
        Out.Navigate -= 1;
    }
    if (MenuDownPresses > 0)
    {
        Out.Navigate += 1;
    }
    if (MenuLeftPresses > 0)
    {
        Out.Adjust -= 1;
    }
    if (MenuRightPresses > 0)
    {
        Out.Adjust += 1;
    }
    Out.bAccept = bMenuAccept;
    Out.bBack = bMenuBack;
    Out.bPause = bMenuPause;
    Out.Taps = PendingMenuTaps;

    MenuUpPresses = 0;
    MenuDownPresses = 0;
    MenuLeftPresses = 0;
    MenuRightPresses = 0;
    bMenuAccept = false;
    bMenuBack = false;
    bMenuPause = false;
    PendingMenuTaps.Reset();

    return Out;
}

// ---------------------------------------------------------------------------
// One-shot match requests
// ---------------------------------------------------------------------------

bool ARKCPlayerController::ConsumeRematchRequest()
{
    const bool bWas = bRematchRequested;
    bRematchRequested = false;
    return bWas;
}

bool ARKCPlayerController::ConsumeFrameDataToggle()
{
    const bool bWas = bFrameDataToggleRequested;
    bFrameDataToggleRequested = false;
    return bWas;
}

bool ARKCPlayerController::ConsumeQuitRequest()
{
    const bool bWas = bQuitRequested;
    bQuitRequested = false;
    return bWas;
}

float ARKCPlayerController::GetRawMoveAxis(RKC::ESide Side) const
{
    if (Side != RKC::ESide::A && Side != RKC::ESide::B)
    {
        return 0.0f;
    }
    return Latches[LatchIndex(Side)].MoveAxis;
}

ARKCPlayerController::FSideLatch& ARKCPlayerController::LatchFor(RKC::ESide Side)
{
    return Latches[LatchIndex(Side)];
}

const ARKCPlayerController::FSideLatch& ARKCPlayerController::LatchFor(RKC::ESide Side) const
{
    return Latches[LatchIndex(Side)];
}

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

void ARKCPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (!InputComponent)
    {
        UE_LOG(LogRockyKombat, Error,
               TEXT("Player input component is missing; the match has no human controls."));
        return;
    }

    // ---- Movement axes -----------------------------------------------------
    InputComponent->BindAxis(TEXT("RKC_MoveX"), this, &ARKCPlayerController::AxisMoveP1);
    InputComponent->BindAxis(TEXT("RKC_MoveX2"), this, &ARKCPlayerController::AxisMoveP2);

    // ---- Player 1 ----------------------------------------------------------
    InputComponent->BindAction(TEXT("RKC_P1Block"), IE_Pressed, this,
                               &ARKCPlayerController::OnP1BlockPressed);
    InputComponent->BindAction(TEXT("RKC_P1Block"), IE_Released, this,
                               &ARKCPlayerController::OnP1BlockReleased);
    InputComponent->BindAction(TEXT("RKC_P1Jab"), IE_Pressed, this,
                               &ARKCPlayerController::OnP1JabPressed);
    InputComponent->BindAction(TEXT("RKC_P1Heavy"), IE_Pressed, this,
                               &ARKCPlayerController::OnP1HeavyPressed);
    InputComponent->BindAction(TEXT("RKC_P1Dodge"), IE_Pressed, this,
                               &ARKCPlayerController::OnP1DodgePressed);

    // ---- Player 2 ----------------------------------------------------------
    InputComponent->BindAction(TEXT("RKC_P2Block"), IE_Pressed, this,
                               &ARKCPlayerController::OnP2BlockPressed);
    InputComponent->BindAction(TEXT("RKC_P2Block"), IE_Released, this,
                               &ARKCPlayerController::OnP2BlockReleased);
    InputComponent->BindAction(TEXT("RKC_P2Jab"), IE_Pressed, this,
                               &ARKCPlayerController::OnP2JabPressed);
    InputComponent->BindAction(TEXT("RKC_P2Heavy"), IE_Pressed, this,
                               &ARKCPlayerController::OnP2HeavyPressed);
    InputComponent->BindAction(TEXT("RKC_P2Dodge"), IE_Pressed, this,
                               &ARKCPlayerController::OnP2DodgePressed);

    // ---- Shared match ------------------------------------------------------
    InputComponent->BindAction(TEXT("RKC_Rematch"), IE_Pressed, this,
                               &ARKCPlayerController::OnRematchRequested);
    InputComponent->BindAction(TEXT("RKC_ToggleFrameData"), IE_Pressed, this,
                               &ARKCPlayerController::OnFrameDataToggleRequested);
    InputComponent->BindAction(TEXT("RKC_QuitMatch"), IE_Pressed, this,
                               &ARKCPlayerController::OnQuitRequested);
    InputComponent->BindAction(TEXT("RKC_Pause"), IE_Pressed, this,
                               &ARKCPlayerController::OnPauseRequested);

    // ---- Menu --------------------------------------------------------------
    InputComponent->BindAction(TEXT("RKC_MenuUp"), IE_Pressed, this,
                               &ARKCPlayerController::OnMenuUp);
    InputComponent->BindAction(TEXT("RKC_MenuDown"), IE_Pressed, this,
                               &ARKCPlayerController::OnMenuDown);
    InputComponent->BindAction(TEXT("RKC_MenuLeft"), IE_Pressed, this,
                               &ARKCPlayerController::OnMenuLeft);
    InputComponent->BindAction(TEXT("RKC_MenuRight"), IE_Pressed, this,
                               &ARKCPlayerController::OnMenuRight);
    InputComponent->BindAction(TEXT("RKC_MenuAccept"), IE_Pressed, this,
                               &ARKCPlayerController::OnMenuAccept);
    InputComponent->BindAction(TEXT("RKC_MenuBack"), IE_Pressed, this,
                               &ARKCPlayerController::OnMenuBack);

    // ---- Touch -------------------------------------------------------------
    // Bound through the same input component as the keys, so a finger and a key
    // travel the same path into the same latches.
    InputComponent->BindTouch(IE_Pressed, this, &ARKCPlayerController::OnTouchPressed);
    InputComponent->BindTouch(IE_Repeat, this, &ARKCPlayerController::OnTouchMoved);
    InputComponent->BindTouch(IE_Released, this, &ARKCPlayerController::OnTouchReleased);

    UE_LOG(LogRockyKombat, Log, TEXT("Player input bound (scheme=%s, %d promised binding(s))."),
           Scheme == ERKCInputScheme::HotSeat ? TEXT("hot-seat") : TEXT("single-side"),
           RKCBindingTable().Num());
}

void ARKCPlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);

    // Keep the pad's coordinate space equal to the real viewport, so a finger
    // lands where the control is drawn.
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
        if (Size.X > 0 && Size.Y > 0)
        {
            Touch.SetViewportSize(FVector2D(Size.X, Size.Y));
        }
    }

    // The pad is laid out inside the device's safe area, so the insets are kept
    // current here - after the viewport size, because the insets are scaled by it.
    RefreshSafeArea(DeltaSeconds);

    // The stale-touch watchdog. A finger the device never told us about leaving
    // is dropped here rather than leaving a fighter walking into the ropes.
    Touch.Tick(DeltaSeconds);

    // A phone has no pause key, so the on-screen pause control raises the same
    // request the key does. It lands in the same latch, so the front end cannot
    // tell (and does not need to tell) which device asked to stop the match.
    if (Touch.ConsumePauseRequest())
    {
        bMenuPause = true;
    }
}

void ARKCPlayerController::RefreshSafeArea(float DeltaSeconds)
{
    // WHY NOT EVERY FRAME: on a phone the platform metric is a JNI call into the
    // activity's safe-zone callback, and a cut-out does not move between frames -
    // asking 60 times a second would buy nothing and cost something every frame.
    // It is asked once a second, and immediately whenever the viewport size
    // changes, because a resize or a rotation is exactly the event that makes the
    // previous answer wrong.
    const FVector2D ViewportSize = Touch.GetViewportSize();
    const bool bViewportChanged = !ViewportSize.Equals(LastSafeAreaViewport);

    SafeAreaSecondsUntilRefresh -= DeltaSeconds;
    if (!bViewportChanged && SafeAreaSecondsUntilRefresh > 0.0f)
    {
        return;
    }

    SafeAreaSecondsUntilRefresh = SafeAreaRefreshSeconds;
    LastSafeAreaViewport = ViewportSize;

    const FMargin Insets = RKCTouch::ReadPlatformSafeInsetsPx(ViewportSize);
    const FMargin Before = Touch.GetSafeInsetsPx();
    Touch.SetSafeInsetsPx(Insets);

    // Logged only when the effective rectangle actually moves. This line is the
    // evidence that the metric was read rather than assumed: on a device it is
    // the cut-out the platform reported, and on a desktop it is the value the
    // override asked for (or nothing, which is itself the finding).
    const FMargin After = Touch.GetSafeInsetsPx();
    if (!FMath::IsNearlyEqual(Before.Left, After.Left, 0.5f) ||
        !FMath::IsNearlyEqual(Before.Top, After.Top, 0.5f) ||
        !FMath::IsNearlyEqual(Before.Right, After.Right, 0.5f) ||
        !FMath::IsNearlyEqual(Before.Bottom, After.Bottom, 0.5f))
    {
        UE_LOG(LogRockyKombat, Display,
               TEXT("safe area: insets L=%.0f T=%.0f R=%.0f B=%.0f px, viewport %.0fx%.0f %s"),
               After.Left, After.Top, After.Right, After.Bottom, ViewportSize.X, ViewportSize.Y,
               Touch.HasSafeAreaInset() ? TEXT("(pad laid out inside the safe rectangle)")
                                        : TEXT("(no safe area reported: pad uses the whole viewport)"));
    }
}

void ARKCPlayerController::AxisMoveP1(float Value)
{
    LatchFor(RKC::ESide::A).MoveAxis = Value;
}

void ARKCPlayerController::AxisMoveP2(float Value)
{
    LatchFor(RKC::ESide::B).MoveAxis = Value;
}

void ARKCPlayerController::OnP1BlockPressed()
{
    LatchFor(RKC::ESide::A).bBlockHeld = true;
}

void ARKCPlayerController::OnP1BlockReleased()
{
    LatchFor(RKC::ESide::A).bBlockHeld = false;
}

void ARKCPlayerController::OnP1JabPressed()
{
    ++LatchFor(RKC::ESide::A).JabPresses;
}

void ARKCPlayerController::OnP1HeavyPressed()
{
    ++LatchFor(RKC::ESide::A).HeavyPresses;
}

void ARKCPlayerController::OnP1DodgePressed()
{
    ++LatchFor(RKC::ESide::A).DodgePresses;
}

void ARKCPlayerController::OnP2BlockPressed()
{
    LatchFor(RKC::ESide::B).bBlockHeld = true;
}

void ARKCPlayerController::OnP2BlockReleased()
{
    LatchFor(RKC::ESide::B).bBlockHeld = false;
}

void ARKCPlayerController::OnP2JabPressed()
{
    ++LatchFor(RKC::ESide::B).JabPresses;
}

void ARKCPlayerController::OnP2HeavyPressed()
{
    ++LatchFor(RKC::ESide::B).HeavyPresses;
}

void ARKCPlayerController::OnP2DodgePressed()
{
    ++LatchFor(RKC::ESide::B).DodgePresses;
}

void ARKCPlayerController::OnRematchRequested()
{
    bRematchRequested = true;
}

void ARKCPlayerController::OnFrameDataToggleRequested()
{
    bFrameDataToggleRequested = true;
}

void ARKCPlayerController::OnQuitRequested()
{
    bQuitRequested = true;
}

void ARKCPlayerController::OnPauseRequested()
{
    bMenuPause = true;
}

void ARKCPlayerController::OnMenuUp()
{
    ++MenuUpPresses;
}

void ARKCPlayerController::OnMenuDown()
{
    ++MenuDownPresses;
}

void ARKCPlayerController::OnMenuLeft()
{
    ++MenuLeftPresses;
}

void ARKCPlayerController::OnMenuRight()
{
    ++MenuRightPresses;
}

void ARKCPlayerController::OnMenuAccept()
{
    bMenuAccept = true;
}

void ARKCPlayerController::OnMenuBack()
{
    bMenuBack = true;
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------

void ARKCPlayerController::OnTouchPressed(ETouchIndex::Type FingerIndex, FVector Location)
{
    const FVector2D Position(Location.X, Location.Y);
    LastTouchPosition = Position;

    if (bGameplayRouting)
    {
        // On the pad, a finger that misses every control is simply not a
        // control; it is not a menu tap.
        Touch.TouchBegin(static_cast<int32>(FingerIndex), Position);
        return;
    }

    // On a menu screen every touch is a candidate tap; the front end decides
    // whether it landed on a row.
    PendingMenuTaps.Add(Position);
}

void ARKCPlayerController::OnTouchMoved(ETouchIndex::Type FingerIndex, FVector Location)
{
    if (!bGameplayRouting)
    {
        return;
    }
    Touch.TouchMove(static_cast<int32>(FingerIndex), FVector2D(Location.X, Location.Y));
}

void ARKCPlayerController::OnTouchReleased(ETouchIndex::Type FingerIndex, FVector Location)
{
    const FVector2D Position(Location.X, Location.Y);

    if (bGameplayRouting)
    {
        Touch.TouchEnd(static_cast<int32>(FingerIndex), TEXT("released"));
        return;
    }

    // A drag on a menu is not a tap. A few pixels of thumb wobble is, so the
    // tolerance is generous rather than exact.
    constexpr float TapSlopPx = 24.0f;
    if (FVector2D::Distance(Position, LastTouchPosition) <= TapSlopPx)
    {
        PendingMenuTaps.Add(Position);
    }
}
