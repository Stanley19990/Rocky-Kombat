// RKCHUD.h
//
// Rocky Kombat - the on-screen match readout.
//
// RESPONSIBILITY: this class renders; it does not decide. Every value it draws
// is read from ARKCGameMode (which in turn reads RKC::FCombatSim, the single
// source of truth for the rules) or from the human input layer, and nothing here
// ever writes to the simulation. That separation is deliberate: if a number on
// screen disagrees with the simulation, the bug is in this file, never in the
// rules, and the on-screen state and the authoritative state cannot drift.
//
// WHY CANVAS AND NOT UMG:
// UMG is enabled in the module rules and remains an option, but the first
// milestone needs a HUD that (a) cannot break a headless/packaged run through a
// missing widget asset, and (b) reports what it drew so a test can prove the HUD
// actually rendered rather than assuming it did. Canvas drawing needs no binary
// content at all, so a clean checkout of the source still plays and still shows
// health, stamina, the round clock and the result. A UMG pass can replace the
// presentation later without touching any caller.
//
// WHAT IT DRAWS (top to bottom):
//   1. A fighter panel per side - name, division, record, HEALTH bar, STAMINA
//      bar, knockdowns, rounds won - mirrored so side B reads from the right.
//   2. A centre header - round number, round clock, and the running score.
//   3. The centre banner - countdown, "FIGHT!", round result, match result.
//   4. The event feed - the last few simulation events in plain language.
//   5. A frame-data / telemetry overlay, toggled with F1.
//   6. A footer with the control legend and the rematch prompt.
//
// The overlay (5) is the tester's window into the rules: fighter state machine
// node, ticks remaining, attack tick within the current punch, the attack
// windows in whole ticks, dodge cooldown and invulnerability ticks, per-side
// statistics, the live separation between fighters, and - for a bot-driven side
// - the plan the AI is actually committed to, its decision/reaction/order
// counters, and the difficulty grade. It also prints a deterministic state
// checksum, which is how two runs of the same match are compared.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Combat/RKCCombatCore.h"
#include "RKCHUD.generated.h"

class ARKCGameMode;
class UFont;
class UTexture2D;

UCLASS()
class ROCKYKOMBAT_API ARKCHUD : public AHUD
{
    GENERATED_BODY()

public:
    ARKCHUD();

    virtual void DrawHUD() override;

    // ---- Self-report. Read by tests and by the evidence record. -------------
    // A HUD that never draws is indistinguishable from a HUD that is missing, so
    // these counters make "the HUD rendered" an observation rather than a claim.

    /** Number of frames on which a full draw pass completed. Zero means the HUD
     *  class was assigned but never rendered. */
    int32 GetDrawnFrameCount() const { return DrawnFrameCount; }

    /** Viewport size of the most recent pass; zero-sized means there was no real
     *  canvas to draw into. */
    FIntPoint GetLastCanvasSize() const { return LastCanvasSize; }

    /** Number of discrete HUD regions drawn in the most recent pass. Used to
     *  assert that the full layout was emitted and not just the banner. */
    int32 GetLastRegionCount() const { return LastRegionCount; }

private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTexture2D>> PresentationTextures;
    /** The match director, or null while the world is still starting up. */
    ARKCGameMode* GetRKCMode() const;

    void DrawFighterPanel(const ARKCGameMode& Mode, RKC::ESide Side, float PanelX, float PanelY,
                          float PanelW, float PanelH, bool bMirrored, float UIScale);
    void DrawCentreHeader(const ARKCGameMode& Mode, float UIScale);
    void DrawBanner(const ARKCGameMode& Mode, float UIScale);
    void DrawEventFeed(const ARKCGameMode& Mode, float UIScale);
    void DrawFrameData(const ARKCGameMode& Mode, float UIScale);
    void DrawFooter(const ARKCGameMode& Mode, float UIScale);

    /** The branded cards: startup, title, menus, options, character select,
     *  versus, loading and the result card. Drawn instead of the match readout
     *  whenever the front-end machine owns the screen, because a menu on top of a
     *  live health bar would suggest a match is running behind it. */
    void DrawFrontEnd(const ARKCGameMode& Mode, float UIScale);

    /** The on-screen pad. Drawn during a match whenever the controller says it
     *  should be, which includes a desktop run with -rkc.TouchForced=1. */
    void DrawTouchPad(const ARKCGameMode& Mode, float UIScale);

    /** Opaque rounded-off backing plate, so text stays readable over any arena. */
    void DrawPanelBacking(float X, float Y, float W, float H, const FLinearColor& Color);

    /** A labelled 0..1 meter. When bMirror is set the fill grows right-to-left,
     *  which is what a viewer expects for the fighter standing on the right. */
    void DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill,
                 bool bMirror);

    /** Text with a 1px dark drop shadow. Returns the drawn width so callers can
     *  right-align without measuring twice. */
    float DrawShadowedText(UFont* Font, const FString& Text, float X, float Y, float Scale,
                           const FLinearColor& Color);

    /** Width of Text at the given font and scale, on this canvas. */
    float MeasureText(UFont* Font, const FString& Text, float Scale) const;

    int32 DrawnFrameCount = 0;
    int32 LastRegionCount = 0;
    FIntPoint LastCanvasSize = FIntPoint::ZeroValue;
};
