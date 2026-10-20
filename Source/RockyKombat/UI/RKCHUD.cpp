// RKCHUD.cpp
//
// Rocky Kombat - canvas match readout. See RKCHUD.h for the design rationale.
//
// LAYOUT MODEL: the screen is laid out in a 1920x1080 design space and then
// scaled by the viewport height, so the HUD reads the same on a 1280x720 laptop
// panel and on a 4K display. Every coordinate below is a design-space number
// multiplied by the scale factor at the call site.
//
// NOTHING HERE WRITES TO THE SIMULATION. The HUD reads RKC::FCombatSim through
// ARKCGameMode and from the human input layer through ARKCPlayerController. If a
// number on screen ever disagrees with the rules, this file is the bug.

#include "UI/RKCHUD.h"

#include "AI/RKCAI.h"
#include "Combat/RKCRoster.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "Match/RKCGameMode.h"
#include "Match/RKCPlayerController.h"
#include "Containers/StringConv.h"

namespace
{
// ---------------------------------------------------------------------------
// Palette. Deliberately few colours: the arena art supplies the mood, the HUD
// only has to be legible over it.
// ---------------------------------------------------------------------------
const FLinearColor ColorPanelBacking(0.02f, 0.02f, 0.035f, 0.62f);
const FLinearColor ColorPanelBackingSoft(0.02f, 0.02f, 0.035f, 0.48f);
const FLinearColor ColorTextPrimary(0.97f, 0.97f, 0.95f);
const FLinearColor ColorTextSecondary(0.72f, 0.74f, 0.78f);
const FLinearColor ColorTextAccent(1.00f, 0.82f, 0.30f);
const FLinearColor ColorHealthGood(0.20f, 0.80f, 0.30f);
const FLinearColor ColorHealthBad(0.85f, 0.15f, 0.12f);
const FLinearColor ColorStamina(0.95f, 0.72f, 0.16f);
const FLinearColor ColorStaminaLow(0.90f, 0.22f, 0.15f);
const FLinearColor ColorBarBacking(0.10f, 0.10f, 0.12f, 0.85f);
const FLinearColor ColorBot(0.45f, 0.75f, 1.00f);
const FLinearColor ColorHuman(0.55f, 1.00f, 0.60f);

/** Design-space height the whole layout is authored against. */
constexpr float DesignHeight = 1080.0f;

/** The keys actually bound to one action, as a display string.
 *
 *  Read from the binding table the self-test checks against DefaultInput.ini
 *  rather than typed out again, so the in-match legend cannot promise a key the
 *  engine did not load. Gamepad aliases return no name and are left out, because
 *  the legend is about the keyboard; the Controls screen covers the pad.
 *
 *  At most two names are kept, so "Numpad 1 / comma" stays short enough to fit on
 *  one legend line. */
FString RKCKeysFor(const TCHAR* Action)
{
    TArray<FString> Names;
    for (const FRKCBindingExpectation& Entry : RKCBindingTable())
    {
        if (FCString::Stricmp(Entry.Action, Action) != 0)
        {
            continue;
        }
        const FString Name = RKCKeyDisplayName(Entry.Key);
        if (!Name.IsEmpty() && !Names.Contains(Name))
        {
            Names.Add(Name);
        }
        if (Names.Num() == 2)
        {
            break;
        }
    }
    return FString::Join(Names, TEXT(" / "));
}

const TCHAR* PhaseText(RKC::EMatchPhase Phase)
{
    switch (Phase)
    {
    case RKC::EMatchPhase::NotStarted:
        return TEXT("NOT STARTED");
    case RKC::EMatchPhase::Countdown:
        return TEXT("COUNTDOWN");
    case RKC::EMatchPhase::Fighting:
        return TEXT("FIGHTING");
    case RKC::EMatchPhase::RoundEnd:
        return TEXT("ROUND END");
    case RKC::EMatchPhase::MatchEnd:
        return TEXT("MATCH END");
    default:
        return TEXT("?");
    }
}

const TCHAR* FighterStateText(RKC::EFighterState State)
{
    switch (State)
    {
    case RKC::EFighterState::Idle:
        return TEXT("IDLE");
    case RKC::EFighterState::Moving:
        return TEXT("MOVING");
    case RKC::EFighterState::AttackStartup:
        return TEXT("ATK STARTUP");
    case RKC::EFighterState::AttackActive:
        return TEXT("ATK ACTIVE");
    case RKC::EFighterState::AttackRecovery:
        return TEXT("ATK RECOVERY");
    case RKC::EFighterState::Blocking:
        return TEXT("BLOCKING");
    case RKC::EFighterState::Dodging:
        return TEXT("DODGING");
    case RKC::EFighterState::HitStun:
        return TEXT("HIT STUN");
    case RKC::EFighterState::KnockedDown:
        return TEXT("KNOCKED DOWN");
    case RKC::EFighterState::Victory:
        return TEXT("VICTORY");
    case RKC::EFighterState::Defeated:
        return TEXT("DEFEATED");
    default:
        return TEXT("?");
    }
}

const TCHAR* ActionText(RKC::EAction Action)
{
    switch (Action)
    {
    case RKC::EAction::Idle:
        return TEXT("IDLE");
    case RKC::EAction::Walk:
        return TEXT("WALK");
    case RKC::EAction::Jab:
        return TEXT("JAB");
    case RKC::EAction::HeavyPunch:
        return TEXT("HEAVY");
    case RKC::EAction::Block:
        return TEXT("BLOCK");
    case RKC::EAction::Dodge:
        return TEXT("DODGE");
    default:
        return TEXT("?");
    }
}

const TCHAR* TrainingText(RKC::ETrainingMode Mode)
{
    switch (Mode)
    {
    case RKC::ETrainingMode::ShowHitboxes:
        return TEXT("HITBOXES");
    case RKC::ETrainingMode::InfiniteHealth:
        return TEXT("INFINITE HEALTH");
    case RKC::ETrainingMode::InfiniteStamina:
        return TEXT("INFINITE STAMINA");
    case RKC::ETrainingMode::NoOpponentInput:
        return TEXT("NO OPPONENT INPUT");
    case RKC::ETrainingMode::DisplayFrameData:
        return TEXT("FRAME DATA");
    default:
        return TEXT("OFF");
    }
}

/** Font used for dense telemetry. Falls back through the engine's built-in
 *  fonts so a missing font asset degrades rather than crashes. */
UFont* DetailFont()
{
    if (UFont* Font = UEngine::GetSmallFont())
    {
        return Font;
    }
    return UEngine::GetMediumFont();
}

UFont* HeadingFont()
{
    if (UFont* Font = UEngine::GetMediumFont())
    {
        return Font;
    }
    return UEngine::GetSmallFont();
}

UFont* BannerFont()
{
    if (UFont* Font = UEngine::GetLargeFont())
    {
        return Font;
    }
    return UEngine::GetMediumFont();
}
} // namespace

ARKCHUD::ARKCHUD()
{
    PrimaryActorTick.bCanEverTick = false;
}

