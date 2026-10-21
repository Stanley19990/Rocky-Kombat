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
#include "Engine/Texture2D.h"
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
    for (RKC::ESide Side : {RKC::ESide::A,RKC::ESide::B}) {
        const auto& F=Mode->GetSim().GetFighter(Side);
        if (F.State==RKC::EFighterState::HitStun) {
            const FVector P=Project(FVector(F.PositionX,0,155));
            for (int32 I=0;I<6;++I) {
                const float Angle=I*PI/3;
                const FVector2D Dir(FMath::Cos(Angle),FMath::Sin(Angle));
                const float Radius=15*Scale;
                DrawLine(P.X+Dir.X*Radius,P.Y+Dir.Y*Radius,P.X+Dir.X*Radius*1.65f,P.Y+Dir.Y*Radius*1.65f,FLinearColor(1,.73f,.32f,1),2*Scale);
            }
        }
    }
    DrawFrameData(*Mode, Scale);
    if (Mode->IsFrameDataVisible()) DrawEventFeed(*Mode, Scale);
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
    if (Mode.IsFrameDataVisible())
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
    if (Mode.IsFrameDataVisible())
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
    const ARKCPlayerController* PC=Mode.GetRKCPlayerController();
    const bool Touch=PC && PC->GetTouchController().ShouldDraw();
    const float W=Canvas->SizeX,H=Canvas->SizeY;
    FString Legend=Touch ? TEXT("MOVE  /  JAB  /  HEAVY  /  HOLD BLOCK  /  DODGE") : TEXT("A / D MOVE     S HOLD BLOCK     Z JAB     X HEAVY     C DODGE     ESC MENU");
    if (!Mode.GetSetup().bBotControlsB && !Mode.GetSetup().bBotControlsA && Mode.GetFrontEnd().GetMatchRequest().Arrangement==RKCFrontEnd::EArrangement::LocalVersus)
        Legend=TEXT("P1: A/D  S  Z/X/C    |    P2: ARROWS  DOWN  NUM 1/2/3    |    ESC MENU");
    const float TS=FMath::Min(.9f*Scale,(W*.92f)/FMath::Max(1.0f,MeasureText(DetailFont(),Legend,1.0f)));
    DrawRect(FLinearColor(.01f,.015f,.025f,.75f),0,H-30*Scale,W,30*Scale);
    DrawShadowedText(DetailFont(),Legend,(W-MeasureText(DetailFont(),Legend,TS))*.5f,H-21*Scale,TS,ColorTextSecondary);
    ++LastRegionCount;
}

