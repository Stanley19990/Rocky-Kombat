// RKCArt.cpp

#include "Visual/RKCArt.h"

#include "Materials/MaterialInterface.h"
#include "PaperFlipbook.h"
#include "RockyKombat.h"

namespace RKCArt
{
const TCHAR* const ArtRoot = TEXT("/Game/RockyKombat/Art");

namespace
{
const TCHAR* const SpriteMaterialPath = TEXT("/Game/RockyKombat/Art/Materials/M_RKC_SpriteMasked");
const TCHAR* const BackdropMaterialPath = TEXT("/Game/RockyKombat/Art/Materials/M_RKC_Backdrop");
const TCHAR* const ArenaTexturePath = TEXT("/Game/RockyKombat/Art/Arena/T_RKC_Arena_MidnightRooftop");

/** Process-lifetime cache. Weak so that a hot-reload or an asset deletion does
 *  not leave a dangling reference held by the module. */
TMap<FString, TWeakObjectPtr<UPaperFlipbook>>& FlipbookCache()
{
    static TMap<FString, TWeakObjectPtr<UPaperFlipbook>> Cache;
    return Cache;
}

/** Shared, non-const, so the cache can be populated on a miss without
 *  const-casting at every call site. */
template <typename T>
T* LoadCached(const TCHAR* Path, TMap<FString, TWeakObjectPtr<T>>& Cache)
{
    const FString Key(Path);
    if (const TWeakObjectPtr<T>* Found = Cache.Find(Key))
    {
        if (Found->IsValid())
        {
            return Found->Get();
        }
    }

    T* Loaded = LoadObject<T>(nullptr, Path);
    Cache.Add(Key, Loaded);
    return Loaded;
}

TMap<FString, TWeakObjectPtr<UMaterialInterface>>& MaterialCache()
{
    static TMap<FString, TWeakObjectPtr<UMaterialInterface>> Cache;
    return Cache;
}

FString RosterName(RKC::ERoster Roster)
{
    return (Roster == RKC::ERoster::Shadow) ? TEXT("Shadow") : TEXT("Rocky");
}
}

int32 FArtSet::Count() const
{
    int32 Total = 0;
    Total += Idle ? 1 : 0;
    Total += Walk ? 1 : 0;
    Total += Jab ? 1 : 0;
    Total += HeavyPunch ? 1 : 0;
    Total += Block ? 1 : 0;
    Total += Dodge ? 1 : 0;
    Total += Knockdown ? 1 : 0;
    Total += Victory ? 1 : 0;
    return Total;
}

FString FlipbookDir(RKC::ERoster Roster)
{
    return FString::Printf(TEXT("%s/Characters/%s/Flipbooks"), ArtRoot, *RosterName(Roster));
}

FString FlipbookPath(RKC::ERoster Roster, const TCHAR* Anim)
{
    return FString::Printf(TEXT("%s/FB_%s_%s"), *FlipbookDir(Roster), *RosterName(Roster), Anim);
}

UPaperFlipbook* LoadFlipbook(const FString& AssetPath)
{
    return LoadCached<UPaperFlipbook>(*AssetPath, FlipbookCache());
}

FArtSet LoadArtSet(RKC::ERoster Roster)
{
    FArtSet Set;

    Set.Idle = LoadFlipbook(FlipbookPath(Roster, TEXT("Idle")));
    Set.Walk = LoadFlipbook(FlipbookPath(Roster, TEXT("Walk")));
    Set.Jab = LoadFlipbook(FlipbookPath(Roster, TEXT("Jab")));
    Set.HeavyPunch = LoadFlipbook(FlipbookPath(Roster, TEXT("HeavyPunch")));
    Set.Block = LoadFlipbook(FlipbookPath(Roster, TEXT("Block")));
    Set.Knockdown = LoadFlipbook(FlipbookPath(Roster, TEXT("Knockdown")));
    Set.Victory = LoadFlipbook(FlipbookPath(Roster, TEXT("Victory")));

    // Shadow's raw Dodge ends on a fallen pose the pack says must not be used as
    // an evade. Prefer the two-frame safe subset whenever it exists, and only
    // fall back to the full animation if the subset was never built.
    if (Roster == RKC::ERoster::Shadow)
    {
        Set.Dodge = LoadFlipbook(FlipbookPath(Roster, TEXT("Dodge_Safe")));
        if (!Set.Dodge)
        {
            Set.Dodge = LoadFlipbook(FlipbookPath(Roster, TEXT("Dodge")));
        }
    }
    else
    {
        Set.Dodge = LoadFlipbook(FlipbookPath(Roster, TEXT("Dodge")));
    }

    // A partially imported set is still usable; Idle is the minimum needed to
    // draw the fighter at all.
    Set.bValid = (Set.Idle != nullptr);

    UE_LOG(LogRockyKombat, Log, TEXT("[RKCArt] %s art set: %d/8 flipbooks%s"),
           *RosterName(Roster), Set.Count(), Set.bValid ? TEXT("") : TEXT(" (art unavailable)"));

    return Set;
}

UPaperFlipbook* SelectFlipbook(const FArtSet& Set, const RKC::FFighterState& S)
{
    if (!Set.bValid)
    {
        return nullptr;
    }

    switch (S.State)
    {
    case RKC::EFighterState::Moving:
        return Set.Walk ? Set.Walk : Set.Idle;

    case RKC::EFighterState::AttackStartup:
    case RKC::EFighterState::AttackActive:
    case RKC::EFighterState::AttackRecovery:
        // Which attack is running is carried by CurrentAction, not by the state.
        if (S.CurrentAction == RKC::EAction::HeavyPunch)
        {
            return Set.HeavyPunch ? Set.HeavyPunch : Set.Idle;
        }
        return Set.Jab ? Set.Jab : Set.Idle;

    case RKC::EFighterState::Blocking:
        return Set.Block ? Set.Block : Set.Idle;

    case RKC::EFighterState::Dodging:
        return Set.Dodge ? Set.Dodge : Set.Idle;

    // The pack ships no dedicated hit-reaction or get-up sequence. Knockdown's
    // first pose stands in as a temporary hit response, exactly as the pack
    // documents, and the knockdown animation itself covers the floor states.
    case RKC::EFighterState::HitStun:
    case RKC::EFighterState::KnockedDown:
        return Set.Knockdown ? Set.Knockdown : Set.Idle;

    case RKC::EFighterState::Victory:
        return Set.Victory ? Set.Victory : Set.Idle;

    case RKC::EFighterState::Defeated:
        return Set.Knockdown ? Set.Knockdown : Set.Idle;

    case RKC::EFighterState::Idle:
    default:
        return Set.Idle;
    }
}

UMaterialInterface* LoadSpriteMaterial()
{
    return LoadCached<UMaterialInterface>(SpriteMaterialPath, MaterialCache());
}

UMaterialInterface* LoadBackdropMaterial()
{
    return LoadCached<UMaterialInterface>(BackdropMaterialPath, MaterialCache());
}

UTexture2D* LoadArenaTexture()
{
    static TWeakObjectPtr<UTexture2D> Cached;
    if (Cached.IsValid())
    {
        return Cached.Get();
    }
    UTexture2D* Loaded = LoadObject<UTexture2D>(nullptr, ArenaTexturePath);
    Cached = Loaded;
    return Loaded;
}

bool IsArtAvailable()
{
    return LoadSpriteMaterial() != nullptr && LoadArenaTexture() != nullptr;
}

void FlushCache()
{
    FlipbookCache().Empty();
    MaterialCache().Empty();
}
}
