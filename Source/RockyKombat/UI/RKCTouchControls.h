// RKCTouchControls.h
//
// Rocky Kombat - the on-screen controls for touch devices.
//
// WHY THIS IS NOT UMG: the match HUD is already drawn as canvas primitives by
// ARKCHUD, and the touch pad has to sit in exactly the same coordinate space as
// the health bars and the frame data, at any resolution, with no widget
// hierarchy to keep in sync. So the pad is a plain C++ layout table plus a
// finger-tracking state machine, drawn by the HUD, and the input it produces is
// the same RKC::FInputState the keyboard produces. The simulation cannot tell
// which device fed it, and does not need to.
//
// MULTI-TOUCH: every finger is tracked separately, so moving and blocking while
// pressing a punch button works. Only one finger can own the stick at a time;
// a second finger that lands in the stick zone becomes a second button finger
// only if it lands on a button.
//
// NO STUCK INPUTS, THREE WAYS:
//   1. Releasing a finger clears exactly the control that finger owned.
//   2. CancelAllTouches() drops every latch and every pending press; the game
//      calls it on focus loss, on backgrounding, and when leaving gameplay.
//   3. A watchdog drops any finger that has produced no update for
//      StaleTouchSeconds, which covers a missed end event on a real device.
//
// SAFE AREA: on a phone held sideways the screen is not all usable. A camera
// cut-out eats one short edge and the system gesture bar eats part of the
// bottom, and a control drawn under either is a control the player cannot
// reliably press. The layout is therefore normalised against the SAFE rectangle
// rather than the raw viewport, so a stick that sits at 15.5% of the safe width
// is 15.5% of the width that a thumb can actually reach, on every device.
// The insets come from the platform (Android reports the cut-out and the
// gesture bar through FDisplayMetrics::TitleSafePaddingSize) and are clamped by
// ClampSafeInsets() so a missing or nonsense metric can never shrink the pad
// into uselessness - with no metric at all the safe rectangle is the viewport
// and the layout is exactly what it was before this existed.

// UNVERIFIED ON HARDWARE: the layout and the state machine can be exercised on a
// desktop build with injected touch positions (see the in-engine self-test and
// the -rkc.TouchSim diagnostic), but that is not a substitute for testing on a
// physical phone. The delivered notes say so explicitly.

#pragma once

#include "CoreMinimal.h"
#include "Combat/RKCCombatCore.h"

// FMargin lives in SlateCore, not in CoreMinimal. The safe area is expressed as
// a margin because that is the shape both the engine metric (a FVector4 of
// Left, Top, Right, Bottom) and the Slate safe-zone ratio use.
#include "Layout/Margin.h"

namespace RKCTouch
{
/** Which control a touch is on. */
enum class EControl : uint8
{
    None = 0,
    Stick = 1,
    Jab = 2,
    Heavy = 3,
    Block = 4,
    Dodge = 5,
    /** Leaves the match. Not a fighter input: it raises the same pause request the
     *  pause key does, because a phone has no keyboard to press. */
    Pause = 6
};

/** One drawable control. Positions are normalised so the pad scales with any
 *  screen; the HUD turns them into pixels with CentrePx()/RadiusPx(). */
struct FWidget
{
    EControl Control = EControl::None;
    FString Label;
    FVector2D Centre = FVector2D(0.0, 0.0);
    float RadiusFraction = 0.06f;   // of min(viewport width, height)
    FLinearColor Colour = FLinearColor::White;
    bool bIsStick = false;
};

/** One finger being tracked right now. */
struct FActiveTouch
{
    int32 Finger = -1;
    EControl Control = EControl::None;
    FVector2D StartPx = FVector2D::ZeroVector;
    FVector2D CurrentPx = FVector2D::ZeroVector;
    float SecondsSinceUpdate = 0.0f;
};

/** Everything the evidence record wants to know about touch, measured. */
struct FDiagnostics
{
    int32 Begins = 0;
    int32 Moves = 0;
    int32 Ends = 0;
    int32 Cancellations = 0;
    int32 StuckTouchesCleared = 0;
    int32 BeginsRejectedOutsideControls = 0;
    int32 StickDirectionChanges = 0;
    int32 PressesJab = 0;
    int32 PressesHeavy = 0;
    int32 PressesDodge = 0;
    int32 BlockPresses = 0;
    int32 BlockReleases = 0;
    int32 MaxSimultaneousFingers = 0;
    int32 EdgesDelivered = 0;
    int32 PressesPause = 0;
};

class FController
{
public:
    /** A finger that produces no update for this long is dropped. Longer than
     *  any deliberate hold, short enough that a missed end event cannot leave a
     *  fighter walking into the ropes. */
    static constexpr float StaleTouchSeconds = 4.0f;

