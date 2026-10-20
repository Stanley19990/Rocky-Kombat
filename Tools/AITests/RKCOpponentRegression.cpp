// RKCOpponentRegression.cpp
//
// Focused regression test for the offline-opponent defect reported from play:
// "the CPU fighter does not fight back and he does not move".
//
// Root cause it guards against: the shipped rules enforce a minimum separation
// between the two fighters, and the bot's strike window used to be derived only
// from its own jab reach (ReachFar - 4 = 100 cm). Whenever the enforced standoff
// was wider than that window, every tactical plan the bot could pick ended in
// "outside strike range", so it spent the round walking into its opponent and
// never threw a punch. Measured live before the fix: a 106 cm standoff against a
// 100 cm jab window, the player's health untouched at 100/100 for a whole round.
//
// This harness plays the SHIPPED AI brain and the SHIPPED combat core against a
// human who never presses anything (an empty FInputState every tick, which is
// exactly what a player who is not touching the controls does) at real game rule
// values, and asserts the bot attacks, lands and hurts them at every standoff
// the game is allowed to hand it.
//
// The human side is driven here rather than through ETrainingMode::NoOpponentInput
// on purpose: that aid is carried by the side it is FOR and blanks the OTHER
// side's input (RKCCombatCore.cpp StepFixed), so passing it for side B would
// have frozen the bot instead. The shipped training preset that makes the CPU
// passive is TrainingA = NoOpponentInput, and it is correct as written.
//
// Pure C++: no engine headers, no engine time, no engine RNG.

#include "AI/RKCAI.h"
#include "Combat/RKCCombatCore.h"

#include <cmath>
#include <cstdio>

