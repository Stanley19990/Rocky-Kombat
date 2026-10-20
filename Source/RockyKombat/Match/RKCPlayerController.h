// RKCPlayerController.h
//
// Human input for Rocky Kombat, from a keyboard and from a touch screen.
//
// RESPONSIBILITY: this class does exactly one thing - turn physical input into
// something the rest of the game can consume. It never touches the simulation,
// never moves a fighter and never decides a rule. The match director
// (ARKCGameMode) asks it for fighter input once per frame and hands the result
// straight to RKC::FCombatSim; the front end (RKCFrontEnd::FMachine) asks it for
// menu input once per frame and never learns which device produced it.
//
// TWO MODES, ONE CONTROLLER: a match and a menu are different modes, and the
// same physical key means different things in each (W moves Player 1 and also
// navigates a menu). Rather than guess, the controller keeps the two sets of
// latches apart - fighter latches, consumed by ConsumeInputForSide, and menu
// latches, consumed by ConsumeMenuInput - and the director tells it which mode
// is live with SetGameplayRouting. A key pressed in the wrong mode is dropped,
// not replayed.
//
// EDGE LATCHING (why presses are counted rather than sampled):
// Attack, dodge and every menu button must fire exactly once per key press, no
// matter how many simulation frames a rendered frame is worth. A key that goes
// down and up inside a single rendered frame would be invisible to a "read the
// key state now" poll, so presses are LATCHED as counts here and the count is
// cleared by the consumer. That makes the input layer lossless: a fast tap is
// never dropped, and a held key is never re-fired. RKC::FCombatSim applies the
// second half of the guarantee (only the first fixed step of a frame may consume
// edges - see FCombatSim::Advance).
//
// MENU INPUT IS NOT GATED BY SetInputEnabled: that switch exists to stop a
// fighter acting during a countdown or on a result screen, and it must not also
// silence the menu. Only the fighter latches are cleared by it.
//
// WHY ONE CONTROLLER DRIVES BOTH FIGHTERS BY DEFAULT:
// Rocky Kombat is a local hot-seat fighter first. Player 1 uses the left hand of
// the keyboard and Player 2 the right hand (see Config/DefaultInput.ini), which
// is what a two-player test on one machine needs and what avoids the local
// multi-player plumbing that would otherwise be untestable on target hardware.
// SingleSide exists for the future where each human has their own controller or
// their own client: the same class then drives one side, sourced from whatever
// device that controller owns. The simulation is unchanged either way.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Combat/RKCCombatCore.h"
#include "UI/RKCTouchControls.h"
#include "RKCPlayerController.generated.h"

/** How this controller's keys map onto fighters. */
UENUM()
enum class ERKCInputScheme : uint8
{
    /** One keyboard drives both fighters (default, local hot-seat testing). */
    HotSeat = 0,
    /** This controller drives exactly one side; the other side is driven by
     *  another controller or by the AI. */
    SingleSide = 1
};

/** One frame of menu input, already merged from keyboard, gamepad and touch. */
struct FRKCMenuInput
{
    /** -1 up, +1 down. One step per press. */
    int32 Navigate = 0;
    /** -1 left, +1 right, for adjustable rows such as the volume sliders. */
    int32 Adjust = 0;
    bool bAccept = false;
    bool bBack = false;
    bool bPause = false;
    /** Taps in PIXEL viewport space, for touch and mouse selection. */
    TArray<FVector2D> Taps;
    /** Viewport the taps were measured in. */
    FVector2D ViewportSize = FVector2D(1280.0, 720.0);
};

/** One binding the game promises to have, in the form a test can check against
 *  the loaded input settings. Declared here so Config/DefaultInput.ini, the
 *  Controls screen and the binding check cannot drift apart: the table below is
 *  the assertion, not a copy of it. */
struct FRKCBindingExpectation
{
    const TCHAR* Action;
    const TCHAR* Key;
    /** True for an axis mapping (movement), false for an action mapping. */
    bool bAxis;
    /** Scale for axis mappings; ignored for actions. */
    float AxisScale;
};

/** Every binding the game relies on. Read by the in-engine self-test. */
const TArray<FRKCBindingExpectation>& RKCBindingTable();

/** Human-readable name of a bound key, for the cross-check against the on-screen
 *  control reference. Returns an empty string for keys that are deliberately not
 *  documented as a player control (gamepad aliases). */
