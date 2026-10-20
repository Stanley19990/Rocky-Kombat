// RKCCombatCore.cpp
//
// Implementation of the Rocky Kombat authoritative combat simulation.
// Pure C++ - no Unreal Engine headers. See RKCCombatCore.h for the design
// constraints, and docs/design-decisions.md for the reasoning behind every
// provisional numeric value used here.

#include "RKCCombatCore.h"

#include <cmath>

namespace RKC
{
namespace
{
    // -----------------------------------------------------------------
    // Local helpers (implementation detail, not part of the public API)
    // -----------------------------------------------------------------

    inline int32_t SideIndex(ESide Side)
    {
        return (Side == ESide::B) ? 1 : 0;
    }

    /** Whole simulation ticks for a duration, rounded to nearest. */
    inline int32_t TicksFromSeconds(float Seconds)
    {
        if (!(Seconds > 0.0f))
        {
            return 0;
        }
        return static_cast<int32_t>((Seconds / FCombatSim::FixedTickSeconds) + 0.5f);
    }

    /** As above but never zero - used for windows that must exist
     *  (active hit frames, stun durations, knockdown duration). */
    inline int32_t TicksFromSecondsMin1(float Seconds)
    {
        const int32_t Ticks = TicksFromSeconds(Seconds);
        return (Ticks < 1) ? 1 : Ticks;
    }

    inline float ClampFloat(float Value, float Low, float High)
    {
        if (Value < Low) return Low;
        if (Value > High) return High;
        return Value;
    }

    inline bool IsWinnerSide(EMatchResult Result, ESide Side)
    {
        if (Result == EMatchResult::FighterAWins) return Side == ESide::A;
        if (Result == EMatchResult::FighterBWins) return Side == ESide::B;
        return false;
    }

    /** Stamina regeneration multipliers by fighter state.
     *  DESIGN DECISION: throwing punches is work, so a fighter recovers much
     *  less stamina while attacking or dodging than while standing. These are
     *  provisional and are recorded in docs/design-decisions.md. */
    constexpr float RegenScaleAttacking = 0.25f;
    constexpr float RegenScaleDodging = 0.25f;
    constexpr float RegenScaleStunned = 0.50f;

    /** Blocking is work: movement with the guard up is deliberately slow. */
    constexpr float BlockWalkSpeedScale = 0.50f;

    /** A punch that is blocked still pushes the defender, but less than a
     *  clean hit. */
    constexpr float BlockedKnockbackScale = 0.50f;

    /** Comparison tolerance used by the judges when comparing round totals. */
    constexpr float JudgeEpsilon = 0.001f;

