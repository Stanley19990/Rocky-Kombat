// RKCCombatCore.h
//
// Rocky Kombat - authoritative, deterministic combat simulation core.
//
// DESIGN CONSTRAINTS (deliberate):
//  * This file and RKCCombatCore.cpp are PURE C++ (no Unreal Engine headers).
//    They can therefore be compiled and unit-tested with a plain C++ compiler
//    (see Tools/CombatCoreTests) AND inside Unreal as part of the game module.
//  * The simulation is a fixed-timestep (1/60 s) state machine so that combat
//    rules are deterministic and independent of animation playback or frame rate.
//    This is required for: reliable round outcomes, future rollback netcode, and
//    server-side re-simulation of competitive matches.
//  * No dynamic allocation, no exceptions, no RTTI, no RNG. Knockdown and KO are
//    determined by explicit numeric rules so every outcome is reproducible.
//
// UNITS: centimetres (Unreal world units), seconds, degrees not used.
// The fight plane is 1-D along +X. Fighters auto-face their opponent.

#pragma once

#include <cstdint>

namespace RKC
{
// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/** Legal, boxing-only actions for Rocky Boxing Arena (RBA).
 *  Kicks / martial-arts / fantasy abilities are NOT part of RBA and must not be
 *  added here; those belong to the Street Fights and Rocky Universe rule sets. */
enum class EAction : uint8_t
{
    Idle = 0,
    Walk = 1,
    Jab = 2,
    HeavyPunch = 3,
    Block = 4,
    Dodge = 5,
    Count = 6
};

/** Fighter state machine. Attack startup/active/recovery are distinct states so
 *  that hit detection windows cannot be confused with presentation. */
enum class EFighterState : uint8_t
{
    Idle = 0,
    Moving = 1,
    AttackStartup = 2,
    AttackActive = 3,
    AttackRecovery = 4,
    Blocking = 5,
    Dodging = 6,
    HitStun = 7,
    KnockedDown = 8,
    Victory = 9,
    Defeated = 10,
    Count = 11
};

/** Match lifecycle phases. */
enum class EMatchPhase : uint8_t
{
    NotStarted = 0,
    Countdown = 1,
    Fighting = 2,
    RoundEnd = 3,
    MatchEnd = 4,
    Count = 5
};

/** Outcome of a finished match. Undecided is only valid before MatchEnd. */
enum class EMatchResult : uint8_t
{
    Undecided = 0,
    FighterAWins = 1,
    FighterBWins = 2,
    Draw = 3,
    Count = 4
};

/** Why a match ended. Distinguishing these matters for competitive records. */
enum class EMatchEndReason : uint8_t
{
    None = 0,
    Knockout = 1,
    TechnicalKnockout = 2,
    RefereeStopsContest = 3,
    Decision = 4,
    DrawDecision = 5,
    Count = 6
};

enum class ERoundEndReason : uint8_t
{
    None = 0,
    TimeExpired = 1,
    Knockout = 2,
    KnockdownLimit = 3,
    DoubleKnockdown = 4,
    Count = 5
};

/** Which fighter, or both. */
enum class ESide : uint8_t
{
    None = 0,
    A = 1,
    B = 2,
    Both = 3
};

/** Simulation events, drained by presentation/HUD/tests.
 *  Events are descriptive only; they never drive the rules. */
enum class ECombatEvent : uint8_t
{
    None = 0,
    AttackStarted = 1,
    AttackHit = 2,
    AttackBlocked = 3,
    GuardBroken = 4,
    DodgeStarted = 5,
    Knockdown = 6,
    GetUp = 7,
    Knockout = 8,
    RoundStarted = 9,
    RoundEnded = 10,
    MatchEnded = 11,
    StaminaExhausted = 12,
    AttackWhiffed = 13,
    Count = 14
};

/** Training aids. Selected at match setup; purely additive instrumentation. */
enum class ETrainingMode : uint8_t
{
    Off = 0,
    ShowHitboxes = 1,
    InfiniteHealth = 2,
    InfiniteStamina = 3,
    NoOpponentInput = 4,
    DisplayFrameData = 5
};

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------

/** Per-action frame data and hitbox definition for one punch.
 *
 *  These numbers are PROVISIONAL PROTOTYPE VALUES. The design document does not
 *  specify exact combat timings, so they are recorded as design decisions in
 *  docs/design-decisions.md and must be re-tuned after playtesting.
 *
 *  Startup/Active/Recovery are expressed in seconds and converted to whole
 *  simulation ticks (1/60 s). Attack image order in the art pack is
 *  anticipation / extension / impact / recovery; the active window is aligned to
 *  the extension+impact images, but gameplay timing is authoritative. */
struct FAttackDef
{
    EAction Action = EAction::Jab;

