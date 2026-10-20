// RKCRoster.cpp

#include "Combat/RKCRoster.h"

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
    // A default-constructed config is already a legal fighter: both attack slots
    // carry their documented defaults (jab 6 dmg, heavy 15 dmg + knockdown).
    FFighterConfig C;

    if (Who == ERoster::Shadow)
    {
        // Lightweight: a little less to hit, a little less able to take it,
        // but faster in every phase and recovering stamina sooner.
        C.MaxHealth = 92.0f;
        C.MaxStamina = 108.0f;
        C.WalkForwardSpeed = 205.0f;
        C.WalkBackwardSpeed = 152.0f;
        C.StaminaRegenPerSecond = 28.0f;
        C.DamageTakenMultiplier = 1.08f;
        C.DodgeDistance = 142.0f;
        C.DodgeCooldownSeconds = 0.350f;

        C.Jab = FAttackDef::MakeJab();
        C.Jab.StartupSeconds = 0.083f;   // 5 ticks, one faster than Rocky's 6
        C.Jab.RecoverySeconds = 0.167f;  // 10 ticks
        C.Jab.StaminaCost = 8.0f;

        C.HeavyPunch = FAttackDef::MakeHeavyPunch();
        C.HeavyPunch.StartupSeconds = 0.183f; // 11 ticks
        C.HeavyPunch.Damage = 13.0f;
        C.HeavyPunch.StaminaCost = 18.0f;
    }

    return C;
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
