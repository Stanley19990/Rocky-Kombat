// RKCTouchControls.cpp

#include "UI/RKCTouchControls.h"

#include "GenericPlatform/GenericApplication.h"
#include "HAL/IConsoleManager.h"

namespace RKCTouch
{
namespace
{
/** Deadzone for the movement stick, as a fraction of min(viewport). Roughly a
 *  thumb's wobble at 1280x720; below it the fighter stands still on purpose. */
constexpr float StickDeadZoneFraction = 0.018f;

/** How far outside a control's radius a touch may land and still count. The
 *  stick is generous because a thumb never lands exactly on a centre; buttons
 *  are tight because two of them sit close together. */
constexpr float StickHitScale = 2.0f;
constexpr float ButtonHitScale = 1.25f;

/** The override for the safe area, as "Left,Top,Right,Bottom" in pixels, empty
 *  meaning "ask the platform".
 *
 *  WHY A CONSOLE VARIABLE AND NOT A PARAMETER: the safe area is the one part of
 *  this layout that cannot be reached from a desktop build without one - a
 *  laptop has no camera cut-out and no gesture bar - so the code path that only
 *  matters on a phone would never execute in the very environment the automated
 *  checks run in. The override makes that path reachable:
 *  -rkc.SafeAreaInsets=40,0,40,30 (the module applies -rkc.* switches to their
 *  console variables before the first frame) lets a desktop run, and a self-test,
 *  assert the layout, the hit testing and the clamping against the same numbers a
 *  phone would report. */
TAutoConsoleVariable<FString> CVarRkcSafeAreaInsets(
    TEXT("rkc.SafeAreaInsets"),
    TEXT(""),
    TEXT("Safe-area insets in pixels as \"Left,Top,Right,Bottom\".\n")
    TEXT("Empty = use the platform metric (display cut-out / gesture bar).\n")
    TEXT("Set from the command line as -rkc.SafeAreaInsets=40,0,40,30."),
    ECVF_Default);

/** Parses "L,T,R,B" in pixels. False for empty or malformed text, which is the
 *  signal to fall back to the platform rather than to a guess. */
bool ParseInsetsPx(const FString& Text, FMargin& Out)
{
    if (Text.IsEmpty())
    {
        return false;
    }
    TArray<FString> Parts;
    Text.TrimStartAndEnd().ParseIntoArray(Parts, TEXT(","), /*InCullEmpty=*/true);
    if (Parts.Num() != 4)
    {
        return false;
    }
    Out = FMargin(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]),
                  FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]));
    return true;
}

/** Clamps the two insets of one axis in place (Low = Left/Top, High =
 *  Right/Bottom).
 *
 *  Two rules, both of which exist so that a bad metric can never make the pad
 *  unusable:
 *   1. No single inset may claim more than MaxFraction of its axis.
 *   2. The two together may not leave less than MinRectFraction of the axis
 *      usable, and they are scaled together when they would, so the safe
 *      rectangle stays centred instead of sliding to one side.
 *  A negative inset becomes zero: some platforms report -1 for "no safe zone",
 *  and an unknown inset must never push a control off the screen. */
void ClampAxisInsets(double Extent, double MaxFraction, double MinRectFraction, float& Low, float& High)
{
    double LowValue = FMath::Max(0.0, static_cast<double>(Low));
    double HighValue = FMath::Max(0.0, static_cast<double>(High));

    const double Cap = Extent * MaxFraction;
    LowValue = FMath::Min(LowValue, Cap);
    HighValue = FMath::Min(HighValue, Cap);

    const double Total = LowValue + HighValue;
    const double Allowed = Extent * (1.0 - MinRectFraction);
    if (Total > Allowed && Total > 0.0)
    {
        const double Shrink = Allowed / Total;
        LowValue *= Shrink;
        HighValue *= Shrink;
    }

    Low = static_cast<float>(LowValue);
    High = static_cast<float>(HighValue);
}
}