ARKCGameMode* ARKCHUD::GetRKCMode() const
{
    if (UWorld* World = GetWorld())
    {
        return Cast<ARKCGameMode>(World->GetAuthGameMode());
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

void ARKCHUD::DrawPanelBacking(float X, float Y, float W, float H, const FLinearColor& Color)
{
    DrawRect(Color, X, Y, W, H);
}

void ARKCHUD::DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill,
                      bool bMirror)
{
    DrawRect(ColorBarBacking, X, Y, W, H);

    const float Clamped = FMath::Clamp(Fraction, 0.0f, 1.0f);
    const float FillW = W * Clamped;
    if (FillW > 0.5f)
    {
        // Mirroring is what makes the right-hand fighter's meters read inward,
        // which is the convention players already know from fighting games.
        const float FillX = bMirror ? (X + W - FillW) : X;
        DrawRect(Fill, FillX, Y, FillW, H);
    }

    // A thin outline keeps the bar readable when the fill is nearly empty and
    // the arena behind it is bright.
    const FLinearColor Outline(1.0f, 1.0f, 1.0f, 0.22f);
    DrawRect(Outline, X, Y, W, 1.0f);
    DrawRect(Outline, X, Y + H - 1.0f, W, 1.0f);
    DrawRect(Outline, X, Y, 1.0f, H);
    DrawRect(Outline, X + W - 1.0f, Y, 1.0f, H);
}

float ARKCHUD::MeasureText(UFont* Font, const FString& Text, float Scale) const
{
    float W = 0.0f;
    float H = 0.0f;
    GetTextSize(Text, W, H, Font, Scale);
    return W;
}

float ARKCHUD::DrawShadowedText(UFont* Font, const FString& Text, float X, float Y, float Scale,
                                const FLinearColor& Color)
{
    if (Text.IsEmpty())
    {
        return 0.0f;
    }

    // The shadow is the whole reason this helper exists: the ring, the crowd and
    // the canvas are all bright enough that plain white text disappears.
    const float Offset = FMath::Max(1.0f, Scale);
    DrawText(Text, FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), X + Offset, Y + Offset, Font, Scale);
    DrawText(Text, Color, X, Y, Font, Scale);

    return MeasureText(Font, Text, Scale);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void ARKCHUD::DrawHUD()
{
    Super::DrawHUD();

    if (!Canvas)
    {
        return;
    }

    ARKCGameMode* Mode = GetRKCMode();
    if (!Mode)
    {
        // The world is still assembling (or this is not an RKC match); drawing a
        // half-populated readout would be worse than drawing nothing.
        return;
    }

    const float Scale = FMath::Clamp(static_cast<float>(Canvas->SizeY) / DesignHeight, 0.5f, 2.0f);

    LastCanvasSize = FIntPoint(Canvas->SizeX, Canvas->SizeY);
    LastRegionCount = 0;

    // The machine's cards replace the match readout rather than sitting on top of
    // it: a menu drawn over a live health bar would suggest a match is running
    // behind it, which is exactly the state the director exists to prevent.
    if (Mode->IsFrontEndActive())
    {
        DrawFrontEnd(*Mode, Scale);
        DrawTouchPad(*Mode, Scale);
        ++DrawnFrameCount;
        Mode->NotifyHudDrew();
        return;
    }

    DrawFighterPanel(*Mode, RKC::ESide::A, 24.0f * Scale, 24.0f * Scale, 560.0f * Scale,
                     212.0f * Scale, /*bMirrored=*/false, Scale);

    const float RightPanelX = (static_cast<float>(Canvas->SizeX) - 24.0f * Scale) - 560.0f * Scale;
    DrawFighterPanel(*Mode, RKC::ESide::B, RightPanelX, 24.0f * Scale, 560.0f * Scale,
                     212.0f * Scale, /*bMirrored=*/true, Scale);

    DrawCentreHeader(*Mode, Scale);
    DrawBanner(*Mode, Scale);
    DrawFrameData(*Mode, Scale);
    DrawEventFeed(*Mode, Scale);
    DrawFooter(*Mode, Scale);

    // The pad stays up during a match, which is the only place it is actually
    // used; the menus are tapped, not played.
    DrawTouchPad(*Mode, Scale);

    ++DrawnFrameCount;

    // The only thing this class ever tells the director: a frame was really
    // drawn. The loading card's warm-up gate is released by this counter, so a
    // HUD that never draws cannot release gameplay behind a black screen.
    Mode->NotifyHudDrew();
}

// ---------------------------------------------------------------------------
// 1. Fighter panels
// ---------------------------------------------------------------------------

void ARKCHUD::DrawFighterPanel(const ARKCGameMode& Mode, RKC::ESide Side, float X, float Y, float W,
                               float H, bool bMirrored, float Scale)
{
    const RKC::FCombatSim& Sim = Mode.GetSim();
    const RKC::FFighterState& Fighter = Sim.GetFighter(Side);
    const RKC::FFighterConfig& Config = Sim.GetConfig(Side);
    const RKC::FMatchState& Match = Sim.GetMatch();

    const RKC::ERoster Who =
        (Side == RKC::ESide::A) ? Mode.GetSetup().FighterA : Mode.GetSetup().FighterB;

    DrawPanelBacking(X, Y, W, H, ColorPanelBacking);

    UFont* Heading = HeadingFont();
    UFont* Detail = DetailFont();

    const float Pad = 16.0f * Scale;
    const float TextLeft = X + Pad;
    const float BarW = W - 2.0f * Pad;

    // Right-aligned text anchors to the inner edge on the mirrored panel.
    auto AlignedX = [&](const FString& Text, UFont* Font, float TextScale) -> float
    {
        if (!bMirrored)
        {
            return TextLeft;
        }
        return (X + W - Pad) - MeasureText(Font, Text, TextScale);
    };

    float CursorY = Y + Pad;

    // ---- Name --------------------------------------------------------------
    {
        const FString Name = RKC::GetRosterDisplayName(Who);
        const float NameScale = 1.20f * Scale;
        DrawShadowedText(Heading, Name, AlignedX(Name, Heading, NameScale), CursorY,
                         NameScale, ColorTextPrimary);
        CursorY += 30.0f * Scale;
    }

    // ---- Division and record ----------------------------------------------
    {
        const FString Sub = FString::Printf(TEXT("%s   %s"), *RKC::GetRosterDivisionName(Who),
                                            *RKC::GetRosterRecord(Who));
        const float SubScale = 0.80f * Scale;
        DrawShadowedText(Detail, Sub, AlignedX(Sub, Detail, SubScale), CursorY, SubScale,
                         ColorTextSecondary);
        CursorY += 22.0f * Scale;
    }

    // ---- Health ------------------------------------------------------------
    {
        const float Fraction = (Config.MaxHealth > 0.0f) ? (Fighter.Health / Config.MaxHealth) : 0.0f;
        const FString Value = FString::Printf(TEXT("HEALTH  %d / %d"),
                                              FMath::Clamp(FMath::RoundToInt(Fighter.Health), 0,
                                                           9999),
                                              FMath::RoundToInt(Config.MaxHealth));
        const float LabelScale = 0.95f * Scale;
        DrawShadowedText(Detail, Value, AlignedX(Value, Detail, LabelScale), CursorY, LabelScale,
                         ColorTextSecondary);
        CursorY += 20.0f * Scale;

        const FLinearColor HpColor = FLinearColor::LerpUsingHSV(
            ColorHealthBad, ColorHealthGood, FMath::Clamp(Fraction, 0.0f, 1.0f));
        DrawBar(TextLeft, CursorY, BarW, 16.0f * Scale, Fraction, HpColor, bMirrored);
        CursorY += 26.0f * Scale;
    }

    // ---- Stamina -----------------------------------------------------------
    {
        const float Fraction = (Config.MaxStamina > 0.0f) ? (Fighter.Stamina / Config.MaxStamina) : 0.0f;
        const FString Value = FString::Printf(TEXT("STAMINA  %d / %d"),
                                              FMath::Clamp(FMath::RoundToInt(Fighter.Stamina), 0,
                                                           9999),
                                              FMath::RoundToInt(Config.MaxStamina));
        const float LabelScale = 0.95f * Scale;
        DrawShadowedText(Detail, Value, AlignedX(Value, Detail, LabelScale), CursorY, LabelScale,
                         ColorTextSecondary);
        CursorY += 20.0f * Scale;

        // Below a fifth of the tank the meter turns red: that is the region where
        // the knockdown and guard-break rules start to bite.
        const FLinearColor StamColor = (Fraction < 0.20f) ? ColorStaminaLow : ColorStamina;
        DrawBar(TextLeft, CursorY, BarW, 12.0f * Scale, Fraction, StamColor, bMirrored);
        CursorY += 24.0f * Scale;
    }

    // ---- Knockdowns and rounds won ----------------------------------------
    {
        const int32 MaxKnockdowns = FMath::Max(1, Sim.GetRules().MaxKnockdownsPerRound);
        const int32 Won = (Side == RKC::ESide::A) ? Match.ScoreA : Match.ScoreB;

        // Knockdown markers: filled squares are knockdowns already suffered this
        // round, which is exactly the count the three-knockdown rule reads.
        const FString KdLabel = FString::Printf(TEXT("DOWN %d/%d"), Fighter.KnockdownsThisRound,
                                                MaxKnockdowns);
        const float LabelScale = 0.90f * Scale;
        const float LabelWidth = MeasureText(Detail, KdLabel, LabelScale);
        const float LabelX = bMirrored ? (X + W - Pad - LabelWidth) : TextLeft;
        DrawShadowedText(Detail, KdLabel, LabelX, CursorY, LabelScale, ColorTextSecondary);

        const float MarkerSize = 9.0f * Scale;
        const float MarkerStep = 13.0f * Scale;
        for (int32 i = 0; i < MaxKnockdowns; ++i)
        {
            const bool bSpent = (i < Fighter.KnockdownsThisRound);
            const float MarkerX = bMirrored ? (LabelX - 10.0f * Scale - (i + 1) * MarkerStep)
                                            : (LabelX + LabelWidth + 10.0f * Scale + i * MarkerStep);
            DrawRect(bSpent ? ColorHealthBad : FLinearColor(1.0f, 1.0f, 1.0f, 0.18f), MarkerX,
                     CursorY + 2.0f * Scale, MarkerSize, MarkerSize);
        }

        const FString RoundLabel = FString::Printf(TEXT(" |  ROUNDS WON %d"), Won);
        DrawShadowedText(Detail, RoundLabel,
                         bMirrored ? LabelX - 10.0f * Scale - MaxKnockdowns * MarkerStep - 150.0f * Scale
                                   : TextLeft + LabelWidth + 10.0f * Scale +
                                         MaxKnockdowns * MarkerStep,
                         CursorY, LabelScale, ColorTextAccent);
        CursorY += 22.0f * Scale;
    }

    // ---- Live state --------------------------------------------------------
    {
        const FString State = FString::Printf(TEXT("STATE %s    ACTION %s"),
                                              FighterStateText(Fighter.State),
                                              ActionText(Fighter.CurrentAction));
        const float StateScale = 0.90f * Scale;
        DrawShadowedText(Detail, State, AlignedX(State, Detail, StateScale), CursorY, StateScale,
                         ColorTextPrimary);
        CursorY += 20.0f * Scale;
    }

    // ---- Who is driving this fighter ---------------------------------------
    {
        const RKC::FBotBrain* Bot = Mode.GetBot(Side);
        if (Bot)
        {
            const RKC::FBotProfile& Profile = Bot->GetProfile();
            const FString Line = FString::Printf(
                TEXT("AI %s / %s   PLAN %s   DEC %d  RX %d  ATK %d  GRD %d  SLP %d"),
                ANSI_TO_TCHAR(RKC::GetArchetypeName(Profile.Archetype)),
                ANSI_TO_TCHAR(RKC::GetDifficultyName(Profile.Difficulty)),
                ANSI_TO_TCHAR(RKC::GetPlanName(Bot->GetPlan())), Bot->GetDecisionCount(),
                Bot->GetReactionCount(), Bot->GetAttackOrders(), Bot->GetGuardOrders(),
                Bot->GetSlipOrders());
            const float LineScale = 0.85f * Scale;
            DrawShadowedText(Detail, Line, AlignedX(Line, Detail, LineScale), CursorY, LineScale,
                             ColorBot);
            CursorY += 18.0f * Scale;
        }
        else
        {
            // Show the raw device reading, so a tester can tell "the character is
            // not moving" apart from "the keyboard is not being read".
            const ARKCPlayerController* PC = Mode.GetRKCPlayerController();
            const float Axis = PC ? PC->GetRawMoveAxis(Side) : 0.0f;
            const FString Line = FString::Printf(TEXT("HUMAN   RAW MOVE AXIS %+.2f%s"), Axis,
                                                 PC && PC->IsInputEnabled() ? TEXT("")
                                                                            : TEXT("   (INPUT LOCKED)"));
            const float LineScale = 0.85f * Scale;
            DrawShadowedText(Detail, Line, AlignedX(Line, Detail, LineScale), CursorY, LineScale,
                             ColorHuman);
            CursorY += 18.0f * Scale;
        }
    }

    // ---- Training aid banner ----------------------------------------------
    {
        const RKC::ETrainingMode Training = Sim.GetTrainingMode(Side);
        if (Training != RKC::ETrainingMode::Off)
        {
            const FString Line = FString::Printf(TEXT("TRAINING: %s"), TrainingText(Training));
            const float LineScale = 0.85f * Scale;
            DrawShadowedText(Detail, Line, AlignedX(Line, Detail, LineScale), CursorY, LineScale,
                             ColorTextAccent);
        }
    }

    ++LastRegionCount;
}

// ---------------------------------------------------------------------------
// 2. Centre header - round, clock, score
// ---------------------------------------------------------------------------

void ARKCHUD::DrawCentreHeader(const ARKCGameMode& Mode, float Scale)
{
    const RKC::FCombatSim& Sim = Mode.GetSim();
    const RKC::FMatchState& Match = Sim.GetMatch();

    const float CanvasW = static_cast<float>(Canvas->SizeX);
    const float PanelW = 420.0f * Scale;
    const float PanelH = 116.0f * Scale;
    const float X = (CanvasW - PanelW) * 0.5f;
    const float Y = 24.0f * Scale;

    DrawPanelBacking(X, Y, PanelW, PanelH, ColorPanelBacking);
    ++LastRegionCount;

    UFont* Heading = HeadingFont();
    UFont* Detail = DetailFont();

    auto Centred = [&](const FString& Text, UFont* Font, float TextScale, float LineY,
                       const FLinearColor& Color)
    {
        const float TextW = MeasureText(Font, Text, TextScale);
        DrawShadowedText(Font, Text, X + (PanelW - TextW) * 0.5f, LineY, TextScale, Color);
    };

    // Round number.
    const FString RoundLine =
        FString::Printf(TEXT("ROUND %d OF %d"), Match.Round, FMath::Max(1, Sim.GetRules().Rounds));
    Centred(RoundLine, Detail, 1.0f * Scale, Y + 12.0f * Scale, ColorTextSecondary);

    // Clock. During the countdown the phase timer is the interesting number;
    // while fighting it is the round clock.
    const float ClockSeconds = (Match.Phase == RKC::EMatchPhase::Countdown)
                                   ? Match.PhaseTimeRemaining
                                   : Match.RoundTimeRemaining;
    const FString ClockLine = FString::Printf(TEXT("%.1f"), FMath::Max(0.0f, ClockSeconds));
    const FLinearColor ClockColor = (ClockSeconds <= 10.0f && Match.Phase == RKC::EMatchPhase::Fighting)
                                        ? ColorHealthBad
                                        : ColorTextPrimary;
    Centred(ClockLine, Heading, 2.0f * Scale, Y + 34.0f * Scale, ClockColor);

    // Score, with the two fighter names so the panel is self-describing.
    const FString ScoreLine = FString::Printf(
        TEXT("%s  %d - %d  %s"),
        *RKC::GetRosterDisplayName(Mode.GetSetup().FighterA).ToUpper(), Match.ScoreA, Match.ScoreB,
        *RKC::GetRosterDisplayName(Mode.GetSetup().FighterB).ToUpper());
    Centred(ScoreLine, Detail, 0.95f * Scale, Y + PanelH - 26.0f * Scale, ColorTextAccent);
}

// ---------------------------------------------------------------------------
// 3. Banner - countdown, FIGHT!, round and match results
// ---------------------------------------------------------------------------

void ARKCHUD::DrawBanner(const ARKCGameMode& Mode, float Scale)
{
    const FString& Text = Mode.GetBannerText();
    if (Text.IsEmpty())
    {
        return;
    }

    const float CanvasW = static_cast<float>(Canvas->SizeX);
    const float CanvasH = static_cast<float>(Canvas->SizeY);

    // The director marks the calling banners (countdown, FIGHT!, results) as
    // prominent; anything else is drawn as a quieter caption.
    const bool bProminent = Mode.IsBannerProminent();
    UFont* Font = bProminent ? BannerFont() : HeadingFont();
    const float TextScale = (bProminent ? 2.1f : 1.15f) * Scale;

    const float TextW = MeasureText(Font, Text, TextScale);
    const float X = (CanvasW - TextW) * 0.5f;
    const float Y = CanvasH * 0.30f;

    // A band behind the text, so a result never fights with the arena art.
    const float BandH = 52.0f * Scale * (bProminent ? 1.6f : 1.0f);
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, bProminent ? 0.55f : 0.30f), 0.0f, Y - 12.0f * Scale,
             CanvasW, BandH);

    DrawShadowedText(Font, Text, X, Y, TextScale,
                     bProminent ? ColorTextAccent : ColorTextPrimary);

    ++LastRegionCount;
}

