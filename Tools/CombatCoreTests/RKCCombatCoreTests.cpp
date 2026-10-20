// RKCCombatCoreTests.cpp
//
// Standalone verification harness for the Rocky Kombat authoritative combat
// simulation core (RKCCombatCore.h / RKCCombatCore.cpp).
//
// WHY THIS EXISTS
//   The combat core is deliberately pure C++ with no Unreal Engine dependency so
//   that the competitive match rules can be verified without launching the
//   editor. This harness is the acceptance evidence for the rules behind
//   acceptance gate B (two fighters complete a reliable offline match):
//   damage, guard, slip, stamina, knockdown, knockout, round scoring and
//   determinism.
//
// BUILD
//   Tools/CombatCoreTests/build_and_run.bat   (MSVC, no test framework needed)
//
// Each check prints the rule it verifies, so the console output doubles as the
// test evidence log. The process exit code is non-zero when any check fails.

#include "RKCCombatCore.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace RKC;

// ---------------------------------------------------------------------------
// Assertion helpers
// ---------------------------------------------------------------------------

static int32_t g_Checks = 0;
static int32_t g_Failures = 0;
static const char* g_TestName = "";

static const char* StateName(EFighterState S)
{
    switch (S)
    {
    case EFighterState::Idle: return "Idle";
    case EFighterState::Moving: return "Moving";
    case EFighterState::AttackStartup: return "AttackStartup";
    case EFighterState::AttackActive: return "AttackActive";
    case EFighterState::AttackRecovery: return "AttackRecovery";
    case EFighterState::Blocking: return "Blocking";
    case EFighterState::Dodging: return "Dodging";
    case EFighterState::HitStun: return "HitStun";
    case EFighterState::KnockedDown: return "KnockedDown";
    case EFighterState::Victory: return "Victory";
    case EFighterState::Defeated: return "Defeated";
    default: return "Unknown";
    }
}

static void Check(bool Condition, const char* What)
{
    ++g_Checks;
    if (!Condition)
    {
        ++g_Failures;
        std::printf("    FAIL [%s] %s\n", g_TestName, What);
    }
}

static void CheckInt(int32_t Actual, int32_t Expected, const char* What)
{
    ++g_Checks;
    if (Actual != Expected)
    {
        ++g_Failures;
        std::printf("    FAIL [%s] %s (actual %d, expected %d)\n", g_TestName, What,
                    static_cast<int>(Actual), static_cast<int>(Expected));
    }
}

static void CheckNear(float Actual, float Expected, float Tolerance, const char* What)
{
    ++g_Checks;
    if (!(std::fabs(Actual - Expected) <= Tolerance))
    {
        ++g_Failures;
        std::printf("    FAIL [%s] %s (actual %.5f, expected %.5f, tolerance %.5f)\n",
                    g_TestName, What, static_cast<double>(Actual),
                    static_cast<double>(Expected), static_cast<double>(Tolerance));
    }
}

static void CheckFighterState(const FCombatSim& Sim, ESide Side, EFighterState Expected,
                              const char* What)
{
    ++g_Checks;
    const EFighterState Actual = Sim.GetFighter(Side).State;
    if (Actual != Expected)
    {
        ++g_Failures;
        std::printf("    FAIL [%s] %s (actual %s, expected %s)\n", g_TestName, What,
                    StateName(Actual), StateName(Expected));
    }
}

// ---------------------------------------------------------------------------
// Simulation helpers
// ---------------------------------------------------------------------------

struct FEventTally
{
    static const int32_t Slots = static_cast<int32_t>(ECombatEvent::Count) + 1;

    int32_t Counts[Slots];
    float LastValue[Slots];

    FEventTally()
    {
        for (int32_t i = 0; i < Slots; ++i)
        {
            Counts[i] = 0;
            LastValue[i] = 0.0f;
        }
    }

    int32_t Get(ECombatEvent Type) const { return Counts[static_cast<int32_t>(Type)]; }

    float ValueOf(ECombatEvent Type) const { return LastValue[static_cast<int32_t>(Type)]; }

    void Add(const FCombatEventRecord& Record)
    {
        ++Counts[static_cast<int32_t>(Record.Type)];
        LastValue[static_cast<int32_t>(Record.Type)] = Record.Value;
    }
};

/** Drains every queued event (presentation-layer behaviour) and tallies them. */
static FEventTally Drain(FCombatSim& Sim)
{
    FEventTally Tally;
    FCombatEventRecord Record;
    while (Sim.PopEvent(Record))
    {
        Tally.Add(Record);
    }
    return Tally;
}

static FInputState InIdle() { return FInputState(); }

static FInputState InJab()
{
    FInputState I;
    I.bJabPressed = true;
    return I;
}

static FInputState InHeavy()
{
    FInputState I;
    I.bHeavyPressed = true;
    return I;
}

static FInputState InBlock()
{
    FInputState I;
    I.bHoldBlock = true;
    return I;
}

static FInputState InDodgeSlip()
{
    FInputState I;
    I.bDodgePressed = true; // direction defaults to slipping away from the opponent
    return I;
}

static FInputState InWalkRight()
{
    FInputState I;
    I.bMoveRight = true;
    return I;
}

/** Advances the simulation by whole fixed ticks (test-side wrapper). */
static void RunTicks(FCombatSim& Sim, int32_t Ticks,
                     FInputState InputA = FInputState(), FInputState InputB = FInputState())
{
    for (int32_t i = 0; i < Ticks; ++i)
    {
        Sim.StepFixed(InputA, InputB);
    }
}

static void RunUntilFighting(FCombatSim& Sim, int32_t MaxTicks = 600)
{
    for (int32_t i = 0; i < MaxTicks && !Sim.IsFighting(); ++i)
    {
        Sim.StepFixed(FInputState(), FInputState());
    }
}

static void RunToMatchEnd(FCombatSim& Sim, int32_t MaxTicks)
{
    for (int32_t i = 0; i < MaxTicks && !Sim.IsMatchOver(); ++i)
    {
        Sim.StepFixed(FInputState(), FInputState());
    }
}

/** Walks fighter A toward B until the pair is inside MaxSeparation. */
static void CloseDistanceA(FCombatSim& Sim, float MaxSeparation, int32_t MaxTicks)
{
    for (int32_t i = 0; i < MaxTicks; ++i)
    {
        const float Separation = std::fabs(Sim.GetFighter(ESide::B).PositionX -
                                           Sim.GetFighter(ESide::A).PositionX);
        if (Separation <= MaxSeparation)
        {
            return;
        }
        Sim.StepFixed(InWalkRight(), FInputState());
    }
}

/**
 * Shared match setup for rule tests.
 *
 * Rules: three 30-second rounds (the product brief) but a short countdown and a
 * short between-round break so the tests stay fast. The 320 cm ring places the
 * two fighters 80 cm apart on the bell, which is inside both the jab
 * (26..104 cm) and heavy punch (26..118 cm) reach slabs, so a punch thrown on
 * the bell is expected to connect.
 */
struct FScenario
{
    FMatchRules Rules;
    FFighterConfig A;
    FFighterConfig B;

    FScenario()
    {
        Rules = FMatchRules();
        Rules.CountdownSeconds = 0.25f;
        Rules.RoundBreakSeconds = 0.5f;
        Rules.PostMatchSeconds = 2.0f;
        Rules.ArenaMinX = -160.0f;
        Rules.ArenaMaxX = 160.0f;
        A = FFighterConfig();
        B = FFighterConfig();
    }
};

/** Frame data of the shipped default configs, asserted by the tests. */
static const int32_t JabStartupTicks = 6;
static const int32_t JabActiveTicks = 4;
static const int32_t JabRecoveryTicks = 12;
static const int32_t HeavyStartupTicks = 13;
static const int32_t HeavyActiveTicks = 6;
static const int32_t HeavyRecoveryTicks = 24;

// ---------------------------------------------------------------------------
// T1 - documented defaults, countdown, inputs ignored before the bell
// ---------------------------------------------------------------------------

static void Test1_DefaultsAndCountdown()
{
    const FMatchRules Defaults;
    CheckInt(Defaults.Rounds, 3, "default match is 3 rounds");
    CheckNear(Defaults.RoundSeconds, 30.0f, 0.001f, "default round is 30 seconds");
    CheckInt(Defaults.MaxKnockdownsPerRound, 3, "three-knockdown rule is the default");
    Check(Defaults.IsValid(), "default rules validate");

    FScenario S;
    S.Rules.CountdownSeconds = 3.0f; // the shipped countdown, tested explicitly
    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);

    Check(Sim.GetMatch().Phase == EMatchPhase::Countdown, "Init enters the countdown");
    CheckInt(Sim.GetMatch().Round, 1, "a match starts in round 1");
    CheckNear(Sim.GetFighter(ESide::A).Health, 100.0f, 0.001f, "A starts at full health");
    CheckNear(Sim.GetFighter(ESide::B).Stamina, 100.0f, 0.001f, "B starts at full stamina");
    CheckNear(Sim.GetFighter(ESide::A).PositionX, -40.0f, 0.001f, "A starts at ArenaMinX + 120");
    CheckNear(Sim.GetFighter(ESide::B).PositionX, 40.0f, 0.001f, "B starts at ArenaMaxX - 120");
    Check(!Sim.IsFighting(), "the fight is not live during the countdown");

    const float StartX = Sim.GetFighter(ESide::A).PositionX;
    for (int32_t i = 0; i < 60; ++i) // one second of jab-spamming and walking
    {
        FInputState Input = InJab();
        Input.bMoveRight = true;
        Sim.StepFixed(Input, FInputState());
    }
    CheckInt(Sim.GetFighter(ESide::A).AttacksThrownTotal, 0, "no attack is possible during the countdown");
    CheckNear(Sim.GetFighter(ESide::A).PositionX, StartX, 0.001f, "no movement during the countdown");
    Check(Sim.GetMatch().Phase == EMatchPhase::Countdown, "still counting down after 1 s of a 3 s countdown");

    RunUntilFighting(Sim, 400);
    Check(Sim.IsFighting(), "the fight starts when the countdown expires");
    Check(Sim.GetMatch().Phase == EMatchPhase::Fighting, "phase is Fighting");
    CheckNear(Sim.GetMatch().RoundTimeRemaining, S.Rules.RoundSeconds, 0.001f,
              "the round clock starts at RoundSeconds");
    CheckNear(Sim.GetRoundTimeFraction(), 1.0f, 0.001f, "GetRoundTimeFraction is 1.0 on the bell");

    const FEventTally Tally = Drain(Sim);
    CheckInt(Tally.Get(ECombatEvent::RoundStarted), 1, "one RoundStarted event");
}

// ---------------------------------------------------------------------------
// T2 - a jab lands once, for the documented damage, and never re-applies
// ---------------------------------------------------------------------------

