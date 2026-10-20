// RKCAITests.cpp
//
// Rocky Kombat - standalone offline-AI harness.
//
// WHY THIS EXISTS: the product requirement is that the opponent AI must be
// provable without launching Unreal. RKCAI.h/.cpp are therefore pure C++ with no
// engine headers, and this program compiles the brain and the combat core with a
// plain MSVC compiler, links them with NO Unreal libraries, and drives complete
// bot-vs-bot matches at the real fixed timestep. A green run here is evidence
// about the shipped AI and the shipped rules, not about a test-only code path.
//
// WHAT IT PROVES, and why each section is here:
//   1. Profiles       - every archetype/grade combination is internally legal,
//                       and the reflex numbers have the frame-data consequences
//                       the design claims (a jab is not reaction-blockable).
//   2. Live matches   - per-tick invariants that no difficulty or personality
//                       may break: in-bounds positions, minimum separation,
//                       health/stamina ceilings, the three-knockdown limit, one
//                       damage event per thrown punch, and never swinging at a
//                       downed opponent.
//   3. Agreement      - a hand-written tick loop must reproduce RunBotMatch
//                       exactly. If the two loops ever disagree, one of them is
//                       not measuring the shipped rules.
//   4. No side channel- record the bot's inputs, replay them through a fresh
//                       simulation with no brain attached, and require a
//                       bit-identical result. The bot cannot see or do anything
//                       a human input device could not.
//   5. Determinism    - identical seeds reproduce an identical match, and
//                       different seeds really do change the match.
//   6. Neutrality     - difficulty and training instrumentation never widen a
//                       fighter's statistics, and descriptive-only training aids
//                       never change an outcome.
//
// Build and run:  cmd.exe /c build_ai_tests.bat > ai_tests.log 2>&1

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

#include "AI/RKCAI.h"

// ---------------------------------------------------------------------------
// Minimum test scaffolding
// ---------------------------------------------------------------------------

static int g_Checks = 0;
static int g_Failures = 0;
static const char* g_Section = "(none)";

static void Section(const char* Name)
{
    g_Section = Name;
    std::printf("\n=== %s ===\n", Name);
}

static void Check(bool bCondition, const char* Message)
{
    ++g_Checks;
    if (!bCondition)
    {
        ++g_Failures;
        std::printf("FAIL  [%s] %s\n", g_Section, Message);
    }
}

static void Report(const char* Message)
{
    std::printf("      %s\n", Message);
}

static void ReportF(const char* Label, double Value)
{
    std::printf("      %-42s %12.3f\n", Label, Value);
}

// ---------------------------------------------------------------------------
// Deterministic state fingerprint. FNV-1a over the observable state, so two
// runs can be compared as one number instead of field by field.
// ---------------------------------------------------------------------------

static uint32_t Mix(uint32_t Hash, uint32_t Value)
{
    Hash ^= Value;
    Hash *= 16777619u;
    return Hash;
}