FController::FController()
{
    // Layout is authored in landscape-normalised space; the bottom corners
    // belong to the thumbs, and the centre stays clear so the arena is visible.
    auto AddButton = [this](EControl Control, const TCHAR* Label, double X, double Y,
                            float Radius, const FLinearColor& Colour)
    {
        FWidget W;
        W.Control = Control;
        W.Label = Label;
        W.Centre = FVector2D(X, Y);
        W.RadiusFraction = Radius;
        W.Colour = Colour;
        W.bIsStick = false;
        Widgets.Add(W);
    };

    FWidget Stick;
    Stick.Control = EControl::Stick;
    Stick.Label = TEXT("MOVE");
    Stick.Centre = FVector2D(0.155, 0.690);
    Stick.RadiusFraction = 0.110f;
    Stick.Colour = FLinearColor(0.85f, 0.85f, 0.90f, 1.0f);
    Stick.bIsStick = true;
    Widgets.Add(Stick);

    // Jab and Heavy sit under the right thumb; Block is held, so it is closest
    // to the natural rest position; Dodge is the outer safety valve.
    AddButton(EControl::Jab,   TEXT("JAB"),    0.845, 0.740, 0.058f, FLinearColor(0.95f, 0.85f, 0.35f, 1.0f));
    AddButton(EControl::Heavy, TEXT("HEAVY"),  0.940, 0.620, 0.058f, FLinearColor(0.95f, 0.45f, 0.30f, 1.0f));
    AddButton(EControl::Block, TEXT("BLOCK"),  0.765, 0.820, 0.062f, FLinearColor(0.45f, 0.75f, 0.95f, 1.0f));
    AddButton(EControl::Dodge, TEXT("DODGE"),  0.940, 0.870, 0.058f, FLinearColor(0.60f, 0.90f, 0.55f, 1.0f));

    // Pause sits in the top right corner, out of both thumbs' reach during a
    // fight but inside a deliberate stretch: a phone has no pause key, and a match
    // a player cannot stop is worse than one they stop by accident.
    AddButton(EControl::Pause, TEXT("II"),     0.928, 0.100, 0.048f,
              FLinearColor(0.90f, 0.35f, 0.32f, 1.0f));
}

FMargin FController::ClampSafeInsets(const FVector2D& Viewport, const FMargin& Requested)
{
    if (!(Viewport.X > 1.0) || !(Viewport.Y > 1.0))
    {
        // No usable viewport yet. Report no safe area rather than an arbitrary
        // one, so the layout falls back to the plain viewport mapping until the
        // first real size arrives.
        return FMargin(0.0f);
    }

    FMargin Out = Requested;
    ClampAxisInsets(Viewport.X, SafeInsetMaxFraction, SafeRectMinFraction, Out.Left, Out.Right);
    ClampAxisInsets(Viewport.Y, SafeInsetMaxFraction, SafeRectMinFraction, Out.Top, Out.Bottom);
    return Out;
}

void FController::GetSafeRect(const FVector2D& Viewport, const FMargin& InsetsPx,
                              FVector2D& OutMin, FVector2D& OutMax)
{
    const FMargin Inset = ClampSafeInsets(Viewport, InsetsPx);
    OutMin = FVector2D(Inset.Left, Inset.Top);
    OutMax = FVector2D(Viewport.X - Inset.Right, Viewport.Y - Inset.Bottom);

    // The clamp already guarantees a usable rectangle for a real viewport; this
    // only protects the degenerate case of a viewport smaller than the insets it
    // was asked to clamp against.
    OutMax.X = FMath::Max(OutMax.X, OutMin.X);
    OutMax.Y = FMath::Max(OutMax.Y, OutMin.Y);
}

