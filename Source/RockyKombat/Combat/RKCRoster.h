// RKCRoster.h
//
// Rocky Kombat - the initial RBA roster and the match-rule presets.
//
// The roster is deliberately small (two fighters) for the first milestone:
// acceptance gate B only requires two placeholder fighters to complete a
// reliable offline match. Both entries are built on the same documented
// combat core; they differ only through FFighterConfig values, which is how
// weight divisions are expressed (see docs/design-decisions.md).

#pragma once

#include "CoreMinimal.h"
#include "Combat/RKCCombatCore.h"

namespace RKC
{
/** The two launch RBA fighters. */
enum class ERoster : uint8_t
{
    Rocky = 0,   // Middleweight - balanced pressure boxer
    Shadow = 1,  // Lightweight  - quicker, lighter hands, more fragile
    Count = 2
};

/** Name shown on the fighter banner and in match results. */
FString GetRosterDisplayName(ERoster Who);

/** Weight division label. RBA divisions are RBA-only; Street Fights must not
 *  inherit these (see the product brief, section 15). */
FString GetRosterDivisionName(ERoster Who);

/** Full fighter configuration for a roster entry. Both attack slots are always
 *  populated, so a returned config is immediately legal. */
FFighterConfig MakeFighterConfig(ERoster Who);

/** Rank of the arena the fighter is introduced in, used by the intro banner. */
FString GetRosterRecord(ERoster Who);

/** Shipped default RBA rules: 3 rounds of 30 seconds, 3-knockdown rule. */
FMatchRules MakeDefaultRules();

/** Single-round exhibition used for fast local testing and smoke runs.
 *  Round length follows the rkc.RoundSeconds override when one is set. */
FMatchRules MakeQuickRules();
}