    /** No single inset may claim more than this share of its axis. */
    static constexpr float SafeInsetMaxFraction = 0.15f;
    /** The safe rectangle always keeps at least this share of the screen. */
    static constexpr float SafeRectMinFraction = 0.5f;

    FController();

    // ---- Layout -------------------------------------------------------------
    const TArray<FWidget>& GetWidgets() const { return Widgets; }

    /** Where a widget's centre lands, in pixels. The layout fraction is applied
     *  to the safe rectangle, so InsetsPx=(0,0,0,0) reproduces the plain
     *  viewport mapping exactly. */
    static FVector2D CentrePx(const FWidget& Widget, const FVector2D& Viewport,
                              const FMargin& InsetsPx = FMargin(0.0f));
    /** A widget's draw radius in pixels, scaled by the safe rectangle's smaller
     *  side so buttons keep their relative size next to a cut-out. */
    static float RadiusPx(const FWidget& Widget, const FVector2D& Viewport,
                          const FMargin& InsetsPx = FMargin(0.0f));

    /** Caps requested insets at SafeInsetMaxFraction of each axis and keeps at
     *  least SafeRectMinFraction of the screen usable, scaling both sides of an
     *  axis together when it would not fit. Public so the HUD, the tests and the
     *  evidence record all describe the same rectangle. */
    static FMargin ClampSafeInsets(const FVector2D& Viewport, const FMargin& Requested);
    /** The usable rectangle (top-left and bottom-right, pixels) for a viewport
     *  and a set of requested insets, after clamping. */
    static void GetSafeRect(const FVector2D& Viewport, const FMargin& InsetsPx,
                            FVector2D& OutMin, FVector2D& OutMax);

    void SetViewportSize(const FVector2D& InSizePx);
    FVector2D GetViewportSize() const { return ViewportSize; }

    /** Records the safe-area insets (Left, Top, Right, Bottom in pixels, as the
     *  safe rectangle's distance from each screen edge). Called once per second
     *  by the player controller from the platform metric. */
    void SetSafeInsetsPx(const FMargin& InInsetsPx);
    /** The insets the layout is actually using, after clamping against the
     *  current viewport. */
    FMargin GetSafeInsetsPx() const { return EffectiveSafeInsetsPx(); }
    /** True when the layout is being squeezed by a non-zero safe area. */
    bool HasSafeAreaInset() const
    {
        const FMargin Inset = EffectiveSafeInsetsPx();
        return Inset.Left > 0.0f || Inset.Top > 0.0f || Inset.Right > 0.0f || Inset.Bottom > 0.0f;
    }

    // ---- Enable / visibility ------------------------------------------------
    void SetEnabled(bool bInEnabled);
    bool IsEnabled() const { return bEnabled; }
    /** True when the pad should be drawn. Kept separate from IsEnabled so a
     *  diagnostic run can force the pad on for a desktop screenshot. */
    bool IsVisible() const { return bEnabled; }
    void SetForcedVisible(bool bInForced) { bForcedVisible = bInForced; }
    bool IsForcedVisible() const { return bForcedVisible; }
    bool ShouldDraw() const { return bEnabled || bForcedVisible; }