static void Test2_JabDamageAndSingleHitGuard()
{
    FScenario S;
    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    int32_t Startup = 0, Active = 0, Recovery = 0;
    FCombatSim::GetAttackWindow(Sim.GetConfig(ESide::A).Jab, Startup, Active, Recovery);
    CheckInt(Startup, JabStartupTicks, "jab startup is 6 ticks (0.100 s)");
    CheckInt(Active, JabActiveTicks, "jab active window is 4 ticks (0.067 s)");
    CheckInt(Recovery, JabRecoveryTicks, "jab recovery is 12 ticks (0.200 s)");

    Sim.StepFixed(InJab(), FInputState());
    CheckFighterState(Sim, ESide::A, EFighterState::AttackStartup, "a jab press enters startup");
    CheckInt(Sim.GetFighter(ESide::A).AttackTick, 1, "the press tick is attack tick 1");
    CheckInt(Sim.GetFighter(ESide::A).AttackTotalTicks, JabStartupTicks + JabActiveTicks + JabRecoveryTicks,
             "attack length is startup + active + recovery");
    CheckNear(Sim.GetFighter(ESide::A).Stamina, 91.0f, 0.01f, "a jab costs 9 stamina");
    CheckInt(Sim.GetFighter(ESide::A).AttacksThrownThisRound, 1, "one attack thrown this round");
    CheckFighterState(Sim, ESide::B, EFighterState::Idle, "the defender is untouched during startup");

    RunTicks(Sim, JabStartupTicks - 1);
    CheckNear(Sim.GetFighter(ESide::B).Health, 100.0f, 0.001f, "no damage before the active window");
    CheckFighterState(Sim, ESide::A, EFighterState::AttackStartup, "still in startup one tick before the window");

    RunTicks(Sim, 1); // first tick of the active window
    CheckFighterState(Sim, ESide::A, EFighterState::AttackActive, "first active tick reached");
    CheckNear(Sim.GetFighter(ESide::B).Health, 94.0f, 0.001f, "a jab deals 6.0 damage");
    CheckNear(Sim.GetFighter(ESide::A).DamageDealtTotal, 6.0f, 0.001f, "damage is recorded for the judges");
    CheckNear(Sim.GetFighter(ESide::B).DamageTakenTotal, 6.0f, 0.001f, "damage taken is recorded");
    CheckInt(Sim.GetFighter(ESide::A).HitsLandedTotal, 1, "one punch landed");
    CheckInt(Sim.GetFighter(ESide::B).HitsTakenTotal, 1, "one punch taken");
    CheckFighterState(Sim, ESide::B, EFighterState::HitStun, "a clean hit puts the defender in hit stun");
    CheckInt(Sim.GetFighter(ESide::B).StateTicksRemaining, 13, "hit stun is 13 ticks (0.217 s)");
    CheckNear(Sim.GetFighter(ESide::B).PositionX, 74.0f, 0.001f, "the jab knocks the defender back 34 cm");

    const FEventTally HitTally = Drain(Sim);
    CheckInt(HitTally.Get(ECombatEvent::AttackHit), 1, "exactly one AttackHit event");
    CheckNear(HitTally.ValueOf(ECombatEvent::AttackHit), 6.0f, 0.001f, "the event carries the damage value");

    // The rest of the active window must not re-apply damage, and the punch must
    // not be reported as a whiff once it has connected.
    RunTicks(Sim, 20);
    CheckNear(Sim.GetFighter(ESide::B).Health, 94.0f, 0.001f, "one punch deals damage exactly once");
    CheckInt(Sim.GetFighter(ESide::A).HitsLandedTotal, 1, "still exactly one landed punch");
    CheckFighterState(Sim, ESide::A, EFighterState::Idle, "the fighter is free again after recovery");
    CheckFighterState(Sim, ESide::B, EFighterState::Idle, "the defender has recovered from hit stun");

    const FEventTally AfterTally = Drain(Sim);
    CheckInt(AfterTally.Get(ECombatEvent::AttackHit), 0, "no further hit events from the same punch");
    CheckInt(AfterTally.Get(ECombatEvent::AttackWhiffed), 0, "a punch that connected is not reported as a whiff");
}

// ---------------------------------------------------------------------------
// T3 - fixed-step accumulation and single-edge input consumption
// ---------------------------------------------------------------------------

static void Test3_AdvanceConsumesEdgesOnce()
{
    FScenario S;
    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    // One rendered frame of 0.25 s is 15 fixed steps; a held button must be
    // consumed by the first step only, so one frame cannot throw 15 punches.
    FInputState Held = InJab();
    Sim.Advance(0.25f, Held, FInputState());
    CheckInt(Sim.GetFighter(ESide::A).AttacksThrownTotal, 1,
             "one frame with a held button throws exactly one punch");

    Held.bJabPressed = false;
    Sim.Advance(0.25f, Held, FInputState());
    CheckInt(Sim.GetFighter(ESide::A).AttacksThrownTotal, 1,
             "holding the button across frames does not re-fire the punch");

    // An absurd frame delta must be clamped, not simulated in full.
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    const int32_t Before = Sim.GetMatch().TickCount;
    Sim.Advance(10.0f, FInputState(), FInputState());
    CheckInt(Sim.GetMatch().TickCount - Before, 15,
             "a 10 s frame delta is clamped to 0.25 s of simulation (15 ticks)");

    // Leftover time is carried in an accumulator, so the tick count does not
    // depend on how the frame deltas are chunked.
    FCombatSim SimA;
    FCombatSim SimB;
    SimA.Init(S.Rules, S.A, S.B);
    SimB.Init(S.Rules, S.A, S.B);
    const float FourFrames = 4.0f * FCombatSim::FixedTickSeconds;
    for (int32_t i = 0; i < 60; ++i)
    {
        SimA.Advance(FCombatSim::FixedTickSeconds, FInputState(), FInputState());
        if ((i % 4) == 0)
        {
            SimB.Advance(FourFrames, FInputState(), FInputState());
        }
    }
    CheckInt(SimA.GetMatch().TickCount, 60, "1/60 s frames produce 60 ticks in one second");
    const int32_t TickDifference = SimB.GetMatch().TickCount - SimA.GetMatch().TickCount;
    Check(TickDifference >= -1 && TickDifference <= 1,
          "chunked frames produce the same tick count within one tick");
    CheckNear(SimB.GetMatch().RoundTimeRemaining, SimA.GetMatch().RoundTimeRemaining, 0.02f,
              "the round clock tracks frame size");
}

// ---------------------------------------------------------------------------
// T4 - guard: chip damage, block stun, no landed-punch credit, guard break
// ---------------------------------------------------------------------------

static void Test4_BlockedHitChipStunAndGuardBreak()
{
    FScenario S;
    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    Sim.StepFixed(InJab(), InBlock());
    RunTicks(Sim, JabStartupTicks - 1, FInputState(), InBlock());
    CheckNear(Sim.GetFighter(ESide::B).Health, 100.0f, 0.001f, "the guard absorbs the punch during startup");
    CheckFighterState(Sim, ESide::B, EFighterState::Blocking, "the defender is holding a guard");

    RunTicks(Sim, 1, FInputState(), InBlock()); // first active tick
    CheckNear(Sim.GetFighter(ESide::B).Health, 99.1f, 0.001f, "a blocked jab chips 15% of 6.0 damage (0.9)");
    CheckNear(Sim.GetFighter(ESide::A).DamageDealtThisRound, 0.9f, 0.001f, "chip damage is credited to A");
    CheckInt(Sim.GetFighter(ESide::B).BlocksThisRound, 1, "the block is counted");
    CheckInt(Sim.GetFighter(ESide::A).HitsLandedThisRound, 0, "a blocked punch is not a landed punch");
    CheckFighterState(Sim, ESide::B, EFighterState::Blocking, "the blocker stays in the blocking state");
    CheckInt(Sim.GetFighter(ESide::B).StateTicksRemaining, 8, "block stun is 8 ticks (0.133 s)");
    CheckNear(Sim.GetFighter(ESide::B).Stamina, 92.0f, 0.02f, "blocking the jab costs 8 stamina");
    CheckNear(Sim.GetFighter(ESide::B).PositionX, 57.0f, 0.001f, "blocked knockback is halved (17 cm)");

    const FEventTally Tally = Drain(Sim);
    CheckInt(Tally.Get(ECombatEvent::AttackBlocked), 1, "one AttackBlocked event");
    CheckInt(Tally.Get(ECombatEvent::AttackHit), 0, "a blocked punch does not raise AttackHit");
    CheckNear(Tally.ValueOf(ECombatEvent::AttackBlocked), 0.9f, 0.001f, "the event carries the chip damage");

    // The rest of the active window must not apply chip damage again.
    RunTicks(Sim, 6, FInputState(), InBlock());
    CheckNear(Sim.GetFighter(ESide::B).Health, 99.1f, 0.001f, "chip damage is applied exactly once per punch");
    CheckInt(Sim.GetFighter(ESide::B).BlocksThisRound, 1, "one block per punch");

    // Guard break: a blocked punch taken with an empty tank blows the guard open.
    FScenario G;
    G.B.MaxStamina = 8.0f;
    G.B.StaminaRegenPerSecond = 0.0f;
    G.B.BlockStaminaRegenPerSecond = 0.0f;
    G.B.BlockHoldDrainPerSecond = 0.0f;
    FCombatSim SimG;
    SimG.Init(G.Rules, G.A, G.B);
    RunUntilFighting(SimG);
    Drain(SimG);

    SimG.StepFixed(InJab(), InBlock());
    RunTicks(SimG, JabStartupTicks, FInputState(), InBlock());
    CheckFighterState(SimG, ESide::B, EFighterState::HitStun, "an empty tank turns a block into a guard break");
    CheckInt(SimG.GetFighter(ESide::B).StateTicksRemaining, 48, "guard break stun is 48 ticks (0.8 s)");
    CheckNear(SimG.GetFighter(ESide::B).Stamina, 0.0f, 0.001f, "the guard breaks at zero stamina");

    const FEventTally GuardTally = Drain(SimG);
    CheckInt(GuardTally.Get(ECombatEvent::GuardBroken), 1, "one GuardBroken event");
    CheckInt(GuardTally.Get(ECombatEvent::AttackBlocked), 0, "no ordinary block event when the guard breaks");
}

// ---------------------------------------------------------------------------
// T5 - the slip: invulnerability window, exact distance, cooldown
// ---------------------------------------------------------------------------

