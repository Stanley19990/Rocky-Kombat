// RKCRoster.cpp

#include "Combat/RKCRoster.h"
#include "Combat/RKCFighterTuning.h"

namespace RKC
{
FString GetRosterDisplayName(ERoster Who)
{
    switch (Who)
    {
    case ERoster::Shadow:
        return TEXT("SHADOW");
    case ERoster::Rocky:
    default:
        return TEXT("ROCKY");
    }
}

FString GetRosterDivisionName(ERoster Who)
{
    switch (Who)
    {
    case ERoster::Shadow:
        return TEXT("LIGHTWEIGHT");
    case ERoster::Rocky:
    default:
        return TEXT("MIDDLEWEIGHT");
    }
}

FString GetRosterRecord(ERoster Who)
{
    switch (Who)
    {
    case ERoster::Shadow:
        return TEXT("14-2-0  (6 KO)");
    case ERoster::Rocky:
    default:
        return TEXT("18-1-0  (12 KO)");
    }
}

FFighterConfig MakeFighterConfig(ERoster Who)
{
    return MakePlayableFighterConfig(Who == ERoster::Shadow);
}

FMatchRules MakeDefaultRules()
{
    // 3 rounds of 30 seconds, as specified by the product brief.
    return FMatchRules();
}

FMatchRules MakeQuickRules()
{
    FMatchRules R;
    R.Rounds = 1;
    R.RoundSeconds = 20.0f;
    R.CountdownSeconds = 2.0f;
    R.RoundBreakSeconds = 4.0f;
    R.PostMatchSeconds = 6.0f;
    return R;
}
}