FString RKCKeyDisplayName(const FString& KeyName);

UCLASS()
class ROCKYKOMBAT_API ARKCPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    ARKCPlayerController();

    /** Chooses which fighter(s) this controller's keys drive. */
    void SetInputScheme(ERKCInputScheme InScheme, RKC::ESide InSingleSide = RKC::ESide::A);
    ERKCInputScheme GetInputScheme() const { return Scheme; }
    RKC::ESide GetSingleSide() const { return SingleSide; }

    /** True when this controller currently owns human input for the given side. */
    bool DrivesSide(RKC::ESide Side) const;

    /** Returns this frame's input for a side and clears that side's latched
     *  button edges. Call exactly once per rendered frame, immediately before the
     *  simulation is advanced, so a press can never leak into a later frame or be
     *  silently dropped. Returns a neutral input when this controller does not
     *  drive the side or input is disabled. On-screen touch controls, when
     *  enabled, are merged in here. */
    RKC::FInputState ConsumeInputForSide(RKC::ESide Side);

    /** Master switch for FIGHTER input. Disabled during attract/result screens so
     *  a stray key press cannot queue an attack that fires on the next round
     *  start. Menu input is deliberately unaffected - see the header comment. */
    void SetInputEnabled(bool bEnabled);
    bool IsInputEnabled() const { return bInputEnabled; }

    // ---- Menu input -------------------------------------------------------
    /** True while the front end owns input: touches become menu taps instead of
     *  virtual-pad presses. */
    void SetGameplayRouting(bool bInGameplay);
    bool IsGameplayRouting() const { return bGameplayRouting; }

    /** One frame of menu input, merged from keyboard, gamepad and touch taps.
     *  Clears the menu latches. Safe to call on a match screen too (it simply
     *  returns nothing once the latches are drained)... it does not require the
     *  menu to be up. */
    FRKCMenuInput ConsumeMenuInput(const FVector2D& ViewportSize);

    /** Drops every latch, every pending edge and every tracked finger. Called on
     *  focus loss, on backgrounding, on pause and whenever the game changes mode.
     *  Returns the number of tracked touches that were cancelled. */
    int32 ClearAllHumanInput(const TCHAR* Reason);

    /** Raw counts, for the input self-test and the evidence record. */
    int32 GetLatchedPressCount() const;

    /** Diagnostic only (used by the scripted-input driver): asks this controller to
     *  run the engine's own input processing now instead of at its next tick, so a
     *  just-dispatched key has already reached its bound handler by the time the
     *  caller reads the latches. APlayerController keeps that step protected, so the
     *  subclass exposes it here rather than the caller reaching past the access
     *  specifier. Runs the real path (PlayerInput -> InputComponent -> bindings);
     *  processing an already-drained stack on the normal tick afterwards is a no-op. */
    void FlushInputStackNow();

    /** True when any human input is held right now or is still pending: a held
     *  guard, a movement axis off centre, an unconsumed press from either the
     *  keyboard latches or the on-screen pad, or a finger resting on the pad.
     *
     *  This is the state a lifecycle transition has to clear. Press counts alone
     *  would miss a held guard, which never becomes an edge, and would not see the
     *  pad at all. The in-engine self-test reads it before and after a simulated
     *  background/return transition instead of assuming the drop happened. */
    bool IsAnyHumanInputHeld() const;

    /** Puts the keyboard latches into the state a player is in while holding a
     *  movement key and the guard, with one punch still pending, so the lifecycle
     *  check can prove the drop clears all three. Never called by the game logic
     *  itself; it exists for the in-engine self-test. */
    void SimulateHeldKeyboardForTest();

    // ---- Touch controls ----------------------------------------------------
    RKCTouch::FController& GetTouchController() { return Touch; }
    const RKCTouch::FController& GetTouchController() const { return Touch; }
    void SetTouchControlsEnabled(bool bEnabled);
    bool AreTouchControlsEnabled() const { return Touch.IsEnabled(); }
    /** Diagnostic: draw and route the pad even where there is no touch screen. */
    void SetTouchForcedVisible(bool bForced);
    bool IsTouchForcedVisible() const { return Touch.IsForcedVisible(); }

    // ---- One-shot menu requests. Each returns true at most once per press. ----
    bool ConsumeRematchRequest();
    bool ConsumeFrameDataToggle();
    bool ConsumeQuitRequest();

    /** Diagnostics: the raw axis value last reported for a side, before the
     *  dead-zone test. Reported by the HUD so a tester can see the device is
     *  actually being read rather than assuming it. */
    float GetRawMoveAxis(RKC::ESide Side) const;