static void Test5_DodgeAvoidsPunch()
{
    FScenario S;
    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    Sim.StepFixed(InJab(), FInputState());  // A starts a jab
    RunTicks(Sim, 1);                       // one tick of jab startup
    Sim.StepFixed(FInputState(), InDodgeSlip());
    CheckFighterState(Sim, ESide::B, EFighterState::Dodging, "the dodge press enters the dodging state");
    CheckInt(Sim.GetFighter(ESide::B).DodgeDir, 1, "B slips away from A (+X is away from -X)");
    CheckNear(Sim.GetFighter(ESide::B).Stamina, 86.0f, 0.02f, "a slip costs 14 stamina");
    CheckInt(Sim.GetFighter(ESide::B).DodgesThisRound, 1, "the slip is counted");

    RunTicks(Sim, 4); // A reaches its first active tick while B is inside the slip window
    CheckFighterState(Sim, ESide::A, EFighterState::AttackActive, "A is in its active window");
    CheckNear(Sim.GetFighter(ESide::B).Health, 100.0f, 0.001f, "the slip avoids the punch entirely");
    CheckInt(Sim.GetFighter(ESide::B).HitsTakenTotal, 0, "no hit registers through the slip");

    RunTicks(Sim, 40);
    const FEventTally Tally = Drain(Sim);
    CheckInt(Tally.Get(ECombatEvent::AttackWhiffed), 1, "the punch that hit nothing is reported as a whiff");
    CheckInt(Tally.Get(ECombatEvent::AttackHit), 0, "no hit event through the slip");
    CheckInt(Tally.Get(ECombatEvent::DodgeStarted), 1, "one DodgeStarted event");
    CheckFighterState(Sim, ESide::B, EFighterState::Idle, "B is back on balance after the slip");
    CheckNear(Sim.GetFighter(ESide::B).PositionX, S.Rules.ArenaMaxX, 0.001f,
              "slip motion is clamped by the ring bounds");
    Check(Sim.GetFighter(ESide::B).DodgeCooldownTicks > 0, "the dodge cooldown is running");
}

static void Test5b_DodgeTravelsExactDistance()
{
    FScenario S;
    S.Rules.ArenaMinX = -700.0f;
    S.Rules.ArenaMaxX = 700.0f;
    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);

    const float Before = Sim.GetFighter(ESide::B).PositionX;
    CheckNear(Before, 580.0f, 0.001f, "B starts at ArenaMaxX - 120 in the wide ring");

    FInputState Dodge;
    Dodge.bDodgePressed = true;
    Dodge.bMoveLeft = true; // slip inward so the ring bounds do not clip the motion
    RunTicks(Sim, 1, FInputState(), Dodge);

    for (int32_t i = 0; i < 60 && Sim.GetFighter(ESide::B).State == EFighterState::Dodging; ++i)
    {
        Sim.StepFixed(FInputState(), FInputState());
    }

    CheckNear(Sim.GetFighter(ESide::B).PositionX, Before - 130.0f, 0.01f,
              "the slip travels exactly DodgeDistance (130 cm)");
    CheckFighterState(Sim, ESide::B, EFighterState::Idle, "the slip finishes back in the idle state");
    CheckFighterState(Sim, ESide::A, EFighterState::Idle, "A was not involved and stays idle");
}

// ---------------------------------------------------------------------------
// T6 - stamina gating and exhaustion
// ---------------------------------------------------------------------------

static void Test6_StaminaGatingAndExhaustion()
{
    FScenario S;
    S.A.MaxStamina = 9.0f;
    S.A.StaminaRegenPerSecond = 0.0f;
    S.A.BlockStaminaRegenPerSecond = 0.0f;
    S.A.BlockHoldDrainPerSecond = 0.0f;
    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    CheckNear(Sim.GetFighter(ESide::A).Stamina, 9.0f, 0.001f, "A starts on a small tank");

    Sim.StepFixed(InJab(), FInputState());
    CheckInt(Sim.GetFighter(ESide::A).AttacksThrownTotal, 1, "the first jab is allowed (cost 9)");
    CheckNear(Sim.GetFighter(ESide::A).Stamina, 0.0f, 0.001f, "the tank is empty after the jab");
    const FEventTally ExhaustTally = Drain(Sim);
    CheckInt(ExhaustTally.Get(ECombatEvent::StaminaExhausted), 1, "StaminaExhausted is reported");

    RunTicks(Sim, 30); // let the punch finish
    Drain(Sim);

    Sim.StepFixed(InJab(), FInputState());
    CheckInt(Sim.GetFighter(ESide::A).AttacksThrownTotal, 1, "a jab with no stamina is refused");
    CheckFighterState(Sim, ESide::A, EFighterState::Idle, "a refused jab leaves the fighter idle");

    Sim.StepFixed(InHeavy(), FInputState());
    CheckInt(Sim.GetFighter(ESide::A).AttacksThrownTotal, 1, "a heavy punch with no stamina is refused");

    FInputState Dodge;
    Dodge.bDodgePressed = true;
    Sim.StepFixed(Dodge, FInputState());
    CheckInt(Sim.GetFighter(ESide::A).DodgesThisRound, 0, "a slip without stamina is refused");
    RunTicks(Sim, 5);
    Check(Sim.GetFighter(ESide::A).State != EFighterState::Dodging,
          "the refused dodge press was consumed, not buffered for later");
}

// ---------------------------------------------------------------------------
// T7 - knockdown, get-up, and the get-up invulnerability window
// ---------------------------------------------------------------------------

static void Test7_KnockdownGetUpAndInvulnerability()
{
    FScenario S;
    S.B.MaxHealth = 70.0f;      // 15 damage, then a 20% canvas restore (14) leaves 69
    S.B.MaxStamina = 15.0f;     // below the knockdown stamina threshold (18)
    S.B.StaminaRegenPerSecond = 0.0f;
    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    int32_t Startup = 0, Active = 0, Recovery = 0;
    FCombatSim::GetAttackWindow(Sim.GetConfig(ESide::A).HeavyPunch, Startup, Active, Recovery);
    CheckInt(Startup, HeavyStartupTicks, "heavy punch startup is 13 ticks (0.217 s)");
    CheckInt(Active, HeavyActiveTicks, "heavy punch active window is 6 ticks (0.100 s)");
    CheckInt(Recovery, HeavyRecoveryTicks, "heavy punch recovery is 24 ticks (0.400 s)");

    Sim.StepFixed(InHeavy(), FInputState());
    RunTicks(Sim, HeavyStartupTicks - 1);
    CheckNear(Sim.GetFighter(ESide::B).Health, 70.0f, 0.001f, "no damage during the heavy startup");
    RunTicks(Sim, 1); // first active tick

    CheckFighterState(Sim, ESide::B, EFighterState::KnockedDown,
                      "a heavy punch on a gassed defender puts them on the canvas");
    CheckNear(Sim.GetFighter(ESide::B).Health, 69.0f, 0.001f,
              "15 damage, then a 20% canvas restore (14)");
    // The canvas restores 35 stamina, but a tank can never exceed its own
    // maximum: 15 + 35 clamps to MaxStamina (15).
    CheckNear(Sim.GetFighter(ESide::B).Stamina, 15.0f, 0.001f,
              "the canvas stamina restore is capped by MaxStamina");
    CheckInt(Sim.GetFighter(ESide::B).KnockdownsThisRound, 1, "one knockdown this round");
    CheckInt(Sim.GetFighter(ESide::B).KnockdownsTotal, 1, "one knockdown in the match");
    CheckInt(Sim.GetFighter(ESide::B).StateTicksRemaining, 132, "the knockdown lasts 132 ticks (2.2 s)");

    const FEventTally KnockdownTally = Drain(Sim);
    CheckInt(KnockdownTally.Get(ECombatEvent::Knockdown), 1, "one Knockdown event");
    CheckInt(KnockdownTally.Get(ECombatEvent::AttackHit), 1, "one AttackHit event");

    // Close the distance while B is down so the next test punch is in range.
    RunTicks(Sim, 100, InWalkRight(), FInputState());
    RunTicks(Sim, 32); // B's count expires during this window

    CheckFighterState(Sim, ESide::B, EFighterState::Idle, "the fighter is up after the count");
    CheckInt(Sim.GetFighter(ESide::B).GetUpsThisRound, 1, "one get-up is counted");
    Check(Sim.GetFighter(ESide::B).bInvulnerable, "the get-up grants invulnerability");
    CheckInt(Sim.GetFighter(ESide::B).InvulnTicksRemaining, 30, "the get-up window is 30 ticks (0.5 s)");
    const FEventTally UpTally = Drain(Sim);
    CheckInt(UpTally.Get(ECombatEvent::GetUp), 1, "one GetUp event");

    const float HealthOnGetUp = Sim.GetFighter(ESide::B).Health;
    const float Separation = std::fabs(Sim.GetFighter(ESide::B).PositionX -
                                        Sim.GetFighter(ESide::A).PositionX);
    Check(Separation < Sim.GetConfig(ESide::A).HeavyPunch.ReachFar,
          "A is inside heavy-punch range for the invulnerability check");

    // Punch straight into the get-up window: it must not land.
    Sim.StepFixed(InHeavy(), FInputState());
    RunTicks(Sim, HeavyStartupTicks + 3);
    CheckNear(Sim.GetFighter(ESide::B).Health, HealthOnGetUp, 0.001f,
              "the get-up window makes the fighter untouchable");
    CheckInt(Sim.GetFighter(ESide::B).KnockdownsThisRound, 1, "no second knockdown through the get-up window");
    const FEventTally InvulnTally = Drain(Sim);
    CheckInt(InvulnTally.Get(ECombatEvent::AttackHit), 0, "no hit registers during the get-up window");

    // Once the window expires the same punch lands normally. The defender is
    // still gassed (stamina 15, under the 18 stamina threshold), so this punch
    // puts them down again and the canvas restore is applied a second time.
    RunTicks(Sim, 40);
    Sim.StepFixed(InHeavy(), FInputState());
    RunTicks(Sim, HeavyStartupTicks + 3);
    CheckNear(Sim.GetFighter(ESide::B).Health, 68.0f, 0.001f,
              "after the get-up window the heavy punch lands for 15 damage (69 - 15 + 14)");
    CheckFighterState(Sim, ESide::B, EFighterState::KnockedDown,
                      "a gassed defender goes down again: stamina 15 is under the threshold");
    CheckInt(Sim.GetFighter(ESide::B).KnockdownsThisRound, 2,
             "two knockdowns, still short of the three-knockdown rule");
    CheckInt(Sim.GetFighter(ESide::B).KnockdownsTotal, 2, "two knockdowns in the match");
    CheckNear(Sim.GetFighter(ESide::B).Stamina, 15.0f, 0.001f,
              "the canvas stamina restore is capped by MaxStamina again");
}

// ---------------------------------------------------------------------------
// Part 2 - knockout, the three-knockdown rule, judging and scorecards
// ---------------------------------------------------------------------------

static FInputState InWalkLeft()
{
    FInputState I;
    I.bMoveLeft = true;
    return I;
}

/** Adds one test's event tally into a running total (used by multi-stage tests). */
static void MergeTally(FEventTally& Into, const FEventTally& From)
{
    for (int32_t i = 0; i < FEventTally::Slots; ++i)
    {
        Into.Counts[i] += From.Counts[i];
        Into.LastValue[i] = From.LastValue[i];
    }
}

