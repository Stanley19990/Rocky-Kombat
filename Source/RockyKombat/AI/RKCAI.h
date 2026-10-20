// RKCAI.h
//
// Rocky Kombat - offline opponent AI.
//
// DESIGN CONSTRAINTS (deliberate, same reasoning as the combat core):
//  * This file and RKCAI.cpp are PURE C++ (no Unreal Engine headers). The bot
//    therefore compiles and runs in the standalone harness in Tools/CombatCoreTests
//    as well as inside the game module. Thousands of complete bot-vs-bot matches
//    can be simulated and asserted on without launching the editor.
//  * The bot NEVER touches the simulation. It reads a const FCombatSim and
//    returns an FInputState, exactly like a human player's input device. It has
//    no privileged knowledge, cannot teleport, and cannot exceed human inputs.
//    Everything it "knows" it learns from the public FMatchState/FFighterState.
//  * Randomness is a seeded xorshift32 generator owned by the brain, so a match
//    is fully reproducible from (rules, configs, profiles, seeds). No engine RNG,
//    no time(), no static mutable state.
//
// WHY TWO DECISION LAYERS
// The bot separates reflexes from tactics, which is what makes the difficulty
// grades behave like real boxing rather than like a difficulty slider:
//   * Reflex layer: reacts to an opponent attack only after ReactionSeconds have
//     elapsed since that attack began. This is a genuine reaction-time model, so
//     it produces genuine frame-data consequences: a heavy punch (0.217 s
//     startup) is blockable by a Contender (0.19 s reflex) but a jab (0.100 s
//     startup) is not reaction-blockable at any grade - the fighter has to
//     predict it instead. That prediction is the tactical layer.
//   * Tactical layer: re-plans every ThinkIntervalSeconds and holds a plan
//     (approach, strike, guard, retreat, hold). Proactive guarding, ring
//     generalship and punish selection live here.
// Difficulty tunes both layers. It never grants the bot extra damage, extra
// health, or information the player does not have.

#pragma once

#include <cstdint>

#include "Combat/RKCCombatCore.h"

namespace RKC
{
// ---------------------------------------------------------------------------
// Plans and personalities
// ---------------------------------------------------------------------------

/** What the bot is trying to do right now. Reported for HUD/telemetry so that a
 *  tester can see why the opponent behaved the way it did. */
enum class EBotPlan : uint8_t
{
    Idle = 0,      // brief neutral; the plan machine's fallback
    Approach = 1,  // walk into strike range
    Strike = 2,    // throw a punch this tick
    Hold = 3,      // sit in range and wait for a punish window
    Retreat = 4,   // back off: stamina or health emergency
    Defend = 5,    // guard up, absorb and answer
    Slip = 6,      // dodge through or away from an incoming punch
    Count = 7
};

/** Boxing personalities. These are AI-only; they are not weight divisions and
 *  they do not change fighter statistics (see docs/design-decisions.md). */
enum class EBotArchetype : uint8_t
{
    Balanced = 0, // jabs, moves, takes what is offered
    Pressure = 1, // walks forward, throws volume, guards less
    Counter = 2,  // waits, guards, punishes recovery frames
    Slugger = 3,  // favours the heavy punch over the jab
    Count = 4
};

/** Difficulty grades. Rookie/Contender/Champion describe REFLEXES AND DISCIPLINE
 *  only - never an unfair statistical advantage. */
enum class EBotDifficulty : uint8_t
{
    Rookie = 0,
    Contender = 1,
    Champion = 2,
    Count = 3
};

/** Human-readable names, used by the HUD and by evidence logs. */
const char* GetPlanName(EBotPlan Plan);
const char* GetArchetypeName(EBotArchetype Archetype);
const char* GetDifficultyName(EBotDifficulty Difficulty);

/** Everything that shapes one bot's behaviour. All values are probabilities or
 *  times; none of them modify FCombatCore tunables. */
struct FBotProfile
{
    EBotArchetype Archetype = EBotArchetype::Balanced;
    EBotDifficulty Difficulty = EBotDifficulty::Contender;

    /** Seconds the bot needs to visually register an opponent attack before the
     *  reflex layer is allowed to respond to it. */
    float ReactionSeconds = 0.190f;

    /** How often the tactical layer re-plans. Long intervals make the bot look
     *  committed to a plan; very short intervals make it look twitchy. */
    float ThinkIntervalSeconds = 0.110f;