FVector2D FController::CentrePx(const FWidget& Widget, const FVector2D& Viewport, const FMargin& InsetsPx)
{
    FVector2D SafeMin;
    FVector2D SafeMax;
    GetSafeRect(Viewport, InsetsPx, SafeMin, SafeMax);

    // The authored fraction is applied to the SAFE rectangle, not to the raw
    // viewport. With no insets the rectangle IS the viewport, so this reduces to
    // the plain mapping - which is what makes the no-metric case byte-for-byte
    // the layout that existed before safe areas did.
    return FVector2D(SafeMin.X + Widget.Centre.X * (SafeMax.X - SafeMin.X),
                     SafeMin.Y + Widget.Centre.Y * (SafeMax.Y - SafeMin.Y));
}

float FController::RadiusPx(const FWidget& Widget, const FVector2D& Viewport, const FMargin& InsetsPx)
{
    FVector2D SafeMin;
    FVector2D SafeMax;
    GetSafeRect(Viewport, InsetsPx, SafeMin, SafeMax);

    // Sized by the safe rectangle's smaller side, so a button keeps its size
    // relative to the screen a thumb can actually reach.
    const double MinDim = FMath::Min(SafeMax.X - SafeMin.X, SafeMax.Y - SafeMin.Y);
    return static_cast<float>(Widget.RadiusFraction * MinDim);
}

void FController::SetViewportSize(const FVector2D& InSizePx)
{
    if (InSizePx.X > 1.0 && InSizePx.Y > 1.0)
    {
        ViewportSize = InSizePx;
    }
}

void FController::SetSafeInsetsPx(const FMargin& InInsetsPx)
{
    // Compared AFTER clamping, because that is the rectangle the layout uses: a
    // change the clamp flattens out cannot move a single control, and treating it
    // as a move would cancel a player's touch for nothing.
    const FMargin Before = EffectiveSafeInsetsPx();
    SafeInsetsPx = InInsetsPx;
    const FMargin After = EffectiveSafeInsetsPx();

    const bool bMoved = !FMath::IsNearlyEqual(Before.Left, After.Left, 0.5f) ||
                        !FMath::IsNearlyEqual(Before.Top, After.Top, 0.5f) ||
                        !FMath::IsNearlyEqual(Before.Right, After.Right, 0.5f) ||
                        !FMath::IsNearlyEqual(Before.Bottom, After.Bottom, 0.5f);
    if (bMoved)
    {
        // Every control has just moved, and a finger may be holding a latch that
        // was aimed at where its control used to be. Drop them all: a pad that
        // jumps under a thumb must not leave a fighter walking into the ropes.
        CancelAllTouches(TEXT("safe-area-changed"));
    }
}

FMargin FController::EffectiveSafeInsetsPx() const
{
    return ClampSafeInsets(ViewportSize, SafeInsetsPx);
}

void FController::SetEnabled(bool bInEnabled)
{
    if (bEnabled == bInEnabled)
    {
        return;
    }
    bEnabled = bInEnabled;
    // Switching the pad off is a cancellation event in its own right: no latch
    // may survive into a state where nothing can release it.
    CancelAllTouches(TEXT("pad-disabled"));
}

EControl FController::ControlAt(const FVector2D& PositionPx) const
{
    // Nearest control wins, so overlapping hit areas near the thumb never
    // resolve to the wrong button.
    //
    // Hit testing MUST use the same rectangle as the drawing, or a finger lands
    // where a button is not: the insets are resolved once here and handed to both
    // the centre and the radius.
    const FMargin Inset = EffectiveSafeInsetsPx();
    EControl Best = EControl::None;
    float BestDistance = TNumericLimits<float>::Max();
    for (const FWidget& Widget : Widgets)
    {
        const FVector2D Centre = CentrePx(Widget, ViewportSize, Inset);
        const float Radius = RadiusPx(Widget, ViewportSize, Inset);
        const float Scale = Widget.bIsStick ? StickHitScale : ButtonHitScale;
        const float Distance = FVector2D::Distance(PositionPx, Centre);
        if (Distance <= Radius * Scale && Distance < BestDistance)
        {
            BestDistance = Distance;
            Best = Widget.Control;
        }
    }
    return Best;
}