// ---------------------------------------------------------------------------
// 4. Event feed
// ---------------------------------------------------------------------------

void ARKCHUD::DrawEventFeed(const ARKCGameMode& Mode, float Scale)
{
    const TArray<FRKCEventFeedLine>& Feed = Mode.GetEventFeed();
    if (Feed.Num() == 0)
    {
        return;
    }

    UFont* Detail = DetailFont();
    const float TextScale = 0.95f * Scale;
    const float LineHeight = 19.0f * Scale;
    const float FooterReserve = 96.0f * Scale;
    const float PanelW = 620.0f * Scale;
    const float PanelH = (Feed.Num() * LineHeight) + 20.0f * Scale;
    const float X = 24.0f * Scale;
    const float Y = static_cast<float>(Canvas->SizeY) - FooterReserve - PanelH;

    DrawPanelBacking(X, Y, PanelW, PanelH, ColorPanelBackingSoft);
    ++LastRegionCount;

    float CursorY = Y + 10.0f * Scale;
    for (const FRKCEventFeedLine& Line : Feed)
    {
        // Lines fade as they age; the director drops them at six seconds.
        const float Age = FMath::Clamp(Line.AgeSeconds / 6.0f, 0.0f, 1.0f);
        const FLinearColor Color(0.85f, 0.88f, 0.92f, 1.0f - Age * 0.85f);
        DrawShadowedText(Detail, Line.Text, X + 12.0f * Scale, CursorY, TextScale, Color);
        CursorY += LineHeight;
    }
}