protected:
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float DeltaSeconds) override;

private:
    /** Everything latched for one fighter's set of keys. */
    struct FSideLatch
    {
        /** -1 fully left, +1 fully right. Continuous, not an edge. */
        float MoveAxis = 0.0f;
        /** Guard is a held state, not an edge. */
        bool bBlockHeld = false;
        /** Latched press counts, cleared by ConsumeInputForSide. */
        int32 JabPresses = 0;
        int32 HeavyPresses = 0;
        int32 DodgePresses = 0;
    };

    FSideLatch& LatchFor(RKC::ESide Side);
    const FSideLatch& LatchFor(RKC::ESide Side) const;

    // Axis bindings
    void AxisMoveP1(float Value);
    void AxisMoveP2(float Value);

    // Player 1 action bindings
    void OnP1BlockPressed();
    void OnP1BlockReleased();
    void OnP1JabPressed();
    void OnP1HeavyPressed();
    void OnP1DodgePressed();

    // Player 2 action bindings
    void OnP2BlockPressed();
    void OnP2BlockReleased();
    void OnP2JabPressed();
    void OnP2HeavyPressed();
    void OnP2DodgePressed();

    // Shared match bindings
    void OnRematchRequested();
    void OnFrameDataToggleRequested();
    void OnQuitRequested();
    void OnPauseRequested();

    // Menu bindings
    void OnMenuUp();
    void OnMenuDown();
    void OnMenuLeft();
    void OnMenuRight();
    void OnMenuAccept();
    void OnMenuBack();

    // Touch bindings
    void OnTouchPressed(ETouchIndex::Type FingerIndex, FVector Location);
    void OnTouchMoved(ETouchIndex::Type FingerIndex, FVector Location);
    void OnTouchReleased(ETouchIndex::Type FingerIndex, FVector Location);

    /** Re-reads the platform's safe-area insets and hands them to the pad. Called
     *  from PlayerTick on a slow timer, and immediately when the viewport size
     *  changes. See the implementation for why it is not read every frame. */
    void RefreshSafeArea(float DeltaSeconds);

    ERKCInputScheme Scheme = ERKCInputScheme::HotSeat;
    RKC::ESide SingleSide = RKC::ESide::A;

    /** Index 0 is side A, index 1 is side B. */
    FSideLatch Latches[2];

    bool bInputEnabled = true;
    bool bGameplayRouting = false;

    RKCTouch::FController Touch;

    /** How often the platform's safe-area metric is re-read. A camera cut-out does
     *  not move between frames, so a phone is asked once a second rather than once
     *  a frame; a viewport change (a resize, a rotation) forces a re-read at once,
     *  because that is the event that can make the previous answer wrong. */
    static constexpr float SafeAreaRefreshSeconds = 1.0f;
    float SafeAreaSecondsUntilRefresh = 0.0f;
    /** The viewport the current insets were read at. A negative size means
     *  "never read", so the first frame always reads once. */
    FVector2D LastSafeAreaViewport = FVector2D(-1.0, -1.0);

    /** Touches captured on a menu screen, replayed as taps in pixel space. */
    TArray<FVector2D> PendingMenuTaps;
    /** A touch that landed somewhere that is not one of the pad's controls is
     *  still reported as a menu tap, so tapping a menu row works anywhere. */
    FVector2D LastTouchPosition = FVector2D::ZeroVector;

    // Menu latches. Counts, for the same reason the fighter presses are counts.
    int32 MenuUpPresses = 0;
    int32 MenuDownPresses = 0;
    int32 MenuLeftPresses = 0;
    int32 MenuRightPresses = 0;
    bool bMenuAccept = false;
    bool bMenuBack = false;
    bool bMenuPause = false;

    /** Menu requests, cleared on read. */
    bool bRematchRequested = false;
    bool bFrameDataToggleRequested = false;
    bool bQuitRequested = false;
};
