#include "Combat/RKCFighterTuning.h"
#include "AI/RKCAI.h"
#include <cstdio>
#include <cmath>

int main()
{
    using namespace RKC;
    int Failures=0, Wins=0, Completed=0, Hits=0, Blocks=0, Dodges=0;
    const auto Rocky=MakePlayableFighterConfig(false), Shadow=MakePlayableFighterConfig(true);
    if (!(Rocky.WalkForwardSpeed==240 && Shadow.WalkForwardSpeed==265 && Rocky.WalkBackwardSpeed==180 && Shadow.WalkBackwardSpeed==200)) ++Failures;
    int S,A,R; FCombatSim::GetAttackWindow(Rocky.Jab,S,A,R); if(S!=5 || A!=3 || R!=9) ++Failures;
    FCombatSim::GetAttackWindow(Shadow.Jab,S,A,R); if(S!=4 || R!=8) ++Failures;
    FMatchRules Rules; Rules.Rounds=1; Rules.RoundSeconds=30; Rules.CountdownSeconds=.1f;
    for (int I=1; I<=100; ++I) {
        const auto& C=(I%2)?Rocky:Shadow;
        auto Outcome=RunBotMatch(Rules,C,C,FBotProfile::Make(EBotArchetype::Balanced,EBotDifficulty::Rookie),FBotProfile::Make(EBotArchetype::Counter,EBotDifficulty::Champion),I*97,I*211);
        Wins+=Outcome.Result==EMatchResult::FighterBWins; Completed+=!Outcome.bTimedOut;
        Hits+=Outcome.HitsLandedB; Blocks+=Outcome.BlocksB; Dodges+=Outcome.DodgesB;
    }
    std::printf("SHIPPED ROSTER: Rocky 240/180 cm/s, Shadow 265/200 cm/s; jab windows 5/3/9 and 4/4/8 ticks\n");
    std::printf("Champion Counter vs Rookie Balanced, identical stats: %d wins / 100, %d completed, %d hits, %d blocks, %d dodges\n",Wins,Completed,Hits,Blocks,Dodges);
    if (Wins<70 || Completed!=100 || Hits<100 || Blocks==0 || Dodges==0) ++Failures;
    std::printf("BALANCE_RESULT: %s (%d failures)\n",Failures?"FAIL":"PASS",Failures);
    return Failures?1:0;
}