/** Runs (stepping idle inputs) until the given round is live. */
static bool RunUntilRound(FCombatSim& Sim, int32_t Round, int32_t MaxTicks = 1200)
{
    for (int32_t i = 0; i < MaxTicks; ++i)
    {
        if (Sim.IsFighting() && Sim.GetMatch().Round >= Round)
        {
            return true;
        }
        Sim.StepFixed(FInputState(), FInputState());
    }
    return Sim.IsFighting() && Sim.GetMatch().Round >= Round;
}

// ---------------------------------------------------------------------------
// T8 - only the heavy punch can put an opponent on the canvas
// ---------------------------------------------------------------------------

static void Test8_HeavyPunchIsTheOnlyKnockdown()
{
    FScenario S;
    S.A.StaminaRegenPerSecond = 0.0f; // exact stamina arithmetic for the cost checks
    S.B.MaxStamina = 5.0f;            // permanently under the 18 stamina threshold
    S.B.StaminaRegenPerSecond = 0.0f;
    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    // The jab is fast and cheap but bCanKnockdown is false, so even a defender
    // sitting on 5 stamina stays on their feet.
    Sim.StepFixed(InJab(), FInputState());
    RunTicks(Sim, JabStartupTicks + 1);
    CheckFighterState(Sim, ESide::B, EFighterState::HitStun, "a jab stuns but never knocks down");
    CheckNear(Sim.GetFighter(ESide::B).Health, 94.0f, 0.001f, "the jab deals its documented 6 damage");
    CheckInt(Sim.GetFighter(ESide::B).KnockdownsThisRound, 0, "a jab cannot knock a gassed defender down");
    CheckNear(Sim.GetFighter(ESide::A).Stamina, 91.0f, 0.001f, "the jab costs 9 stamina");

    // The heavy punch is the knockdown punch: 15 damage, and the defender goes
    // down because 5 stamina is at or below the 18 stamina threshold.
    CloseDistanceA(Sim, 100.0f, 200);
    Sim.StepFixed(InHeavy(), FInputState());
    RunTicks(Sim, HeavyStartupTicks + 1);
    CheckFighterState(Sim, ESide::B, EFighterState::KnockedDown, "the heavy punch is the knockdown punch");
    CheckNear(Sim.GetFighter(ESide::B).Health, 99.0f, 0.001f,
              "15 damage, then a 20% canvas restore (94 - 15 + 20)");
    CheckInt(Sim.GetFighter(ESide::B).KnockdownsThisRound, 1, "the heavy punch scores the knockdown");
    CheckNear(Sim.GetFighter(ESide::A).Stamina, 71.0f, 0.001f, "the heavy punch costs 20 stamina");
    CheckInt(Sim.GetFighter(ESide::B).HitsTakenTotal, 2, "the defender has taken two punches");

    const FEventTally Tally = Drain(Sim);
    CheckInt(Tally.Get(ECombatEvent::Knockdown), 1, "one Knockdown event");
    CheckInt(Tally.Get(ECombatEvent::AttackHit), 2, "two AttackHit events");
    CheckInt(Tally.Get(ECombatEvent::AttackWhiffed), 0, "neither punch whiffed");
}

// ---------------------------------------------------------------------------
// T9 - knockout: the fight ends the moment a fighter is out on damage
// ---------------------------------------------------------------------------

static void Test9_KnockoutByDamage()
{
    FScenario S;
    S.B.MaxHealth = 6.0f; // one clean jab finishes it: 6 damage is a full tank
    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);
    CheckNear(Sim.GetFighter(ESide::B).Health, 6.0f, 0.001f, "B starts on a 6 HP budget");

    Sim.StepFixed(InJab(), FInputState());
    RunTicks(Sim, JabStartupTicks + 1);

    const FMatchState& M = Sim.GetMatch();
    Check(Sim.IsMatchOver(), "the knockout ends the match immediately, without waiting for the bell");
    CheckInt(static_cast<int32_t>(M.Phase), static_cast<int32_t>(EMatchPhase::MatchEnd), "phase is MatchEnd");
    CheckInt(static_cast<int32_t>(M.EndReason), static_cast<int32_t>(EMatchEndReason::Knockout), "the end reason is Knockout");
    CheckInt(static_cast<int32_t>(M.Result), static_cast<int32_t>(EMatchResult::FighterAWins), "A wins by knockout");
    CheckInt(static_cast<int32_t>(M.LastRoundEndReason), static_cast<int32_t>(ERoundEndReason::Knockout),
             "the round ended in a knockout");
    CheckInt(M.ScoreA, 10, "the knockout round is scored 10-9 to A");
    CheckInt(M.ScoreB, 9, "the knockout round is scored 10-9 to A");
    CheckNear(Sim.GetFighter(ESide::B).Health, 0.0f, 0.001f, "the beaten fighter is on 0 health");
    CheckFighterState(Sim, ESide::B, EFighterState::KnockedDown, "a counted-out fighter stays on the canvas");
    CheckFighterState(Sim, ESide::A, EFighterState::Victory, "the winner is in the victory state");

    const FEventTally Tally = Drain(Sim);
    CheckInt(Tally.Get(ECombatEvent::Knockout), 1, "one Knockout event");
    CheckInt(Tally.Get(ECombatEvent::AttackHit), 1, "the finishing punch is reported as a hit");
    CheckInt(Tally.Get(ECombatEvent::MatchEnded), 1, "one MatchEnded event");
    CheckInt(Tally.Get(ECombatEvent::RoundEnded), 0,
             "a knockout is not a judges' decision, so no RoundEnded event is produced");

    // The bell must not restart anything once the bout is over.
    const int32_t ScoreA = M.ScoreA;
    RunTicks(Sim, 300);
    Check(Sim.IsMatchOver(), "the match stays over after the post-fight window");
    CheckInt(Sim.GetMatch().ScoreA, ScoreA, "the result cannot be re-scored after the final bell");
}

// ---------------------------------------------------------------------------
// T10 - the guard: chip damage cannot be avoided, and can end a fight
// ---------------------------------------------------------------------------

static void Test10_GuardChipAndChipKnockout()
{
    FScenario S;
    S.B.MaxHealth = 3.0f; // a blocked heavy punch chips 3 (15 * 0.20)
    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    // Hold the guard for the whole exchange.
    Sim.StepFixed(InHeavy(), InBlock());
    CheckFighterState(Sim, ESide::B, EFighterState::Blocking, "the guard goes up on the same tick");
    RunTicks(Sim, HeavyStartupTicks + 1, FInputState(), InBlock());

    const FEventTally Tally = Drain(Sim);
    CheckInt(Tally.Get(ECombatEvent::AttackBlocked), 1, "the heavy punch is reported as blocked");
    CheckInt(Tally.Get(ECombatEvent::GuardBroken), 0, "a full stamina tank is not broken by one punch");
    CheckInt(Tally.Get(ECombatEvent::AttackWhiffed), 0, "a blocked punch is not a whiff");
    CheckInt(Tally.Get(ECombatEvent::Knockout), 1, "chip damage can finish a fight");
    CheckNear(Tally.ValueOf(ECombatEvent::AttackBlocked), 3.0f, 0.001f, "chip damage is 20% of 15");

    CheckNear(Sim.GetFighter(ESide::B).Health, 0.0f, 0.001f, "the chip damage took the last 3 health");
    CheckInt(Sim.GetFighter(ESide::B).BlocksThisRound, 1, "one block is recorded");
    CheckInt(Sim.GetFighter(ESide::A).HitsLandedTotal, 0,
             "a blocked punch is NOT a landed punch, so scorecards cannot be inflated by chip");
    CheckNear(Sim.GetFighter(ESide::A).DamageDealtTotal, 3.0f, 0.001f, "chip damage still counts as damage dealt");
    CheckFighterState(Sim, ESide::B, EFighterState::KnockedDown, "the beaten fighter goes to the canvas");
    CheckInt(static_cast<int32_t>(Sim.GetMatch().EndReason), static_cast<int32_t>(EMatchEndReason::Knockout),
             "the fight ends as a knockout");
    CheckInt(static_cast<int32_t>(Sim.GetMatch().Result), static_cast<int32_t>(EMatchResult::FighterAWins),
             "the chip knockout is awarded to the fighter who threw the punch");
    CheckFighterState(Sim, ESide::A, EFighterState::Victory, "the winner is in the victory state");
}
/** Steps until the given fighter is back in Idle (its attack or stun finished). */
static void WaitFighterIdle(FCombatSim& Sim, ESide Side, int32_t MaxTicks = 180)
{
    for (int32_t i = 0; i < MaxTicks && Sim.GetFighter(Side).State != EFighterState::Idle; ++i)
    {
        Sim.StepFixed(FInputState(), FInputState());
    }
}

// ---------------------------------------------------------------------------
// T11 - the three-knockdown rule stops the contest
// ---------------------------------------------------------------------------

static void Test11_ThreeKnockdownTechnicalKnockout()
{
    FScenario S;
    S.Rules.Rounds = 1;
    // B runs on an 18-stamina tank and never regenerates, so every clean heavy
    // punch lands at (or below) the 18-stamina knockdown threshold. The 35
    // stamina a knockdown restores is clamped straight back to 18, so the next
    // heavy punch knocks B down again.
    S.B.MaxStamina = 18.0f;
    S.B.StaminaRegenPerSecond = 0.0f;
    // Knockback is zeroed so the pair keeps its 80 cm distance and all three
    // punches reach; the rule under test is the knockdown count, not footwork.
    S.A.HeavyPunch.Knockback = 0.0f;

    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    CheckNear(Sim.GetFighter(ESide::B).Stamina, 18.0f, 0.001f,
              "B starts the round on an 18-stamina tank");

    for (int32_t Trip = 0; Trip < 3; ++Trip)
    {
        WaitFighterIdle(Sim, ESide::A);
        // Wait for B to be back on its feet and past the get-up invulnerability.
        int32_t Guard = 0;
        while (Guard++ < 600 &&
               (Sim.GetFighter(ESide::B).State == EFighterState::KnockedDown ||
                Sim.GetFighter(ESide::B).bInvulnerable))
        {
            Sim.StepFixed(FInputState(), FInputState());
        }
        Sim.StepFixed(InHeavy(), FInputState());
        RunTicks(Sim, HeavyStartupTicks + HeavyActiveTicks + 2);
        CheckInt(Sim.GetFighter(ESide::B).KnockdownsThisRound, Trip + 1,
                 "the heavy punch puts B down once more");
    }

    const FMatchState& M = Sim.GetMatch();
    CheckInt(Sim.GetFighter(ESide::B).KnockdownsThisRound, 3, "B has been knocked down three times");
    Check(Sim.IsMatchOver(), "three knockdowns in one round stop the contest");
    CheckInt(static_cast<int32_t>(M.EndReason), static_cast<int32_t>(EMatchEndReason::TechnicalKnockout),
             "a three-knockdown stoppage is a technical knockout");
    CheckInt(static_cast<int32_t>(M.LastRoundEndReason), static_cast<int32_t>(ERoundEndReason::KnockdownLimit),
             "the round ends on the knockdown limit");
    CheckInt(static_cast<int32_t>(M.Result), static_cast<int32_t>(EMatchResult::FighterAWins),
             "A is awarded the stoppage");
    CheckInt(static_cast<int32_t>(M.LastRoundWinner), static_cast<int32_t>(ESide::A),
             "the round is awarded to A as well");
    CheckNear(Sim.GetFighter(ESide::B).Health, 100.0f, 0.001f,
              "each knockdown's 20% health restore cancels the 15 damage, so B is never knocked out");
    CheckInt(Sim.GetFighter(ESide::A).AttacksThrownTotal, 3, "A threw exactly three heavy punches");
    CheckFighterState(Sim, ESide::A, EFighterState::Victory, "A is the winner");
    CheckFighterState(Sim, ESide::B, EFighterState::KnockedDown, "B is left on the canvas");

    const FEventTally T = Drain(Sim);
    CheckInt(T.Get(ECombatEvent::Knockdown), 3, "three knockdown events were raised");
    CheckInt(T.Get(ECombatEvent::Knockout), 1, "the stoppage raises exactly one knockout event");
}