    float StartupSeconds = 0.100f;
    float ActiveSeconds = 0.067f;
    float RecoverySeconds = 0.200f;

    float Damage = 6.0f;
    /** Fraction of Damage that still lands when the defender blocks. */
    float ChipDamageRatio = 0.15f;
    float StaminaCost = 9.0f;

    /** Hitbox extent as a distance range ahead of the attacker's origin,
     *  along the facing direction. */
    float ReachNear = 26.0f;
    float ReachFar = 104.0f;
    /** Vertical band of the hitbox above the fighter's feet. */
    float HitboxLowZ = 55.0f;
    float HitboxHighZ = 178.0f;

    float HitStunSeconds = 0.217f;
    float BlockStunSeconds = 0.133f;
    float Knockback = 34.0f;
    /** Stamina the defender loses when the punch is blocked. */
    float BlockStaminaDamage = 8.0f;

    /** Damage multiplier applied when the defender is caught during the startup
     *  or active phase of their own attack (a "clean counter"). */
    float CounterDamageMultiplier = 1.6f;

    /** If true, this punch can put the opponent on the canvas when it lands on
     *  a defender whose stamina is below Rules.KnockdownStaminaThreshold. */
    bool bCanKnockdown = false;

    bool IsValid() const
    {
        return ActiveSeconds > 0.0f && StartupSeconds >= 0.0f &&
               Damage > 0.0f && ReachFar > ReachNear && HitboxHighZ > HitboxLowZ;
    }

    /** Documented default light punch: fast, cheap, cannot knock down. */
    static FAttackDef MakeJab()
    {
        return FAttackDef();
    }

    /** Documented default heavy punch: slow, expensive, and the only RBA punch
     *  that can put an opponent on the canvas.
     *
     *  FIX NOTE (2026-10-16): the heavy punch MUST be installed explicitly.
     *  The old code relied on FCombatSim::Init() repairing an invalid
     *  HeavyPunch, but a default-constructed FAttackDef is a valid jab, so a
     *  default-constructed FFighterConfig silently got a jab clone in the heavy
     *  slot (6 damage, bCanKnockdown = false), which made the knockdown and
     *  three-knockdown rules unreachable in a default match. Verified with
     *  Tools/CombatCoreTests/probe_rkc_heavydefault.cpp and covered by
     *  Tools/CombatCoreTests/RKCCombatCoreTests.cpp. */
    static FAttackDef MakeHeavyPunch()
    {
        FAttackDef D;
        D.Action = EAction::HeavyPunch;
        D.StartupSeconds = 0.217f;
        D.ActiveSeconds = 0.100f;
        D.RecoverySeconds = 0.400f;
        D.Damage = 15.0f;
        D.ChipDamageRatio = 0.20f;
        D.StaminaCost = 20.0f;
        D.ReachNear = 26.0f;
        D.ReachFar = 118.0f;
        D.HitboxLowZ = 55.0f;
        D.HitboxHighZ = 178.0f;
        D.HitStunSeconds = 0.333f;
        D.BlockStunSeconds = 0.200f;
        D.Knockback = 58.0f;
        D.BlockStaminaDamage = 17.0f;
        D.CounterDamageMultiplier = 1.6f;
        D.bCanKnockdown = true;
        return D;
    }
};

/** Per-fighter physical and recovery characteristics.
 *  Weight-division differences are expressed through these values (see
 *  docs/design-decisions.md: division table is provisional). */
struct FFighterConfig
{
    float MaxHealth = 100.0f;
    float MaxStamina = 100.0f;

    float WalkForwardSpeed = 190.0f; // cm/s advancing
    float WalkBackwardSpeed = 140.0f; // cm/s retreating (slower, as in boxing)

    float HalfWidth = 30.0f;  // hurtbox half-width on X
    float Height = 200.0f;    // hurtbox height, matches ~200 px sprite standing height