namespace
{
int Failures = 0;
int Checks = 0;

void Check(bool bCondition, const char* What)
{
    ++Checks;
    if (!bCondition)
    {
        ++Failures;
        std::printf("      FAIL  %s\n", What);
    }
}

/** Shipped rules with the enforced standoff replaced. A default-constructed
 *  FMatchRules is exactly what the game's MakeDefaultRules() returns
 *  (RKCRoster.cpp: "return FMatchRules()"); the game mode then overrides
 *  MinSeparation from rkc.MinSeparation. */
RKC::FMatchRules RulesWithStandoff(float MinSeparation)
{
    RKC::FMatchRules Rules;
    Rules.MinSeparation = MinSeparation;
    return Rules;
}

/** A default-constructed FFighterConfig is the Rocky roster entry exactly: the
 *  roster only overrides health, stamina, walk speed, dodge and Shadow's frame
 *  data, and both roster entries keep the shared jab/heavy reach. This harness
 *  has to stay engine-free (RKCRoster.cpp pulls CoreMinimal.h), so the reach
 *  comes from the same config defaults the roster builds on. */
RKC::FFighterConfig ShippedConfig()
{
    return RKC::FFighterConfig();
}

/** The standoff ceiling the game mode enforces (RKCGameMode::ResolveRules):
 *  the SHORTER of the two punches minus a margin, so a configured standoff can
 *  never be placed beyond what the bot can actually punch across. */
float GameModeStandoffCeiling()
{
    const RKC::FFighterConfig Probe = ShippedConfig();
    return std::fmin(Probe.Jab.ReachFar, Probe.HeavyPunch.ReachFar) - 12.0f;
}

/** How the human side behaves. */
enum class EHuman : int
{
    Still = 0,     // never touches the controls
    HoldRight = 1, // pins themselves against the right-hand arena bound
    HoldLeft = 2,  // pins themselves against the left-hand arena bound
    Mashing = 3    // changes direction every 10 ticks
};

struct FDuel
{
    int AttacksThrown = 0;
    int HitsLanded = 0;
    int DamageEvents = 0;
    float DamageDealt = 0.0f;
    float HumanHealth = 0.0f;
    float LeftMostHumanX = 1.0e9f;
    float RightMostHumanX = -1.0e9f;
    int AttacksWhilePinned = 0;
    int TicksPinned = 0;
    float FinalSeparation = 0.0f;
    bool bTimedOut = true;
    RKC::FBotMatchOutcome Outcome;
};

/** Plays the shipped bot (side A) against a human on side B. Side A's input
 *  always comes from the shipped FBotBrain. */
FDuel RunDuel(float MinSeparation, RKC::EBotDifficulty Difficulty, uint32_t Seed,
              EHuman Human, int MaxTicks = 12000)
{
    FDuel D;

    const RKC::FMatchRules Rules = RulesWithStandoff(MinSeparation);
    const RKC::FFighterConfig HumanConfig = ShippedConfig();

    RKC::FCombatSim Sim;
    Sim.Init(Rules, ShippedConfig(), HumanConfig);

    RKC::FBotBrain Brain;
    Brain.Init(RKC::ESide::A, RKC::FBotProfile::Make(RKC::EBotArchetype::Balanced, Difficulty),
               Seed);

    const float Dt = RKC::FCombatSim::FixedTickSeconds;

    int Tick = 0;
    int PrevAttacks = 0;
    float PrevHumanHealth = HumanConfig.MaxHealth;
    bool bSawPinned = false;

    while (Tick < MaxTicks && !Sim.IsMatchOver())
    {
        const RKC::FInputState BotInput = Brain.ComputeInput(Dt, Sim);

        RKC::FInputState HumanInput;
        switch (Human)
        {
        case EHuman::HoldRight: HumanInput.bMoveRight = true; break;
        case EHuman::HoldLeft: HumanInput.bMoveLeft = true; break;
        case EHuman::Mashing:
            if (((Tick / 10) % 2) == 0)
            {
                HumanInput.bMoveLeft = true;
            }
            else
            {
                HumanInput.bMoveRight = true;
            }
            break;
        case EHuman::Still:
        default:
            break;
        }

        Sim.StepFixed(BotInput, HumanInput);
        ++Tick;

        const RKC::FFighterState& HumanState = Sim.GetFighter(RKC::ESide::B);
        const float BoundDistance = std::fmin(HumanState.PositionX - Rules.ArenaMinX,
                                              Rules.ArenaMaxX - HumanState.PositionX);
        if (BoundDistance < 8.0f)
        {
            bSawPinned = true;
            ++D.TicksPinned;
        }

        const int Attacks = Sim.GetFighter(RKC::ESide::A).AttacksThrownTotal;
        if (Attacks > PrevAttacks)
        {
            if (bSawPinned)
            {
                D.AttacksWhilePinned += (Attacks - PrevAttacks);
            }
            PrevAttacks = Attacks;
        }

        if (HumanState.Health < PrevHumanHealth)
        {
            ++D.DamageEvents;
            PrevHumanHealth = HumanState.Health;
        }

        D.LeftMostHumanX = std::fmin(D.LeftMostHumanX, HumanState.PositionX);
        D.RightMostHumanX = std::fmax(D.RightMostHumanX, HumanState.PositionX);
    }

    const RKC::FFighterState& A = Sim.GetFighter(RKC::ESide::A);
    const RKC::FFighterState& B = Sim.GetFighter(RKC::ESide::B);

    D.AttacksThrown = A.AttacksThrownTotal;
    D.HitsLanded = A.HitsLandedTotal;
    D.DamageDealt = A.DamageDealtTotal;
    D.HumanHealth = B.Health;
    D.FinalSeparation = std::fabs(B.PositionX - A.PositionX);
    D.bTimedOut = !Sim.IsMatchOver();

    // Fill the shipped outcome structure so the raw numbers can be cross-read
    // against the bot-vs-bot harness output.
    const RKC::FMatchState& Match = Sim.GetMatch();
    D.Outcome.Result = Match.Result;
    D.Outcome.EndReason = Match.EndReason;
    D.Outcome.ScoreA = Match.ScoreA;
    D.Outcome.ScoreB = Match.ScoreB;
    D.Outcome.RoundsFought = Match.Round;
    D.Outcome.Ticks = Tick;
    D.Outcome.DurationSeconds = static_cast<float>(Tick) * Dt;
    D.Outcome.bTimedOut = D.bTimedOut;
    D.Outcome.HitsLandedA = A.HitsLandedTotal;
    D.Outcome.AttacksThrownA = A.AttacksThrownTotal;
    D.Outcome.KnockdownsA = A.KnockdownsTotal;
    D.Outcome.DamageDealtA = A.DamageDealtTotal;
    D.Outcome.HealthA = A.Health;
    D.Outcome.HitsLandedB = B.HitsLandedTotal;
    D.Outcome.AttacksThrownB = B.AttacksThrownTotal;
    D.Outcome.KnockdownsB = B.KnockdownsTotal;
    D.Outcome.DamageDealtB = B.DamageDealtTotal;
    D.Outcome.HealthB = B.Health;
    D.Outcome.BotDecisionsA = Brain.GetDecisionCount();
    D.Outcome.BotAttacksA = Brain.GetAttackOrders();
    D.Outcome.FinalSeparation = D.FinalSeparation;
    D.Outcome.WorstSeparationViolation = 0.0f;

    return D;
}

/** The defect, stated as one predicate: the bot must throw punches, land them,
 *  and actually take health off the human. */
bool FightsBack(const FDuel& D)
{
    return D.AttacksThrown > 0 && D.HitsLanded > 0 && D.DamageDealt > 0.0f && D.DamageEvents > 0;
}
} // namespace