    /** Chance, per tactical re-plan, of turning a "in range, nothing better to
     *  do" situation into a thrown punch. */
    float Aggression = 0.50f;

    /** Chance of choosing heavy over jab when both are in range and affordable. */
    float HeavyBias = 0.28f;

    /** Chance, per tactical re-plan, of proactively raising the guard while the
     *  opponent is in threat range. This is how the bot defends against jabs,
     *  which is the only way any grade can. */
    float GuardChance = 0.42f;

    /** Chance, per REFLEX opportunity, of slipping an incoming punch instead of
     *  guarding it. Slips are stronger (they avoid all damage) but riskier. */
    float SlipChance = 0.14f;

    /** Preferred standing distance, as a fraction of this fighter's own jab
     *  reach. Below 1.0 the bot stands inside its own reach. */
    float PreferredRangeFraction = 0.86f;

    /** The bot retreats when stamina drops below this fraction of its maximum. */
    float RetreatStaminaFraction = 0.26f;

    /** The bot retreats when its health fraction falls this far below the
     *  opponent's health fraction and the round clock allows it. */
    float HealthPanicMargin = 0.35f;

    /** When true the bot will throw the heavy punch at an opponent who is in
     *  attack recovery (the classic punish). Disciplined bots do this a lot. */
    bool bPunishRecovery = true;

    /** When true the bot will not attack a knocked-down opponent (wasted swings
     *  and bad sportsmanship). */
    bool bNeverHitDowned = true;

    /** Builds a profile from a grade and a personality. */
    static FBotProfile Make(EBotArchetype Archetype, EBotDifficulty Difficulty);

    /** True when every field is inside its documented range. Used by tests. */
    bool IsValid() const;
};

// ---------------------------------------------------------------------------
// Brain
// ---------------------------------------------------------------------------

/** One bot's controller-side logic. Owns no simulation state. */
class FBotBrain
{
public:
    FBotBrain();

    /** @param InSide  which fighter this brain drives.
     *  @param InProfile personality and difficulty.
     *  @param InSeed  non-zero seed; identical seeds give identical matches. */
    void Init(ESide InSide, const FBotProfile& InProfile, uint32_t InSeed);

    /** Produce this tick's intent.
     *
     *  Must be called once per simulation advance and the returned FInputState
     *  handed straight to FCombatSim. Attack/dodge edges are set only on the tick
     *  the plan wants them, so a held button can never auto-repeat: the brain
     *  re-arms deliberately.
     *
     *  @param DeltaSeconds wall-clock or fixed step since the previous call. It
     *         drives the reflex clock and the tactical clock; it never moves the
     *         fighter. */
    FInputState ComputeInput(float DeltaSeconds, const FCombatSim& Sim);

    EBotPlan GetPlan() const { return Plan; }
    /** Number of tactical decisions made. Used as evidence that the bot really
     *  was driving the fighter rather than idling. */
    int32_t GetDecisionCount() const { return DecisionCount; }
    int32_t GetReactionCount() const { return ReactionCount; }
    int32_t GetAttackOrders() const { return AttackOrders; }
    int32_t GetGuardOrders() const { return GuardOrders; }
    int32_t GetSlipOrders() const { return SlipOrders; }
    uint32_t GetRngState() const { return RngState; }
    const FBotProfile& GetProfile() const { return Profile; }
    ESide GetSide() const { return Side; }
    /** The opponent attack currently being tracked by the reflex layer, or
     *  EAction::Idle when none. Reported for the frame-data overlay. */
    EAction GetTrackedAttack() const { return TrackedAttack; }

private:
    /** What the reflex layer decided for the current tick. */
    enum class EReflexResponse : uint8_t
    {
        None = 0,
        Guard = 1,
        Slip = 2
    };

    /** Watches the opponent's current attack and, once the reaction time has
     *  elapsed, decides whether to guard or slip it. */
    void UpdateReflex(float DeltaSeconds, const FCombatSim& Sim);
    /** Picks the next tactical plan. */
    void ChoosePlan(const FCombatSim& Sim);
    /** Turns the current plan (and any reflex decision) into this tick's input. */
    FInputState ApplyPlan(const FCombatSim& Sim);

    float NextFloat01();
    bool Roll(float Probability);

    ESide Side = ESide::A;
    ESide Foe = ESide::B;
    FBotProfile Profile;
    uint32_t RngState = 0x9E3779B9u;