bool FController::TouchBegin(int32 Finger, const FVector2D& PositionPx)
{
    ++Diagnostics.Begins;

    if (!bEnabled || Finger < 0)
    {
        return false;
    }

    // A finger that is already tracked restarts cleanly rather than being
    // counted twice.
    TouchEnd(Finger, TEXT("restart"));

    const EControl Control = ControlAt(PositionPx);
    if (Control == EControl::None)
    {
        ++Diagnostics.BeginsRejectedOutsideControls;
        return false;
    }
    if (Control == EControl::Stick && StickFinger >= 0)
    {
        // One finger owns the stick; extra fingers in the stick zone are not
        // allowed to fight over movement.
        ++Diagnostics.BeginsRejectedOutsideControls;
        return false;
    }

    FActiveTouch Touch;
    Touch.Finger = Finger;
    Touch.Control = Control;
    Touch.StartPx = PositionPx;
    Touch.CurrentPx = PositionPx;
    Touch.SecondsSinceUpdate = 0.0f;
    ActiveTouches.Add(Touch);
    RefreshFingerCount(Diagnostics, ActiveTouches.Num());

    switch (Control)
    {
    case EControl::Stick:
        StickFinger = Finger;
        StickAxis = 0.0f;
        break;
    case EControl::Block:
        bBlockHeld = true;
        ++Diagnostics.BlockPresses;
        break;
    case EControl::Jab:
        ++PendingJab;
        ++Diagnostics.PressesJab;
        break;
    case EControl::Heavy:
        ++PendingHeavy;
        ++Diagnostics.PressesHeavy;
        break;
    case EControl::Dodge:
        ++PendingDodge;
        ++Diagnostics.PressesDodge;
        break;
    case EControl::Pause:
        // Not a fighter input: the request is handed to the controller, which
        // raises the same pause the pause key does.
        bPauseRequested = true;
        ++Diagnostics.PressesPause;
        break;
    default:
        break;
    }
    return true;
}

void FController::TouchMove(int32 Finger, const FVector2D& PositionPx)
{
    ++Diagnostics.Moves;
    FActiveTouch* Touch = FindTouch(Finger);
    if (!Touch)
    {
        return;
    }
    Touch->CurrentPx = PositionPx;
    Touch->SecondsSinceUpdate = 0.0f;

    if (Touch->Control != EControl::Stick)
    {
        return;
    }

    const float DeadZone = StickDeadZoneFraction * static_cast<float>(
        FMath::Min(ViewportSize.X, ViewportSize.Y));
    const float Delta = static_cast<float>(Touch->CurrentPx.X - Touch->StartPx.X);

    float NewAxis = 0.0f;
    // Hysteresis: once a direction is engaged it needs a deliberate return past
    // the deadzone to disengage, so a shaking thumb cannot stutter the walk.
    if (Delta < -DeadZone || (StickAxis < 0.0f && Delta < DeadZone * 0.4f))
    {
        NewAxis = -1.0f;
    }
    else if (Delta > DeadZone || (StickAxis > 0.0f && Delta > -DeadZone * 0.4f))
    {
        NewAxis = 1.0f;
    }

    if (!FMath::IsNearlyEqual(NewAxis, StickAxis))
    {
        ++Diagnostics.StickDirectionChanges;
    }
    StickAxis = NewAxis;
}

void FController::TouchEnd(int32 Finger, const TCHAR* Reason)
{
    ++Diagnostics.Ends;
    const int32 Index = ActiveTouches.IndexOfByPredicate(
        [Finger](const FActiveTouch& T) { return T.Finger == Finger; });
    if (Index == INDEX_NONE)
    {
        return;
    }

    const EControl Control = ActiveTouches[Index].Control;
    ActiveTouches.RemoveAt(Index);

    switch (Control)
    {
    case EControl::Stick:
        if (StickFinger == Finger)
        {
            StickFinger = -1;
            StickAxis = 0.0f;
        }
        break;
    case EControl::Block:
        // Only clear the hold if no other finger is still on the block button.
        if (!ActiveTouches.ContainsByPredicate([](const FActiveTouch& T) { return T.Control == EControl::Block; }))
        {
            if (bBlockHeld)
            {
                ++Diagnostics.BlockReleases;
            }
            bBlockHeld = false;
        }
        break;
    default:
        break;
    }
    (void)Reason;
}

