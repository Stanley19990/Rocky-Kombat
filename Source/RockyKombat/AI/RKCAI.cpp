// RKCAI.cpp
//
// Offline opponent AI. See RKCAI.h for the design rationale.
//
// Everything in this file is pure C++: no engine headers, no engine RNG, no
// engine time. That is what lets the standalone harness play thousands of
// complete matches in a few seconds and assert on the results.

#include "AI/RKCAI.h"

#include <cmath>
#include <cstdio>

namespace RKC
{
namespace
{
/** Probability helpers. Kept file-local so no other translation unit can
 *  accidentally depend on the AI's tuning constants. */
float Clamp01(float V)
{
    return V < 0.0f ? 0.0f : (V > 1.0f ? 1.0f : V);
}

float ClampF(float V, float Lo, float Hi)
{
    return V < Lo ? Lo : (V > Hi ? Hi : V);
}

/** States in which a fighter accepts a new action. Mirrors the early-out in
 *  FCombatSim::TickFighter: an attack, hit stun, a knockdown or an in-progress
 *  dodge all lock the fighter out of new input. */
bool IsFreeToAct(EFighterState State)
{
    return State == EFighterState::Idle || State == EFighterState::Moving ||
           State == EFighterState::Blocking;
}

const char* const PlanNames[static_cast<int32_t>(EBotPlan::Count)] = {
    "Idle", "Approach", "Strike", "Hold", "Retreat", "Defend", "Slip"
};

const char* const ArchetypeNames[static_cast<int32_t>(EBotArchetype::Count)] = {
    "Balanced", "Pressure", "Counter", "Slugger"
};

const char* const DifficultyNames[static_cast<int32_t>(EBotDifficulty::Count)] = {
    "Rookie", "Contender", "Champion"
};
} // namespace

const char* GetPlanName(EBotPlan Plan)
{
    const int32_t Index = static_cast<int32_t>(Plan);
    if (Index < 0 || Index >= static_cast<int32_t>(EBotPlan::Count))
    {
        return "?";
    }
    return PlanNames[Index];
}

const char* GetArchetypeName(EBotArchetype Archetype)
{
    const int32_t Index = static_cast<int32_t>(Archetype);
    if (Index < 0 || Index >= static_cast<int32_t>(EBotArchetype::Count))
    {
        return "?";
    }
    return ArchetypeNames[Index];
}

const char* GetDifficultyName(EBotDifficulty Difficulty)
{
    const int32_t Index = static_cast<int32_t>(Difficulty);
    if (Index < 0 || Index >= static_cast<int32_t>(EBotDifficulty::Count))
    {
        return "?";
    }
    return DifficultyNames[Index];
}

// ---------------------------------------------------------------------------
// FBotProfile
// ---------------------------------------------------------------------------

FBotProfile FBotProfile::Make(EBotArchetype Archetype, EBotDifficulty Difficulty)
{
    FBotProfile P;
    P.Archetype = Archetype;
    P.Difficulty = Difficulty;

    // Difficulty sets reflexes and discipline. Note that ReactionSeconds is
    // compared against a real attack's startup, so these numbers have measurable
    // frame-data consequences:
    //   jab   startup 0.100 s  -> only a sub-0.10 s reflex could block it
    //   heavy startup 0.217 s  -> a Contender (0.190 s) can block it
    switch (Difficulty)
    {
    case EBotDifficulty::Rookie:
        P.ReactionSeconds = 0.300f;
        P.ThinkIntervalSeconds = 0.170f;
        P.Aggression = 0.52f;
        P.HeavyBias = 0.30f;
        P.GuardChance = 0.26f;
        P.SlipChance = 0.05f;
        P.PreferredRangeFraction = 0.98f;
        P.RetreatStaminaFraction = 0.30f;
        P.HealthPanicMargin = 0.45f;
        P.bPunishRecovery = false;
        break;

    case EBotDifficulty::Champion:
        P.ReactionSeconds = 0.120f;
        P.ThinkIntervalSeconds = 0.080f;
        P.Aggression = 0.54f;
        P.HeavyBias = 0.24f;
        P.GuardChance = 0.60f;
        P.SlipChance = 0.22f;
        P.PreferredRangeFraction = 0.84f;
        P.RetreatStaminaFraction = 0.24f;
        P.HealthPanicMargin = 0.30f;
        P.bPunishRecovery = true;
        break;

    case EBotDifficulty::Contender:
    default:
        P.ReactionSeconds = 0.190f;
        P.ThinkIntervalSeconds = 0.110f;
        P.Aggression = 0.50f;
        P.HeavyBias = 0.28f;
        P.GuardChance = 0.44f;
        P.SlipChance = 0.14f;
        P.PreferredRangeFraction = 0.88f;
        P.RetreatStaminaFraction = 0.26f;
        P.HealthPanicMargin = 0.35f;
        P.bPunishRecovery = true;
        break;
    }

    // Personality bends the difficulty baseline. These are behavioural only:
    // no archetype changes damage, health, reach, stamina or speed.
    switch (Archetype)
    {
    case EBotArchetype::Pressure:
        P.Aggression = Clamp01(P.Aggression + 0.22f);
        P.GuardChance = Clamp01(P.GuardChance - 0.14f);
        P.SlipChance = Clamp01(P.SlipChance - 0.04f);
        P.HeavyBias = Clamp01(P.HeavyBias - 0.02f);
        P.PreferredRangeFraction = ClampF(P.PreferredRangeFraction - 0.10f, 0.5f, 1.2f);
        P.HealthPanicMargin = Clamp01(P.HealthPanicMargin - 0.05f);
        break;

    case EBotArchetype::Counter:
        P.Aggression = Clamp01(P.Aggression - 0.14f);
        P.GuardChance = Clamp01(P.GuardChance + 0.16f);
        P.SlipChance = Clamp01(P.SlipChance + 0.10f);
        P.HeavyBias = Clamp01(P.HeavyBias + 0.06f);
        P.PreferredRangeFraction = ClampF(P.PreferredRangeFraction + 0.08f, 0.5f, 1.2f);
        P.ThinkIntervalSeconds = ClampF(P.ThinkIntervalSeconds - 0.010f, 0.02f, 1.0f);
        break;

    case EBotArchetype::Slugger:
        P.HeavyBias = Clamp01(P.HeavyBias + 0.30f);
        P.Aggression = Clamp01(P.Aggression + 0.04f);
        P.GuardChance = Clamp01(P.GuardChance - 0.08f);
        P.PreferredRangeFraction = ClampF(P.PreferredRangeFraction - 0.04f, 0.5f, 1.2f);
        break;

    case EBotArchetype::Balanced:
    default:
        break;
    }

    return P;
}

bool FBotProfile::IsValid() const
{
    return ReactionSeconds > 0.0f && ReactionSeconds <= 1.0f &&
           ThinkIntervalSeconds >= 0.02f && ThinkIntervalSeconds <= 1.0f &&
           Aggression >= 0.0f && Aggression <= 1.0f &&
           HeavyBias >= 0.0f && HeavyBias <= 1.0f &&
           GuardChance >= 0.0f && GuardChance <= 1.0f &&
           SlipChance >= 0.0f && SlipChance <= 1.0f &&
           PreferredRangeFraction > 0.4f && PreferredRangeFraction <= 1.2f &&
           RetreatStaminaFraction >= 0.0f && RetreatStaminaFraction <= 0.9f &&
           HealthPanicMargin >= 0.0f && HealthPanicMargin <= 1.0f;
}

// ---------------------------------------------------------------------------
// FBotBrain
// ---------------------------------------------------------------------------

FBotBrain::FBotBrain()
{
}

void FBotBrain::Init(ESide InSide, const FBotProfile& InProfile, uint32_t InSeed)
{
    Side = InSide;
    Foe = (InSide == ESide::A) ? ESide::B : ESide::A;
    Profile = InProfile;
    // A zero seed would stall xorshift32 forever, so remap it. The remapped
    // value is still a pure function of the seed, so matches stay reproducible.
    RngState = (InSeed == 0u) ? 0x9E3779B9u : InSeed;

    Plan = EBotPlan::Idle;
    PlanAgeSeconds = 0.0f;
    PlanMinSeconds = 0.0f;
    bPlanOwesAttack = false;
    PendingAction = EAction::Jab;

    ThinkAccumulator = 0.0f;

    TrackedAttack = EAction::Idle;
    TrackedAttackSeconds = 0.0f;
    bReflexSpent = false;
    Reflex = EReflexResponse::None;
    ReflexHoldSeconds = 0.0f;

    DecisionCount = 0;
    ReactionCount = 0;
    AttackOrders = 0;
    GuardOrders = 0;
    SlipOrders = 0;

    // Burn a few draws so that two brains with nearby seeds do not start with
    // an identical first decision.
    for (int32_t i = 0; i < 4; ++i)
    {
        (void)NextFloat01();
    }
}

float FBotBrain::NextFloat01()
{
    // xorshift32. Deterministic, allocation-free and identical on every
    // platform, which is what makes a seeded match reproducible.
    uint32_t X = RngState;
    X ^= X << 13;
    X ^= X >> 17;
    X ^= X << 5;
    RngState = (X == 0u) ? 0x9E3779B9u : X;
    return static_cast<float>(RngState >> 8) * (1.0f / 16777216.0f);
}

bool FBotBrain::Roll(float Probability)
{
    if (Probability <= 0.0f)
    {
        return false;
    }
    if (Probability >= 1.0f)
    {
        return true;
    }
    return NextFloat01() < Probability;
}

void FBotBrain::UpdateReflex(float DeltaSeconds, const FCombatSim& Sim)
{
    Reflex = EReflexResponse::None;

    const FFighterState& Me = Sim.GetFighter(Side);
    const FFighterState& FoeState = Sim.GetFighter(Foe);

    // Track the opponent's current punch. Only startup and active frames count
    // as a live threat: as soon as the opponent enters recovery the bot stops
    // reacting to that punch, so the NEXT punch is seen as a fresh event rather
    // than inheriting the previous one's reaction clock.
    const bool bFoeThreatening = (FoeState.State == EFighterState::AttackStartup ||
                                  FoeState.State == EFighterState::AttackActive);

    if (bFoeThreatening)
    {
        if (FoeState.CurrentAction != TrackedAttack)
        {
            TrackedAttack = FoeState.CurrentAction;
            TrackedAttackSeconds = 0.0f;
            bReflexSpent = false;
        }
        else
        {
            TrackedAttackSeconds += DeltaSeconds;
        }
    }
    else
    {
        if (TrackedAttack != EAction::Idle)
        {
            TrackedAttack = EAction::Idle;
            bReflexSpent = false;
        }
        TrackedAttackSeconds = 0.0f;
    }

    if (TrackedAttack == EAction::Idle || bReflexSpent || !Sim.IsFighting())
    {
        return;
    }

    // The reflex is only allowed to fire once ReactionSeconds have passed since
    // this punch began. Short-startup punches therefore cannot be reaction-
    // blocked by any grade, exactly as in a real fighting game.
    if (TrackedAttackSeconds < Profile.ReactionSeconds)
    {
        return;
    }

    const FFighterConfig& FoeCfg = Sim.GetConfig(Foe);
    const FAttackDef& Incoming =
        (TrackedAttack == EAction::HeavyPunch) ? FoeCfg.HeavyPunch : FoeCfg.Jab;

    const float Separation = std::fabs(FoeState.PositionX - Me.PositionX);

    // Only worth responding to if the punch could actually reach us.
    const bool bThreat = Separation <= (Incoming.ReachFar + 12.0f) &&
                         Separation >= (Incoming.ReachNear - 12.0f);
    if (!bThreat)
    {
        return;
    }

    bReflexSpent = true;
    ++ReactionCount;

    const FFighterConfig& MyCfg = Sim.GetConfig(Side);
    const bool bCanSlip = Me.DodgeCooldownTicks == 0 &&
                          Me.Stamina >= MyCfg.DodgeStaminaCost &&
                          IsFreeToAct(Me.State);

    if (bCanSlip && Roll(Profile.SlipChance))
    {
        Reflex = EReflexResponse::Slip;
        return;
    }

    Reflex = EReflexResponse::Guard;
    // Hold the guard for the rest of the incoming punch even if the opponent
    // cancels into another attack; letting go on a per-tick basis would make the
    // bot flicker its hands in and out of the guard.
    ReflexHoldSeconds = ClampF(Incoming.StartupSeconds + Incoming.ActiveSeconds + 0.06f,
                               0.12f, 0.45f);
    ++GuardOrders;
}

void FBotBrain::ChoosePlan(const FCombatSim& Sim)
{
    ++DecisionCount;

    PlanAgeSeconds = 0.0f;
    bPlanOwesAttack = false;

    const FFighterState& Me = Sim.GetFighter(Side);
    const FFighterState& FoeState = Sim.GetFighter(Foe);

    if (!Sim.IsFighting())
    {
        Plan = EBotPlan::Idle;
        PlanMinSeconds = 0.05f;
        return;
    }

    // A fighter who is not free to act cannot start anything. Re-plan cheaply so
    // the bot is ready the instant it is released, without burning decisions.
    if (!IsFreeToAct(Me.State))
    {
        Plan = EBotPlan::Hold;
        PlanMinSeconds = 0.0f;
        return;
    }

    const FFighterConfig& MyCfg = Sim.GetConfig(Side);
    const FFighterConfig& FoeCfg = Sim.GetConfig(Foe);

    const float Separation = std::fabs(FoeState.PositionX - Me.PositionX);
    const float JabReach = MyCfg.Jab.ReachFar;
    const float HeavyReach = MyCfg.HeavyPunch.ReachFar;

    // The simulation measures a hit centre-to-centre, so a punch lands at or
    // below its own ReachFar. The small margin stops the bot throwing punches
    // that whiff by a centimetre.
    //
    // The match rules also shove the fighters apart whenever they are closer
    // than MinSeparation, so a standoff wider than the strike window leaves the
    // bot walking into a wall of separation it can never punch across - which is
    // exactly how the offline opponent used to freeze: a 106 cm enforced standoff
    // against a 100 cm jab window meant it trailed the player for a whole round
    // and never threw a punch. Widen the windows to cover the enforced standoff,
    // but never past the real reach, where the punch would whiff anyway.
    const FMatchRules& Rules = Sim.GetRules();
    const float RuleFloor = Rules.MinSeparation + 2.0f;
    const float StrikeWindow = ClampF(RuleFloor, JabReach - 4.0f, JabReach);
    const float HeavyWindow = ClampF(RuleFloor, HeavyReach - 4.0f, HeavyReach);

    // Preferred range has to sit inside the window the bot can actually strike
    // from, or it walks to a standoff it then refuses to punch from.
    const float Preferred =
        ClampF(JabReach * Profile.PreferredRangeFraction, 0.0f, StrikeWindow - 2.0f);

    const bool bCanJab = Me.Stamina >= MyCfg.Jab.StaminaCost + 1.0f;
    const bool bCanHeavy = Me.Stamina >= MyCfg.HeavyPunch.StaminaCost + 1.0f;
    const bool bStaminaEmergency = Me.Stamina <= MyCfg.MaxStamina * Profile.RetreatStaminaFraction;

    const float MyHealthFraction = MyCfg.MaxHealth > 0.0f ? Me.Health / MyCfg.MaxHealth : 0.0f;
    const float FoeHealthFraction =
        FoeCfg.MaxHealth > 0.0f ? FoeState.Health / FoeCfg.MaxHealth : 0.0f;
    const bool bHealthBehind =
        (MyHealthFraction + Profile.HealthPanicMargin) < FoeHealthFraction;

    // An opponent on the canvas cannot be hit at all (FCombatSim::AttackOverlaps
    // refuses a knocked-down or defeated defender), so swinging is pure waste.
    if (Profile.bNeverHitDowned &&
        (FoeState.State == EFighterState::KnockedDown ||
         FoeState.State == EFighterState::Defeated))
    {
        Plan = (Separation < Preferred * 0.6f) ? EBotPlan::Retreat : EBotPlan::Hold;
        PlanMinSeconds = 0.20f;
        return;
    }

    // 1. Gassed: never trade on an empty tank. Back off and recover.
    if (bStaminaEmergency)
    {
        Plan = EBotPlan::Retreat;
        PlanMinSeconds = 0.35f;
        return;
    }

    // 2. Punish window: the opponent is stuck in attack recovery.
    if (Profile.bPunishRecovery && FoeState.IsAttackRecovering() && bCanHeavy &&
        Separation <= HeavyWindow)
    {
        Plan = EBotPlan::Strike;
        PendingAction = EAction::HeavyPunch;
        bPlanOwesAttack = true;
        PlanMinSeconds = 0.30f;
        return;
    }

    // 3. Inside our own strike range.
    if (Separation <= StrikeWindow)
    {
        if (bCanJab && Roll(Profile.Aggression))
        {
            Plan = EBotPlan::Strike;
            PendingAction = (bCanHeavy && Roll(Profile.HeavyBias)) ? EAction::HeavyPunch
                                                                  : EAction::Jab;
            bPlanOwesAttack = true;
            // A heavy punch is a long commitment; stay with it until it resolves.
            PlanMinSeconds = (PendingAction == EAction::HeavyPunch) ? 0.30f : 0.22f;
            return;
        }

        if (Roll(Profile.GuardChance))
        {
            Plan = EBotPlan::Defend;
            PlanMinSeconds = 0.20f;
            ++GuardOrders;
            return;
        }

        Plan = EBotPlan::Hold;
        PlanMinSeconds = 0.14f;
        return;
    }

    // 3b. Between the two windows: the jab would whiff, so the heavy punch is the
    //     only tool that reaches. Throw it rather than walking into a jab that
    //     cannot connect.
    if (bCanHeavy && Separation <= HeavyWindow && Roll(Profile.Aggression))
    {
        Plan = EBotPlan::Strike;
        PendingAction = EAction::HeavyPunch;
        bPlanOwesAttack = true;
        PlanMinSeconds = 0.30f;
        return;
    }

    // 4. Outside strike range.
    if (bHealthBehind && Separation > 60.0f && Roll(0.35f))
    {
        Plan = EBotPlan::Retreat;
        PlanMinSeconds = 0.30f;
        return;
    }

    if (Separation <= Preferred * 1.6f && Roll(Profile.GuardChance * 0.5f))
    {
        // Closing behind a guard: the standard way to walk a boxer down without
        // eating a counter on the way in.
        Plan = EBotPlan::Defend;
        PlanMinSeconds = 0.16f;
        ++GuardOrders;
        return;
    }

    Plan = EBotPlan::Approach;
    PlanMinSeconds = 0.10f;
}

FInputState FBotBrain::ApplyPlan(const FCombatSim& Sim)
{
    FInputState In;

    const FFighterState& Me = Sim.GetFighter(Side);
    const FFighterState& FoeState = Sim.GetFighter(Foe);
    const FFighterConfig& MyCfg = Sim.GetConfig(Side);

    const bool bTowardIsRight = (FoeState.PositionX > Me.PositionX);
    const float Separation = std::fabs(FoeState.PositionX - Me.PositionX);

    // -----------------------------------------------------------------------
    // Reflex layer wins over the tactical plan: a punch already in flight beats
    // whatever the bot had intended to do.
    // -----------------------------------------------------------------------
    if (Reflex == EReflexResponse::Slip)
    {
        // Slip away from the incoming punch. FCombatSim derives DodgeDir from the
        // movement flags on the same tick as the dodge edge, defaulting to
        // backwards when no direction is given; we always give one explicitly so
        // the intent is unambiguous.
        if (bTowardIsRight)
        {
            In.bMoveLeft = true;
        }
        else
        {
            In.bMoveRight = true;
        }
        In.bDodgePressed = true;
        ++SlipOrders;
        return In;
    }

    if (Reflex == EReflexResponse::Guard || ReflexHoldSeconds > 0.0f)
    {
        In.bHoldBlock = true;
        return In;
    }

    switch (Plan)
    {
    case EBotPlan::Approach:
        if (bTowardIsRight)
        {
            In.bMoveRight = true;
        }
        else
        {
            In.bMoveLeft = true;
        }
        break;

    case EBotPlan::Retreat:
        if (bTowardIsRight)
        {
            In.bMoveLeft = true;
        }
        else
        {
            In.bMoveRight = true;
        }
        break;

    case EBotPlan::Strike:
    {
        const FAttackDef& Def =
            (PendingAction == EAction::HeavyPunch) ? MyCfg.HeavyPunch : MyCfg.Jab;
        const bool bInRange = Separation <= Def.ReachFar;
        const bool bAffordable = Me.Stamina >= Def.StaminaCost;

        // Only spend the one-shot press when the fighter can actually act on it.
        // Spending it during our own attack would silently waste the order and
        // make the bot look passive.
        if (bPlanOwesAttack && bInRange && bAffordable && IsFreeToAct(Me.State))
        {
            if (PendingAction == EAction::HeavyPunch)
            {
                In.bHeavyPressed = true;
            }
            else
            {
                In.bJabPressed = true;
            }
            bPlanOwesAttack = false;
            ++AttackOrders;
        }
        break;
    }

    case EBotPlan::Defend:
        In.bHoldBlock = true;
        break;

    case EBotPlan::Hold:
    case EBotPlan::Idle:
    case EBotPlan::Count:
    default:
        // A deliberate neutral tick: no input at all.
        break;
    }

    return In;
}

FInputState FBotBrain::ComputeInput(float DeltaSeconds, const FCombatSim& Sim)
{
    if (DeltaSeconds < 0.0f)
    {
        DeltaSeconds = 0.0f;
    }

    if (ReflexHoldSeconds > 0.0f)
    {
        ReflexHoldSeconds -= DeltaSeconds;
        if (ReflexHoldSeconds < 0.0f)
        {
            ReflexHoldSeconds = 0.0f;
        }
    }

    UpdateReflex(DeltaSeconds, Sim);

    // The tactical clock only advances when the reflex layer is quiet; a bot
    // that re-plans while guarding would keep dropping its hands.
    const bool bReflexBusy =
        (Reflex != EReflexResponse::None) || (ReflexHoldSeconds > 0.0f) || !Sim.IsFighting();

    PlanAgeSeconds += DeltaSeconds;
    ThinkAccumulator += DeltaSeconds;

    if (!bReflexBusy && ThinkAccumulator >= Profile.ThinkIntervalSeconds &&
        PlanAgeSeconds >= PlanMinSeconds)
    {
        ThinkAccumulator = 0.0f;
        ChoosePlan(Sim);
    }
    else if (bReflexBusy)
    {
        // Do not let the clock bank up while guarding, or the bot would burst
        // through several plans the moment the guard drops.
        ThinkAccumulator = 0.0f;
    }

    return ApplyPlan(Sim);
}

// ---------------------------------------------------------------------------
// Headless bot-vs-bot matches
// ---------------------------------------------------------------------------

FBotMatchOutcome RunBotMatch(const FMatchRules& Rules,
                             const FFighterConfig& ConfigA, const FFighterConfig& ConfigB,
                             const FBotProfile& ProfileA, const FBotProfile& ProfileB,
                             uint32_t SeedA, uint32_t SeedB,
                             int32_t MaxTicks,
                             ETrainingMode TrainingA, ETrainingMode TrainingB)
{
    FBotMatchOutcome Out;

    FCombatSim Sim;
    Sim.Init(Rules, ConfigA, ConfigB, TrainingA, TrainingB);

    FBotBrain BrainA;
    FBotBrain BrainB;
    BrainA.Init(ESide::A, ProfileA, SeedA);
    BrainB.Init(ESide::B, ProfileB, SeedB);

    const float Dt = FCombatSim::FixedTickSeconds;

    int32_t Tick = 0;
    float WorstViolation = 0.0f;

    while (Tick < MaxTicks && !Sim.IsMatchOver())
    {
        // Both brains read the same pre-tick state and neither can see the
        // other's intent, exactly as a network opponent could not.
        const FInputState InputA = BrainA.ComputeInput(Dt, Sim);
        const FInputState InputB = BrainB.ComputeInput(Dt, Sim);

        Sim.StepFixed(InputA, InputB);
        ++Tick;

        const float Separation = std::fabs(Sim.GetFighter(ESide::B).PositionX -
                                           Sim.GetFighter(ESide::A).PositionX);
        const float Shortfall = Rules.MinSeparation - Separation;
        if (Shortfall > WorstViolation)
        {
            WorstViolation = Shortfall;
        }
    }

    const FMatchState& Match = Sim.GetMatch();
    const FFighterState& A = Sim.GetFighter(ESide::A);
    const FFighterState& B = Sim.GetFighter(ESide::B);

    Out.Result = Match.Result;
    Out.EndReason = Match.EndReason;
    Out.LastRoundEndReason = Match.LastRoundEndReason;
    Out.ScoreA = Match.ScoreA;
    Out.ScoreB = Match.ScoreB;
    Out.RoundsFought = Match.Round;
    Out.Ticks = Tick;
    Out.DurationSeconds = static_cast<float>(Tick) * Dt;
    Out.bTimedOut = !Sim.IsMatchOver();

    Out.HitsLandedA = A.HitsLandedTotal;
    Out.HitsLandedB = B.HitsLandedTotal;
    Out.AttacksThrownA = A.AttacksThrownTotal;
    Out.AttacksThrownB = B.AttacksThrownTotal;
    Out.BlocksA = A.BlocksThisRound;
    Out.BlocksB = B.BlocksThisRound;
    Out.DodgesA = A.DodgesThisRound;
    Out.DodgesB = B.DodgesThisRound;
    Out.KnockdownsA = A.KnockdownsTotal;
    Out.KnockdownsB = B.KnockdownsTotal;

    Out.DamageDealtA = A.DamageDealtTotal;
    Out.DamageDealtB = B.DamageDealtTotal;
    Out.HealthA = A.Health;
    Out.HealthB = B.Health;

    Out.BotDecisionsA = BrainA.GetDecisionCount();
    Out.BotDecisionsB = BrainB.GetDecisionCount();
    Out.BotAttacksA = BrainA.GetAttackOrders();
    Out.BotAttacksB = BrainB.GetAttackOrders();

    Out.FinalSeparation = std::fabs(B.PositionX - A.PositionX);
    Out.WorstSeparationViolation = WorstViolation;

    return Out;
}

void DescribeMatchOutcome(const FBotMatchOutcome& Outcome, char* Buffer, int BufferSize)
{
    if (Buffer == nullptr || BufferSize <= 0)
    {
        return;
    }

    const char* ResultName = "Undecided";
    switch (Outcome.Result)
    {
    case EMatchResult::FighterAWins: ResultName = "A wins"; break;
    case EMatchResult::FighterBWins: ResultName = "B wins"; break;
    case EMatchResult::Draw: ResultName = "Draw"; break;
    case EMatchResult::Undecided: ResultName = "Undecided"; break;
    default: break;
    }

    const char* EndName = "None";
    switch (Outcome.EndReason)
    {
    case EMatchEndReason::Knockout: EndName = "KO"; break;
    case EMatchEndReason::TechnicalKnockout: EndName = "TKO (3 knockdowns)"; break;
    case EMatchEndReason::RefereeStopsContest: EndName = "RSC"; break;
    case EMatchEndReason::Decision: EndName = "Decision"; break;
    case EMatchEndReason::DrawDecision: EndName = "Draw decision"; break;
    case EMatchEndReason::None: EndName = "None"; break;
    default: break;
    }

    std::snprintf(
        Buffer, static_cast<size_t>(BufferSize),
        "result=%s end=%s scores=%d-%d rounds=%d ticks=%d time=%.1fs%s | "
        "A: hits=%d thrown=%d kd=%d dmg=%.0f hp=%.0f | "
        "B: hits=%d thrown=%d kd=%d dmg=%.0f hp=%.0f | "
        "blocks=%d/%d dodges=%d/%d decisions=%d/%d orders=%d/%d "
        "sep=%.1f worstViolation=%.3f",
        ResultName, EndName, Outcome.ScoreA, Outcome.ScoreB, Outcome.RoundsFought,
        Outcome.Ticks, Outcome.DurationSeconds, Outcome.bTimedOut ? " TIMED-OUT" : "",
        Outcome.HitsLandedA, Outcome.AttacksThrownA, Outcome.KnockdownsA,
        Outcome.DamageDealtA, Outcome.HealthA,
        Outcome.HitsLandedB, Outcome.AttacksThrownB, Outcome.KnockdownsB,
        Outcome.DamageDealtB, Outcome.HealthB,
        Outcome.BlocksA, Outcome.BlocksB, Outcome.DodgesA, Outcome.DodgesB,
        Outcome.BotDecisionsA, Outcome.BotDecisionsB,
        Outcome.BotAttacksA, Outcome.BotAttacksB,
        Outcome.FinalSeparation, Outcome.WorstSeparationViolation);
}

} // namespace RKC