// ---------------------------------------------------------------------------
// T12 - scorecards: the 10-point must system across three rounds
// ---------------------------------------------------------------------------

static void Test12_ScorecardsBetweenRoundRestoreAndDecision()
{
    FScenario S;
    S.Rules.Rounds = 3;
    S.Rules.RoundSeconds = 2.0f; // short rounds keep the harness fast
    S.A.StaminaRegenPerSecond = 0.0f;
    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    // --- Round 1: A lands two jabs, B never answers. ----------------------
    Sim.StepFixed(InJab(), FInputState());
    RunTicks(Sim, JabStartupTicks + 1);
    CloseDistanceA(Sim, 95.0f, 200); // the jab's knockback pushes B out of range
    Sim.StepFixed(InJab(), FInputState());
    RunTicks(Sim, JabStartupTicks + 1);

    CheckNear(Sim.GetFighter(ESide::B).Health, 88.0f, 0.001f, "two jabs deal 12 damage in round 1");
    CheckInt(Sim.GetFighter(ESide::A).HitsLandedThisRound, 2, "two punches landed in round 1");
    CheckInt(Sim.GetMatch().Round, 1, "the first round is still running");
    CheckNear(Sim.GetFighter(ESide::A).Stamina, 82.0f, 0.001f, "A spent 18 stamina on two jabs");
    Drain(Sim);

    Check(RunUntilRound(Sim, 2), "the bout moves on to round 2 after the bell and the break");
    CheckInt(Sim.GetMatch().ScoreA, 10, "round 1 is scored 10-9 to A (damage and punches landed)");
    CheckInt(Sim.GetMatch().ScoreB, 9, "round 1 is scored 10-9 to A");
    CheckNear(Sim.GetFighter(ESide::A).Stamina, 100.0f, 0.001f,
              "the corner restores stamina between rounds (A was on 82)");
    CheckNear(Sim.GetFighter(ESide::B).Health, 100.0f, 0.001f,
              "the corner restores 15% health (88 + 15 clamps to the 100 maximum)");
    CheckNear(Sim.GetFighter(ESide::B).Stamina, 100.0f, 0.001f, "B's tank is full again");
    CheckInt(Sim.GetFighter(ESide::A).HitsLandedThisRound, 0, "per-round statistics are reset for the new round");
    CheckNear(Sim.GetFighter(ESide::A).DamageDealtThisRound, 0.0f, 0.001f, "per-round damage is reset");
    CheckInt(Sim.GetFighter(ESide::A).HitsLandedTotal, 2, "match totals survive the round reset");
    CheckNear(Sim.GetFighter(ESide::A).DamageDealtTotal, 12.0f, 0.001f, "match damage totals survive the round reset");

    // --- Round 2: B lands one jab, A never answers. ------------------------
    Sim.StepFixed(FInputState(), InJab());
    RunTicks(Sim, JabStartupTicks + 1);
    CheckInt(Sim.GetFighter(ESide::A).HitsTakenThisRound, 1, "B lands a jab in round 2");
    CheckNear(Sim.GetFighter(ESide::A).Health, 94.0f, 0.001f, "B's jab deals 6");
    Drain(Sim);

    Check(RunUntilRound(Sim, 3), "the bout moves on to round 3");
    CheckInt(Sim.GetMatch().ScoreA, 19, "A: 10 then 9 is 19 on the cards");
    CheckInt(Sim.GetMatch().ScoreB, 19, "B: 9 then 10 is 19 on the cards");
    CheckNear(Sim.GetFighter(ESide::A).Health, 100.0f, 0.001f, "A is patched up before round 3");

    // --- Round 3: A lands a jab and takes the fight on the cards. ----------
    Sim.StepFixed(InJab(), FInputState());
    RunTicks(Sim, JabStartupTicks + 1);
    RunToMatchEnd(Sim, 1200);

    const FMatchState& M = Sim.GetMatch();
    Check(Sim.IsMatchOver(), "the fight is over after the final bell");
    CheckInt(static_cast<int32_t>(M.EndReason), static_cast<int32_t>(EMatchEndReason::Decision),
             "three completed rounds are decided on the cards");
    CheckInt(static_cast<int32_t>(M.Result), static_cast<int32_t>(EMatchResult::FighterAWins), "A wins 29-28");
    CheckInt(M.ScoreA, 29, "29 on A's card");
    CheckInt(M.ScoreB, 28, "28 on B's card");
    CheckInt(static_cast<int32_t>(M.LastRoundEndReason), static_cast<int32_t>(ERoundEndReason::TimeExpired),
             "the last round ended on the clock");
    CheckInt(static_cast<int32_t>(M.LastRoundWinner), static_cast<int32_t>(ESide::A), "A took the last round");
    CheckFighterState(Sim, ESide::A, EFighterState::Victory, "the winner is in the victory state");
    CheckFighterState(Sim, ESide::B, EFighterState::Defeated, "a beaten fighter who is still standing is Defeated");
    CheckNear(Sim.GetFighter(ESide::B).Health, 94.0f, 0.001f, "B ends the fight on 94 health");
    CheckNear(Sim.GetTotalDamageDealt(ESide::A), 18.0f, 0.001f, "A dealt 12 + 6 damage across the fight");
    CheckNear(Sim.GetTotalDamageDealt(ESide::B), 6.0f, 0.001f, "B dealt 6 damage across the fight");
}

// ---------------------------------------------------------------------------
// T13 - an even contest is a draw, not a silent win
// ---------------------------------------------------------------------------

static void Test13_EvenContestIsADrawDecision()
{
    FScenario S;
    S.Rules.Rounds = 2;
    S.Rules.RoundSeconds = 1.5f;
    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    CheckNear(Sim.GetRoundTimeFraction(), 1.0f, 0.001f, "the round clock starts full on the bell");
    RunTicks(Sim, 30);
    CheckNear(Sim.GetRoundTimeFraction(), 60.0f / 90.0f, 0.02f, "half a second off a 1.5 s round clock");
    Check(Sim.GetMatch().Phase == EMatchPhase::Fighting, "the round is still live");

    RunToMatchEnd(Sim, 900);

    const FMatchState& M = Sim.GetMatch();
    Check(Sim.IsMatchOver(), "an uneventful match still reaches a proper conclusion");
    CheckInt(static_cast<int32_t>(M.EndReason), static_cast<int32_t>(EMatchEndReason::DrawDecision),
             "level cards produce a draw decision");
    CheckInt(static_cast<int32_t>(M.Result), static_cast<int32_t>(EMatchResult::Draw), "the result is a draw");
    CheckInt(M.ScoreA, 20, "two even rounds are each scored 10-10");
    CheckInt(M.ScoreB, 20, "two even rounds are each scored 10-10");
    CheckFighterState(Sim, ESide::A, EFighterState::Idle, "a drawn fight leaves A standing");
    CheckFighterState(Sim, ESide::B, EFighterState::Idle, "a drawn fight leaves B standing");
    CheckInt(Sim.GetFighter(ESide::A).AttacksThrownTotal, 0, "nobody threw a punch");
    CheckNear(Sim.GetFighter(ESide::A).Health, 100.0f, 0.001f, "nobody took damage");
}

// ===========================================================================
// PART 3 - fixed-timestep contract, counter hits, judging order, arena bounds,
//          event-queue bounding, whiff reporting and training aids.
//
// Every expected number below was MEASURED first with a throwaway calibration
// probe (Tools/CombatCoreTests/probe_part3_calibration.cpp) so these tests
// assert verified behaviour rather than guesses. Where a scenario needs the two
// fighters to hold a fixed distance, knockback is zeroed on both and stamina
// regeneration is switched off; those tweaks are noted at the point of use and
// change none of the rules under test.
// ===========================================================================

/**
 * Zeroes knockback on both fighters so a scripted exchange keeps its range.
 * Knockback is a physical/presentation effect, not a judging input, so this
 * isolates the rule under test without weakening it.
 */
static void PinThePair(FScenario& S)
{
    S.A.Jab.Knockback = 0.0f;
    S.A.HeavyPunch.Knockback = 0.0f;
    S.B.Jab.Knockback = 0.0f;
    S.B.HeavyPunch.Knockback = 0.0f;
}

// ---------------------------------------------------------------------------
// T14 - Advance() timestep contract: 1/60 s ticks, a 0.25 s delta clamp, and a
//       simulation outcome that does not depend on the caller's frame rate
// ---------------------------------------------------------------------------