    /** Hard ceiling on fixed steps performed by a single Advance() call. */
    constexpr int32_t MaxTicksPerAdvance = 32;
}

// ---------------------------------------------------------------------------
// Construction / configuration
// ---------------------------------------------------------------------------

FCombatSim::FCombatSim()
{
    // A freshly constructed simulator has never been Init()ed; StepFixed()
    // moves it into the countdown and then into round 1.
    Match.Phase = EMatchPhase::NotStarted;
}

void FCombatSim::Init(const FMatchRules& InRules, const FFighterConfig& ConfigA,
                      const FFighterConfig& ConfigB, ETrainingMode TrainingModeA,
                      ETrainingMode TrainingModeB)
{
    Rules = InRules;
    if (!Rules.IsValid())
    {
        // Never simulate with nonsense rules; fall back to the documented
        // defaults rather than producing undefined match outcomes.
        Rules = FMatchRules();
    }

    Configs[0] = ConfigA;
    Configs[1] = ConfigB;
    Configs[0].Jab.Action = EAction::Jab;
    Configs[0].HeavyPunch.Action = EAction::HeavyPunch;
    Configs[1].Jab.Action = EAction::Jab;
    Configs[1].HeavyPunch.Action = EAction::HeavyPunch;

    // Sanity clamps so a bad data row cannot divide by zero or produce an
    // un-hittable fighter.
    for (int32_t i = 0; i < 2; ++i)
    {
        if (!(Configs[i].MaxHealth > 0.0f)) Configs[i].MaxHealth = 100.0f;
        if (!(Configs[i].MaxStamina > 0.0f)) Configs[i].MaxStamina = 100.0f;
        if (!(Configs[i].HalfWidth > 0.0f)) Configs[i].HalfWidth = 30.0f;
        if (!(Configs[i].Height > 0.0f)) Configs[i].Height = 200.0f;
        if (!(Configs[i].DamageTakenMultiplier > 0.0f)) Configs[i].DamageTakenMultiplier = 1.0f;
        // Repair only genuinely unusable data. The documented jab/heavy values
        // now arrive with the config itself (FAttackDef::MakeJab / MakeHeavyPunch),
        // so a default-constructed fighter is always complete and the heavy slot
        // can never silently inherit jab data.
        if (!Configs[i].Jab.IsValid()) Configs[i].Jab = FAttackDef::MakeJab();
        if (!Configs[i].HeavyPunch.IsValid()) Configs[i].HeavyPunch = FAttackDef::MakeHeavyPunch();
        Configs[i].Jab.Action = EAction::Jab;
        Configs[i].HeavyPunch.Action = EAction::HeavyPunch;
    }

    Training[0] = TrainingModeA;
    Training[1] = TrainingModeB;

    Match = FMatchState();
    TickAccumulator = 0.0f;
    CurrentRoundScored = 0;
    EventHead = 0;
    EventCount = 0;

    BeginCountdown();
}

void FCombatSim::Rematch()
{
    // A rematch reuses the exact same rules, fighters and training settings.
    const FMatchRules SavedRules = Rules;
    const FFighterConfig SavedA = Configs[0];
    const FFighterConfig SavedB = Configs[1];
    const ETrainingMode SavedTrainingA = Training[0];
    const ETrainingMode SavedTrainingB = Training[1];
    Init(SavedRules, SavedA, SavedB, SavedTrainingA, SavedTrainingB);
}

// ---------------------------------------------------------------------------
// Driving the simulation
// ---------------------------------------------------------------------------

void FCombatSim::Advance(float DeltaSeconds, const FInputState& InputA, const FInputState& InputB)
{
    if (!(DeltaSeconds > 0.0f))
    {
        return;
    }

    // A single frame can never be allowed to turn into an unbounded number of
    // simulation steps (editor hitching, debugger break, alt-tab, etc.).
    if (DeltaSeconds > MaxTickDeltaSeconds)
    {
        DeltaSeconds = MaxTickDeltaSeconds;
    }

    TickAccumulator += DeltaSeconds;

    int32_t Steps = 0;
    bool bFirstStep = true;
    while (TickAccumulator >= FixedTickSeconds && Steps < MaxTicksPerAdvance)
    {
        FInputState StepA = InputA;
        FInputState StepB = InputB;

        // Button presses are edge-triggered: only the first fixed step of this
        // frame may consume them. This is the anti-double-fire guarantee at
        // the input layer.
        if (!bFirstStep)
        {
            StepA.ClearEdges();
            StepB.ClearEdges();
        }

        StepFixed(StepA, StepB);

        TickAccumulator -= FixedTickSeconds;
        bFirstStep = false;
        ++Steps;
    }

    if (Steps >= MaxTicksPerAdvance)
    {
        // Drop the backlog instead of accumulating a permanent time debt.
        TickAccumulator = 0.0f;
    }
}

void FCombatSim::StepFixed(FInputState InputA, FInputState InputB)
{
    Match.TickCount++;

    // Training instrumentation. These change what the SIMULATION sees but not
    // the rules themselves; every training aid is explicitly opt-in per fighter.
    if (Training[SideIndex(ESide::A)] == ETrainingMode::NoOpponentInput)
    {
        InputB = FInputState();
    }
    if (Training[SideIndex(ESide::B)] == ETrainingMode::NoOpponentInput)
    {
        InputA = FInputState();
    }

    switch (Match.Phase)
    {
    case EMatchPhase::NotStarted:
        BeginCountdown();
        return;

    case EMatchPhase::Countdown:
    case EMatchPhase::RoundEnd:
    case EMatchPhase::MatchEnd:
        // No fighter acts during the countdown, the between-round break, or
        // after the final bell.
        TickPhase(FixedTickSeconds);
        return;

    case EMatchPhase::Fighting:
    default:
        break;
    }

    // 1. Fighter state machines and intent (may start attacks / dodges).
    TickFighter(ESide::A, InputA);
    TickFighter(ESide::B, InputB);

    // 2. Dodge motion and invulnerability windows.
    ResolveDodge(ESide::A);
    ResolveDodge(ESide::B);

    // 3. Hit resolution for both fighters, evaluated against the state as it
    //    was BEFORE any hit of this tick was applied, so simultaneous punches
    //    both register (a real boxing trade) instead of depending on order.
    ResolveHits();

    // 4. Physical constraints: anti-overlap, arena bounds, auto-facing.
    EnforceSeparationAndBounds();

    // 5. Round clock.
    if (Match.Phase == EMatchPhase::Fighting)
    {
        Match.RoundTimeRemaining -= FixedTickSeconds;
        if (Match.RoundTimeRemaining <= 0.0f)
        {
            Match.RoundTimeRemaining = 0.0f;
            EnterRoundEnd(ERoundEndReason::TimeExpired);
        }
    }
}

void FCombatSim::TickPhase(float UnusedDeltaFixed)
{
    (void)UnusedDeltaFixed; // phase timing is always advanced by one fixed tick

    switch (Match.Phase)
    {
    case EMatchPhase::NotStarted:
        BeginCountdown();
        break;

    case EMatchPhase::Countdown:
        Match.PhaseTimeRemaining -= FixedTickSeconds;
        if (Match.PhaseTimeRemaining <= 0.0f)
        {
            StartRound();
        }
        break;

    case EMatchPhase::RoundEnd:
        Match.PhaseTimeRemaining -= FixedTickSeconds;
        if (Match.PhaseTimeRemaining <= 0.0f)
        {
            if (Match.Round >= Rules.Rounds)
            {
                ScoreRoundsAndFinish();
            }
            else
            {
                Match.Round++;
                StartRound();
            }
        }
        break;

    case EMatchPhase::MatchEnd:
        Match.PhaseTimeRemaining -= FixedTickSeconds;
        if (Match.PhaseTimeRemaining < 0.0f)
        {
            Match.PhaseTimeRemaining = 0.0f;
        }
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// One fighter, one tick
// ---------------------------------------------------------------------------

void FCombatSim::TickFighter(ESide Side, FInputState& In)
{
    FFighterState& S = StateOf(Side);
    const FFighterConfig& C = ConfigOf(Side);

    // --- timers that run in every state -----------------------------------
    if (S.InvulnTicksRemaining > 0)
    {
        S.InvulnTicksRemaining--;
        S.bInvulnerable = (S.InvulnTicksRemaining > 0);
    }
    if (S.DodgeCooldownTicks > 0)
    {
        S.DodgeCooldownTicks--;
    }

    RegenerateStamina(Side, FixedTickSeconds);

    // --- attack phases own the fighter until they finish -------------------
    if (S.IsAttackPhase())
    {
        ResolveAttackPhases(Side);
        if (S.IsAttackPhase())
        {
            return; // still committed to the punch
        }
        // Otherwise the punch just finished and the fighter may act again on
        // this very tick (no phantom extra frame of lockout).
    }

    // --- dodge motion is owned by ResolveDodge() ---------------------------
    if (S.State == EFighterState::Dodging)
    {
        return;
    }

    if (S.State == EFighterState::Defeated)
    {
        S.CurrentAction = EAction::Idle;
        return;
    }

    if (S.State == EFighterState::Victory)
    {
        S.CurrentAction = EAction::Idle;
        S.StateTicksRemaining = 0;
        return;
    }

    // --- stun and knockdown -------------------------------------------------
    if (S.State == EFighterState::HitStun || S.State == EFighterState::KnockedDown)
    {
        if (S.StateTicksRemaining > 0)
        {
            S.StateTicksRemaining--;
        }

        if (S.StateTicksRemaining <= 0)
        {
            if (S.State == EFighterState::HitStun)
            {
                S.State = EFighterState::Idle;
                S.CurrentAction = EAction::Idle;
            }
            else
            {
                // Rising from the canvas grants a short invulnerability window
                // so the opponent cannot punch the fighter during the get-up.
                S.State = EFighterState::Idle;
                S.CurrentAction = EAction::Idle;
                S.GetUpsThisRound++;
                S.InvulnTicksRemaining = TicksFromSecondsMin1(C.GetUpInvulnSeconds);
                S.bInvulnerable = true;
                PushEvent(ECombatEvent::GetUp, Side);
            }
        }
        return; // stunned or down: no movement, no attacks
    }

    // --- block stun: guard is held for the stun duration --------------------
    if (S.State == EFighterState::Blocking && S.StateTicksRemaining > 0)
    {
        S.StateTicksRemaining--;
        return;
    }

    // -----------------------------------------------------------------------
    // From here the fighter is free to act: Idle, Moving or Blocking.
    // -----------------------------------------------------------------------

    // Guard up / guard down.
    if (In.bHoldBlock)
    {
        if (S.State != EFighterState::Blocking)
        {
            S.State = EFighterState::Blocking;
            S.CurrentAction = EAction::Block;
        }
    }
    else if (S.State == EFighterState::Blocking)
    {
        S.State = EFighterState::Idle;
        S.CurrentAction = EAction::Idle;
    }

    // Dodge (edge-triggered; a refused dodge still consumes the press so a
    // held button cannot queue a dodge for later).
    if (In.bDodgePressed)
    {
        In.bDodgePressed = false;
        if (S.DodgeCooldownTicks == 0 && HasStamina(Side, C.DodgeStaminaCost))
        {
            int32_t Dir = 0;
            if (In.bMoveRight && !In.bMoveLeft)      Dir = 1;
            else if (In.bMoveLeft && !In.bMoveRight) Dir = -1;
            else                                     Dir = -S.Facing; // default: slip backwards

            if (Dir == 0)
            {
                Dir = -S.Facing;
            }

            S.State = EFighterState::Dodging;
            S.CurrentAction = EAction::Dodge;
            S.StateTicksRemaining = TicksFromSecondsMin1(C.DodgeSeconds);
            S.DodgeDir = Dir;
            S.bInvulnerable = false;
            S.DodgesThisRound++;
            SpendStamina(Side, C.DodgeStaminaCost);
            PushEvent(ECombatEvent::DodgeStarted, Side, EAction::Dodge, static_cast<float>(Dir));
            return;
        }
    }

    // Punches (edge-triggered, stamina-gated).
    if (In.bJabPressed)
    {
        In.bJabPressed = false;
        if (HasStamina(Side, AttackDefOf(Side, EAction::Jab).StaminaCost))
        {
            StartAttack(Side, EAction::Jab);
            return;
        }
    }

    if (In.bHeavyPressed)
    {
        In.bHeavyPressed = false;
        if (HasStamina(Side, AttackDefOf(Side, EAction::HeavyPunch).StaminaCost))
        {
            StartAttack(Side, EAction::HeavyPunch);
            return;
        }
    }

    ApplyMovement(Side, In);
}

void FCombatSim::ApplyMovement(ESide Side, FInputState& In)
{
    FFighterState& S = StateOf(Side);
    const FFighterConfig& C = ConfigOf(Side);

    if (S.IsAttackPhase() || S.State == EFighterState::HitStun ||
        S.State == EFighterState::KnockedDown || S.State == EFighterState::Dodging ||
        S.State == EFighterState::Defeated || S.State == EFighterState::Victory)
    {
        return;
    }

    const int32_t Dir = (In.bMoveRight ? 1 : 0) - (In.bMoveLeft ? 1 : 0);
    const bool bBlocking = (S.State == EFighterState::Blocking);
    const float Scale = bBlocking ? BlockWalkSpeedScale : 1.0f;

    if (Dir != 0)
    {
        // Boxers advance faster than they retreat.
        const bool bAdvancing = ((Dir * S.Facing) > 0);
        const float Speed = bAdvancing ? C.WalkForwardSpeed : C.WalkBackwardSpeed;
        S.PositionX += static_cast<float>(Dir) * Speed * Scale * FixedTickSeconds;

        if (!bBlocking)
        {
            S.State = EFighterState::Moving;
        }
        S.CurrentAction = bBlocking ? EAction::Block : EAction::Walk;
    }
    else
    {
        if (S.State == EFighterState::Moving)
        {
            S.State = EFighterState::Idle;
        }
        S.CurrentAction = bBlocking ? EAction::Block : EAction::Idle;
    }
}

// ---------------------------------------------------------------------------
// Attacks
// ---------------------------------------------------------------------------

void FCombatSim::StartAttack(ESide Side, EAction Action)
{
    FFighterState& S = StateOf(Side);
    const FAttackDef& Def = AttackDefOf(Side, Action);

    int32_t Startup = 0, Active = 0, Recovery = 0;
    GetAttackWindow(Def, Startup, Active, Recovery);

    S.CurrentAction = Action;
    S.State = EFighterState::AttackStartup;
    S.bAttackHasLanded = false;
    S.AttackTick = 0;
    S.AttackTotalTicks = Startup + Active + Recovery;
    S.StateTicksRemaining = Startup;

    S.AttacksThrownThisRound++;
    S.AttacksThrownTotal++;
    SpendStamina(Side, Def.StaminaCost);

    PushEvent(ECombatEvent::AttackStarted, Side, Action, static_cast<float>(S.AttackTotalTicks));

    // Enter the attack timeline proper: attack tick 1 is the first startup tick.
    ResolveAttackPhases(Side);
}

void FCombatSim::ResolveAttackPhases(ESide Side)
{
    FFighterState& S = StateOf(Side);
    if (!S.IsAttackPhase())
    {
        return;
    }

    const FAttackDef& Def = AttackDefOf(Side, S.CurrentAction);
    int32_t Startup = 0, Active = 0, Recovery = 0;
    GetAttackWindow(Def, Startup, Active, Recovery);
    const int32_t Total = Startup + Active + Recovery;
    S.AttackTotalTicks = Total;

    // AttackTick is the number of ticks elapsed inside this attack, 1-based.
    S.AttackTick++;

    const int32_t T = S.AttackTick;
    if (T <= Startup)
    {
        S.State = EFighterState::AttackStartup;
        S.StateTicksRemaining = Startup - T;
    }
    else if (T <= Startup + Active)
    {
        S.State = EFighterState::AttackActive;
        S.StateTicksRemaining = (Startup + Active) - T;
    }
    else if (T <= Total)
    {
        S.State = EFighterState::AttackRecovery;
        S.StateTicksRemaining = Total - T;
    }
    else
    {
        // The attack is over. A punch that never connected is reported as a
        // whiff so the presentation layer can play the right animation.
        if (!S.bAttackHasLanded)
        {
            PushEvent(ECombatEvent::AttackWhiffed, Side, S.CurrentAction);
        }
        S.State = EFighterState::Idle;
        S.StateTicksRemaining = 0;
        S.CurrentAction = EAction::Idle;
        S.AttackTick = 0;
        S.AttackTotalTicks = 0;
        S.bAttackHasLanded = false;
    }
}

void FCombatSim::GetAttackWindow(const FAttackDef& Def, int32_t& OutStartupTicks,
                                int32_t& OutActiveTicks, int32_t& OutRecoveryTicks)
{
    OutStartupTicks = TicksFromSeconds(Def.StartupSeconds);
    OutActiveTicks = TicksFromSecondsMin1(Def.ActiveSeconds);
    OutRecoveryTicks = TicksFromSeconds(Def.RecoverySeconds);
}

bool FCombatSim::AttackOverlaps(ESide AttackerSide, const FAttackDef& Def, ESide DefenderSide) const
{
    const FFighterState& A = Fighters[SideIndex(AttackerSide)];
    const FFighterState& D = Fighters[SideIndex(DefenderSide)];
    const FFighterConfig& DC = Configs[SideIndex(DefenderSide)];

    // A dodging fighter inside its invulnerability window cannot be hit.
    if (D.bInvulnerable)
    {
        return false;
    }

    // A fighter already on the canvas or beaten cannot be punched again.
    if (D.State == EFighterState::KnockedDown || D.State == EFighterState::Defeated)
    {
        return false;
    }

    // Horizontal slab test along the attacker's facing direction. These are
    // explicit gameplay slabs (FAttackDef / FFighterConfig), never the PNG
    // silhouettes of the sprite frames.
    const float Delta = (D.PositionX - A.PositionX) * static_cast<float>(A.Facing);
    if (Delta < Def.ReachNear || Delta > Def.ReachFar)
    {
        return false;
    }

    // Vertical band test: attacker's hitbox band vs the defender's hurtbox.
    if (Def.HitboxLowZ >= DC.Height || Def.HitboxHighZ <= 0.0f)
    {
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Dodges
// ---------------------------------------------------------------------------

void FCombatSim::ResolveDodge(ESide Side)
{
    FFighterState& S = StateOf(Side);
    if (S.State != EFighterState::Dodging)
    {
        return;
    }

    const FFighterConfig& C = ConfigOf(Side);

    const int32_t Startup   = TicksFromSeconds(C.DodgeStartupSeconds);
    const int32_t InvulnEnd = TicksFromSeconds(C.DodgeInvulnEndSeconds);
    const int32_t Total     = TicksFromSecondsMin1(C.DodgeSeconds);

    // Elapsed is 0 on the tick the dodge starts.
    const int32_t Elapsed = Total - S.StateTicksRemaining;

    // Invulnerability window: after the dodge startup, up to InvulnEnd.
    S.bInvulnerable = (Elapsed >= Startup) && (Elapsed < InvulnEnd);
    S.InvulnTicksRemaining = S.bInvulnerable ? (InvulnEnd - Elapsed) : 0;

    // The slip shift happens after startup, spread evenly over the remaining
    // ticks so the total distance travelled is exactly DodgeDistance.
    const int32_t MotionTicks = Total - Startup;
    if (Elapsed >= Startup && MotionTicks > 0)
    {
        const float Step = C.DodgeDistance / static_cast<float>(MotionTicks);
        S.PositionX += static_cast<float>(S.DodgeDir) * Step;
    }

    if (S.StateTicksRemaining > 0)
    {
        S.StateTicksRemaining--;
    }

    if (S.StateTicksRemaining <= 0)
    {
        S.State = EFighterState::Idle;
        S.CurrentAction = EAction::Idle;
        S.bInvulnerable = false;
        S.InvulnTicksRemaining = 0;
        S.DodgeDir = 0;
        S.DodgeCooldownTicks = TicksFromSecondsMin1(C.DodgeCooldownSeconds);
    }
}

// ---------------------------------------------------------------------------
// Hit resolution
// ---------------------------------------------------------------------------

void FCombatSim::ResolveHits()
{
    struct FPendingHit
    {
        bool bLand = false;
        ESide Attacker = ESide::None;
        ESide Defender = ESide::None;
        EAction Action = EAction::Idle;
        bool bCounter = false;
        float DefenderStaminaBefore = 0.0f;
    };

    FPendingHit Pending[2];
    const ESide Sides[2] = { ESide::A, ESide::B };

    // Pass 1: decide what would land, reading state as it was BEFORE any hit of
    // this tick is applied. This makes simultaneous punches a real trade.
    for (int32_t i = 0; i < 2; ++i)
    {
        const ESide Attacker = Sides[i];
        const ESide Defender = Opponent(Attacker);
        const FFighterState& AS = Fighters[SideIndex(Attacker)];

        if (AS.State != EFighterState::AttackActive)
        {
            continue;
        }
        if (AS.bAttackHasLanded)
        {
            // One punch can deal damage once. This is the guard that makes
            // duplicate result processing impossible.
            continue;
        }

        const FAttackDef& Def = AttackDefOf(Attacker, AS.CurrentAction);
        if (!AttackOverlaps(Attacker, Def, Defender))
        {
            continue;
        }

        const FFighterState& DS = Fighters[SideIndex(Defender)];

        Pending[i].bLand = true;
        Pending[i].Attacker = Attacker;
        Pending[i].Defender = Defender;
        Pending[i].Action = AS.CurrentAction;
        Pending[i].DefenderStaminaBefore = DS.Stamina;
        // A "clean counter" is a punch that lands while the defender is in the
        // startup or active phase of their own punch.
        Pending[i].bCounter = (DS.State == EFighterState::AttackStartup ||
                               DS.State == EFighterState::AttackActive);
    }

    // Pass 2: apply in a fixed order (A then B) so the result is deterministic.
    bool bKnockedDownA = false;
    bool bKnockedDownB = false;

    for (int32_t i = 0; i < 2; ++i)
    {
        if (!Pending[i].bLand)
        {
            continue;
        }
        if (Match.Phase != EMatchPhase::Fighting)
        {
            // The fight already ended on an earlier hit of this same tick.
            break;
        }

        const ESide Attacker = Pending[i].Attacker;
        const ESide Defender = Pending[i].Defender;
        const FAttackDef& Def = AttackDefOf(Attacker, Pending[i].Action);

        const bool bBlocked = (Fighters[SideIndex(Defender)].State == EFighterState::Blocking);
        if (bBlocked)
        {
            ApplyBlockedHit(Attacker, Defender, Def);
        }
        else
        {
            ApplyHit(Attacker, Defender, Def, Pending[i].bCounter, Pending[i].DefenderStaminaBefore);
        }
    }

    // Double knockdown: both fighters on the canvas in the same tick ends the
    // round immediately (unless the match already ended above).
    if (Match.Phase == EMatchPhase::Fighting)
    {
        bKnockedDownA = (Fighters[0].State == EFighterState::KnockedDown);
        bKnockedDownB = (Fighters[1].State == EFighterState::KnockedDown);
        if (bKnockedDownA && bKnockedDownB)
        {
            EnterRoundEnd(ERoundEndReason::DoubleKnockdown);
        }
    }
}

void FCombatSim::ApplyHit(ESide Attacker, ESide Defender, const FAttackDef& Def,
                          bool bCounter, float DefenderStaminaBefore)
{
    FFighterState& AS = StateOf(Attacker);
    FFighterState& DS = StateOf(Defender);
    const FFighterConfig& DC = ConfigOf(Defender);

    float Damage = Def.Damage;
    if (bCounter)
    {
        Damage *= Def.CounterDamageMultiplier;
    }
    Damage *= DC.DamageTakenMultiplier;
    if (Training[SideIndex(Defender)] == ETrainingMode::InfiniteHealth)
    {
        Damage = 0.0f;
    }

    if (Damage > 0.0f)
    {
        DS.Health -= Damage;
        if (DS.Health < 0.0f)
        {
            DS.Health = 0.0f;
        }
    }

    // Scorecards / statistics.
    AS.DamageDealtThisRound += Damage;
    AS.DamageDealtTotal += Damage;
    DS.DamageTakenThisRound += Damage;
    DS.DamageTakenTotal += Damage;
    AS.HitsLandedThisRound++;
    AS.HitsLandedTotal++;
    DS.HitsTakenThisRound++;
    DS.HitsTakenTotal++;

    // The punch is now spent.
    AS.bAttackHasLanded = true;

    PushEvent(ECombatEvent::AttackHit, Attacker, Def.Action, Damage);

    // Taking a clean hit always interrupts whatever the defender was doing.
    DS.StateTicksRemaining = 0;
    DS.AttackTick = 0;
    DS.AttackTotalTicks = 0;
    DS.bAttackHasLanded = false;
    DS.CurrentAction = EAction::Idle;

    // Knockdown is decided by an explicit numeric rule, never by chance.
    bool bKnockdown = false;
    if (Def.bCanKnockdown)
    {
        const float Threshold = bCounter ? Rules.CounterKnockdownStaminaThreshold
                                         : Rules.KnockdownStaminaThreshold;
        if (DefenderStaminaBefore <= Threshold)
        {
            bKnockdown = true;
        }
    }

    const ESide Winner = (Attacker == ESide::A) ? ESide::A : ESide::B;
    const EMatchResult WinnerResult = (Attacker == ESide::A) ? EMatchResult::FighterAWins
                                                             : EMatchResult::FighterBWins;

    if (DS.Health <= 0.0f)
    {
        // Knockout - the contest is over immediately, exactly as in boxing.
        DS.Health = 0.0f;
        DS.State = EFighterState::KnockedDown;
        DS.StateTicksRemaining = TicksFromSecondsMin1(DC.KnockdownSeconds);
        DS.bInvulnerable = false;
        DS.InvulnTicksRemaining = 0;
        PushEvent(ECombatEvent::Knockout, Defender, Def.Action);
        AwardRoundTo(Winner);
        Match.LastRoundEndReason = ERoundEndReason::Knockout;
        EnterMatchEnd(EMatchEndReason::Knockout, WinnerResult);
        return;
    }

    DS.PositionX += static_cast<float>(AS.Facing) * Def.Knockback;

    if (bKnockdown)
    {
        ApplyKnockdown(Defender);

        if (DS.KnockdownsThisRound >= Rules.MaxKnockdownsPerRound)
        {
            // Three-knockdown rule: the round (and the fight) is stopped.
            PushEvent(ECombatEvent::Knockout, Defender, Def.Action);
            AwardRoundTo(Winner);
            Match.LastRoundEndReason = ERoundEndReason::KnockdownLimit;
            EnterMatchEnd(EMatchEndReason::TechnicalKnockout, WinnerResult);
        }
        return;
    }

    DS.State = EFighterState::HitStun;
    DS.StateTicksRemaining = TicksFromSecondsMin1(Def.HitStunSeconds);
}

void FCombatSim::ApplyBlockedHit(ESide Attacker, ESide Defender, const FAttackDef& Def)
{
    FFighterState& AS = StateOf(Attacker);
    FFighterState& DS = StateOf(Defender);
    const FFighterConfig& DC = ConfigOf(Defender);

    const int32_t DefIdx = SideIndex(Defender);

    float Chip = Def.Damage * Def.ChipDamageRatio * DC.DamageTakenMultiplier;
    if (Training[DefIdx] == ETrainingMode::InfiniteHealth)
    {
        Chip = 0.0f;
    }

    if (Chip > 0.0f)
    {
        DS.Health -= Chip;
        if (DS.Health < 0.0f)
        {
            DS.Health = 0.0f;
        }
        AS.DamageDealtThisRound += Chip;
        AS.DamageDealtTotal += Chip;
        DS.DamageTakenThisRound += Chip;
        DS.DamageTakenTotal += Chip;
    }

    // A blocked punch is NOT a landed punch: HitsLanded deliberately does not
    // increment here, so scorecards cannot be inflated by chip damage.
    AS.bAttackHasLanded = true;
    DS.BlocksThisRound++;

    const bool bInfiniteStamina = (Training[DefIdx] == ETrainingMode::InfiniteStamina);
    if (!bInfiniteStamina)
    {
        DS.Stamina -= Def.BlockStaminaDamage;
        if (DS.Stamina < 0.0f)
        {
            DS.Stamina = 0.0f;
        }
    }

    DS.PositionX += static_cast<float>(AS.Facing) * Def.Knockback * BlockedKnockbackScale;

    // Guard break: blocking a punch with the tank empty blows the guard open
    // and leaves the defender in a long stun instead of ordinary block stun.
    const bool bGuardBroken = !bInfiniteStamina && (DS.Stamina <= Rules.GuardBreakStaminaThreshold);

    if (bGuardBroken)
    {
        DS.State = EFighterState::HitStun;
        DS.StateTicksRemaining = TicksFromSecondsMin1(Rules.GuardBreakStunSeconds);
        DS.CurrentAction = EAction::Idle;
        PushEvent(ECombatEvent::GuardBroken, Defender, Def.Action);
    }
    else
    {
        DS.State = EFighterState::Blocking;
        DS.StateTicksRemaining = TicksFromSecondsMin1(Def.BlockStunSeconds);
        PushEvent(ECombatEvent::AttackBlocked, Attacker, Def.Action, Chip);
    }

    if (DS.Health <= 0.0f)
    {
        // Extremely rare, but chip damage can finish a fight.
        DS.Health = 0.0f;
        DS.State = EFighterState::KnockedDown;
        DS.StateTicksRemaining = TicksFromSecondsMin1(DC.KnockdownSeconds);
        DS.bInvulnerable = false;
        DS.InvulnTicksRemaining = 0;
        PushEvent(ECombatEvent::Knockout, Defender, Def.Action);
        AwardRoundTo(Attacker);
        Match.LastRoundEndReason = ERoundEndReason::Knockout;
        EnterMatchEnd(EMatchEndReason::Knockout,
                      (Attacker == ESide::A) ? EMatchResult::FighterAWins
                                             : EMatchResult::FighterBWins);
    }
}

void FCombatSim::ApplyKnockdown(ESide Side)
{
    FFighterState& S = StateOf(Side);
    const FFighterConfig& C = ConfigOf(Side);

    S.State = EFighterState::KnockedDown;
    S.StateTicksRemaining = TicksFromSecondsMin1(C.KnockdownSeconds);
    S.CurrentAction = EAction::Idle;
    S.AttackTick = 0;
    S.AttackTotalTicks = 0;
    S.bAttackHasLanded = false;
    S.bInvulnerable = false;
    S.InvulnTicksRemaining = 0;
    S.DodgeDir = 0;

    S.KnockdownsThisRound++;
    S.KnockdownsTotal++;

    // Going down buys a breather: some health and stamina come back on the
    // canvas, which is a deliberate design decision (no free full heal).
    if (Training[SideIndex(Side)] != ETrainingMode::InfiniteHealth)
    {
        S.Health = ClampFloat(S.Health + (Rules.KnockdownHealthRestoreFraction * C.MaxHealth),
                              0.0f, C.MaxHealth);
    }
    if (Training[SideIndex(Side)] != ETrainingMode::InfiniteStamina)
    {
        S.Stamina = ClampFloat(S.Stamina + Rules.KnockdownStaminaRestore, 0.0f, C.MaxStamina);
    }

    PushEvent(ECombatEvent::Knockdown, Side);
}

// ---------------------------------------------------------------------------
// Physical constraints
// ---------------------------------------------------------------------------

void FCombatSim::EnforceSeparationAndBounds()
{
    FFighterState& A = Fighters[0];
    FFighterState& B = Fighters[1];

    A.PositionX = ClampFloat(A.PositionX, Rules.ArenaMinX, Rules.ArenaMaxX);
    B.PositionX = ClampFloat(B.PositionX, Rules.ArenaMinX, Rules.ArenaMaxX);

    // Anti-overlap: keep the boxers at least Rules.MinSeparation apart. The
    // correction is split symmetrically so the rule cannot be gamed by attack
    // order, but a fighter already pinned against an arena bound cannot absorb
    // its half of the push. The leftover is therefore given to the fighter that
    // still has room, otherwise the separation rule would silently fail (by one
    // tick of walk distance) whenever a fight is driven onto the ropes.
    const float Delta = B.PositionX - A.PositionX;
    const float AbsDelta = (Delta < 0.0f) ? -Delta : Delta;
    const float Shortfall = Rules.MinSeparation - AbsDelta;
    if (Shortfall > 0.0f)
    {
        const float Sign = (Delta >= 0.0f) ? 1.0f : -1.0f;

        // Room each fighter has in the direction the push would move it.
        const float RoomA = (Sign > 0.0f) ? (A.PositionX - Rules.ArenaMinX)
                                          : (Rules.ArenaMaxX - A.PositionX);
        const float RoomB = (Sign > 0.0f) ? (Rules.ArenaMaxX - B.PositionX)
                                          : (B.PositionX - Rules.ArenaMinX);

        const float Half = Shortfall * 0.5f;
        float MoveA = (RoomA < Half) ? RoomA : Half;
        float MoveB = (RoomB < Half) ? RoomB : Half;

        // Reallocate anything the bounds made one fighter unable to absorb.
        float Remaining = Shortfall - MoveA - MoveB;
        if (Remaining > 0.0f)
        {
            const float SpareA = RoomA - MoveA;
            const float GiveA = (SpareA < Remaining) ? SpareA : Remaining;
            MoveA += GiveA;
            Remaining -= GiveA;
        }
        if (Remaining > 0.0f)
        {
            const float SpareB = RoomB - MoveB;
            const float GiveB = (SpareB < Remaining) ? SpareB : Remaining;
            MoveB += GiveB;
        }

        A.PositionX -= Sign * MoveA;
        B.PositionX += Sign * MoveB;

        A.PositionX = ClampFloat(A.PositionX, Rules.ArenaMinX, Rules.ArenaMaxX);
        B.PositionX = ClampFloat(B.PositionX, Rules.ArenaMinX, Rules.ArenaMaxX);
    }

    // Boxers always square up: facing is derived from position, never input.
    A.Facing = (B.PositionX >= A.PositionX) ? 1 : -1;
    B.Facing = (A.PositionX >= B.PositionX) ? 1 : -1;
}

// ---------------------------------------------------------------------------
// Judging, scoring, round and match flow
// ---------------------------------------------------------------------------

ESide FCombatSim::JudgeRound() const
{
    const FFighterState& A = Fighters[0];
    const FFighterState& B = Fighters[1];

    // 1. Damage dealt.
    if (A.DamageDealtThisRound > B.DamageDealtThisRound + JudgeEpsilon) return ESide::A;
    if (B.DamageDealtThisRound > A.DamageDealtThisRound + JudgeEpsilon) return ESide::B;

    // 2. Punches landed.
    if (A.HitsLandedThisRound > B.HitsLandedThisRound) return ESide::A;
    if (B.HitsLandedThisRound > A.HitsLandedThisRound) return ESide::B;

    // 3. Stamina remaining (the fresher fighter wins a tie).
    if (A.Stamina > B.Stamina + JudgeEpsilon) return ESide::A;
    if (B.Stamina > A.Stamina + JudgeEpsilon) return ESide::B;

    return ESide::Both; // genuine 10-10 round
}

void FCombatSim::AwardRoundTo(ESide Side)
{
    // DUPLICATE-RESULT GUARD: a round index can be scored exactly once, so no
    // reward can ever be awarded twice for the same round.
    if (Match.Round <= CurrentRoundScored)
    {
        return;
    }

    Match.LastRoundWinner = Side;

    if (Side == ESide::A)
    {
        Match.ScoreA += Rules.JudgeWinScore;
        Match.ScoreB += Rules.JudgeLoseScore;
    }
    else if (Side == ESide::B)
    {
        Match.ScoreB += Rules.JudgeWinScore;
        Match.ScoreA += Rules.JudgeLoseScore;
    }
    else
    {
        Match.ScoreA += Rules.JudgeDrawScore;
        Match.ScoreB += Rules.JudgeDrawScore;
    }

    CurrentRoundScored = Match.Round;
}

void FCombatSim::EnterRoundEnd(ERoundEndReason Reason)
{
    // GUARD: only a fight in progress can produce a round result, and the round
    // index must not already have been scored.
    if (Match.Phase != EMatchPhase::Fighting)
    {
        return;
    }
    if (Match.Round <= CurrentRoundScored)
    {
        return;
    }

    Match.Phase = EMatchPhase::RoundEnd;
    Match.PhaseTimeRemaining = Rules.RoundBreakSeconds;
    Match.LastRoundEndReason = Reason;

    // The bell freezes both fighters.
    for (int32_t i = 0; i < 2; ++i)
    {
        FFighterState& S = Fighters[i];
        if (S.IsAttackPhase())
        {
            S.State = EFighterState::Idle;
        }
        S.CurrentAction = EAction::Idle;
        S.StateTicksRemaining = 0;
        S.AttackTick = 0;
        S.AttackTotalTicks = 0;
        S.bAttackHasLanded = false;
        S.bInvulnerable = false;
        S.InvulnTicksRemaining = 0;
        S.DodgeDir = 0;
        if (S.State != EFighterState::KnockedDown)
        {
            S.State = EFighterState::Idle;
        }
    }

    const ESide Winner = JudgeRound();
    AwardRoundTo(Winner);
    PushEvent(ECombatEvent::RoundEnded, Winner, EAction::Idle, static_cast<float>(Match.Round));
}

void FCombatSim::EnterMatchEnd(EMatchEndReason Reason, EMatchResult Result)
{
    // GUARD: the match result is awarded exactly once.
    if (Match.Phase == EMatchPhase::MatchEnd)
    {
        return;
    }

    Match.Phase = EMatchPhase::MatchEnd;
    Match.PhaseTimeRemaining = Rules.PostMatchSeconds;
    Match.EndReason = Reason;
    Match.Result = Result;

    for (int32_t i = 0; i < 2; ++i)
    {
        FFighterState& S = Fighters[i];
        const ESide Side = (i == 0) ? ESide::A : ESide::B;

        if (S.IsAttackPhase())
        {
            S.State = EFighterState::Idle;
        }
        S.CurrentAction = EAction::Idle;
        S.StateTicksRemaining = 0;
        S.AttackTick = 0;
        S.AttackTotalTicks = 0;
        S.bAttackHasLanded = false;
        S.bInvulnerable = false;
        S.InvulnTicksRemaining = 0;
        S.DodgeDir = 0;

        if (Result == EMatchResult::Draw)
        {
            if (S.State != EFighterState::KnockedDown)
            {
                S.State = EFighterState::Idle;
            }
        }
        else if (IsWinnerSide(Result, Side))
        {
            if (S.State != EFighterState::KnockedDown)
            {
                S.State = EFighterState::Victory;
            }
        }
        else
        {
            // A fighter who was counted out stays on the canvas; everyone else
            // is simply beaten.
            if (S.Health <= 0.0f || S.State == EFighterState::KnockedDown)
            {
                S.State = EFighterState::KnockedDown;
            }
            else
            {
                S.State = EFighterState::Defeated;
            }
        }
    }

    ESide WinnerSide = ESide::None;
    if (Result == EMatchResult::FighterAWins) WinnerSide = ESide::A;
    else if (Result == EMatchResult::FighterBWins) WinnerSide = ESide::B;

    PushEvent(ECombatEvent::MatchEnded, WinnerSide, EAction::Idle, static_cast<float>(Result));
}

void FCombatSim::ScoreRoundsAndFinish()
{
    if (Match.ScoreA > Match.ScoreB)
    {
        EnterMatchEnd(EMatchEndReason::Decision, EMatchResult::FighterAWins);
    }
    else if (Match.ScoreB > Match.ScoreA)
    {
        EnterMatchEnd(EMatchEndReason::Decision, EMatchResult::FighterBWins);
    }
    else
    {
        EnterMatchEnd(EMatchEndReason::DrawDecision, EMatchResult::Draw);
    }
}

void FCombatSim::BeginCountdown()
{
    Match.Phase = EMatchPhase::Countdown;
    Match.Round = 1;
    Match.ScoreA = 0;
    Match.ScoreB = 0;
    Match.Result = EMatchResult::Undecided;
    Match.EndReason = EMatchEndReason::None;
    Match.LastRoundEndReason = ERoundEndReason::None;
    Match.LastRoundWinner = ESide::None;
    Match.PhaseTimeRemaining = Rules.CountdownSeconds;
    Match.RoundTimeRemaining = Rules.RoundSeconds;

    CurrentRoundScored = 0;

    ResetFighter(ESide::A);
    ResetFighter(ESide::B);
    ResetRoundStatistics();
}

void FCombatSim::StartRound()
{
    Match.Phase = EMatchPhase::Fighting;
    Match.PhaseTimeRemaining = 0.0f;
    Match.RoundTimeRemaining = Rules.RoundSeconds;
    Match.LastRoundEndReason = ERoundEndReason::None;
    Match.LastRoundWinner = ESide::None;

    for (int32_t i = 0; i < 2; ++i)
    {
        FFighterState& S = Fighters[i];
        const FFighterConfig& C = Configs[i];
        const ESide Side = (i == 0) ? ESide::A : ESide::B;

        if (Match.Round > 1)
        {
            // Between rounds the corners restore a fraction of health and
            // stamina. Nothing is restored for free on a knockout, because a
            // knockout ends the match outright.
            if (Training[i] != ETrainingMode::InfiniteHealth)
            {
                S.Health = ClampFloat(
                    S.Health + (Rules.BetweenRoundHealthRestoreFraction * C.MaxHealth),
                    0.0f, C.MaxHealth);
            }
            if (Training[i] != ETrainingMode::InfiniteStamina)
            {
                S.Stamina = ClampFloat(
                    S.Stamina + (Rules.BetweenRoundStaminaRestoreFraction * C.MaxStamina),
                    0.0f, C.MaxStamina);
            }
        }

        // Reset the fighter for the new round.
        S.PositionX = (i == 0) ? (Rules.ArenaMinX + 120.0f) : (Rules.ArenaMaxX - 120.0f);
        S.Facing = (i == 0) ? 1 : -1;
        S.State = EFighterState::Idle;
        S.CurrentAction = EAction::Idle;
        S.StateTicksRemaining = 0;
        S.AttackTick = 0;
        S.AttackTotalTicks = 0;
        S.bAttackHasLanded = false;
        S.bInvulnerable = false;
        S.InvulnTicksRemaining = 0;
        S.DodgeCooldownTicks = 0;
        S.DodgeDir = 0;

        (void)Side;
    }

    ResetRoundStatistics();
    PushEvent(ECombatEvent::RoundStarted, ESide::None, EAction::Idle, static_cast<float>(Match.Round));
}

void FCombatSim::ResetFighter(ESide Side)
{
    FFighterState& S = StateOf(Side);
    const FFighterConfig& C = ConfigOf(Side);
    const bool bFirst = (Side != ESide::B);

    S.PositionX = bFirst ? (Rules.ArenaMinX + 120.0f) : (Rules.ArenaMaxX - 120.0f);
    S.Facing = bFirst ? 1 : -1;

    S.Health = C.MaxHealth;
    S.Stamina = C.MaxStamina;

    S.State = EFighterState::Idle;
    S.CurrentAction = EAction::Idle;
    S.StateTicksRemaining = 0;
    S.AttackTick = 0;
    S.bAttackHasLanded = false;
    S.AttackTotalTicks = 0;

    S.bInvulnerable = false;
    S.InvulnTicksRemaining = 0;
    S.DodgeCooldownTicks = 0;
    S.DodgeDir = 0;

    S.KnockdownsThisRound = 0;
    S.KnockdownsTotal = 0;
    S.GetUpsThisRound = 0;

    S.DamageDealtThisRound = 0.0f;
    S.DamageTakenThisRound = 0.0f;
    S.HitsLandedThisRound = 0;
    S.HitsTakenThisRound = 0;
    S.BlocksThisRound = 0;
    S.DodgesThisRound = 0;
    S.AttacksThrownThisRound = 0;
    S.StaminaSpentThisRound = 0.0f;

    S.DamageDealtTotal = 0.0f;
    S.DamageTakenTotal = 0.0f;
    S.HitsLandedTotal = 0;
    S.HitsTakenTotal = 0;
    S.AttacksThrownTotal = 0;
}

void FCombatSim::ResetRoundStatistics()
{
    for (int32_t i = 0; i < 2; ++i)
    {
        FFighterState& S = Fighters[i];
        S.KnockdownsThisRound = 0;
        S.GetUpsThisRound = 0;
        S.DamageDealtThisRound = 0.0f;
        S.DamageTakenThisRound = 0.0f;
        S.HitsLandedThisRound = 0;
        S.HitsTakenThisRound = 0;
        S.BlocksThisRound = 0;
        S.DodgesThisRound = 0;
        S.AttacksThrownThisRound = 0;
        S.StaminaSpentThisRound = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Stamina
// ---------------------------------------------------------------------------

bool FCombatSim::HasStamina(ESide Side, float Cost) const
{
    if (Training[SideIndex(Side)] == ETrainingMode::InfiniteStamina)
    {
        return true;
    }
    return Fighters[SideIndex(Side)].Stamina >= Cost;
}

void FCombatSim::SpendStamina(ESide Side, float Amount)
{
    FFighterState& S = StateOf(Side);
    const FFighterConfig& C = ConfigOf(Side);

    if (Amount <= 0.0f)
    {
        return;
    }

    if (Training[SideIndex(Side)] == ETrainingMode::InfiniteStamina)
    {
        S.Stamina = C.MaxStamina;
        return;
    }

    S.Stamina -= Amount;
    S.StaminaSpentThisRound += Amount;
    if (S.Stamina <= 0.0f)
    {
        S.Stamina = 0.0f;
        PushEvent(ECombatEvent::StaminaExhausted, Side);
    }
}

void FCombatSim::RegenerateStamina(ESide Side, float DeltaSeconds)
{
    const int32_t Idx = SideIndex(Side);
    if (Training[Idx] == ETrainingMode::InfiniteStamina)
    {
        StateOf(Side).Stamina = ConfigOf(Side).MaxStamina;
        return;
    }

    FFighterState& S = Fighters[Idx];
    const FFighterConfig& C = Configs[Idx];

    if (S.Stamina >= C.MaxStamina)
    {
        S.Stamina = C.MaxStamina;
        return;
    }

    float Rate = C.StaminaRegenPerSecond;
    if (S.State == EFighterState::Blocking)
    {
        // Holding a guard is work: regeneration is nearly cancelled out by the
        // drain of keeping the hands up.
        Rate = C.BlockStaminaRegenPerSecond - C.BlockHoldDrainPerSecond;
    }
    else if (S.IsAttackPhase())
    {
        Rate = C.StaminaRegenPerSecond * RegenScaleAttacking;
    }
    else if (S.State == EFighterState::Dodging)
    {
        Rate = C.StaminaRegenPerSecond * RegenScaleDodging;
    }
    else if (S.IsStunned())
    {
        Rate = C.StaminaRegenPerSecond * RegenScaleStunned;
    }

    if (Rate <= 0.0f)
    {
        return;
    }

    S.Stamina = ClampFloat(S.Stamina + (Rate * DeltaSeconds), 0.0f, C.MaxStamina);
}

// ---------------------------------------------------------------------------
// Events and accessors
// ---------------------------------------------------------------------------

void FCombatSim::PushEvent(ECombatEvent Type, ESide Side, EAction Action, float Value)
{
    // Bounded ring buffer: no allocation, and a presentation layer that stops
    // draining can never grow the simulator's memory footprint.
    if (EventCount >= MaxEventQueue)
    {
        return;
    }

    const int32_t Index = (EventHead + EventCount) % MaxEventQueue;
    Events[Index].Type = Type;
    Events[Index].Side = Side;
    Events[Index].Action = Action;
    Events[Index].Value = Value;
    Events[Index].Tick = Match.TickCount;
    EventCount++;
}

bool FCombatSim::PopEvent(FCombatEventRecord& OutEvent)
{
    if (EventCount <= 0)
    {
        return false;
    }

    OutEvent = Events[EventHead];
    EventHead = (EventHead + 1) % MaxEventQueue;
    EventCount--;
    return true;
}

const FFighterState& FCombatSim::GetFighter(ESide Side) const
{
    return Fighters[SideIndex(Side)];
}

const FFighterConfig& FCombatSim::GetConfig(ESide Side) const
{
    return Configs[SideIndex(Side)];
}

ETrainingMode FCombatSim::GetTrainingMode(ESide Side) const
{
    return Training[SideIndex(Side)];
}

float FCombatSim::GetRoundTimeFraction() const
{
    if (!(Rules.RoundSeconds > 0.0f))
    {
        return 0.0f;
    }
    return ClampFloat(Match.RoundTimeRemaining / Rules.RoundSeconds, 0.0f, 1.0f);
}

float FCombatSim::GetTotalDamageDealt(ESide Side) const
{
    return Fighters[SideIndex(Side)].DamageDealtTotal;
}

ESide FCombatSim::Opponent(ESide Side) const
{
    if (Side == ESide::A) return ESide::B;
    if (Side == ESide::B) return ESide::A;
    return ESide::None;
}

FFighterState& FCombatSim::StateOf(ESide Side)
{
    return Fighters[SideIndex(Side)];
}

const FFighterState& FCombatSim::StateOf(ESide Side) const
{
    return Fighters[SideIndex(Side)];
}

FFighterConfig& FCombatSim::ConfigOf(ESide Side)
{
    return Configs[SideIndex(Side)];
}

const FAttackDef& FCombatSim::AttackDefOf(ESide Side, EAction Action) const
{
    const FFighterConfig& C = Configs[SideIndex(Side)];
    if (Action == EAction::HeavyPunch)
    {
        return C.HeavyPunch;
    }
    // Jab is the default definition for any non-heavy action; the simulation
    // never asks for the frame data of Block / Dodge / Idle.
    return C.Jab;
}

} // namespace RKC
