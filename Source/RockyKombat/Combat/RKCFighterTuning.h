#pragma once
#include "Combat/RKCCombatCore.h"

namespace RKC
{
inline FFighterConfig MakePlayableFighterConfig(bool bShadow)
{
    // A default-constructed config is already a legal fighter: both attack slots
    // carry their documented defaults (jab 6 dmg, heavy 15 dmg + knockdown).
    FFighterConfig C;
    C.WalkForwardSpeed = 240.0f;
    C.WalkBackwardSpeed = 180.0f;
    C.Jab.StartupSeconds = 5.0f / 60.0f;
    C.Jab.ActiveSeconds = 3.0f / 60.0f;
    C.Jab.RecoverySeconds = 9.0f / 60.0f;
    C.HeavyPunch.StartupSeconds = 11.0f / 60.0f;
    C.HeavyPunch.ActiveSeconds = 5.0f / 60.0f;
    C.HeavyPunch.RecoverySeconds = 16.0f / 60.0f;
    C.DodgeSeconds = 18.0f / 60.0f;

    if (bShadow)
    {
        // Lightweight: a little less to hit, a little less able to take it,
        // but faster in every phase and recovering stamina sooner.
        C.MaxHealth = 92.0f;
        C.MaxStamina = 108.0f;
        C.WalkForwardSpeed = 265.0f;
        C.WalkBackwardSpeed = 200.0f;
        C.StaminaRegenPerSecond = 28.0f;
        C.DamageTakenMultiplier = 1.08f;
        C.DodgeDistance = 142.0f;
        C.DodgeCooldownSeconds = 0.350f;

        C.Jab = FAttackDef::MakeJab();
        C.Jab.StartupSeconds = 4.0f / 60.0f;   // 4 ticks; Rocky takes 5
        C.Jab.RecoverySeconds = 8.0f / 60.0f;  // 8 ticks
        C.Jab.StaminaCost = 8.0f;

        C.HeavyPunch = FAttackDef::MakeHeavyPunch();
        C.HeavyPunch.StartupSeconds = 10.0f / 60.0f;
        C.HeavyPunch.ActiveSeconds = 5.0f / 60.0f;
        C.HeavyPunch.RecoverySeconds = 15.0f / 60.0f; // 10 ticks
        C.HeavyPunch.Damage = 13.0f;
        C.HeavyPunch.StaminaCost = 18.0f;
    }

    return C;
}
}