// ---------------------------------------------------------------------------
// 5. Frame data / telemetry overlay (F1)
// ---------------------------------------------------------------------------

void ARKCHUD::DrawFrameData(const ARKCGameMode& Mode, float Scale)
{
    if (!Mode.IsFrameDataVisible())
    {
        return;
    }

    const RKC::FCombatSim& Sim = Mode.GetSim();
    const RKC::FMatchState& Match = Sim.GetMatch();
    const RKC::FFighterState& FighterA = Sim.GetFighter(RKC::ESide::A);
    const RKC::FFighterState& FighterB = Sim.GetFighter(RKC::ESide::B);
    const RKC::FFighterConfig& ConfigA = Sim.GetConfig(RKC::ESide::A);
    const RKC::FFighterConfig& ConfigB = Sim.GetConfig(RKC::ESide::B);

    // Whole-tick attack windows: these are the numbers the rules actually use,
    // which is why they are printed in ticks rather than in seconds.
    int32 JabStartupA = 0, JabActiveA = 0, JabRecoveryA = 0;
    int32 HeavyStartupA = 0, HeavyActiveA = 0, HeavyRecoveryA = 0;
    RKC::FCombatSim::GetAttackWindow(ConfigA.Jab, JabStartupA, JabActiveA, JabRecoveryA);
    RKC::FCombatSim::GetAttackWindow(ConfigA.HeavyPunch, HeavyStartupA, HeavyActiveA, HeavyRecoveryA);

    int32 JabStartupB = 0, JabActiveB = 0, JabRecoveryB = 0;
    int32 HeavyStartupB = 0, HeavyActiveB = 0, HeavyRecoveryB = 0;
    RKC::FCombatSim::GetAttackWindow(ConfigB.Jab, JabStartupB, JabActiveB, JabRecoveryB);
    RKC::FCombatSim::GetAttackWindow(ConfigB.HeavyPunch, HeavyStartupB, HeavyActiveB, HeavyRecoveryB);

    const float Separation = FMath::Abs(FighterA.PositionX - FighterB.PositionX);

    TArray<FString> Lines;

    Lines.Add(FString::Printf(
        TEXT("FRAME DATA   phase=%s  round=%d/%d  tick=%d  checksum=%08X  arena pieces=%d  rig parts A/B=%d/%d"),
        PhaseText(Match.Phase), Match.Round, FMath::Max(1, Sim.GetRules().Rounds), Match.TickCount,
        ARKCGameMode::ComputeStateChecksum(Sim), Mode.GetArenaPieceCount(),
        Mode.GetFighterPartCount(RKC::ESide::A), Mode.GetFighterPartCount(RKC::ESide::B)));

    Lines.Add(FString::Printf(
        TEXT("RULE SET   min separation=%.0fcm (live %.1fcm)  bounds X [%.0f .. %.0f]  knockdown stamina<=%.0f  counter knockdown<=%.0f  KD limit=%d/round"),
        Sim.GetRules().MinSeparation, Separation, Sim.GetRules().ArenaMinX, Sim.GetRules().ArenaMaxX,
        Sim.GetRules().KnockdownStaminaThreshold, Sim.GetRules().CounterKnockdownStaminaThreshold,
        Sim.GetRules().MaxKnockdownsPerRound));

    Lines.Add(FString::Printf(
        TEXT("A  state=%-12s action=%-5s state ticks=%3d  attack tick=%2d/%2d  pos=%+7.1fcm facing=%+d  dodge cd=%2d  invuln=%2d  landed=%s"),
        FighterStateText(FighterA.State), ActionText(FighterA.CurrentAction),
        FighterA.StateTicksRemaining, FighterA.AttackTick, FighterA.AttackTotalTicks,
        FighterA.PositionX, FighterA.Facing, FighterA.DodgeCooldownTicks,
        FighterA.InvulnTicksRemaining, FighterA.bAttackHasLanded ? TEXT("Y") : TEXT("N")));

    Lines.Add(FString::Printf(
        TEXT("B  state=%-12s action=%-5s state ticks=%3d  attack tick=%2d/%2d  pos=%+7.1fcm facing=%+d  dodge cd=%2d  invuln=%2d  landed=%s"),
        FighterStateText(FighterB.State), ActionText(FighterB.CurrentAction),
        FighterB.StateTicksRemaining, FighterB.AttackTick, FighterB.AttackTotalTicks,
        FighterB.PositionX, FighterB.Facing, FighterB.DodgeCooldownTicks,
        FighterB.InvulnTicksRemaining, FighterB.bAttackHasLanded ? TEXT("Y") : TEXT("N")));

    Lines.Add(FString::Printf(
        TEXT("A  windows(ticks) jab %d/%d/%d  heavy %d/%d/%d   damage dealt=%.1f taken=%.1f  hits=%d taken=%d  blocks=%d dodges=%d thrown=%d"),
        JabStartupA, JabActiveA, JabRecoveryA, HeavyStartupA, HeavyActiveA, HeavyRecoveryA,
        FighterA.DamageDealtTotal, FighterA.DamageTakenTotal, FighterA.HitsLandedTotal,
        FighterA.HitsTakenTotal, FighterA.BlocksThisRound, FighterA.DodgesThisRound,
        FighterA.AttacksThrownTotal));

    Lines.Add(FString::Printf(
        TEXT("B  windows(ticks) jab %d/%d/%d  heavy %d/%d/%d   damage dealt=%.1f taken=%.1f  hits=%d taken=%d  blocks=%d dodges=%d thrown=%d"),
        JabStartupB, JabActiveB, JabRecoveryB, HeavyStartupB, HeavyActiveB, HeavyRecoveryB,
        FighterB.DamageDealtTotal, FighterB.DamageTakenTotal, FighterB.HitsLandedTotal,
        FighterB.HitsTakenTotal, FighterB.BlocksThisRound, FighterB.DodgesThisRound,
        FighterB.AttacksThrownTotal));

    Lines.Add(FString::Printf(
        TEXT("EVENTS  pending=%d  feed lines=%d  banner age=%.2fs  seconds since match end=%.1f  rematch offered=%s"),
        Sim.PendingEventCount(), Mode.GetEventFeed().Num(), Mode.GetBannerAge(),
        Mode.GetSecondsSinceMatchEnd(), Mode.IsRematchOffered() ? TEXT("YES") : TEXT("NO")));

    // The AI's own view. This is the evidence that the opponent was really being
    // driven by the brain rather than standing still.
    for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
    {
        const RKC::ESide Side = (SideIndex == 0) ? RKC::ESide::A : RKC::ESide::B;
        const RKC::FBotBrain* Bot = Mode.GetBot(Side);
        if (!Bot)
        {
            Lines.Add(FString::Printf(TEXT("%s  driven by HUMAN input"), SideIndex == 0 ? TEXT("A") : TEXT("B")));
            continue;
        }

        const RKC::FBotProfile& Profile = Bot->GetProfile();
        Lines.Add(FString::Printf(
            TEXT("%s  AI brain: %s / %s  reaction=%.3fs  think=%.3fs  plan=%s  tracking=%s  decisions=%d reactions=%d attacks=%d guard=%d slip=%d rng=%08X"),
            SideIndex == 0 ? TEXT("A") : TEXT("B"),
            ANSI_TO_TCHAR(RKC::GetArchetypeName(Profile.Archetype)),
            ANSI_TO_TCHAR(RKC::GetDifficultyName(Profile.Difficulty)), Profile.ReactionSeconds,
            Profile.ThinkIntervalSeconds, ANSI_TO_TCHAR(RKC::GetPlanName(Bot->GetPlan())),
            ActionText(Bot->GetTrackedAttack()), Bot->GetDecisionCount(), Bot->GetReactionCount(),
            Bot->GetAttackOrders(), Bot->GetGuardOrders(), Bot->GetSlipOrders(),
            Bot->GetRngState()));
    }

    UFont* Detail = DetailFont();
    const float TextScale = 0.85f * Scale;
    const float LineHeight = 16.0f * Scale;
    const float X = 24.0f * Scale;
    const float Y = 252.0f * Scale;
    const float PanelW = FMath::Min(static_cast<float>(Canvas->SizeX) - 48.0f * Scale,
                                    1460.0f * Scale);
    const float PanelH = (Lines.Num() * LineHeight) + 18.0f * Scale;

    DrawPanelBacking(X, Y, PanelW, PanelH, FLinearColor(0.0f, 0.02f, 0.05f, 0.70f));
    ++LastRegionCount;

    float CursorY = Y + 9.0f * Scale;
    for (int32 i = 0; i < Lines.Num(); ++i)
    {
        const FLinearColor Color = (i == 0) ? ColorTextAccent : ColorTextSecondary;
        DrawShadowedText(Detail, Lines[i], X + 10.0f * Scale, CursorY, TextScale, Color);
        CursorY += LineHeight;
    }
}