void ARKCHUD::DrawFrontEnd(const ARKCGameMode& Mode, float IgnoredScale)
{
    const auto& M = Mode.GetFrontEnd();
    const auto Screen = M.GetScreen();
    using E = RKCFrontEnd::EScreen;
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    const float S = FMath::Min(W/1280.0f,H/720.0f);
    const FLinearColor Gold(.94f,.69f,.29f,1), Muted(.65f,.69f,.76f,1);
    if (PresentationTextures.IsEmpty()) {
        for (const TCHAR* Name : {TEXT("RockycatLogo"), TEXT("RBAChampionBelt"), TEXT("ArenaPoster"), TEXT("RockyPortrait"), TEXT("ShadowPortrait")}) {
            PresentationTextures.Add(LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/RockyKombat/Presentation/%s.%s"),Name,Name)));
        }
    }
    auto Image = [&](int32 Index,float X,float Y,float BW,float BH,bool Cover=false,float Alpha=1.0f) {
        UTexture2D* T=PresentationTextures.IsValidIndex(Index)?PresentationTextures[Index].Get():nullptr;
        if (!T || !T->GetSizeX() || !T->GetSizeY()) return;
        const float Ratio=float(T->GetSizeX())/T->GetSizeY();
        float U=0,V=0,UL=1,VL=1;
        if (Cover) {
            if (Ratio>BW/BH) { UL=BW/(BH*Ratio); U=(1-UL)*.5f; }
            else { VL=BH/(BW/Ratio); V=(1-VL)*.5f; }
        } else {
            const float IW=FMath::Min(BW,BH*Ratio), IH=IW/Ratio;
            X+=(BW-IW)*.5f; Y+=(BH-IH)*.5f; BW=IW; BH=IH;
        }
        DrawTexture(T,X,Y,BW,BH,U,V,UL,VL,FLinearColor(1,1,1,Alpha),BLEND_Translucent);
        ++LastRegionCount;
    };
    auto Centre = [&](const FString& Text,float Y,float Size,const FLinearColor& C) {
        const float TS=Size*S;
        DrawShadowedText(HeadingFont(),Text,(W-MeasureText(HeadingFont(),Text,TS))*.5f,Y,TS,C);
        ++LastRegionCount;
    };
    DrawRect(FLinearColor(.012f,.018f,.03f,1),0,0,W,H);
    DrawRect(Gold,0,0,W,3*S);
    if (Screen==E::Boot) {
        const float Half=M.GetBootDuration()*.5f;
        const bool Belt=M.GetScreenAge()>=Half;
        const float Age=Belt?M.GetScreenAge()-Half:M.GetScreenAge();
        const float Fade=FMath::Clamp(Age/.25f,0.0f,1.0f);
        Image(Belt?1:0,W*.2f,H*.15f,W*.6f,H*.58f,false,Fade);
        Centre(Belt?TEXT("ROCKY BOXING ARENA"):TEXT("ROCKYCAT PRESENTS"),H*.79f,1.25f,Gold);
        Centre(TEXT("Tap or press ENTER to continue"),H*.91f,.65f,Muted);
        return;
    }
    const bool Poster=Screen==E::Title || Screen==E::Menu || Screen==E::Loading;
    if (Poster) {
        Image(2,0,0,W,H,true);
        DrawRect(FLinearColor(.008f,.012f,.024f,Screen==E::Menu?.62f:.28f),0,0,W,H);
        DrawRect(FLinearColor(.008f,.012f,.024f,.88f),0,H*.73f,W,H*.27f);
    }
    if (Screen==E::Title) {
        Centre(TEXT("ROCKY KOMBAT"),H*.77f,2.5f,FLinearColor::White);
        Centre(TEXT("THE UNDERDOGS ARENA"),H*.85f,.95f,Gold);
        Centre(TEXT("TAP TO ENTER  /  PRESS ENTER"),H*.93f,.8f,Muted);
        return;
    }
    const bool Portrait=Screen==E::CharacterSelect;
    const bool Versus=Screen==E::Versus;
    Centre(M.GetHeading(),H*.065f,1.65f,FLinearColor::White);
    Centre(M.GetSubheading(),H*.135f,.8f,Gold);
    DrawRect(FLinearColor(.94f,.69f,.29f,.25f),W*.08f,H*.19f,W*.84f,S);
    const auto& R=M.GetMatchRequest();
    auto FighterCard = [&](RKC::ERoster Fighter,float X,float Y,float CW,float CH,bool Right,const FString& Label) {
        const FLinearColor Accent=Right?FLinearColor(.20f,.48f,.96f,1):FLinearColor(.91f,.20f,.20f,1);
        DrawRect(FLinearColor(.035f,.045f,.065f,1),X,Y,CW,CH);
        DrawRect(Accent,X,Y,4*S,CH);
        Image(Fighter==RKC::ERoster::Rocky?3:4,X+8*S,Y+8*S,CW-16*S,CH-82*S);
        const FString Name=RKC::GetRosterDisplayName(Fighter).ToUpper();
        DrawRect(FLinearColor(.01f,.015f,.025f,.94f),X,Y+CH-88*S,CW,88*S);
        DrawShadowedText(HeadingFont(),Name,X+20*S,Y+CH-75*S,1.4f*S,FLinearColor::White);
        DrawShadowedText(DetailFont(),Label,X+20*S,Y+CH-44*S,.9f*S,Accent);
        DrawShadowedText(DetailFont(),Fighter==RKC::ERoster::Rocky?TEXT("POWER / PRESSURE BOXER"):TEXT("SPEED / COUNTER SPECIALIST"),X+20*S,Y+CH-22*S,.75f*S,Gold);
        ++LastRegionCount;
    };
    if (Portrait) {
        RKC::ERoster Fighter=M.GetCharacterStage()==0?R.FighterA:R.FighterB;
        if (M.GetItems().IsValidIndex(M.GetSelection())) {
            const FString& Id=M.GetItems()[M.GetSelection()].Id;
            if (Id==TEXT("fighter_rocky")) Fighter=RKC::ERoster::Rocky;
            if (Id==TEXT("fighter_shadow")) Fighter=RKC::ERoster::Shadow;
        }
        FighterCard(Fighter,W*.07f,H*.23f,W*.35f,H*.68f,false,TEXT("RBA FIGHTER"));
        DrawShadowedText(DetailFont(),TEXT("CHOOSE YOUR CORNER"),W*.52f,H*.25f,1.0f*S,Gold);
    } else if (Versus) {
        const float Slide=(1-FMath::Clamp(M.GetScreenAge()/.35f,0.0f,1.0f))*65*S;
        FighterCard(R.FighterA,W*.06f-Slide,H*.24f,W*.36f,H*.65f,false,M.GetCornerLabel(0));
        FighterCard(R.FighterB,W*.58f+Slide,H*.24f,W*.36f,H*.65f,true,M.GetCornerLabel(1));
        Centre(TEXT("VS"),H*.46f,3.0f,Gold);
        if (Screen==E::Versus) { Centre(TEXT("GET READY"),H*.93f,.95f,FLinearColor::White); return; }
    }
    if (Screen==E::Menu) {
        Image(1,W*.06f,H*.32f,W*.36f,H*.30f);
        DrawShadowedText(HeadingFont(),TEXT("ENTER THE ARENA"),W*.07f,H*.67f,1.3f*S,Gold);
    }
    if (Screen==E::Loading) {
        DrawRect(FLinearColor(.01f,.015f,.025f,.92f),W*.16f,H*.77f,W*.68f,H*.13f);
        DrawBar(W*.20f,H*.82f,W*.60f,8*S,M.GetLoadingProgress(),Gold,false);
        Centre(FString::Printf(TEXT("PREPARING THE RING  %d%%"),FMath::RoundToInt(M.GetLoadingProgress()*100)),H*.77f,.85f,FLinearColor::White);
        return;
    }
    if (Screen==E::Results) Centre(M.GetResultText(),H*.28f,1.9f,Gold);
    const auto& Items=M.GetItems();
    if (Items.Num()>7) {
        DrawRect(FLinearColor(.18f,.20f,.25f,1),W*.965f,H*.33f,W*.035f,H*.30f);
        DrawShadowedText(DetailFont(),TEXT("UP"),W*.968f,H*.35f,.65f*S,Gold);
        DrawShadowedText(DetailFont(),TEXT("DN"),W*.968f,H*.59f,.65f*S,Gold);
    }
    for (int32 I=0;I<Items.Num();++I) {
        const FBox2D Rect=M.GetItemRect(I,FVector2D(W,H));
        if (!Rect.bIsValid) continue;
        const bool Selected=I==M.GetSelection(); const auto& Item=Items[I];
        DrawRect(Selected?FLinearColor(.52f,.10f,.11f,.97f):FLinearColor(.035f,.045f,.065f,.96f),Rect.Min.X,Rect.Min.Y,Rect.GetSize().X,Rect.GetSize().Y);
        if (Selected) DrawRect(Gold,Rect.Min.X,Rect.Min.Y,4*S,Rect.GetSize().Y);
        DrawShadowedText(DetailFont(),Item.Label,Rect.Min.X+18*S,Rect.Min.Y+12*S,.95f*S,Item.bEnabled?FLinearColor::White:Muted*.6f);
        ++LastRegionCount;
    }
    if (Items.IsValidIndex(M.GetSelection())) {
        const FString Hint=Items[M.GetSelection()].Hint;
        // Hints get their own line; long copy never collides with the row label.
        const float HintScale=FMath::Min(.75f*S,(W*.88f)/FMath::Max(1.0f,MeasureText(DetailFont(),Hint,1.0f)));
        DrawShadowedText(DetailFont(),Hint,W*.06f,H*.93f,HintScale,Gold);
    }
    if (!Portrait && !Versus && Screen!=E::Menu && Screen!=E::Results) {
        float Y=(Screen==E::Controls || Screen==E::LocalVersusJoin)?H*.23f:H*.82f;
        int32 Count=0; const int32 Max=(Screen==E::Controls || Screen==E::LocalVersusJoin)?10:2;
        for (const FString& Line:M.GetBodyLines()) {
            if (Count++>=Max) break;
            float TS=FMath::Min(.75f*S,(W*.88f)/FMath::Max(1.0f,MeasureText(DetailFont(),Line,1.0f)));
            DrawShadowedText(DetailFont(),Line,W*.06f,Y,TS,Muted); Y+=23*S;
        }
    }
    DrawShadowedText(DetailFont(),TEXT("TAP TO SELECT     W/S / ARROWS     ENTER CONFIRM     ESC BACK"),W*.06f,H*.975f,.65f*S,Muted);
    ++LastRegionCount;
}

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