int32 FController::CancelAllTouches(const TCHAR* Reason)
{
    const int32 Dropped = ActiveTouches.Num();
    const bool bHadBlock = bBlockHeld;
    ActiveTouches.Reset();
    StickFinger = -1;
    StickAxis = 0.0f;
    bBlockHeld = false;
    PendingJab = 0;
    PendingHeavy = 0;
    PendingDodge = 0;
    bPauseRequested = false;

    if (Dropped > 0 || bHadBlock)
    {
        ++Diagnostics.Cancellations;
        if (bHadBlock)
        {
            ++Diagnostics.BlockReleases;
        }
    }
    (void)Reason;
    return Dropped;
}

void FController::Tick(float DeltaSeconds)
{
    if (ActiveTouches.Num() == 0 || DeltaSeconds <= 0.0f)
    {
        return;
    }
    for (int32 Index = ActiveTouches.Num() - 1; Index >= 0; --Index)
    {
        ActiveTouches[Index].SecondsSinceUpdate += DeltaSeconds;
        if (ActiveTouches[Index].SecondsSinceUpdate > StaleTouchSeconds)
        {
            // The device never told us this finger left. Drop it and its latch;
            // the alternative is a fighter that never stops walking.
            const int32 Finger = ActiveTouches[Index].Finger;
            TouchEnd(Finger, TEXT("stale-watchdog"));
            ++Diagnostics.StuckTouchesCleared;
        }
    }
}

void FController::ApplyTo(RKC::FInputState& Out)
{
    if (!bEnabled)
    {
        return;
    }

    Out.bMoveLeft = (StickAxis < -0.5f);
    Out.bMoveRight = (StickAxis > 0.5f);
    Out.bHoldBlock = bBlockHeld;

    // One press, one edge: the counters are consumed here, exactly like the
    // keyboard latches in ARKCPlayerController.
    if (PendingJab > 0)
    {
        Out.bJabPressed = true;
        ++Diagnostics.EdgesDelivered;
    }
    if (PendingHeavy > 0)
    {
        Out.bHeavyPressed = true;
        ++Diagnostics.EdgesDelivered;
    }
    if (PendingDodge > 0)
    {
        Out.bDodgePressed = true;
        ++Diagnostics.EdgesDelivered;
    }
    // A press already consumed by an earlier frame must not survive a state the
    // simulation has moved on from.
    PendingJab = 0;
    PendingHeavy = 0;
    PendingDodge = 0;
}

bool FController::ConsumePauseRequest()
{
    const bool bRequested = bPauseRequested;
    bPauseRequested = false;
    return bRequested;
}

FVector2D FController::GetStickKnobOffsetPx() const
{
    if (StickFinger < 0)
    {
        return FVector2D::ZeroVector;
    }
    const FActiveTouch* Touch = ActiveTouches.FindByPredicate(
        [this](const FActiveTouch& T) { return T.Finger == StickFinger; });
    if (!Touch)
    {
        return FVector2D::ZeroVector;
    }
    // The knob follows the drag but stays inside the outer ring.
    const FWidget* StickWidget = Widgets.FindByPredicate([](const FWidget& W) { return W.bIsStick; });
    const float MaxOffset = StickWidget
        ? RadiusPx(*StickWidget, ViewportSize, EffectiveSafeInsetsPx())
        : 0.1f * static_cast<float>(FMath::Min(ViewportSize.X, ViewportSize.Y));
    FVector2D Offset = Touch->CurrentPx - Touch->StartPx;
    if (Offset.Size() > MaxOffset)
    {
        Offset = Offset.GetSafeNormal() * MaxOffset;
    }
    return Offset;
}