    float StaminaRegenPerSecond = 24.0f;
    /** Blocking is work: regenerating while holding a guard is much slower. */
    float BlockStaminaRegenPerSecond = 5.0f;
    /** Passive drain while holding the guard up. */
    float BlockHoldDrainPerSecond = 4.0f;

    float DodgeSeconds = 0.367f;
    float DodgeStartupSeconds = 0.050f;
    float DodgeInvulnEndSeconds = 0.267f;
    float DodgeDistance = 130.0f;
    float DodgeStaminaCost = 14.0f;
    float DodgeCooldownSeconds = 0.400f;

    float KnockdownSeconds = 2.200f;
    float GetUpInvulnSeconds = 0.500f;

    /** Damage multiplier applied to incoming damage (0.9 = 10% resistance). */
    float DamageTakenMultiplier = 1.0f;

    /** Attack frame data for this fighter. Index by EAction.
     *  Both slots are populated with their documented defaults so that a
     *  default-constructed config is already a legal, sensible fighter. */
    FAttackDef Jab = FAttackDef::MakeJab();
    FAttackDef HeavyPunch = FAttackDef::MakeHeavyPunch();
};

/** Match-level rules. All values are provisional and configurable; the source
 *  document does not supply exact figures for round length or judging. */
struct FMatchRules
{
    int32_t Rounds = 3;
    // DESIGN DECISION: the product brief specifies 3 rounds of 30 seconds, so
    // that is the shipped default. The value stays configurable per match type.
    float RoundSeconds = 30.0f;
    float CountdownSeconds = 3.0f;
    float RoundBreakSeconds = 8.0f;
    /** Time the result is displayed before rematch is offered. */
    float PostMatchSeconds = 6.0f;

    /** Three-knockdown rule: a fighter losing this many knockdowns in one round
     *  loses the round by technical knockout. */
    int32_t MaxKnockdownsPerRound = 3;

    /** A punch that can knock down does so when the defender's stamina at the
     *  moment of impact is at or below this value. */
    float KnockdownStaminaThreshold = 18.0f;
    /** Landing a heavy punch on a defender caught in attack startup also knocks
     *  them down when their stamina is at or below this higher threshold. */
    float CounterKnockdownStaminaThreshold = 42.0f;

    /** Guard break: blocking a hit with stamina at or below this value breaks
     *  the guard and inflicts a long stun instead of normal block stun. */
    float GuardBreakStaminaThreshold = 0.0f;
    float GuardBreakStunSeconds = 0.800f;

    float BetweenRoundStaminaRestoreFraction = 1.0f;
    float BetweenRoundHealthRestoreFraction = 0.15f;
    float KnockdownStaminaRestore = 35.0f;
    float KnockdownHealthRestoreFraction = 0.20f;

    /** Judging: 10-point must system, provisional.
     *  Round winner is decided by, in order: damage dealt, punches landed,
     *  stamina remaining. Equal on all three => 10-10 round. */
    int32_t JudgeWinScore = 10;
    int32_t JudgeLoseScore = 9;
    int32_t JudgeDrawScore = 10;

    /** Minimum separation enforced between fighters along X (anti-overlap). */
    float MinSeparation = 58.0f;
    /** Fight-plane bounds on X. Fighters are clamped inside. */
    float ArenaMinX = -420.0f;
    float ArenaMaxX = 420.0f;

    bool IsValid() const
    {
        return Rounds >= 1 && RoundSeconds > 0.0f && ArenaMaxX > ArenaMinX &&
               MaxKnockdownsPerRound >= 1;
    }
};

// ---------------------------------------------------------------------------
// Runtime state
// ---------------------------------------------------------------------------

/** Player or AI intent for one simulation tick.
 *  Button presses are edge-triggered: the simulation consumes them, so a held
 *  key cannot re-trigger an attack. This is the anti-double-fire guarantee at
 *  the input layer. */
struct FInputState
{
    bool bMoveLeft = false;
    bool bMoveRight = false;
    bool bHoldBlock = false;
    bool bDodgePressed = false;   // consumed by the sim
    bool bJabPressed = false;     // consumed by the sim
    bool bHeavyPressed = false;   // consumed by the sim