static uint32_t BitsOf(float Value)
{
    uint32_t Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

static uint32_t HashFighter(uint32_t Hash, const RKC::FFighterState& F)
{
    Hash = Mix(Hash, BitsOf(F.PositionX));
    Hash = Mix(Hash, static_cast<uint32_t>(F.Facing + 1000));
    Hash = Mix(Hash, BitsOf(F.Health));
    Hash = Mix(Hash, BitsOf(F.Stamina));
    Hash = Mix(Hash, static_cast<uint32_t>(F.State));
    Hash = Mix(Hash, static_cast<uint32_t>(F.CurrentAction));
    Hash = Mix(Hash, static_cast<uint32_t>(F.StateTicksRemaining + 4096));
    Hash = Mix(Hash, static_cast<uint32_t>(F.AttackTick));
    Hash = Mix(Hash, F.bAttackHasLanded ? 1u : 0u);
    Hash = Mix(Hash, static_cast<uint32_t>(F.KnockdownsThisRound));
    Hash = Mix(Hash, BitsOf(F.DamageDealtTotal));
    Hash = Mix(Hash, BitsOf(F.DamageTakenTotal));
    Hash = Mix(Hash, static_cast<uint32_t>(F.HitsLandedTotal));
    Hash = Mix(Hash, static_cast<uint32_t>(F.HitsTakenTotal));
    Hash = Mix(Hash, static_cast<uint32_t>(F.AttacksThrownTotal));
    return Hash;
}

static uint32_t HashState(const RKC::FCombatSim& Sim)
{
    uint32_t Hash = 2166136261u;
    Hash = HashFighter(Hash, Sim.GetFighter(RKC::ESide::A));
    Hash = HashFighter(Hash, Sim.GetFighter(RKC::ESide::B));

    const RKC::FMatchState& M = Sim.GetMatch();
    Hash = Mix(Hash, static_cast<uint32_t>(M.Phase));
    Hash = Mix(Hash, static_cast<uint32_t>(M.Round));
    Hash = Mix(Hash, BitsOf(M.PhaseTimeRemaining));
    Hash = Mix(Hash, BitsOf(M.RoundTimeRemaining));
    Hash = Mix(Hash, static_cast<uint32_t>(M.Result));
    Hash = Mix(Hash, static_cast<uint32_t>(M.EndReason));
    Hash = Mix(Hash, static_cast<uint32_t>(M.ScoreA + 64));
    Hash = Mix(Hash, static_cast<uint32_t>(M.ScoreB + 64));
    Hash = Mix(Hash, static_cast<uint32_t>(M.TickCount));
    return Hash;
}

// ---------------------------------------------------------------------------
// Scenario definitions
// ---------------------------------------------------------------------------

static const int32_t kMaxTicks = 60 * 60 * 10; // ten simulated minutes

/** The two launch roster configs, built exactly as the game builds them.
 *  RKCRoster.h is deliberately NOT included here: it pulls in engine headers, and
 *  this harness must stay engine-free. A default-constructed FFighterConfig is
 *  already a legal fighter (documented in RKCCombatCore.h). */
static RKC::FFighterConfig MakeBalancedConfig()
{
    return RKC::FFighterConfig();
}

/** A deliberately different fighter: same legality, different numbers. Used to
 *  prove the AI has no privileged knowledge of its opponent's statistics. */
static RKC::FFighterConfig MakeFragileConfig()
{
    RKC::FFighterConfig C;
    C.MaxHealth = 82.0f;
    C.MaxStamina = 88.0f;
    C.WalkForwardSpeed = 205.0f;
    C.WalkBackwardSpeed = 155.0f;
    C.Jab.Damage = 5.0f;
    C.HeavyPunch.Damage = 13.0f;
    C.DamageTakenMultiplier = 1.08f;
    return C;
}

static bool ConfigsEqual(const RKC::FFighterConfig& L, const RKC::FFighterConfig& R)
{
    const bool bScalarsEqual =
        L.MaxHealth == R.MaxHealth && L.MaxStamina == R.MaxStamina &&
        L.WalkForwardSpeed == R.WalkForwardSpeed && L.WalkBackwardSpeed == R.WalkBackwardSpeed &&
        L.HalfWidth == R.HalfWidth && L.Height == R.Height &&
        L.StaminaRegenPerSecond == R.StaminaRegenPerSecond &&
        L.BlockStaminaRegenPerSecond == R.BlockStaminaRegenPerSecond &&
        L.BlockHoldDrainPerSecond == R.BlockHoldDrainPerSecond &&
        L.DodgeSeconds == R.DodgeSeconds && L.DodgeStartupSeconds == R.DodgeStartupSeconds &&
        L.DodgeInvulnEndSeconds == R.DodgeInvulnEndSeconds && L.DodgeDistance == R.DodgeDistance &&
        L.DodgeStaminaCost == R.DodgeStaminaCost &&
        L.DodgeCooldownSeconds == R.DodgeCooldownSeconds &&
        L.KnockdownSeconds == R.KnockdownSeconds &&
        L.GetUpInvulnSeconds == R.GetUpInvulnSeconds &&
        L.DamageTakenMultiplier == R.DamageTakenMultiplier;

    const bool bJabEqual = L.Jab.Damage == R.Jab.Damage &&
                           L.Jab.StartupSeconds == R.Jab.StartupSeconds &&
                           L.Jab.ActiveSeconds == R.Jab.ActiveSeconds &&
                           L.Jab.RecoverySeconds == R.Jab.RecoverySeconds &&
                           L.Jab.ReachNear == R.Jab.ReachNear && L.Jab.ReachFar == R.Jab.ReachFar &&
                           L.Jab.StaminaCost == R.Jab.StaminaCost &&
                           L.Jab.bCanKnockdown == R.Jab.bCanKnockdown;

    const bool bHeavyEqual =
        L.HeavyPunch.Damage == R.HeavyPunch.Damage &&
        L.HeavyPunch.StartupSeconds == R.HeavyPunch.StartupSeconds &&
        L.HeavyPunch.ActiveSeconds == R.HeavyPunch.ActiveSeconds &&
        L.HeavyPunch.RecoverySeconds == R.HeavyPunch.RecoverySeconds &&
        L.HeavyPunch.ReachNear == R.HeavyPunch.ReachNear &&
        L.HeavyPunch.ReachFar == R.HeavyPunch.ReachFar &&
        L.HeavyPunch.StaminaCost == R.HeavyPunch.StaminaCost &&
        L.HeavyPunch.bCanKnockdown == R.HeavyPunch.bCanKnockdown;

    return bScalarsEqual && bJabEqual && bHeavyEqual;
}

// ---------------------------------------------------------------------------
// A hand-written tick loop, instrumented. Kept separate from RunBotMatch on
// purpose: section 3 requires the two to agree.
// ---------------------------------------------------------------------------

struct FManualOutcome
{
    int32_t Ticks = 0;
    uint32_t Hash = 0;
    bool bReachedMatchEnd = false;
    RKC::EMatchResult Result = RKC::EMatchResult::Undecided;
    RKC::EMatchEndReason EndReason = RKC::EMatchEndReason::None;
    int32_t ScoreA = 0;
    int32_t ScoreB = 0;

    // Invariant violations observed at any tick.
    float WorstSeparationShortfall = 0.0f;
    int32_t PositionBoundViolations = 0;
    int32_t HealthCeilingViolations = 0;
    int32_t StaminaCeilingViolations = 0;
    int32_t KnockdownLimitViolations = 0;
    int32_t DoubleDamageOnOneAttack = 0;
    int32_t AttacksAtDownedOpponent = 0;

    int32_t HitEvents = 0;
    int32_t AttacksStarted = 0;
    int32_t KnockdownEvents = 0;
    int32_t GetUpEvents = 0;
    int32_t GuardBreakEvents = 0;
    int32_t StaminaExhaustedEvents = 0;
};

/** Runs one complete bot-vs-bot match with per-tick invariant accounting and
 *  optional input capture. @param OutCapturedA/B receive the exact FInputState
 *  the brains produced, so section 4 can replay them. */
static FManualOutcome RunInstrumented(const RKC::FMatchRules& Rules,
                                      const RKC::FFighterConfig& ConfigA,
                                      const RKC::FFighterConfig& ConfigB,
                                      const RKC::FBotProfile& ProfileA,
                                      const RKC::FBotProfile& ProfileB, uint32_t SeedA,
                                      uint32_t SeedB,
                                      RKC::ETrainingMode TrainingA = RKC::ETrainingMode::Off,
                                      RKC::ETrainingMode TrainingB = RKC::ETrainingMode::Off,
                                      std::vector<RKC::FInputState>* OutCapturedA = nullptr,
                                      std::vector<RKC::FInputState>* OutCapturedB = nullptr)
{
    FManualOutcome Out;

    // Draining events must never change an outcome: events are descriptive only.
    // This loop drains aggressively; RunBotMatch never drains at all. Section 3
    // compares the two, which is what turns that claim into a measurement.
    RKC::FCombatSim Sim;
    Sim.Init(Rules, ConfigA, ConfigB, TrainingA, TrainingB);

    RKC::FBotBrain BrainA;
    RKC::FBotBrain BrainB;
    BrainA.Init(RKC::ESide::A, ProfileA, SeedA);
    BrainB.Init(RKC::ESide::B, ProfileB, SeedB);

    const float Dt = RKC::FCombatSim::FixedTickSeconds;

    // Per-attack damage counters, so a single punch can be shown to register at
    // most one damage event no matter how long its active window lasts.
    int32_t DamageEventsThisAttackA = 0;
    int32_t DamageEventsThisAttackB = 0;

    int32_t Tick = 0;
    while (Tick < kMaxTicks && !Sim.IsMatchOver())
    {
        const RKC::FInputState InputA = BrainA.ComputeInput(Dt, Sim);
        const RKC::FInputState InputB = BrainB.ComputeInput(Dt, Sim);

        if (OutCapturedA)
        {
            OutCapturedA->push_back(InputA);
        }
        if (OutCapturedB)
        {
            OutCapturedB->push_back(InputB);
        }

        // ---- pre-tick invariant: never throw a punch at a downed opponent ----
        // The default profile forbids it (FBotProfile::bNeverHitDowned), and the
        // AI is required to refuse it before the rules have to.
        const RKC::EFighterState StateA = Sim.GetFighter(RKC::ESide::A).State;
        const RKC::EFighterState StateB = Sim.GetFighter(RKC::ESide::B).State;
        const bool bThrowA = InputA.bJabPressed || InputA.bHeavyPressed;
        const bool bThrowB = InputB.bJabPressed || InputB.bHeavyPressed;
        if (ProfileA.bNeverHitDowned && bThrowA && StateB == RKC::EFighterState::KnockedDown)
        {
            ++Out.AttacksAtDownedOpponent;
        }
        if (ProfileB.bNeverHitDowned && bThrowB && StateA == RKC::EFighterState::KnockedDown)
        {
            ++Out.AttacksAtDownedOpponent;
        }

        Sim.StepFixed(InputA, InputB);
        ++Tick;

        // ---- drain events ----------------------------------------------------
        RKC::FCombatEventRecord Ev;
        while (Sim.PopEvent(Ev))
        {
            switch (Ev.Type)
            {
            case RKC::ECombatEvent::AttackStarted:
                ++Out.AttacksStarted;
                if (Ev.Side == RKC::ESide::A)
                {
                    DamageEventsThisAttackA = 0;
                }
                else if (Ev.Side == RKC::ESide::B)
                {
                    DamageEventsThisAttackB = 0;
                }
                break;

            case RKC::ECombatEvent::AttackHit:
                ++Out.HitEvents;
                if (Ev.Side == RKC::ESide::A)
                {
                    if (++DamageEventsThisAttackA > 1)
                    {
                        ++Out.DoubleDamageOnOneAttack;
                    }
                }
                else if (Ev.Side == RKC::ESide::B)
                {
                    if (++DamageEventsThisAttackB > 1)
                    {
                        ++Out.DoubleDamageOnOneAttack;
                    }
                }
                break;

            case RKC::ECombatEvent::Knockdown:
                ++Out.KnockdownEvents;
                break;
            case RKC::ECombatEvent::GetUp:
                ++Out.GetUpEvents;
                break;
            case RKC::ECombatEvent::GuardBroken:
                ++Out.GuardBreakEvents;
                break;
            case RKC::ECombatEvent::StaminaExhausted:
                ++Out.StaminaExhaustedEvents;
                break;
            default:
                break;
            }
        }

        // ---- post-tick invariants -------------------------------------------
        const RKC::FFighterState& FA = Sim.GetFighter(RKC::ESide::A);
        const RKC::FFighterState& FB = Sim.GetFighter(RKC::ESide::B);

        if (FA.PositionX < Rules.ArenaMinX - 0.001f || FA.PositionX > Rules.ArenaMaxX + 0.001f ||
            FB.PositionX < Rules.ArenaMinX - 0.001f || FB.PositionX > Rules.ArenaMaxX + 0.001f)
        {
            ++Out.PositionBoundViolations;
        }

        const float Separation = std::fabs(FB.PositionX - FA.PositionX);
        const float Shortfall = Rules.MinSeparation - Separation;
        if (Shortfall > Out.WorstSeparationShortfall)
        {
            Out.WorstSeparationShortfall = Shortfall;
        }

        if (FA.Health > ConfigA.MaxHealth + 0.001f || FB.Health > ConfigB.MaxHealth + 0.001f)
        {
            ++Out.HealthCeilingViolations;
        }
        if (FA.Stamina > ConfigA.MaxStamina + 0.001f || FB.Stamina > ConfigB.MaxStamina + 0.001f)
        {
            ++Out.StaminaCeilingViolations;
        }

        // The three-knockdown rule is a round rule, so the per-round count may
        // never exceed the configured limit.
        if (FA.KnockdownsThisRound > Rules.MaxKnockdownsPerRound ||
            FB.KnockdownsThisRound > Rules.MaxKnockdownsPerRound)
        {
            ++Out.KnockdownLimitViolations;
        }
    }

    Out.Ticks = Tick;
    Out.bReachedMatchEnd = Sim.IsMatchOver();
    Out.Hash = HashState(Sim);

    const RKC::FMatchState& Match = Sim.GetMatch();
    Out.Result = Match.Result;
    Out.EndReason = Match.EndReason;
    Out.ScoreA = Match.ScoreA;
    Out.ScoreB = Match.ScoreB;
    return Out;
}

/** Replays a captured input sequence through a fresh simulation with no brain
 *  attached. Used by section 4 to prove the AI has no side channel. */
static uint32_t ReplayInputs(const RKC::FMatchRules& Rules, const RKC::FFighterConfig& ConfigA,
                             const RKC::FFighterConfig& ConfigB,
                             const std::vector<RKC::FInputState>& SeqA,
                             const std::vector<RKC::FInputState>& SeqB)
{
    RKC::FCombatSim Sim;
    Sim.Init(Rules, ConfigA, ConfigB);

    for (size_t i = 0; i < SeqA.size() && i < SeqB.size(); ++i)
    {
        Sim.StepFixed(SeqA[i], SeqB[i]);
    }
    return HashState(Sim);
}

// ---------------------------------------------------------------------------
// 1. Profile integrity
// ---------------------------------------------------------------------------

static void TestProfiles(const RKC::FMatchRules& Rules)
{
    (void)Rules;
    Section("1. Profile integrity");

    const RKC::EBotArchetype Archetypes[] = {RKC::EBotArchetype::Balanced,
                                             RKC::EBotArchetype::Pressure,
                                             RKC::EBotArchetype::Counter,
                                             RKC::EBotArchetype::Slugger};
    const RKC::EBotDifficulty Difficulties[] = {RKC::EBotDifficulty::Rookie,
                                                RKC::EBotDifficulty::Contender,
                                                RKC::EBotDifficulty::Champion};

    int Combos = 0;
    for (RKC::EBotArchetype Archetype : Archetypes)
    {
        for (RKC::EBotDifficulty Difficulty : Difficulties)
        {
            const RKC::FBotProfile P = RKC::FBotProfile::Make(Archetype, Difficulty);
            Check(P.IsValid(), "every archetype/grade profile is internally legal");
            Check(P.bNeverHitDowned, "no grade swings at a downed opponent");
            Check(P.ReactionSeconds > 0.0f, "reaction time is positive");
            Check(P.ThinkIntervalSeconds > 0.0f, "think interval is positive");
            Check(P.Aggression >= 0.0f && P.Aggression <= 1.0f, "aggression stays in 0..1");
            Check(P.GuardChance >= 0.0f && P.GuardChance <= 1.0f, "guard chance stays in 0..1");
            Check(P.SlipChance >= 0.0f && P.SlipChance <= 1.0f, "slip chance stays in 0..1");
            ++Combos;
        }
    }
    Check(Combos == 12, "all 12 archetype/grade combinations were exercised");

    const float RookieReflex =
        RKC::FBotProfile::Make(RKC::EBotArchetype::Balanced, RKC::EBotDifficulty::Rookie).ReactionSeconds;
    const float ContenderReflex =
        RKC::FBotProfile::Make(RKC::EBotArchetype::Balanced, RKC::EBotDifficulty::Contender).ReactionSeconds;
    const float ChampionReflex =
        RKC::FBotProfile::Make(RKC::EBotArchetype::Balanced, RKC::EBotDifficulty::Champion).ReactionSeconds;

    ReportF("rookie reaction seconds", RookieReflex);
    ReportF("contender reaction seconds", ContenderReflex);
    ReportF("champion reaction seconds", ChampionReflex);
    Check(RookieReflex > ContenderReflex && ContenderReflex > ChampionReflex,
          "reaction time improves strictly with grade");

    // The design claims a real frame-data consequence: a grade's reflex can beat
    // a heavy punch's startup but can never beat a jab's. This section is what
    // stops that claim from silently drifting when the numbers are re-tuned.
    const RKC::FAttackDef Jab = RKC::FAttackDef::MakeJab();
    const RKC::FAttackDef Heavy = RKC::FAttackDef::MakeHeavyPunch();
    ReportF("jab startup seconds", Jab.StartupSeconds);
    ReportF("heavy startup seconds", Heavy.StartupSeconds);

    Check(RookieReflex > Jab.StartupSeconds, "no grade can reaction-block a jab");
    Check(ContenderReflex > Jab.StartupSeconds, "no grade can reaction-block a jab");
    Check(ChampionReflex > Jab.StartupSeconds, "no grade can reaction-block a jab");

    Check(ContenderReflex <= Heavy.StartupSeconds, "a contender can reaction-block a heavy punch");
    Check(ChampionReflex <= Heavy.StartupSeconds, "a champion can reaction-block a heavy punch");
    Check(RookieReflex > Heavy.StartupSeconds, "a rookie cannot reaction-block a heavy punch");

    const RKC::FBotProfile RookieBal =
        RKC::FBotProfile::Make(RKC::EBotArchetype::Balanced, RKC::EBotDifficulty::Rookie);
    const RKC::FBotProfile ChampionBal =
        RKC::FBotProfile::Make(RKC::EBotArchetype::Balanced, RKC::EBotDifficulty::Champion);
    Check(!RookieBal.bPunishRecovery, "a rookie does not punish recovery frames");
    Check(ChampionBal.bPunishRecovery, "a champion does punish recovery frames");

    // Personality must bend behaviour without touching the profile's legality.
    const RKC::FBotProfile Pressure =
        RKC::FBotProfile::Make(RKC::EBotArchetype::Pressure, RKC::EBotDifficulty::Contender);
    const RKC::FBotProfile Counter =
        RKC::FBotProfile::Make(RKC::EBotArchetype::Counter, RKC::EBotDifficulty::Contender);
    const RKC::FBotProfile Slugger =
        RKC::FBotProfile::Make(RKC::EBotArchetype::Slugger, RKC::EBotDifficulty::Contender);
    Check(Pressure.Aggression > Counter.Aggression, "a pressure fighter is more aggressive than a counter puncher");
    Check(Counter.GuardChance > Pressure.GuardChance, "a counter puncher guards more than a pressure fighter");
    Check(Slugger.HeavyBias > Counter.HeavyBias, "a slugger favours the heavy punch");
}

// ---------------------------------------------------------------------------
// 2 + 3 + 5 + 6. Live matches, agreement with RunBotMatch, determinism,
//                neutrality
// ---------------------------------------------------------------------------

static void TestLiveMatches(const RKC::FMatchRules& Rules, const RKC::FFighterConfig& ConfigA,
                            const RKC::FFighterConfig& ConfigB)
{
    Section("2. Live bot-vs-bot matches: per-tick invariants");

    const uint32_t Seeds[][2] = {{20261017u, 911042u}, {7u, 13u},      {101u, 202u},
                                 {31337u, 424242u},  {1u, 999999u},    {55u, 77u}};
    const int SeedPairs = static_cast<int>(sizeof(Seeds) / sizeof(Seeds[0]));

    const RKC::FBotProfile ContenderBal =
        RKC::FBotProfile::Make(RKC::EBotArchetype::Balanced, RKC::EBotDifficulty::Contender);

    int32_t TotalTicks = 0;
    int32_t TotalHitEvents = 0;
    int32_t TotalAttacks = 0;
    int32_t TotalKnockdowns = 0;
    int32_t TotalGetUps = 0;
    int32_t NonDecisiveMatches = 0;
    int32_t TimedOutMatches = 0;

    std::vector<int32_t> TickCounts;

    for (int i = 0; i < SeedPairs; ++i)
    {
        const FManualOutcome Outcome =
            RunInstrumented(Rules, ConfigA, ConfigB, ContenderBal, ContenderBal, Seeds[i][0],
                            Seeds[i][1]);

        Check(Outcome.bReachedMatchEnd, "every bot match reaches a match end inside the tick budget");
        if (!Outcome.bReachedMatchEnd)
        {
            ++TimedOutMatches;
        }
        Check(Outcome.WorstSeparationShortfall <= 0.0005f,
              "minimum separation is never breached (fighters never overlap)");
        Check(Outcome.PositionBoundViolations == 0, "fighters stay inside the ring bounds");
        Check(Outcome.HealthCeilingViolations == 0, "no fighter ever exceeds maximum health");
        Check(Outcome.StaminaCeilingViolations == 0, "no fighter ever exceeds maximum stamina");
        Check(Outcome.KnockdownLimitViolations == 0, "the three-knockdown limit is never exceeded");
        Check(Outcome.DoubleDamageOnOneAttack == 0, "one thrown punch registers at most one hit");
        Check(Outcome.AttacksAtDownedOpponent == 0, "the AI never throws at a downed opponent");
        Check(Outcome.Result != RKC::EMatchResult::Undecided, "a finished match always has a result");

        if (Outcome.Result == RKC::EMatchResult::Draw)
        {
            ++NonDecisiveMatches;
        }

        TotalTicks += Outcome.Ticks;
        TotalHitEvents += Outcome.HitEvents;
        TotalAttacks += Outcome.AttacksStarted;
        TotalKnockdowns += Outcome.KnockdownEvents;
        TotalGetUps += Outcome.GetUpEvents;
        TickCounts.push_back(Outcome.Ticks);
    }

    ReportF("matches run", static_cast<double>(SeedPairs));
    ReportF("total fixed ticks simulated", static_cast<double>(TotalTicks));
    ReportF("total attack events", static_cast<double>(TotalAttacks));
    ReportF("total hit events", static_cast<double>(TotalHitEvents));
    ReportF("total knockdown events", static_cast<double>(TotalKnockdowns));
    ReportF("total get-up events", static_cast<double>(TotalGetUps));
    ReportF("draws", static_cast<double>(NonDecisiveMatches));
    ReportF("timeouts (must be 0)", static_cast<double>(TimedOutMatches));

    Check(TotalAttacks > 0, "the bot-vs-bot matches contained at least one thrown punch");
    Check(TotalHitEvents > 0, "the bot matches produced real damage");

    Section("3. Agreement with the shipped RunBotMatch harness");

    for (int i = 0; i < SeedPairs; ++i)
    {
        const FManualOutcome Manual =
            RunInstrumented(Rules, ConfigA, ConfigB, ContenderBal, ContenderBal, Seeds[i][0],
                            Seeds[i][1]);
        const RKC::FBotMatchOutcome Shipped =
            RKC::RunBotMatch(Rules, ConfigA, ConfigB, ContenderBal, ContenderBal, Seeds[i][0],
                             Seeds[i][1]);

        Check(!Shipped.bTimedOut, "RunBotMatch does not time out on this seed pair");
        Check(Shipped.Ticks == Manual.Ticks,
              "the instrumented loop and RunBotMatch simulate the same number of ticks");
        Check(Shipped.Result == Manual.Result, "the two loops agree on the match result");
        Check(Shipped.EndReason == Manual.EndReason, "the two loops agree on the end reason");
        Check(Shipped.ScoreA == Manual.ScoreA && Shipped.ScoreB == Manual.ScoreB,
              "the two loops agree on the score");
        Check(Shipped.WorstSeparationViolation <= 0.0005f,
              "RunBotMatch itself reports no separation breach");
        Check(Shipped.BotDecisionsA > 0 && Shipped.BotDecisionsB > 0,
              "both brains made tactical decisions");
        Check(Shipped.BotAttacksA > 0 || Shipped.BotAttacksB > 0,
              "at least one brain ordered an attack");
    }

    // Show one full human-readable outcome, so the evidence log carries a
    // concrete match rather than only pass/fail lines.
    {
        const RKC::FBotMatchOutcome Sample =
            RKC::RunBotMatch(Rules, ConfigA, ConfigB, ContenderBal, ContenderBal, Seeds[0][0],
                             Seeds[0][1]);
        char Buffer[640];
        RKC::DescribeMatchOutcome(Sample, Buffer, static_cast<int>(sizeof(Buffer)));
        Check(Buffer[0] != '\0', "DescribeMatchOutcome produces a report");
        Report("sample outcome, seed pair 20261017 / 911042:");
        std::printf("%s\n", Buffer);
    }

    Section("4. No side channel: replayed inputs reproduce the match exactly");

    {
        std::vector<RKC::FInputState> SeqA;
        std::vector<RKC::FInputState> SeqB;
        const FManualOutcome Captured =
            RunInstrumented(Rules, ConfigA, ConfigB, ContenderBal, ContenderBal, Seeds[0][0],
                            Seeds[0][1], RKC::ETrainingMode::Off, RKC::ETrainingMode::Off, &SeqA,
                            &SeqB);

        Check(!SeqA.empty(), "the bot produced an input sequence to capture");
        Check(SeqA.size() == static_cast<size_t>(Captured.Ticks),
              "one captured input per simulated tick");

        const uint32_t Replayed = ReplayInputs(Rules, ConfigA, ConfigB, SeqA, SeqB);
        ReportF("captured state fingerprint", static_cast<double>(Captured.Hash));
        ReportF("replayed state fingerprint", static_cast<double>(Replayed));
        Check(Captured.Hash == Replayed,
              "a brain-free replay of the captured inputs is bit-identical");

        // And the same replay again, to show the replay itself is deterministic.
        Check(ReplayInputs(Rules, ConfigA, ConfigB, SeqA, SeqB) == Captured.Hash,
              "replaying the same inputs twice is bit-identical");
    }

    Section("5. Determinism: seeds reproduce, and matter");

    for (int i = 0; i < SeedPairs; ++i)
    {
        const RKC::FBotMatchOutcome First =
            RKC::RunBotMatch(Rules, ConfigA, ConfigB, ContenderBal, ContenderBal, Seeds[i][0],
                             Seeds[i][1]);
        const RKC::FBotMatchOutcome Second =
            RKC::RunBotMatch(Rules, ConfigA, ConfigB, ContenderBal, ContenderBal, Seeds[i][0],
                             Seeds[i][1]);
        Check(First.Ticks == Second.Ticks && First.Result == Second.Result &&
                  First.ScoreA == Second.ScoreA && First.ScoreB == Second.ScoreB &&
                  First.HitsLandedA == Second.HitsLandedA &&
                  First.HitsLandedB == Second.HitsLandedB &&
                  First.FinalSeparation == Second.FinalSeparation,
              "identical seeds reproduce an identical match");
    }

    {
        // If every seed produced the same match, the seed would not be reaching
        // the brain at all. Distinct tick counts prove it does.
        bool bAnyDifference = false;
        for (size_t i = 1; i < TickCounts.size(); ++i)
        {
            if (TickCounts[i] != TickCounts[0])
            {
                bAnyDifference = true;
                break;
            }
        }
        Check(bAnyDifference, "different seeds produce different matches");
    }

    Section("6. Neutrality: no difficulty and no instrumentation may widen statistics");

    {
        RKC::FCombatSim Sim;
        Sim.Init(Rules, ConfigA, ConfigB);
        RKC::FBotBrain RookieBrain;
        RKC::FBotBrain ChampionBrain;
        RookieBrain.Init(RKC::ESide::A, RKC::FBotProfile::Make(RKC::EBotArchetype::Balanced,
                                                              RKC::EBotDifficulty::Rookie),
                         Seeds[0][0]);
        ChampionBrain.Init(RKC::ESide::B,
                           RKC::FBotProfile::Make(RKC::EBotArchetype::Balanced,
                                                  RKC::EBotDifficulty::Champion),
                           Seeds[0][1]);

        const float Dt = RKC::FCombatSim::FixedTickSeconds;
        int32_t Tick = 0;
        while (Tick < kMaxTicks && !Sim.IsMatchOver())
        {
            RKC::FInputState IA = RookieBrain.ComputeInput(Dt, Sim);
            RKC::FInputState IB = ChampionBrain.ComputeInput(Dt, Sim);
            Sim.StepFixed(IA, IB);
            ++Tick;
        }

        // The simulation's own copy of the fighter configuration must be exactly
        // what was submitted: a Champion must not have gained reach, health, or
        // damage while nobody was looking.
        Check(ConfigsEqual(Sim.GetConfig(RKC::ESide::A), ConfigA),
              "a rookie-grade match leaves the fighter configuration untouched");
        Check(ConfigsEqual(Sim.GetConfig(RKC::ESide::B), ConfigB),
              "a champion-grade match leaves the fighter configuration untouched");
    }

    {
        // Descriptive-only training aids must not change a fight. If they do, an
        // "instrumentation" toggle is really a gameplay modifier.
        const FManualOutcome Off =
            RunInstrumented(Rules, ConfigA, ConfigB, ContenderBal, ContenderBal, Seeds[1][0],
                            Seeds[1][1]);
        const FManualOutcome Hitboxes = RunInstrumented(
            Rules, ConfigA, ConfigB, ContenderBal, ContenderBal, Seeds[1][0], Seeds[1][1],
            RKC::ETrainingMode::ShowHitboxes, RKC::ETrainingMode::ShowHitboxes);
        const FManualOutcome FrameData = RunInstrumented(
            Rules, ConfigA, ConfigB, ContenderBal, ContenderBal, Seeds[1][0], Seeds[1][1],
            RKC::ETrainingMode::DisplayFrameData, RKC::ETrainingMode::DisplayFrameData);

        Check(Hitboxes.Hash == Off.Hash, "the hitbox overlay does not change a match");
        Check(FrameData.Hash == Off.Hash, "the frame-data overlay does not change a match");
    }

    Section("7. Difficulty changes discipline, not outcomes by fiat");

    {
        int32_t RookieDefensive = 0;
        int32_t ChampionDefensive = 0;
        int32_t RookieDecisions = 0;
        int32_t ChampionDecisions = 0;

        for (int i = 0; i < SeedPairs; ++i)
        {
            RKC::FCombatSim Sim;
            Sim.Init(Rules, ConfigA, ConfigB);
            RKC::FBotBrain Brain;
            const RKC::EBotDifficulty Grade =
                (i % 2 == 0) ? RKC::EBotDifficulty::Rookie : RKC::EBotDifficulty::Champion;
            Brain.Init(RKC::ESide::A, RKC::FBotProfile::Make(RKC::EBotArchetype::Balanced, Grade),
                       Seeds[i][0]);

            const float Dt = RKC::FCombatSim::FixedTickSeconds;
            int32_t Tick = 0;
            while (Tick < kMaxTicks && !Sim.IsMatchOver())
            {
                RKC::FInputState IA = Brain.ComputeInput(Dt, Sim);
                RKC::FInputState IB;
                Sim.StepFixed(IA, IB);
                ++Tick;
            }

            if (Grade == RKC::EBotDifficulty::Rookie)
            {
                RookieDefensive += Brain.GetGuardOrders() + Brain.GetSlipOrders();
                RookieDecisions += Brain.GetDecisionCount();
            }
            else
            {
                ChampionDefensive += Brain.GetGuardOrders() + Brain.GetSlipOrders();
                ChampionDecisions += Brain.GetDecisionCount();
            }
        }

        ReportF("rookie defensive orders", static_cast<double>(RookieDefensive));
        ReportF("champion defensive orders", static_cast<double>(ChampionDefensive));
        ReportF("rookie decisions", static_cast<double>(RookieDecisions));
        ReportF("champion decisions", static_cast<double>(ChampionDecisions));

        Check(ChampionDefensive > RookieDefensive,
              "a champion grade defends measurably more often than a rookie grade");
        Check(RookieDecisions > 0 && ChampionDecisions > 0, "both grades issued orders");
    }

    Section("8. Asymmetric fighters");

    {
        // Two different fighters must still complete a legal match; the AI must
        // cope with statistics it is never told about.
        const RKC::FBotMatchOutcome Shipped =
            RKC::RunBotMatch(Rules, ConfigA, ConfigB, ContenderBal, ContenderBal, Seeds[2][0],
                             Seeds[2][1]);
        const RKC::FBotMatchOutcome Mixed =
            RKC::RunBotMatch(Rules, ConfigA, MakeFragileConfig(), ContenderBal,
                             RKC::FBotProfile::Make(RKC::EBotArchetype::Pressure,
                                                    RKC::EBotDifficulty::Rookie),
                             Seeds[2][0], Seeds[2][1]);

        Check(!Shipped.bTimedOut, "a mirror match completes");
        Check(!Mixed.bTimedOut, "a heavyweight-vs-fragile match completes");
        Check(Mixed.WorstSeparationViolation <= 0.0005f, "the mixed match keeps the separation rule");
        Check(Mixed.HitsLandedA + Mixed.HitsLandedB > 0, "the mixed match produced landed punches");
        ReportF("mirror match ticks", static_cast<double>(Shipped.Ticks));
        ReportF("mixed match ticks", static_cast<double>(Mixed.Ticks));
        ReportF("mixed match damage A", Mixed.DamageDealtA);
        ReportF("mixed match damage B", Mixed.DamageDealtB);
    }
}

// ---------------------------------------------------------------------------

int main()
{
    std::printf("ROCKY KOMBAT - offline AI harness (pure C++, no Unreal)\n");
    std::printf("simulation timestep: %.6f s   max ticks per match: %d\n",
                static_cast<double>(RKC::FCombatSim::FixedTickSeconds), kMaxTicks);

    const RKC::FMatchRules Rules = RKC::FMatchRules();
    Check(Rules.IsValid(), "the default rule set is valid");

    const RKC::FFighterConfig ConfigA = MakeBalancedConfig();
    const RKC::FFighterConfig ConfigB = MakeBalancedConfig();

    TestProfiles(Rules);
    TestLiveMatches(Rules, ConfigA, ConfigB);

    std::printf("\n================ SUMMARY ================\n");
    std::printf("checks run : %d\n", g_Checks);
    std::printf("failures   : %d\n", g_Failures);
    std::printf("result     : %s\n", (g_Failures == 0) ? "PASS" : "FAIL");
    std::printf("========================================\n");

    return (g_Failures == 0) ? 0 : 1;
}
