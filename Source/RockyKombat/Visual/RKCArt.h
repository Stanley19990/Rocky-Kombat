// RKCArt.h
//
// Runtime access to the imported Paper2D artwork, kept deliberately separate
// from the greybox presentation in RKCVisuals.
//
// WHY THIS EXISTS: the fighters and the ring must keep working when the artwork
// is not there. A clean source checkout has no Content/RockyKombat/Art at all,
// so every lookup here is soft (a string path resolved at runtime) and every
// caller must handle a null result by falling back to the greybox rig. Nothing
// in this file decides anything about gameplay: it maps simulation state to a
// flipbook and nothing else.
//
// ASSET LAYOUT (produced by Tools/ArtImport/rkc_import_art.py):
//   /Game/RockyKombat/Art/Materials/M_RKC_SpriteMasked
//   /Game/RockyKombat/Art/Materials/M_RKC_SpriteTranslucent
//   /Game/RockyKombat/Art/Materials/M_RKC_Backdrop
//   /Game/RockyKombat/Art/Arena/T_RKC_Arena_MidnightRooftop
//   /Game/RockyKombat/Art/Characters/<Roster>/Flipbooks/FB_<Roster>_<Anim>
//
// The paths are soft strings rather than hard FSoftObjectPath properties in a
// UObject so that the module compiles and links before any art exists. Cook
// inclusion is handled by Config/DefaultGame.ini (DirectoriesToAlwaysCook).

#pragma once

#include "CoreMinimal.h"
#include "Combat/RKCCombatCore.h"
#include "Combat/RKCRoster.h"

class UMaterialInterface;
class UPaperFlipbook;
class UTexture2D;

namespace RKCArt
{
/** Root folder of every imported asset. */
extern const TCHAR* const ArtRoot;

/** One fighter's complete animation set, resolved once at spawn.
 *
 *  bValid is false when the artwork is absent, which is the signal for the
 *  caller to keep presenting the greybox rig. A partially loaded set is still
 *  usable: any individual pointer may be null, and SelectFlipbook() falls back
 *  through Idle for anything missing. */
struct FArtSet
{
    UPaperFlipbook* Idle = nullptr;
    UPaperFlipbook* Walk = nullptr;
    UPaperFlipbook* Jab = nullptr;
    UPaperFlipbook* HeavyPunch = nullptr;
    UPaperFlipbook* Block = nullptr;
    UPaperFlipbook* Dodge = nullptr;
    UPaperFlipbook* Knockdown = nullptr;
    UPaperFlipbook* Victory = nullptr;

    /** True when at least Idle loaded, i.e. the fighter can be drawn as art. */
    bool bValid = false;

    /** Number of non-null entries, reported by the self-test. */
    int32 Count() const;
};

/** Directory holding one roster's flipbooks. */
FString FlipbookDir(RKC::ERoster Roster);

/** Full asset path of one flipbook. */
FString FlipbookPath(RKC::ERoster Roster, const TCHAR* Anim);

/** Loads a flipbook by asset path, or null when it is not present.
 *  Results are cached for the life of the process. */
UPaperFlipbook* LoadFlipbook(const FString& AssetPath);

/** Resolves every animation for one roster.
 *
 *  Shadow's Dodge deliberately resolves to FB_Shadow_Dodge_Safe: the pack
 *  documents frames 2-3 of that animation as a low fallen pose that must not be
 *  used as an evade (START_HERE.txt, "IMPORTANT QUALITY LIMIT"). The importer
 *  builds that two-frame subset. */
FArtSet LoadArtSet(RKC::ERoster Roster);

/** Chooses the flipbook that presents a fighter state. Never returns null when
 *  Set.bValid is true. */
UPaperFlipbook* SelectFlipbook(const FArtSet& Set, const RKC::FFighterState& S);

/** The sprite material for the flipbooks (masked, unlit). Null when absent. */
UMaterialInterface* LoadSpriteMaterial();

/** The unlit backdrop material for the arena image. Null when absent. */
UMaterialInterface* LoadBackdropMaterial();

/** The Midnight Rooftop Ring image. Null when absent. */
UTexture2D* LoadArenaTexture();

/** True when the minimum art set needed to draw a match is present. */
bool IsArtAvailable();

/** Releases the caches. Only used by diagnostics that re-scan after an import. */
void FlushCache();
}