static void Test14_AdvanceTimestepContract()
{
    CheckNear(FCombatSim::FixedTickSeconds, 1.0f / 60.0f, 1e-9f,
              "the simulation timestep is fixed at 1/60 s");
    CheckNear(FCombatSim::MaxTickDeltaSeconds, 0.25f, 1e-9f,
              "one frame may represent at most 0.25 s of simulation");

    // A single frame is clamped to 0.25 s => exactly 15 fixed ticks (0.25 * 60).
    {
        FScenario S;
        FCombatSim Sim;
        Sim.Init(S.Rules, S.A, S.B);
        Sim.Advance(10.0f, FInputState(), FInputState());
        CheckInt(Sim.GetMatch().TickCount, 15,
                 "Advance(10 s) is clamped to 0.25 s, i.e. exactly 15 ticks");
        Sim.Advance(10.0f, FInputState(), FInputState());
        CheckInt(Sim.GetMatch().TickCount, 30, "a second clamped frame adds 15 more ticks");
        Sim.Advance(1.0e9f, FInputState(), FInputState());
        CheckInt(Sim.GetMatch().TickCount, 45, "an absurd delta is clamped exactly the same way");
        Sim.Advance(FCombatSim::FixedTickSeconds, FInputState(), FInputState());
        CheckInt(Sim.GetMatch().TickCount, 46, "one exact timestep advances exactly one tick");
    }

    // Sub-tick deltas accumulate in the leftover instead of being discarded.
    {
        FScenario S;
        FCombatSim Sim;
        Sim.Init(S.Rules, S.A, S.B);
        const float D = 1.0f / 240.0f;
        for (int32_t i = 0; i < 4; ++i) Sim.Advance(D, FInputState(), FInputState());
        CheckInt(Sim.GetMatch().TickCount, 1, "four 1/240 s deltas accumulate into one tick");
        for (int32_t i = 0; i < 4; ++i) Sim.Advance(D, FInputState(), FInputState());
        CheckInt(Sim.GetMatch().TickCount, 2, "eight 1/240 s deltas give two ticks");
        for (int32_t i = 0; i < 2; ++i) Sim.Advance(D, FInputState(), FInputState());
        CheckInt(Sim.GetMatch().TickCount, 2, "ten 1/240 s deltas still give two ticks");
        for (int32_t i = 0; i < 20; ++i) Sim.Advance(D, FInputState(), FInputState());
        CheckInt(Sim.GetMatch().TickCount, 7,
                 "thirty 1/240 s deltas give seven ticks: the leftover is carried, not lost");
    }

    // Frame-rate independence: 5 s of walking must end in the identical place no
    // matter how the caller chops the wall-clock time up.
    {
        FScenario S;
        const int32_t RefTicks = 300; // 5.0 s at 60 Hz
        FCombatSim Ref;
        Ref.Init(S.Rules, S.A, S.B);
        for (int32_t i = 0; i < RefTicks; ++i) Ref.StepFixed(InWalkRight(), InWalkLeft());
        const float RefAx = Ref.GetFighter(ESide::A).PositionX;
        const float RefBx = Ref.GetFighter(ESide::B).PositionX;

        struct FRate { float Delta; int32_t Calls; const char* Label; };
        const FRate Rates[4] = {
            { 1.0f / 60.0f,  300, "300 Advance(1/60) calls" },
            { 1.0f / 30.0f,  150, "150 Advance(1/30) calls" },
            { 1.0f / 120.0f, 600, "600 Advance(1/120) calls" },
            { 1.0f / 15.0f,   75, "75 Advance(1/15) calls" },
        };
        for (int32_t r = 0; r < 4; ++r)
        {
            FCombatSim Sim;
            Sim.Init(S.Rules, S.A, S.B);
            for (int32_t i = 0; i < Rates[r].Calls; ++i)
            {
                Sim.Advance(Rates[r].Delta, InWalkRight(), InWalkLeft());
            }
            CheckInt(Sim.GetMatch().TickCount, RefTicks, Rates[r].Label);
            CheckNear(Sim.GetFighter(ESide::A).PositionX, RefAx, 1e-4f,
                      "A ends in the identical place regardless of frame rate");
            CheckNear(Sim.GetFighter(ESide::B).PositionX, RefBx, 1e-4f,
                      "B ends in the identical place regardless of frame rate");
        }
    }

    // Advance(1/60) is exactly StepFixed, including the anti-double-fire rule
    // that a button edge is consumed by only one tick per frame.
    {
        FScenario S;
        FCombatSim A1, A2;
        A1.Init(S.Rules, S.A, S.B);
        A2.Init(S.Rules, S.A, S.B);
        RunUntilFighting(A1);
        RunUntilFighting(A2);
        for (int32_t t = 0; t < 400; ++t)
        {
            const FInputState IA = ((t % 37) == 0) ? InJab()
                                  : (((t % 37) == 11) ? InWalkRight() : FInputState());
            const FInputState IB = ((t % 53) == 0) ? InBlock() : FInputState();
            A1.StepFixed(IA, IB);
            A2.Advance(FCombatSim::FixedTickSeconds, IA, IB);
        }
        CheckInt(A1.GetMatch().TickCount, A2.GetMatch().TickCount,
                 "StepFixed and Advance(1/60) advance the same number of ticks");
        CheckNear(A1.GetFighter(ESide::B).Health, A2.GetFighter(ESide::B).Health, 1e-6f,
                  "StepFixed and Advance(1/60) leave B on identical health");
        CheckNear(A1.GetFighter(ESide::A).Health, A2.GetFighter(ESide::A).Health, 1e-6f,
                  "StepFixed and Advance(1/60) leave A on identical health");
    }
}

// ---------------------------------------------------------------------------
// T15 - counter hits: a clean counter does 1.6x damage, and knocks down a
//       defender whose stamina at impact is at or below the counter threshold
// ---------------------------------------------------------------------------

struct FCounterCase
{
    const char*   Label;
    float         BMaxStamina;   // B spends 9 on the jab it is throwing, so the
                                 // stamina at the moment of impact is this minus 9
    bool          bBJabs;        // true catches B in attack startup => a counter
    float         ExpectDamage;
    bool          bExpectKnockdown;
    EFighterState ExpectState;
    float         ExpectHealth;
};

static void RunCounterCaseAndCheck(const FCounterCase& C)
{
    FScenario S;
    S.A.StaminaRegenPerSecond = 0.0f; // keep the stamina at impact exact
    S.B.MaxStamina = C.BMaxStamina;
    S.B.StaminaRegenPerSecond = 0.0f;

    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    // A commits to a heavy punch. Eight ticks later B either starts its own jab
    // (caught in startup => clean counter) or stands still (not a counter).
    Sim.StepFixed(InHeavy(), FInputState());

    // Drain A's own attack start immediately: it is the only AttackStarted event
    // in the queue at this point, so its payload is unambiguous. (Draining at the
    // end of the exchange would instead report B's jab start, because the tally
    // keeps the LAST payload seen for each event type.)
    {
        const FEventTally T0 = Drain(Sim);
        CheckInt(T0.Get(ECombatEvent::AttackStarted), 1,
                 "the heavy punch announces exactly one attack start");
        CheckNear(T0.ValueOf(ECombatEvent::AttackStarted), 43.0f, 0.001f,
                  "AttackStarted reports the heavy punch's 43-tick total (13+6+24)");
    }

    RunTicks(Sim, 8);
    Sim.StepFixed(FInputState(), C.bBJabs ? InJab() : FInputState());
    RunTicks(Sim, 3);

    // Confirm the setup really is what the case claims before asserting the hit.
    CheckFighterState(Sim, ESide::B, C.bBJabs ? EFighterState::AttackStartup : EFighterState::Idle,
                      C.Label);

    // The heavy punch's first active tick: the hit lands here.
    Sim.StepFixed(FInputState(), FInputState());

    CheckNear(Sim.GetFighter(ESide::A).DamageDealtThisRound, C.ExpectDamage, 0.001f, C.Label);
    CheckInt(Sim.GetFighter(ESide::B).KnockdownsThisRound, C.bExpectKnockdown ? 1 : 0, C.Label);
    CheckFighterState(Sim, ESide::B, C.ExpectState, C.Label);
    CheckNear(Sim.GetFighter(ESide::B).Health, C.ExpectHealth, 0.001f, C.Label);

    const FEventTally T = Drain(Sim);
    CheckInt(T.Get(ECombatEvent::AttackHit), 1, "the heavy punch reports exactly one landing");
    CheckNear(T.ValueOf(ECombatEvent::AttackHit), C.ExpectDamage, 0.001f,
              "the AttackHit event carries the damage that was applied");

    // The heavy punch's own start was drained before the exchange, so the only
    // attack start left is B's jab - and only if B actually threw one.
    CheckInt(T.Get(ECombatEvent::AttackStarted), C.bBJabs ? 1 : 0,
             "B's jab announces its start only in the cases where B threw it");
    if (C.bBJabs)
    {
        CheckNear(T.ValueOf(ECombatEvent::AttackStarted), 22.0f, 0.001f,
                  "the jab's attack start reports its 22-tick total (6+4+12)");
    }
}

static void Test15_CounterDamageAndCounterKnockdown()
{
    const FCounterCase Cases[3] = {
        { "counter heavy at impact stamina 33 (<= 42) deals 24 and knocks B down",
          42.0f, true,  24.0f, true,  EFighterState::KnockedDown, 96.0f },
        { "plain heavy at impact stamina 33 (> 18) deals 15 and leaves B standing",
          33.0f, false, 15.0f, false, EFighterState::HitStun,      85.0f },
        { "counter heavy at impact stamina 71 (> 42) deals 24 but leaves B standing",
          80.0f, true,  24.0f, false, EFighterState::HitStun,      76.0f },
    };
    for (int32_t i = 0; i < 3; ++i)
    {
        RunCounterCaseAndCheck(Cases[i]);
    }

    // The multipliers and thresholds the three cases above exercise.
    CheckNear(FFighterConfig().HeavyPunch.Damage, 15.0f, 1e-6f, "the heavy punch's base damage is 15");
    CheckNear(FFighterConfig().HeavyPunch.CounterDamageMultiplier, 1.6f, 1e-6f,
              "the heavy punch's counter multiplier is 1.6x");
    CheckNear(FFighterConfig().HeavyPunch.Damage * FFighterConfig().HeavyPunch.CounterDamageMultiplier,
              24.0f, 1e-4f, "15 base damage x 1.6 is the 24 damage a counter heavy deals");
    CheckNear(FMatchRules().KnockdownStaminaThreshold, 18.0f, 1e-6f,
              "the ordinary knockdown threshold is 18 stamina");
    CheckNear(FMatchRules().CounterKnockdownStaminaThreshold, 42.0f, 1e-6f,
              "the counter knockdown threshold is 42 stamina");
    CheckNear(FMatchRules().KnockdownHealthRestoreFraction, 0.20f, 1e-6f,
              "a knockdown restores 20% of max health, so 100-24+20 = the 96 observed");
    CheckNear(FMatchRules().KnockdownStaminaRestore, 35.0f, 1e-6f,
              "a knockdown restores 35 stamina (33+35 clamped to the 42 maximum)");
}

// ---------------------------------------------------------------------------
// T16 - how a round is decided: damage dealt, then punches landed, then stamina
//       remaining, and only then a genuine 10-10
// ---------------------------------------------------------------------------