    void ClearEdges()
    {
        bDodgePressed = false;
        bJabPressed = false;
        bHeavyPressed = false;
    }
};

/** Complete observable state for one fighter. */
struct FFighterState
{
    float PositionX = 0.0f;
    int32_t Facing = 1; // +1 faces +X, -1 faces -X

    float Health = 100.0f;
    float Stamina = 100.0f;

    EFighterState State = EFighterState::Idle;
    EAction CurrentAction = EAction::Idle;

    /** Ticks remaining in the current state. */
    int32_t StateTicksRemaining = 0;
    /** Tick within the current attack, counted from attack start. */
    int32_t AttackTick = 0;
    /** Set once an attack has registered on its target. Cleared when a new
     *  attack starts. This is the primary "no repeated damage from one hit
     *  event" guard. */
    bool bAttackHasLanded = false;
    /** Total ticks the current attack lasts (startup+active+recovery). */
    int32_t AttackTotalTicks = 0;

    bool bInvulnerable = false;
    int32_t InvulnTicksRemaining = 0;
    int32_t DodgeCooldownTicks = 0;
    int32_t DodgeDir = 0;

    int32_t KnockdownsThisRound = 0;
    int32_t KnockdownsTotal = 0;
    int32_t GetUpsThisRound = 0;

    // Round statistics used by the judges and by post-match summaries.
    float DamageDealtThisRound = 0.0f;
    float DamageTakenThisRound = 0.0f;
    int32_t HitsLandedThisRound = 0;
    int32_t HitsTakenThisRound = 0;
    int32_t BlocksThisRound = 0;
    int32_t DodgesThisRound = 0;
    int32_t AttacksThrownThisRound = 0;
    float StaminaSpentThisRound = 0.0f;

    // Accumulated totals across the whole match.
    float DamageDealtTotal = 0.0f;
    float DamageTakenTotal = 0.0f;
    int32_t HitsLandedTotal = 0;
    int32_t HitsTakenTotal = 0;
    int32_t AttacksThrownTotal = 0;

    bool IsAttackPhase() const
    {
        return State == EFighterState::AttackStartup ||
               State == EFighterState::AttackActive ||
               State == EFighterState::AttackRecovery;
    }
    bool IsAttackRecovering() const
    {
        return State == EFighterState::AttackRecovery;
    }
    /** A fighter that can be hit but cannot currently defend. */
    bool IsStunned() const
    {
        return State == EFighterState::HitStun || State == EFighterState::KnockedDown;
    }
    bool IsDefeated() const { return State == EFighterState::Defeated; }
};

/** Match-level observable state. */
struct FMatchState
{
    EMatchPhase Phase = EMatchPhase::NotStarted;
    int32_t Round = 1;
    /** Remaining seconds in the current phase. */
    float PhaseTimeRemaining = 0.0f;
    /** Remaining seconds on the round clock. */
    float RoundTimeRemaining = 0.0f;
    EMatchResult Result = EMatchResult::Undecided;
    EMatchEndReason EndReason = EMatchEndReason::None;
    ERoundEndReason LastRoundEndReason = ERoundEndReason::None;
    ESide LastRoundWinner = ESide::None;

    int32_t ScoreA = 0;
    int32_t ScoreB = 0;

    int32_t TickCount = 0;
};

/** One simulation event, drained by the presentation layer. */
struct FCombatEventRecord
{
    ECombatEvent Type = ECombatEvent::None;
    ESide Side = ESide::None;
    EAction Action = EAction::Idle;
    float Value = 0.0f;
    int32_t Tick = 0;
};

// ---------------------------------------------------------------------------
// Simulator
// ---------------------------------------------------------------------------

/** Fixed-timestep combat simulation. This is the single source of truth for
 *  RBA match rules. Rendering, audio, animation and input never modify it
 *  directly - they submit FInputState and read the resulting state. */
class FCombatSim
{
public:
    static constexpr float FixedTickSeconds = 1.0f / 60.0f;
    static constexpr int32_t MaxEventQueue = 128;
    /** Guard against a hung caller passing an absurd delta. */
    static constexpr float MaxTickDeltaSeconds = 0.25f;

    FCombatSim();

    /** Configure and start a fresh match. */
    void Init(const FMatchRules& InRules, const FFighterConfig& ConfigA,
              const FFighterConfig& ConfigB, ETrainingMode TrainingModeA = ETrainingMode::Off,
              ETrainingMode TrainingModeB = ETrainingMode::Off);