// ---------------------------------------------------------------------------
// 6. Footer - control legend and rematch prompt
// ---------------------------------------------------------------------------

void ARKCHUD::DrawFooter(const ARKCGameMode& Mode, float Scale)
{
    UFont* Detail = DetailFont();
    const float TextScale = 0.90f * Scale;
    const float CanvasW = static_cast<float>(Canvas->SizeX);
    const float CanvasH = static_cast<float>(Canvas->SizeY);

        // The legend is GENERATED from the binding table the self-test verifies, not
    // typed out again here. A legend that disagrees with the keys the engine
    // actually loaded is worse than no legend at all, and this is the one copy
    // that cannot drift.
    const FString Line1 = FString::Printf(TEXT("P1   %s move    %s block (hold)    %s jab    %s heavy    %s dodge"),
                                          *RKCKeysFor(TEXT("RKC_MoveX")), *RKCKeysFor(TEXT("RKC_P1Block")),
                                          *RKCKeysFor(TEXT("RKC_P1Jab")), *RKCKeysFor(TEXT("RKC_P1Heavy")),
                                          *RKCKeysFor(TEXT("RKC_P1Dodge")));
    const FString Line2 = FString::Printf(TEXT("P2   %s move    %s block (hold)    %s jab    %s heavy    %s dodge"),
                                          *RKCKeysFor(TEXT("RKC_MoveX2")), *RKCKeysFor(TEXT("RKC_P2Block")),
                                          *RKCKeysFor(TEXT("RKC_P2Jab")), *RKCKeysFor(TEXT("RKC_P2Heavy")),
                                          *RKCKeysFor(TEXT("RKC_P2Dodge")));
    const FString Line3 = FString::Printf(
        TEXT("%s pause    %s back    %s rematch    %s quit    %s frame data (%s)    rkc.BotSide=%d"),
        *RKCKeysFor(TEXT("RKC_Pause")), *RKCKeysFor(TEXT("RKC_MenuBack")),
        *RKCKeysFor(TEXT("RKC_Rematch")), *RKCKeysFor(TEXT("RKC_QuitMatch")),
        *RKCKeysFor(TEXT("RKC_ToggleFrameData")),
        Mode.IsFrameDataVisible() ? TEXT("ON") : TEXT("OFF"),
        Mode.GetSetup().bBotControlsA && Mode.GetSetup().bBotControlsB
            ? 3
            : (Mode.GetSetup().bBotControlsA ? 2 : (Mode.GetSetup().bBotControlsB ? 1 : 0)));

    const FString TouchLine =
        TEXT("TOUCH   left thumb stick    right thumb jab / heavy / block (hold) / dodge");

    const ARKCPlayerController* ReadOnlyPC = Mode.GetRKCPlayerController();
    const bool bPadOnScreen =
        ReadOnlyPC != nullptr && ReadOnlyPC->GetTouchController().ShouldDraw();

    const float LineHeight = 17.0f * Scale;
    const int32 LegendLines = bPadOnScreen ? 4 : 3;
    const float BiasY = CanvasH - 20.0f * Scale - LineHeight * static_cast<float>(LegendLines);

    // A backing strip keeps three lines of text from being lost in the apron of
    // the ring.
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), 0.0f, BiasY - 8.0f * Scale, CanvasW,
             LineHeight * static_cast<float>(LegendLines) + 16.0f * Scale);

    const FString* Lines[4] = { &Line1, &Line2, &Line3, &TouchLine };
    float CursorY = BiasY;
    for (int32 Index = 0; Index < LegendLines; ++Index)
    {
        const float TextW = MeasureText(Detail, *Lines[Index], TextScale);
        DrawShadowedText(Detail, *Lines[Index], (CanvasW - TextW) * 0.5f, CursorY, TextScale,
                         ColorTextSecondary);
        CursorY += LineHeight;
    }

    ++LastRegionCount;

    // ---- Rematch prompt ----------------------------------------------------
    if (Mode.IsRematchOffered())
    {
        const FString Prompt = TEXT("PRESS  R  FOR A REMATCH");
        UFont* Font = HeadingFont();
        const float PromptScale = 1.25f * Scale;
        const float TextW = MeasureText(Font, Prompt, PromptScale);
        const float X = (CanvasW - TextW) * 0.5f;
        const float Y = CanvasH * 0.58f;

        // Pulse so the prompt reads as an invitation rather than a result.
        const float Pulse = 0.5f + 0.5f * FMath::Sin(Mode.GetSecondsSinceMatchEnd() * 6.0f);
        const FLinearColor Color(1.0f, 0.85f, 0.35f, 0.65f + 0.35f * Pulse);

        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f), 0.0f, Y - 8.0f * Scale, CanvasW,
                 34.0f * Scale);
        DrawShadowedText(Font, Prompt, X, Y, PromptScale, Color);
        ++LastRegionCount;
    }
}