int main()
{
    std::printf("Rocky Kombat - offline opponent regression\n");
    std::printf("==========================================\n\n");

    const RKC::FFighterConfig Probe = ShippedConfig();
    std::printf("=== 0. Frame data this test reasons about ===\n");
    std::printf("      jab reach (cm)                                 %.1f\n", Probe.Jab.ReachFar);
    std::printf("      heavy reach (cm)                               %.1f\n",
                Probe.HeavyPunch.ReachFar);
    std::printf("      jab strike window, no standoff (cm)            %.1f\n",
                Probe.Jab.ReachFar - 4.0f);
    std::printf("      game-mode standoff ceiling (cm)                %.1f\n",
                GameModeStandoffCeiling());
    std::printf("      resolved shipped standoff (cm)                  90.0\n");

    const float ShippedStandoff = 90.0f;
    Check(ShippedStandoff <= GameModeStandoffCeiling(),
          "the shipped standoff is inside what the bot can punch across");
    Check(Probe.Jab.ReachFar - 4.0f < 106.0f,
          "the historical buggy standoff (106 cm) was outside the old jab window");

    std::printf("\n=== 1. Shipped configuration: bot vs a human who never presses a button ===\n");
    {
        const FDuel D = RunDuel(ShippedStandoff, RKC::EBotDifficulty::Champion, 20261019u,
                                EHuman::Still);
        char Line[512];
        RKC::DescribeMatchOutcome(D.Outcome, Line, static_cast<int>(sizeof(Line)));
        std::printf("      %s\n", Line);
        std::printf("      attacks thrown by the bot                      %d\n", D.AttacksThrown);
        std::printf("      punches landed by the bot                      %d\n", D.HitsLanded);
        std::printf("      damage-dealing events                          %d\n", D.DamageEvents);
        std::printf("      damage dealt by the bot                        %.1f\n", D.DamageDealt);
        std::printf("      passive human health remaining                 %.1f\n", D.HumanHealth);

        Check(D.AttacksThrown > 0, "the bot throws at least one punch");
        Check(D.HitsLanded > 0, "at least one punch lands on the passive human");
        Check(D.DamageDealt > 0.0f, "the bot actually hurts the passive human");
        Check(D.DamageEvents > 0, "the human's health bar moves");
        Check(D.HumanHealth < Probe.MaxHealth, "the human is not still at full health");
        Check(!D.bTimedOut, "the fight reaches a decision instead of stalling to the tick limit");
    }

    std::printf("\n=== 2. Standoff sweep: the bot must fight at every standoff it may be given ===\n");
    std::printf("      %-10s %-8s %-8s %-9s %-10s %s\n",
                "standoff", "thrown", "landed", "damage", "human hp", "verdict");
    {
        // The sweep runs to the heavy punch's real reach, because that is the
        // widest standoff the AI can still answer with a punch (a standoff
        // between the jab and heavy windows is handled by the heavy band).
        const float Standoffs[] = {58.0f, 70.0f, 80.0f, 88.0f, 90.0f, 92.0f, 96.0f,
                                   100.0f, 102.0f, 104.0f, 106.0f, 110.0f, 114.0f, 116.0f};
        for (float Sep : Standoffs)
        {
            const FDuel D = RunDuel(Sep, RKC::EBotDifficulty::Champion,
                                    20261019u + static_cast<uint32_t>(Sep), EHuman::Still);
            std::printf("      %-10.0f %-8d %-8d %-9.1f %-10.1f %s\n",
                        Sep, D.AttacksThrown, D.HitsLanded, D.DamageDealt, D.HumanHealth,
                        FightsBack(D) ? "fights back" : "PASSIVE - DEFECT");
            Check(FightsBack(D), "the bot fights back at every standoff it can reach");
        }
    }

    std::printf("\n=== 3. Beyond the heavy punch: the game-mode clamp is the only guard ===\n");
    {
        // A standoff past both punches cannot be attacked across by any window;
        // this is why ResolveRules must keep clamping the configured standoff to
        // GameModeStandoffCeiling(). Reported here so the limit is on the record.
        const float Unreachable[] = {120.0f, 140.0f};
        for (float Sep : Unreachable)
        {
            const FDuel D = RunDuel(Sep, RKC::EBotDifficulty::Champion,
                                    20261019u + static_cast<uint32_t>(Sep), EHuman::Still);
            std::printf("      standoff %-6.0f thrown=%-4d landed=%-4d damage=%-8.1f "
                        "beyond-heavy-reach=%s\n",
                        Sep, D.AttacksThrown, D.HitsLanded, D.DamageDealt,
                        (Sep > Probe.HeavyPunch.ReachFar) ? "yes" : "no");
            Check(Sep > GameModeStandoffCeiling(),
                  "the game-mode clamp rejects a standoff past the reach ceiling");
        }
    }

    std::printf("\n=== 4. The default difficulty must press the attack, not stall ===\n");
    {
        const FDuel Rookie = RunDuel(ShippedStandoff, RKC::EBotDifficulty::Rookie,
                                     20261019u, EHuman::Still);
        const FDuel Champion = RunDuel(ShippedStandoff, RKC::EBotDifficulty::Champion,
                                       20261019u, EHuman::Still);
        std::printf("      rookie   thrown=%-4d landed=%-4d damage=%-7.1f decisions=%d\n",
                    Rookie.AttacksThrown, Rookie.HitsLanded, Rookie.DamageDealt,
                    Rookie.Outcome.BotDecisionsA);
        std::printf("      champion thrown=%-4d landed=%-4d damage=%-7.1f decisions=%d\n",
                    Champion.AttacksThrown, Champion.HitsLanded, Champion.DamageDealt,
                    Champion.Outcome.BotDecisionsA);
        Check(FightsBack(Rookie), "Rookie fights back");
        Check(FightsBack(Champion), "Champion fights back");
        Check(Champion.Outcome.BotDecisionsA > Rookie.Outcome.BotDecisionsA,
              "Champion re-reads the fight more often than Rookie (sharper, not passiver)");
    }

    std::printf("\n=== 5. The bot keeps fighting with the human jammed on a bound ===\n");
    {
        const RKC::FMatchRules Rules = RulesWithStandoff(ShippedStandoff);
        const FDuel Right = RunDuel(ShippedStandoff, RKC::EBotDifficulty::Champion, 424242u,
                                    EHuman::HoldRight);
        std::printf("      human holds right: travelled %.1f..%.1f cm, pinned for %d ticks\n",
                    Right.LeftMostHumanX, Right.RightMostHumanX, Right.TicksPinned);
        std::printf("      thrown=%-4d landed=%-4d damage=%-7.1f attacks-while-pinned=%d\n",
                    Right.AttacksThrown, Right.HitsLanded, Right.DamageDealt,
                    Right.AttacksWhilePinned);
        Check(Right.TicksPinned > 0, "the human did reach the right-hand bound");
        Check(Right.AttacksWhilePinned > 0, "the bot keeps punching a human jammed on the bound");
        Check(Right.DamageDealt > 0.0f, "the bot still lands damage on a jammed human");
        Check(Right.RightMostHumanX <= Rules.ArenaMaxX + 0.5f,
              "the human never leaves the arena at the right-hand bound");

        // Walking left means walking INTO the bot (the bot spawns on the left of
        // the human and pushes them right), so this case never reaches a bound:
        // the human is knocked out on the way. It is kept because it is the
        // opposite drift direction, and it must still produce a fight.
        const FDuel Left = RunDuel(ShippedStandoff, RKC::EBotDifficulty::Champion, 424242u,
                                   EHuman::HoldLeft);
        std::printf("      human holds left:  travelled %.1f..%.1f cm, pinned for %d ticks\n",
                    Left.LeftMostHumanX, Left.RightMostHumanX, Left.TicksPinned);
        std::printf("      thrown=%-4d landed=%-4d damage=%-7.1f attacks-while-pinned=%d\n",
                    Left.AttacksThrown, Left.HitsLanded, Left.DamageDealt,
                    Left.AttacksWhilePinned);
        Check(FightsBack(Left), "the bot fights back against a human walking into it");
        Check(Left.LeftMostHumanX >= Rules.ArenaMinX - 0.5f,
              "the human never leaves the arena at the left-hand bound");
    }

    std::printf("\n=== 6. A human who moves still gets fought ===\n");
    {
        const FDuel D = RunDuel(ShippedStandoff, RKC::EBotDifficulty::Champion, 777u,
                                EHuman::Mashing);
        std::printf("      thrown=%-4d landed=%-4d damage=%-7.1f human hp=%.1f\n",
                    D.AttacksThrown, D.HitsLanded, D.DamageDealt, D.HumanHealth);
        Check(FightsBack(D), "the bot fights back against a moving human");
    }

    std::printf("\n=========== SUMMARY ===========\n");
    std::printf("checks run : %d\n", Checks);
    std::printf("failures   : %d\n", Failures);
    std::printf("result     : %s\n", (Failures == 0) ? "PASS" : "FAIL");
    std::printf("===============================\n");
    return (Failures == 0) ? 0 : 1;
}