    /** Reset to a brand-new match with the same configuration (rematch). */
    void Rematch();

    /** Advance the simulation by DeltaSeconds, using whole fixed ticks.
     *  Leftover time is carried in an accumulator. */
    void Advance(float DeltaSeconds, const FInputState& InputA, const FInputState& InputB);

    /** Advance by exactly one fixed tick. Used by deterministic tests and by
     *  future rollback/ re-simulation. Inputs are consumed (edges cleared). */
    void StepFixed(FInputState InputA, FInputState InputB);

    const FFighterState& GetFighter(ESide Side) const;
    const FMatchState& GetMatch() const { return Match; }
    const FMatchRules& GetRules() const { return Rules; }
    const FFighterConfig& GetConfig(ESide Side) const;
    ETrainingMode GetTrainingMode(ESide Side) const;

    /** True while the match is being contested (inputs are honoured). */
    bool IsFighting() const { return Match.Phase == EMatchPhase::Fighting; }
    bool IsMatchOver() const { return Match.Phase == EMatchPhase::MatchEnd; }

    /** Pop the oldest pending event. Returns false when the queue is empty. */
    bool PopEvent(FCombatEventRecord& OutEvent);
    int32_t PendingEventCount() const { return EventCount; }

    /** Fraction of the match's configured round clock still remaining, 0..1. */
    float GetRoundTimeFraction() const;

    /** Cumulative damage dealt by a side across the match (for judging/summary). */
    float GetTotalDamageDealt(ESide Side) const;

    /** Exposed for tests: the tick at which the given attack's active window
     *  begins and ends, relative to the attack's start tick. */
    static void GetAttackWindow(const FAttackDef& Def, int32_t& OutStartupTicks,
                                int32_t& OutActiveTicks, int32_t& OutRecoveryTicks);

private:
    void ResetFighter(ESide Side);
    void ResetRoundStatistics();
    void StartRound();
    void BeginCountdown();
    void EnterRoundEnd(ERoundEndReason Reason);
    void EnterMatchEnd(EMatchEndReason Reason, EMatchResult Result);
    void TickPhase(float UnusedDeltaFixed);
    void TickFighter(ESide Side, FInputState& In);
    void ResolveAttackPhases(ESide Side);
    void ResolveDodge(ESide Side);
    void ApplyMovement(ESide Side, FInputState& In);
    void EnforceSeparationAndBounds();
    void ResolveHits();
    void StartAttack(ESide Side, EAction Action);
    void ApplyHit(ESide Attacker, ESide Defender, const FAttackDef& Def, bool bCounter,
                  float DefenderStaminaBefore);
    void ApplyBlockedHit(ESide Attacker, ESide Defender, const FAttackDef& Def);
    void ApplyKnockdown(ESide Side);
    bool HasStamina(ESide Side, float Cost) const;
    void PushEvent(ECombatEvent Type, ESide Side, EAction Action = EAction::Idle, float Value = 0.0f);
    void SpendStamina(ESide Side, float Amount);
    void RegenerateStamina(ESide Side, float DeltaSeconds);
    ESide Opponent(ESide Side) const;
    FFighterState& StateOf(ESide Side);
    const FFighterState& StateOf(ESide Side) const;
    FFighterConfig& ConfigOf(ESide Side);
    const FAttackDef& AttackDefOf(ESide Side, EAction Action) const;
    /** Returns true when the defender's hurtbox overlaps the attack hitbox.
     *  Hurtboxes are explicit gameplay slabs driven by FAttackDef / FFighterConfig,
     *  never the PNG silhouettes. */
    bool AttackOverlaps(ESide AttackerSide, const FAttackDef& Def, ESide DefenderSide) const;
    void AwardRoundTo(ESide Side);
    void ScoreRoundsAndFinish();
    ESide JudgeRound() const;

    FMatchRules Rules;
    FFighterConfig Configs[2];
    FFighterState Fighters[2];
    FMatchState Match;
    ETrainingMode Training[2];

    float TickAccumulator = 0.0f;
    int32_t CurrentRoundScored = 0; // highest round index already added to the judges' cards

    FCombatEventRecord Events[MaxEventQueue];
    int32_t EventHead = 0;
    int32_t EventCount = 0;
};

} // namespace RKC