// ---------------------------------------------------------------------------
// 7. Front end - the branded cards
// ---------------------------------------------------------------------------
// Every string below comes from the front-end machine; this file chooses only
// where it goes. That is what keeps the menu and the rules from disagreeing about
// which screen is up: there is one screen variable in the project and it lives in
// the machine, not here.

void ARKCHUD::DrawFrontEnd(const ARKCGameMode& Mode, float Scale)
{
    const RKCFrontEnd::FMachine& Machine = Mode.GetFrontEnd();
    UFont* Heading = HeadingFont();
    UFont* Detail = DetailFont();

    const float CanvasW = static_cast<float>(Canvas->SizeX);
    const float CanvasH = static_cast<float>(Canvas->SizeY);
    const RKCFrontEnd::EScreen Screen = Machine.GetScreen();

    // A dark wash rather than an opaque fill: the ring is still visible behind the
    // menu, so the branding and the arena read as one product.
    DrawRect(FLinearColor(0.015f, 0.02f, 0.04f, 0.88f), 0.0f, 0.0f, CanvasW, CanvasH);

    // A red band at the top edge, and a thin white rule under the heading block.
    DrawRect(FLinearColor(0.80f, 0.13f, 0.15f, 0.95f), 0.0f, 0.0f, CanvasW, 5.0f * Scale);

    float CursorY = 54.0f * Scale;

    const FString HeadingText = Machine.GetHeading();
    if (!HeadingText.IsEmpty())
    {
        const float HeadingScale = 2.0f * Scale;
        const float TextW = MeasureText(Heading, HeadingText, HeadingScale);
        DrawShadowedText(Heading, HeadingText, FMath::Max(12.0f, (CanvasW - TextW) * 0.5f), CursorY,
                         HeadingScale, FLinearColor::White);
        CursorY += 46.0f * Scale;
        ++LastRegionCount;
    }

    const FString SubText = Machine.GetSubheading();
    if (!SubText.IsEmpty())
    {
        const float SubScale = 1.05f * Scale;
        const float TextW = MeasureText(Detail, SubText, SubScale);
        DrawShadowedText(Detail, SubText, FMath::Max(12.0f, (CanvasW - TextW) * 0.5f), CursorY,
                         SubScale, ColorTextSecondary);
        CursorY += 30.0f * Scale;
        ++LastRegionCount;
    }

    DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.18f), CanvasW * 0.15f, CursorY, CanvasW * 0.70f,
             2.0f * Scale);
    CursorY += 22.0f * Scale;

    // ---- Choosing screens: the versus card ---------------------------------
    const bool bIsVersusCard = (Screen == RKCFrontEnd::EScreen::Versus);
    const bool bIsCharacterCard = (Screen == RKCFrontEnd::EScreen::CharacterSelect);

    if (bIsVersusCard || bIsCharacterCard)
    {
        const FString& Left = Machine.GetLeftCaption();
        const FString& Right = Machine.GetRightCaption();
        const float CaptionScale = 1.1f * Scale;
        const float Top = CanvasH * 0.42f;

        if (!Left.IsEmpty())
        {
            const TArray<FString> Lines = { Left };
            DrawShadowedText(Detail, Lines[0], CanvasW * 0.10f, Top, CaptionScale, ColorTextPrimary);
        }
        if (!Right.IsEmpty())
        {
            const float TextW = MeasureText(Detail, Right, CaptionScale);
            DrawShadowedText(Detail, Right, CanvasW * 0.90f - TextW, Top, CaptionScale,
                             ColorTextPrimary);
        }

        // "VS" between the two corners, pulsing so the card reads as an imminent
        // fight rather than as a static screen.
        const FString Vs = TEXT("VS");
        const float VsScale = 2.6f * Scale;
        const float VsW = MeasureText(Heading, Vs, VsScale);
        const float Pulse = 0.55f + 0.45f * FMath::Sin(Machine.GetScreenAge() * 5.0f);
        DrawShadowedText(Heading, Vs, (CanvasW - VsW) * 0.5f, Top - 26.0f * Scale, VsScale,
                         FLinearColor(1.0f, 0.30f, 0.25f, Pulse));
        ++LastRegionCount;
    }

    // ---- Choosing screens: the loading card --------------------------------
    if (Screen == RKCFrontEnd::EScreen::Loading)
    {
        const float BarW = FMath::Min(760.0f * Scale, CanvasW - 120.0f * Scale);
        const float BarX = (CanvasW - BarW) * 0.5f;
        const float BarY = CanvasH * 0.60f;
        const float Progress = FMath::Clamp(Machine.GetLoadingProgress(), 0.0f, 1.0f);

        DrawBar(BarX, BarY, BarW, 26.0f * Scale, Progress, FLinearColor(0.88f, 0.22f, 0.20f, 1.0f),
                false);

        // The bar is drawn with the percentage beside it on purpose: a moving bar
        // is not evidence that anything is finishing.
        const FString Percent = FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Progress * 100.0f));
        const float TextW = MeasureText(Heading, Percent, 1.3f * Scale);
        DrawShadowedText(Heading, Percent, (CanvasW - TextW) * 0.5f, BarY - 34.0f * Scale,
                         1.3f * Scale, ColorTextPrimary);

        const FString& LoadDetail = Machine.GetLoadDetail();
        if (!LoadDetail.IsEmpty())
        {
            const float DetailW = MeasureText(Detail, LoadDetail, 0.9f * Scale);
            DrawShadowedText(Detail, LoadDetail, (CanvasW - DetailW) * 0.5f, BarY + 34.0f * Scale,
                             0.9f * Scale, ColorTextSecondary);
        }
        ++LastRegionCount;
    }

    // ---- Choosing screens: the result card ---------------------------------
    if (Screen == RKCFrontEnd::EScreen::Results)
    {
        const FString& Result = Machine.GetResultText();
        if (!Result.IsEmpty())
        {
            const float ResultScale = 2.2f * Scale;
            const float TextW = MeasureText(Heading, Result, ResultScale);
            const float Y = CanvasH * 0.40f;
            DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f), 0.0f, Y - 10.0f * Scale, CanvasW,
                     54.0f * Scale);
            DrawShadowedText(Heading, Result, (CanvasW - TextW) * 0.5f, Y, ResultScale,
                             ColorTextAccent);
            ++LastRegionCount;
        }
    }

    // ---- The row list ------------------------------------------------------
    const TArray<RKCFrontEnd::FMenuItem>& Items = Machine.GetItems();
    if (Items.Num() > 0)
    {
        const int32 Selection = FMath::Clamp(Machine.GetSelection(), 0, Items.Num() - 1);
        const float RowHeight = 38.0f * Scale;
        const float RowGap = 4.0f * Scale;

        // A long list (the controls or options screen) scrolls in a window that
        // always contains the selection, so the highlighted row can never be off
        // screen.
        const int32 MaxVisibleRows = 11;
        int32 FirstVisible = 0;
        if (Items.Num() > MaxVisibleRows)
        {
            FirstVisible = FMath::Clamp(Selection - MaxVisibleRows / 2, 0,
                                        Items.Num() - MaxVisibleRows);
        }
        const int32 LastVisible = FMath::Min(Items.Num(), FirstVisible + MaxVisibleRows);

        const float ListW = FMath::Min(760.0f * Scale, CanvasW - 80.0f * Scale);
        const float ListX = (CanvasW - ListW) * 0.5f;
        float ListY = FMath::Max(CursorY, CanvasH * 0.34f);

        const float ListH = (RowHeight + RowGap) * static_cast<float>(LastVisible - FirstVisible);
        if (ListY + ListH > CanvasH - 130.0f * Scale)
        {
            ListY = FMath::Max(64.0f * Scale, CanvasH - 130.0f * Scale - ListH);
        }

        for (int32 Index = FirstVisible; Index < LastVisible; ++Index)
        {
            const RKCFrontEnd::FMenuItem& Item = Items[Index];
            const bool bSelected = (Index == Selection);
            const float RowY = ListY + (RowHeight + RowGap) * static_cast<float>(Index - FirstVisible);

            // Selection is a filled red plate with a marker; a disabled row is
            // dimmer than an enabled one and never marked, so "cannot be chosen"
            // is visible before it is attempted.
            FLinearColor RowColour(0.05f, 0.06f, 0.09f, 0.72f);
            if (!Item.bEnabled)
            {
                RowColour = FLinearColor(0.09f, 0.09f, 0.11f, 0.60f);
            }
            else if (bSelected)
            {
                RowColour = FLinearColor(0.78f, 0.14f, 0.16f, 0.92f);
            }
            DrawRect(RowColour, ListX, RowY, ListW, RowHeight);

            const FLinearColor LabelColour = !Item.bEnabled
                                                 ? FLinearColor(0.48f, 0.48f, 0.52f, 1.0f)
                                                 : ColorTextPrimary;
            const FString Marker = bSelected ? TEXT(">  ") : TEXT("   ");
            DrawShadowedText(Detail, Marker + Item.Label, ListX + 16.0f * Scale,
                             RowY + 10.0f * Scale, 1.05f * Scale, LabelColour);

            if (bSelected && !Item.Hint.IsEmpty())
            {
                const float HintScale = 0.85f * Scale;
                const float HintW = MeasureText(Detail, Item.Hint, HintScale);
                DrawShadowedText(Detail, Item.Hint, ListX + ListW - HintW - 16.0f * Scale,
                                 RowY + 13.0f * Scale, HintScale,
                                 FLinearColor(0.95f, 0.95f, 0.98f, 0.92f));
            }
            ++LastRegionCount;
        }

        if (Items.Num() > MaxVisibleRows)
        {
            const FString Scroll = FString::Printf(TEXT("%d more"), Items.Num() - MaxVisibleRows);
            DrawShadowedText(Detail, Scroll, ListX + ListW - MeasureText(Detail, Scroll, 0.8f * Scale),
                             ListY + ListH + 6.0f * Scale, 0.8f * Scale, ColorTextSecondary);
        }

        CursorY = ListY + ListH + 30.0f * Scale;
    }

    // ---- Body and footer text ----------------------------------------------
    float BodyY = FMath::Max(CursorY, CanvasH * 0.62f);

    // A local-versus rematch needs both players; saying so is the difference
    // between a card that looks stuck and a card that is waiting for someone.
    if (Machine.IsAwaitingSecondReady())
    {
        const FString Ready = TEXT("WAITING FOR PLAYER 2 ...");
        const float ReadyW = MeasureText(Heading, Ready, 1.2f * Scale);
        DrawShadowedText(Heading, Ready, (CanvasW - ReadyW) * 0.5f, BodyY, 1.2f * Scale,
                         ColorTextAccent);
        BodyY += 30.0f * Scale;
    }

    for (const FString& Line : Machine.GetBodyLines())
    {
        DrawShadowedText(Detail, Line, 40.0f * Scale, BodyY, 0.9f * Scale, ColorTextSecondary);
        BodyY += 20.0f * Scale;
    }

    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.40f), 0.0f, CanvasH - 96.0f * Scale, CanvasW,
             96.0f * Scale);
    float FooterY = CanvasH - 88.0f * Scale;
    for (const FString& Line : Machine.GetFooterLines())
    {
        DrawShadowedText(Detail, Line, 40.0f * Scale, FooterY, 0.85f * Scale, ColorTextSecondary);
        FooterY += 18.0f * Scale;
    }

    const FString Footer = Machine.GetFooter();
    if (!Footer.IsEmpty())
    {
        const float FooterW = MeasureText(Detail, Footer, 0.85f * Scale);
        DrawShadowedText(Detail, Footer, (CanvasW - FooterW) * 0.5f, CanvasH - 26.0f * Scale,
                         0.85f * Scale, ColorTextAccent);
    }
    ++LastRegionCount;
}