    EBotPlan Plan = EBotPlan::Idle;
    float PlanAgeSeconds = 0.0f;
    float PlanMinSeconds = 0.0f;
    /** Set while the current plan still owes its one-shot edge (the punch or the
     *  dodge) and has not spent it yet. */
    bool bPlanOwesAttack = false;

    float ThinkAccumulator = 0.0f;

    /** The punch this bot is currently committed to throwing, if the current
     *  plan is a Strike that has not been spent yet. */
    EAction PendingAction = EAction::Jab;

    /** Reflex layer: None, or the response chosen for this tick. */
    EReflexResponse Reflex = EReflexResponse::None;
    /** Seconds the bot keeps its guard up after a reactive block. */
    float ReflexHoldSeconds = 0.0f;

    /** EAction of the opponent's attack the reflex layer is currently watching,
     *  and how long it has been running. */
    EAction TrackedAttack = EAction::Idle;
    float TrackedAttackSeconds = 0.0f;
    bool bReflexSpent = false;

    int32_t DecisionCount = 0;
    int32_t ReactionCount = 0;
    int32_t AttackOrders = 0;
    int32_t GuardOrders = 0;
    int32_t SlipOrders = 0;
};

// ---------------------------------------------------------------------------
// Headless bot-vs-bot matches (test and evidence harness)
// ---------------------------------------------------------------------------

/** Everything measured about one completed (or timed-out) bot match. */
struct FBotMatchOutcome
{
    EMatchResult Result = EMatchResult::Undecided;
    EMatchEndReason EndReason = EMatchEndReason::None;
    ERoundEndReason LastRoundEndReason = ERoundEndReason::None;

    int32_t ScoreA = 0;
    int32_t ScoreB = 0;
    int32_t RoundsFought = 1;

    int32_t Ticks = 0;
    float DurationSeconds = 0.0f;
    /** True when the match did not reach EMatchPhase::MatchEnd inside MaxTicks.
     *  A non-zero count here is a rule-system failure, not a cosmetic issue. */
    bool bTimedOut = false;

    // Per-side counters, read from the authoritative simulation.
    int32_t HitsLandedA = 0;
    int32_t HitsLandedB = 0;
    int32_t AttacksThrownA = 0;
    int32_t AttacksThrownB = 0;
    int32_t BlocksA = 0;
    int32_t BlocksB = 0;
    int32_t DodgesA = 0;
    int32_t DodgesB = 0;
    int32_t KnockdownsA = 0;
    int32_t KnockdownsB = 0;

    float DamageDealtA = 0.0f;
    float DamageDealtB = 0.0f;
    float HealthA = 0.0f;
    float HealthB = 0.0f;

    int32_t BotDecisionsA = 0;
    int32_t BotDecisionsB = 0;
    int32_t BotAttacksA = 0;
    int32_t BotAttacksB = 0;

    /** Final centre-to-centre distance, for asserting the anti-overlap rule held
     *  through a whole live match. */
    float FinalSeparation = 0.0f;

    /** Highest separation error (below Rules.MinSeparation) observed at any tick.
     *  Must be 0 for a match to count as physically legal. */
    float WorstSeparationViolation = 0.0f;
};

/** Runs one complete headless match between two bots.
 *
 *  This is the same simulation the game runs, driven at the same fixed timestep,
 *  so a green result here is evidence about the shipped rules rather than about
 *  a test-only code path.
 *
 *  @param MaxTicks hard stop so a rules bug can never hang a test run. */
FBotMatchOutcome RunBotMatch(const FMatchRules& Rules,
                             const FFighterConfig& ConfigA, const FFighterConfig& ConfigB,
                             const FBotProfile& ProfileA, const FBotProfile& ProfileB,
                             uint32_t SeedA, uint32_t SeedB,
                             int32_t MaxTicks = 60 * 60 * 10,
                             ETrainingMode TrainingA = ETrainingMode::Off,
                             ETrainingMode TrainingB = ETrainingMode::Off);

/** Formats a human-readable one-block summary of an outcome.
 *  @param Buffer     destination, always null-terminated when BufferSize > 0.
 *  @param BufferSize capacity in bytes. 640 is comfortably enough. */
void DescribeMatchOutcome(const FBotMatchOutcome& Outcome, char* Buffer, int BufferSize);

} // namespace RKC