// T16a: more damage wins even with fewer punches landed and worse stamina.
static void Test16a_DamageDealtIsTheFirstCriterion()
{
    FScenario S;
    S.Rules.Rounds = 1;
    S.Rules.RoundSeconds = 4.0f;
    S.A.StaminaRegenPerSecond = 0.0f;
    S.B.StaminaRegenPerSecond = 0.0f;
    PinThePair(S);

    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    // B lands two plain jabs (A is idle, so neither is a counter): 12 damage.
    for (int32_t i = 0; i < 2; ++i)
    {
        Sim.StepFixed(FInputState(), InJab());
        WaitFighterIdle(Sim, ESide::B);
        WaitFighterIdle(Sim, ESide::A);
    }
    CheckNear(Sim.GetFighter(ESide::B).DamageDealtThisRound, 12.0f, 0.001f,
              "B has landed two jabs for 12 damage");
    CheckInt(Sim.GetFighter(ESide::B).HitsLandedThisRound, 2, "B has landed two punches");

    // A answers with a single heavy punch: 15 damage from one punch.
    Sim.StepFixed(InHeavy(), FInputState());
    RunTicks(Sim, HeavyStartupTicks + HeavyActiveTicks);
    CheckNear(Sim.GetFighter(ESide::A).DamageDealtThisRound, 15.0f, 0.001f,
              "A has landed one heavy punch for 15 damage");
    CheckInt(Sim.GetFighter(ESide::A).HitsLandedThisRound, 1, "A has landed a single punch");

    Check(Sim.GetFighter(ESide::A).Stamina < Sim.GetFighter(ESide::B).Stamina,
          "A finishes the round on LESS stamina than B");

    RunToMatchEnd(Sim, 1200);

    const FMatchState& M = Sim.GetMatch();
    Check(Sim.IsMatchOver(), "the round runs out on the clock and the fight ends");
    CheckInt(M.ScoreA, 10, "15 damage beats 12: the round goes to A despite fewer punches");
    CheckInt(M.ScoreB, 9, "B is scored 9");
    CheckInt(static_cast<int32_t>(M.Result), static_cast<int32_t>(EMatchResult::FighterAWins),
             "A wins the decision");
    CheckInt(static_cast<int32_t>(M.LastRoundWinner), static_cast<int32_t>(ESide::A),
             "the round is awarded on damage dealt, not on the punch count or stamina");
    CheckInt(static_cast<int32_t>(M.LastRoundEndReason), static_cast<int32_t>(ERoundEndReason::TimeExpired),
             "the round ended on the clock");
}

// T16b: a simultaneous double knockdown ends the round 10-10 and draws the fight.
static void Test16b_DoubleKnockdownDrawsTheRound()
{
    FScenario S;
    S.Rules.Rounds = 1;
    S.A.MaxStamina = 30.0f; S.A.StaminaRegenPerSecond = 0.0f;
    S.B.MaxStamina = 30.0f; S.B.StaminaRegenPerSecond = 0.0f;

    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    // Both fighters commit to a heavy punch on the same tick. Each is caught in
    // the other's attack startup, so both land as counters for 24 damage, and
    // both are on 10 stamina at impact (30 - 20), under the 18 threshold.
    Sim.StepFixed(InHeavy(), InHeavy());
    RunTicks(Sim, 120);

    const FMatchState& M = Sim.GetMatch();
    CheckInt(Sim.GetFighter(ESide::A).KnockdownsThisRound, 1, "A is knocked down by the counter");
    CheckInt(Sim.GetFighter(ESide::B).KnockdownsThisRound, 1, "B is knocked down by the counter");
    CheckFighterState(Sim, ESide::A, EFighterState::KnockedDown, "A is on the canvas");
    CheckFighterState(Sim, ESide::B, EFighterState::KnockedDown, "B is on the canvas");
    CheckNear(Sim.GetFighter(ESide::A).Health, 96.0f, 0.001f,
              "A took 24 damage and was restored 20 on the way down");
    CheckNear(Sim.GetFighter(ESide::B).Health, 96.0f, 0.001f,
              "B took 24 damage and was restored 20 on the way down");
    CheckNear(Sim.GetFighter(ESide::A).Stamina, 30.0f, 0.001f,
              "10 stamina + 35 restored clamps to the 30 maximum");
    CheckInt(static_cast<int32_t>(M.LastRoundEndReason),
             static_cast<int32_t>(ERoundEndReason::DoubleKnockdown),
             "a simultaneous knockdown ends the round as a double knockdown");
    CheckInt(static_cast<int32_t>(M.LastRoundWinner), static_cast<int32_t>(ESide::Both),
             "neither fighter is awarded the round");
    CheckInt(M.ScoreA, 10, "a double knockdown round is scored 10-10");
    CheckInt(M.ScoreB, 10, "a double knockdown round is scored 10-10");
    Check(Sim.IsMatchOver(), "the single-round fight is over");
    CheckInt(static_cast<int32_t>(M.Result), static_cast<int32_t>(EMatchResult::Draw),
             "level cards make the fight a draw");
    CheckInt(static_cast<int32_t>(M.EndReason), static_cast<int32_t>(EMatchEndReason::DrawDecision),
             "the draw is recorded as a draw decision, never a silent win");

    const FEventTally T = Drain(Sim);
    CheckInt(T.Get(ECombatEvent::Knockdown), 2, "two knockdown events were raised");
    CheckInt(T.Get(ECombatEvent::RoundEnded), 1, "exactly one round-ended event was raised");
    CheckNear(T.ValueOf(ECombatEvent::RoundEnded), 1.0f, 0.001f, "the ended round is round 1");
    CheckInt(T.Get(ECombatEvent::MatchEnded), 1, "exactly one match-ended event was raised");
    CheckNear(T.ValueOf(ECombatEvent::MatchEnded), 3.0f, 0.001f,
              "the match-ended event carries the Draw result (3)");
}

// T16c: with damage level, the punch count decides - even though the fighter
//       with more punches has the worse stamina.
static void Test16c_PunchesLandedIsTheSecondCriterion()
{
    FScenario S;
    S.Rules.Rounds = 1;
    S.Rules.RoundSeconds = 4.0f;
    S.A.StaminaRegenPerSecond = 0.0f;
    S.B.StaminaRegenPerSecond = 0.0f;
    PinThePair(S);
    // B is durable, so A's two jabs (3 each) total the same 6 damage as B's
    // single jab (6), isolating the punch count from the damage count.
    S.B.DamageTakenMultiplier = 0.5f;

    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    Sim.StepFixed(InJab(), FInputState());          // A jab #1 -> 3 damage
    WaitFighterIdle(Sim, ESide::A);
    WaitFighterIdle(Sim, ESide::B);

    Sim.StepFixed(FInputState(), InJab());          // B jab   -> 6 damage
    WaitFighterIdle(Sim, ESide::B);
    WaitFighterIdle(Sim, ESide::A);

    Sim.StepFixed(InJab(), FInputState());          // A jab #2 -> 3 damage
    WaitFighterIdle(Sim, ESide::A);
    WaitFighterIdle(Sim, ESide::B);

    CheckNear(Sim.GetFighter(ESide::A).DamageDealtThisRound, 6.0f, 0.001f,
              "A landed two jabs for 6 damage in total (B resists half of each)");
    CheckNear(Sim.GetFighter(ESide::B).DamageDealtThisRound, 6.0f, 0.001f,
              "B landed one jab for 6 damage");
    CheckInt(Sim.GetFighter(ESide::A).HitsLandedThisRound, 2, "A landed two punches");
    CheckInt(Sim.GetFighter(ESide::B).HitsLandedThisRound, 1, "B landed one punch");
    Check(Sim.GetFighter(ESide::A).Stamina < Sim.GetFighter(ESide::B).Stamina,
          "A has the worse stamina: two jabs cost more than one");

    RunToMatchEnd(Sim, 1200);

    const FMatchState& M = Sim.GetMatch();
    CheckInt(M.ScoreA, 10, "with damage level, the punch count decides for A");
    CheckInt(M.ScoreB, 9, "B is scored 9");
    CheckInt(static_cast<int32_t>(M.Result), static_cast<int32_t>(EMatchResult::FighterAWins),
             "A wins on punches landed, before stamina is ever consulted");
}

// T16d: with damage and punches level, stamina remaining decides.
static void Test16d_StaminaRemainingIsTheThirdCriterion()
{
    FScenario S;
    S.Rules.Rounds = 1;
    S.Rules.RoundSeconds = 5.0f;
    S.A.StaminaRegenPerSecond = 0.0f;
    S.B.StaminaRegenPerSecond = 0.0f;
    PinThePair(S);

    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);

    // One jab each: damage dealt and punches landed end up exactly level.
    Sim.StepFixed(InJab(), FInputState());
    WaitFighterIdle(Sim, ESide::A);
    WaitFighterIdle(Sim, ESide::B);
    Sim.StepFixed(FInputState(), InJab());
    WaitFighterIdle(Sim, ESide::B);
    WaitFighterIdle(Sim, ESide::A);

    CheckNear(Sim.GetFighter(ESide::A).DamageDealtThisRound, 6.0f, 0.001f, "A's jab lands for 6");
    CheckNear(Sim.GetFighter(ESide::B).DamageDealtThisRound, 6.0f, 0.001f, "B's jab lands for 6");
    CheckInt(Sim.GetFighter(ESide::A).HitsLandedThisRound, 1, "A landed one punch");
    CheckInt(Sim.GetFighter(ESide::B).HitsLandedThisRound, 1, "B landed one punch");

    // A backs out of the jab's 104 cm reach and burns stamina on three punches
    // into thin air.
    for (int32_t i = 0; i < 200; ++i)
    {
        const float Sep = std::fabs(Sim.GetFighter(ESide::B).PositionX -
                                    Sim.GetFighter(ESide::A).PositionX);
        if (Sep > 110.0f)
        {
            break;
        }
        Sim.StepFixed(InWalkLeft(), FInputState());
    }
    for (int32_t i = 0; i < 3; ++i)
    {
        Sim.StepFixed(InJab(), FInputState());
        WaitFighterIdle(Sim, ESide::A);
    }

    CheckInt(Sim.GetFighter(ESide::A).HitsLandedThisRound, 1,
             "all three out-of-range jabs whiffed, so the punch count is still level");
    CheckNear(Sim.GetFighter(ESide::A).DamageDealtThisRound, 6.0f, 0.001f,
              "the whiffed jabs add no damage");
    CheckNear(Sim.GetFighter(ESide::A).Stamina, 64.0f, 0.5f,
              "A spent 9 stamina on the jab that landed plus 27 on the three whiffs");
    CheckNear(Sim.GetFighter(ESide::B).Stamina, 91.0f, 0.5f, "B spent 9 stamina on its one jab");

    RunToMatchEnd(Sim, 1200);

    const FMatchState& M = Sim.GetMatch();
    CheckInt(M.ScoreA, 9, "level damage and level punches push the round onto stamina");
    CheckInt(M.ScoreB, 10, "the fresher fighter, B, takes the round");
    CheckInt(static_cast<int32_t>(M.Result), static_cast<int32_t>(EMatchResult::FighterBWins),
             "B wins the decision on stamina remaining");
}

// ---------------------------------------------------------------------------
// T17 - the event queue is bounded; overflow drops the NEWEST records
// ---------------------------------------------------------------------------