// ---------------------------------------------------------------------------
// 8. Touch pad
// ---------------------------------------------------------------------------
// The layout lives in RKCTouch::FController (normalised, device independent) and
// this function only paints it, so what a finger hits and what a player sees are
// the same geometry. The controller reports whether the pad is live; a menu is
// tapped rather than played, so the pad is hidden over the front end unless a
// diagnostic run has explicitly forced it on for a screenshot.

void ARKCHUD::DrawTouchPad(const ARKCGameMode& Mode, float Scale)
{
    ARKCPlayerController* PC = Mode.GetRKCPlayerController();
    if (PC == nullptr)
    {
        return;
    }

    const RKCTouch::FController& Touch = PC->GetTouchController();
    if (!Touch.ShouldDraw())
    {
        return;
    }
    if (Mode.IsFrontEndActive() && !Touch.IsForcedVisible())
    {
        return;
    }

    const FVector2D Viewport(static_cast<double>(Canvas->SizeX), static_cast<double>(Canvas->SizeY));

    // The canvas has no disc primitive, so a disc is built from horizontal spans
    // and an outline from short segments. Cheap at pad sizes, and it reads as a
    // button rather than as a box - which is the whole point of a touch pad.
    auto Disc = [this](const FVector2D& Centre, float Radius, const FLinearColor& Colour)
    {
        if (Radius < 1.0f)
        {
            return;
        }
        const float Step = FMath::Max(1.0f, Radius / 10.0f);
        for (float Offset = -Radius; Offset <= Radius; Offset += Step)
        {
            const float Half = FMath::Sqrt(FMath::Max(0.0f, Radius * Radius - Offset * Offset));
            DrawRect(Colour, static_cast<float>(Centre.X) - Half, static_cast<float>(Centre.Y) + Offset,
                     Half * 2.0f, Step);
        }
    };

    auto Ring = [this](const FVector2D& Centre, float Radius, const FLinearColor& Colour,
                       float Thickness)
    {
        constexpr int32 Segments = 32;
        FVector2D Previous(static_cast<float>(Centre.X) + Radius, static_cast<float>(Centre.Y));
        for (int32 Index = 1; Index <= Segments; ++Index)
        {
            const float Angle = (2.0f * PI * static_cast<float>(Index)) / static_cast<float>(Segments);
            const FVector2D Next(static_cast<float>(Centre.X) + FMath::Cos(Angle) * Radius,
                                 static_cast<float>(Centre.Y) + FMath::Sin(Angle) * Radius);
            DrawLine(static_cast<float>(Previous.X), static_cast<float>(Previous.Y),
                     static_cast<float>(Next.X), static_cast<float>(Next.Y), Colour, Thickness);
            Previous = Next;
        }
    };

    const TArray<RKCTouch::FActiveTouch>& ActiveTouches = Touch.GetActiveTouches();

    // The safe area the pad is drawn inside. Taking it from the controller rather
    // than re-deriving it here is what guarantees the drawn pad and the hit-tested
    // pad are the same pad: a control drawn under a camera cut-out would be a
    // control the player cannot press.
    const FMargin SafeInsets = Touch.GetSafeInsetsPx();

    for (const RKCTouch::FWidget& Widget : Touch.GetWidgets())
    {
        const FVector2D Centre = RKCTouch::FController::CentrePx(Widget, Viewport, SafeInsets);
        const float Radius = RKCTouch::FController::RadiusPx(Widget, Viewport, SafeInsets);
        if (Radius < 2.0f)
        {
            continue;
        }

        // A held control is filled brighter. This is the only touch feedback a
        // player gets that a press was registered, so it is derived from the same
        // latch the simulation reads rather than from a cosmetic timer.
        bool bHeld = false;
        for (const RKCTouch::FActiveTouch& Active : ActiveTouches)
        {
            if (Active.Control == Widget.Control)
            {
                bHeld = true;
                break;
            }
        }
        if (Widget.Control == RKCTouch::EControl::Stick)
        {
            bHeld = Touch.IsStickActive();
        }
        else if (Widget.Control == RKCTouch::EControl::Block)
        {
            bHeld = Touch.IsBlockHeld();
        }

        const FLinearColor Accent(Widget.Colour.R, Widget.Colour.G, Widget.Colour.B, 0.95f);
        const FLinearColor Fill = bHeld ? FLinearColor(Accent.R, Accent.G, Accent.B, 0.55f)
                                        : FLinearColor(0.04f, 0.05f, 0.08f, 0.55f);

        Disc(Centre, Radius, Fill);
        Ring(Centre, Radius, Accent, FMath::Max(2.0f, 2.0f * Scale));

        if (Widget.bIsStick)
        {
            // The knob is where the finger is, clamped by the controller to the
            // ring, so the drawn stick and the reported axis cannot disagree.
            const FVector2D Knob = Centre + Touch.GetStickKnobOffsetPx();
            Disc(Knob, Radius * 0.34f,
                 bHeld ? FLinearColor(1.0f, 1.0f, 1.0f, 0.85f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.55f));
            Ring(Knob, Radius * 0.34f, FLinearColor(1.0f, 1.0f, 1.0f, 0.75f), 2.0f);
        }
        else
        {
            const float TextScale = 1.0f * Scale;
            const float TextW = MeasureText(HeadingFont(), Widget.Label, TextScale);
            DrawShadowedText(HeadingFont(), Widget.Label,
                             static_cast<float>(Centre.X) - TextW * 0.5f,
                             static_cast<float>(Centre.Y) - 9.0f * Scale, TextScale,
                             FLinearColor::White);
        }

        ++LastRegionCount;
    }

    // A diagnostic caption, drawn only when the pad was forced on. A real device
    // run shows the pad alone, because the numbers are for the tester.
    if (Touch.IsForcedVisible())
    {
        const FString Caption =
            FString::Printf(TEXT("touch pad (diagnostic)  %s"), *Touch.DescribeState());
        DrawShadowedText(DetailFont(), Caption, 24.0f * Scale, 22.0f * Scale, 0.85f * Scale,
                         ColorTextAccent);
        ++LastRegionCount;

        // The safe area is stated explicitly, because it is the one thing a
        // screenshot of the pad cannot otherwise prove: whether the pad moved in
        // because the device reported a cut-out, or because nothing was reported.
        const FString SafeCaption = FString::Printf(
            TEXT("safe area: insets L=%.0f T=%.0f R=%.0f B=%.0f px of %.0fx%.0f  %s"),
            SafeInsets.Left, SafeInsets.Top, SafeInsets.Right, SafeInsets.Bottom, Viewport.X,
            Viewport.Y,
            Touch.HasSafeAreaInset() ? TEXT("inside the safe rectangle")
                                     : TEXT("no inset: full viewport"));
        DrawShadowedText(DetailFont(), SafeCaption, 24.0f * Scale, 36.0f * Scale, 0.85f * Scale,
                         ColorTextAccent);
        ++LastRegionCount;
    }
}