    // ---- Input --------------------------------------------------------------
    /** Returns true when the touch landed on a control and is now tracked. */
    bool TouchBegin(int32 Finger, const FVector2D& PositionPx);
    void TouchMove(int32 Finger, const FVector2D& PositionPx);
    void TouchEnd(int32 Finger, const TCHAR* Reason);
    /** Drops every tracked finger and every latch. Safe to call at any time. */
    int32 CancelAllTouches(const TCHAR* Reason);
    /** Advances the stale-touch watchdog. Called once per frame by the HUD. */
    void Tick(float DeltaSeconds);

    // ---- Output -------------------------------------------------------------
    /** Writes this pad's contribution into an input state: movement, block and
     *  the edge-triggered presses. Pending presses are consumed, so one press
     *  produces exactly one edge. */
    void ApplyTo(RKC::FInputState& Out);

    /** True once per tap on the on-screen pause control, and clears the request.
     *  Separate from ApplyTo because pausing is not a fighter input: it is the
     *  same request the pause key raises. */
    bool ConsumePauseRequest();

    // ---- Read-only state (for the HUD, tests and evidence) ------------------
    int32 GetTrackedFingerCount() const { return ActiveTouches.Num(); }
    bool IsStickActive() const { return StickFinger >= 0; }
    FVector2D GetStickKnobOffsetPx() const;
    bool IsBlockHeld() const { return bBlockHeld; }
    float GetStickAxis() const { return StickAxis; }
    const TArray<FActiveTouch>& GetActiveTouches() const { return ActiveTouches; }
    const FDiagnostics& GetDiagnostics() const { return Diagnostics; }
    void ResetDiagnostics() { Diagnostics = FDiagnostics(); }
    FString DescribeState() const;

    /** The control whose centre is nearest a point, or None. Used by the
     *  controller to route a touch and by taps on the front end. */
    EControl ControlAt(const FVector2D& PositionPx) const;

private:
    FActiveTouch* FindTouch(int32 Finger);
    static void RefreshFingerCount(FDiagnostics& Diag, int32 Count);
    /** SafeInsetsPx clamped against the size the layout is currently using. */
    FMargin EffectiveSafeInsetsPx() const;

    TArray<FWidget> Widgets;
    TArray<FActiveTouch> ActiveTouches;
    FVector2D ViewportSize = FVector2D(1280.0, 720.0);
    /** Requested safe-area insets. Zero on any platform that reports none. */
    FMargin SafeInsetsPx = FMargin(0.0f);

    int32 StickFinger = -1;
    float StickAxis = 0.0f;      // -1, 0 or +1
    bool bBlockHeld = false;
    int32 PendingJab = 0;
    int32 PendingHeavy = 0;
    int32 PendingDodge = 0;

    bool bEnabled = false;
    bool bForcedVisible = false;
    bool bPauseRequested = false;

    FDiagnostics Diagnostics;
};

/** The platform's safe-area insets in VIEWPORT pixels (Left, Top, Right,
 *  Bottom), or all zero on a platform that does not report one.
 *
 *  Android reports the display cut-out and the navigation/gesture bar through
 *  FDisplayMetrics::TitleSafePaddingSize, which the platform fills from the same
 *  JNI safezone callback the Slate layer uses. That metric is in display pixels,
 *  so it is scaled to the viewport (ViewportSizePx) here - the two are the same
 *  size on a phone held fullscreen, and are not in a window. Pass the size the
 *  layout is using, or leave it zero to take the metric unscaled.
 *
 *  The console variable rkc.SafeAreaInsets ("L,T,R,B" in pixels, empty = use the
 *  platform) overrides the metric, which is how a desktop build, and the
 *  self-test, can prove the cut-out layout without a phone. */
FMargin ReadPlatformSafeInsetsPx(const FVector2D& ViewportSizePx = FVector2D::ZeroVector);
}