static void Test17_EventQueueIsBoundedAndDropsTheNewest()
{
    FScenario S;
    S.Rules.RoundSeconds = 30.0f;
    S.A.MaxStamina = 10000.0f; S.A.StaminaRegenPerSecond = 0.0f;
    S.B.MaxHealth = 10000.0f;  S.B.MaxStamina = 10000.0f; S.B.StaminaRegenPerSecond = 0.0f;

    FCombatSim Sim;
    Sim.Init(S.Rules, S.A, S.B);
    RunUntilFighting(Sim);
    Drain(Sim);
    const int32_t TicksAtBell = Sim.GetMatch().TickCount;

    const int32_t Jabs = 70;
    for (int32_t j = 0; j < Jabs; ++j)
    {
        Sim.StepFixed(InJab(), FInputState());
        while (Sim.GetFighter(ESide::A).IsAttackPhase())
        {
            Sim.StepFixed(InWalkRight(), FInputState());
        }
    }

    CheckInt(Sim.GetFighter(ESide::A).AttacksThrownTotal, Jabs, "A threw 70 jabs");
    CheckInt(Sim.PendingEventCount(), FCombatSim::MaxEventQueue,
             "the queue is saturated at its 128-record bound, not grown past it");

    int32_t Counts[16] = { 0 };
    int32_t Order[300] = { 0 };
    int32_t Total = 0;
    int32_t FirstTick = -1;
    int32_t LastTick = -1;
    bool bMonotonic = true;
    FCombatEventRecord R;
    while (Sim.PopEvent(R) && Total < 300)
    {
        if (Total == 0)
        {
            FirstTick = R.Tick;
        }
        Counts[static_cast<int32_t>(R.Type)]++;
        Order[Total] = static_cast<int32_t>(R.Type);
        if (R.Tick < LastTick)
        {
            bMonotonic = false;
        }
        LastTick = R.Tick;
        ++Total;
    }

    const int32_t Started = Counts[static_cast<int32_t>(ECombatEvent::AttackStarted)];
    const int32_t Landed = Counts[static_cast<int32_t>(ECombatEvent::AttackHit)];
    const int32_t Whiffed = Counts[static_cast<int32_t>(ECombatEvent::AttackWhiffed)];

    CheckInt(Total, FCombatSim::MaxEventQueue, "draining yields exactly the 128 buffered records");
    CheckInt(Sim.PendingEventCount(), 0, "the queue is empty after draining");
    Check(bMonotonic, "events are delivered in non-decreasing tick order");
    CheckInt(Started, 64, "the first 64 attacks are recorded: their AttackStarted events were kept");
    CheckInt(Landed, 37, "37 of the recorded attacks landed");
    CheckInt(Whiffed, 27, "27 of the recorded attacks whiffed");
    CheckInt(Landed + Whiffed, Started, "every recorded attack has exactly one recorded resolution");
    CheckInt(Counts[static_cast<int32_t>(ECombatEvent::StaminaExhausted)], 0,
             "unlimited stamina means no exhaustion events");
    CheckInt(Order[0], static_cast<int32_t>(ECombatEvent::AttackStarted),
             "the OLDEST event (the first jab's start) is still at the head of the queue");
    CheckInt(FirstTick, TicksAtBell + 1, "the head record is the very first attack of the round");
    CheckInt(Jabs - Started, 6, "the six NEWEST attacks were dropped, i.e. drop-newest not drop-oldest");
}

// ---------------------------------------------------------------------------
// T18 - arena bounds and the anti-overlap separation rule
// ---------------------------------------------------------------------------

static void Test18_ArenaBoundsAndAntiOverlap()
{
    // Walking backwards can never leave the ring.
    {
        FScenario S;
        FCombatSim Sim;
        Sim.Init(S.Rules, S.A, S.B);
        RunUntilFighting(Sim);
        Drain(Sim);
        float MinSeen = 1.0e9f;
        float MaxAbsSeen = 0.0f;
        for (int32_t i = 0; i < 400; ++i)
        {
            Sim.StepFixed(InWalkLeft(), FInputState());
            const float Ax = Sim.GetFighter(ESide::A).PositionX;
            if (Ax < MinSeen)
            {
                MinSeen = Ax;
            }
            if (std::fabs(Ax) > MaxAbsSeen)
            {
                MaxAbsSeen = std::fabs(Ax);
            }
        }
        CheckNear(Sim.GetFighter(ESide::A).PositionX, S.Rules.ArenaMinX, 0.001f,
                  "A is pinned exactly on the -X arena bound (-160)");
        Check(MinSeen >= S.Rules.ArenaMinX - 0.001f, "A never gets past the -X bound");
        Check(MaxAbsSeen <= 160.0f + 0.001f, "A's coordinate never exceeds the bound");
        CheckInt(Sim.GetFighter(ESide::A).Facing, 1, "A still squares up to the opponent");
    }

    // Walking into each other stops at MinSeparation, split symmetrically.
    {
        FScenario S;
        FCombatSim Sim;
        Sim.Init(S.Rules, S.A, S.B);
        RunUntilFighting(Sim);
        Drain(Sim);
        RunTicks(Sim, 200, InWalkRight(), InWalkLeft());
        const float Sep = std::fabs(Sim.GetFighter(ESide::B).PositionX -
                                    Sim.GetFighter(ESide::A).PositionX);
        CheckNear(Sep, S.Rules.MinSeparation, 0.001f, "the pair stops exactly one MinSeparation apart");
        CheckNear(Sim.GetFighter(ESide::A).PositionX, -29.0f, 0.01f, "the correction is split symmetrically (A)");
        CheckNear(Sim.GetFighter(ESide::B).PositionX, 29.0f, 0.01f, "the correction is split symmetrically (B)");
    }

    // Driving the opponent onto the ropes must not break the separation rule.
    {
        FScenario S;
        FCombatSim Sim;
        Sim.Init(S.Rules, S.A, S.B);
        RunUntilFighting(Sim);
        Drain(Sim);
        float MinSep = 1.0e9f;
        for (int32_t i = 0; i < 600; ++i)
        {
            Sim.StepFixed(InWalkRight(), InWalkRight());
            const float Sep = std::fabs(Sim.GetFighter(ESide::B).PositionX -
                                        Sim.GetFighter(ESide::A).PositionX);
            if (Sep < MinSep)
            {
                MinSep = Sep;
            }
        }
        CheckNear(Sim.GetFighter(ESide::B).PositionX, S.Rules.ArenaMaxX, 0.001f,
                  "B is driven onto the +X ropes (160)");
        CheckNear(Sim.GetFighter(ESide::A).PositionX, 102.0f, 0.001f,
                  "A is held one MinSeparation behind B on the ropes");
        Check(MinSep >= S.Rules.MinSeparation - 0.001f,
                  "even against the ropes the separation rule still holds");
    }
}
// ===========================================================================
// Test registry and entry point.
//
// The harness is single-translation-unit: every test function above is static
// and is invoked from the table below, so no test can silently stop being run
// without the table changing too.
// ===========================================================================

struct FTestCase
{
    const char* Name;
    void (*Fn)();
};

static const FTestCase g_Tests[] =
{
    { "T1  documented defaults, countdown, inputs inert before the bell", Test1_DefaultsAndCountdown },
    { "T2  jab damage and the single-hit guard", Test2_JabDamageAndSingleHitGuard },
    { "T3  fixed-step accumulation, edge inputs consumed once", Test3_AdvanceConsumesEdgesOnce },
    { "T4  blocked hit: chip, block stun, guard break", Test4_BlockedHitChipStunAndGuardBreak },
    { "T5  the slip avoids a punch inside its invuln window", Test5_DodgeAvoidsPunch },
    { "T5b the slip travels exactly DodgeDistance", Test5b_DodgeTravelsExactDistance },
    { "T6  stamina gating, cost accounting, exhaustion", Test6_StaminaGatingAndExhaustion },
    { "T7  knockdown, get-up and get-up invulnerability", Test7_KnockdownGetUpAndInvulnerability },
    { "T8  only the heavy punch can knock an opponent down", Test8_HeavyPunchIsTheOnlyKnockdown },
    { "T9  knockout by damage ends the fight", Test9_KnockoutByDamage },
    { "T10 guard chip cannot be dodged and can end a fight", Test10_GuardChipAndChipKnockout },
    { "T11 three knockdowns in a round are a TKO", Test11_ThreeKnockdownTechnicalKnockout },
    { "T12 scorecards, between-round restore, decision", Test12_ScorecardsBetweenRoundRestoreAndDecision },
    { "T13 a level contest is a draw decision", Test13_EvenContestIsADrawDecision },
    { "T14 Advance() timestep contract and frame-rate independence", Test14_AdvanceTimestepContract },
    { "T15 counter hits: 1.6x damage and counter knockdown", Test15_CounterDamageAndCounterKnockdown },
    { "T16a round judging: damage dealt is the first criterion", Test16a_DamageDealtIsTheFirstCriterion },
    { "T16b simultaneous double knockdown draws the round", Test16b_DoubleKnockdownDrawsTheRound },
    { "T16c round judging: punches landed is the second criterion", Test16c_PunchesLandedIsTheSecondCriterion },
    { "T16d round judging: stamina remaining is the third criterion", Test16d_StaminaRemainingIsTheThirdCriterion },
    { "T17 the event queue is bounded and drops the newest", Test17_EventQueueIsBoundedAndDropsTheNewest },
    { "T18 arena bounds and the anti-overlap separation rule", Test18_ArenaBoundsAndAntiOverlap },
};

int main()
{
    const int32_t Total = static_cast<int32_t>(sizeof(g_Tests) / sizeof(g_Tests[0]));

    std::printf("=================================================================\n");
    std::printf(" ROCKY KOMBAT - combat core rule harness\n");
    std::printf(" core   : Source/RockyKombat/Combat/RKCCombatCore.cpp\n");
    std::printf(" groups : %d\n", static_cast<int>(Total));
    std::printf("=================================================================\n\n");

    for (int32_t i = 0; i < Total; ++i)
    {
        g_TestName = g_Tests[i].Name;
        const int32_t FailuresBefore = g_Failures;
        std::printf("[%2d/%2d] %s\n", static_cast<int>(i + 1), static_cast<int>(Total), g_Tests[i].Name);
        g_Tests[i].Fn();
        const int32_t NewFailures = g_Failures - FailuresBefore;
        std::printf("        %s (%d new failure%s)\n\n",
                    (NewFailures == 0) ? "ok" : "FAILED",
                    static_cast<int>(NewFailures), (NewFailures == 1) ? "" : "s");
    }

    std::printf("=================================================================\n");
    std::printf(" %d checks run, %d failed\n", static_cast<int>(g_Checks), static_cast<int>(g_Failures));
    std::printf(" %s\n", (g_Failures == 0) ? "RESULT: PASS" : "RESULT: FAIL");
    std::printf("=================================================================\n");
    return (g_Failures == 0) ? 0 : 1;
}