FActiveTouch* FController::FindTouch(int32 Finger)
{
    return ActiveTouches.FindByPredicate([Finger](const FActiveTouch& T) { return T.Finger == Finger; });
}

void FController::RefreshFingerCount(FDiagnostics& Diag, int32 Count)
{
    Diag.MaxSimultaneousFingers = FMath::Max(Diag.MaxSimultaneousFingers, Count);
}

FString FController::DescribeState() const
{
    return FString::Printf(
        TEXT("fingers=%d stick=%s axis=%+.0f block=%s pending(jab=%d heavy=%d dodge=%d pause=%d) "
             "stats(begins=%d moves=%d ends=%d cancels=%d stuckCleared=%d maxFingers=%d edges=%d)"),
        ActiveTouches.Num(), StickFinger >= 0 ? TEXT("yes") : TEXT("no"), StickAxis,
        bBlockHeld ? TEXT("held") : TEXT("up"), PendingJab, PendingHeavy, PendingDodge,
        bPauseRequested ? 1 : 0, Diagnostics.Begins, Diagnostics.Moves, Diagnostics.Ends,
        Diagnostics.Cancellations, Diagnostics.StuckTouchesCleared,
        Diagnostics.MaxSimultaneousFingers, Diagnostics.EdgesDelivered);
}

FMargin ReadPlatformSafeInsetsPx(const FVector2D& ViewportSizePx)
{
    // 1. The explicit override, in viewport pixels. Checked first so that a test
    //    or a capture run is repeatable regardless of the device it runs on.
    FMargin Override(0.0f);
    if (ParseInsetsPx(CVarRkcSafeAreaInsets.GetValueOnGameThread(), Override))
    {
        return Override;
    }

    // 2. The platform's own metric. On Android TitleSafePaddingSize is the
    //    display cut-out and the navigation/gesture bar, filled from the JNI
    //    safe-zone callback; on a desktop it is zero unless the engine's own
    //    safe-zone ratio is configured.
    FDisplayMetrics Metrics;
    // RebuildDisplayMetrics fills these, but the struct's constructor only zeroes
    // the padding fields, so they are cleared here rather than read uninitialised.
    Metrics.PrimaryDisplayWidth = 0;
    Metrics.PrimaryDisplayHeight = 0;
    FDisplayMetrics::RebuildDisplayMetrics(Metrics);

    // The metric is expressed in DISPLAY pixels (Android multiplies its ratios by
    // the display size), while the layout works in viewport pixels. On a phone
    // held fullscreen the two sizes are equal and the scale is exactly 1; in a
    // window they are not, and an inset that was not scaled would push the pad
    // further in than the cut-out actually reaches.
    double ScaleX = 1.0;
    double ScaleY = 1.0;
    if (Metrics.PrimaryDisplayWidth > 1 && ViewportSizePx.X > 1.0)
    {
        ScaleX = ViewportSizePx.X / static_cast<double>(Metrics.PrimaryDisplayWidth);
    }
    if (Metrics.PrimaryDisplayHeight > 1 && ViewportSizePx.Y > 1.0)
    {
        ScaleY = ViewportSizePx.Y / static_cast<double>(Metrics.PrimaryDisplayHeight);
    }

    // X = Left, Y = Top, Z = Right, W = Bottom. Whatever the platform reports is
    // clamped later by ClampSafeInsets(), so a nonsense value is bounded rather
    // than believed.
    return FMargin(static_cast<float>(Metrics.TitleSafePaddingSize.X * ScaleX),
                   static_cast<float>(Metrics.TitleSafePaddingSize.Y * ScaleY),
                   static_cast<float>(Metrics.TitleSafePaddingSize.Z * ScaleX),
                   static_cast<float>(Metrics.TitleSafePaddingSize.W * ScaleY));
}
}
