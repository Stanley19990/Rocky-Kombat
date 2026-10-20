// RKCFighter.h
//
// A fighter in the ring.
//
// RESPONSIBILITY SPLIT (important for later netcode work):
//   * RKC::FCombatSim owns the truth: position, health, stamina, state machine.
//   * ARKCFighter owns presentation only. It never decides anything. Each frame
//     the match director calls SyncFromSim(), which reads the simulation state
//     and poses the greybox rig to match it.
// The fighter is a Pawn so that a PlayerController can possess it and so that
// input for that side has an obvious owner, but it deliberately has no
// MovementComponent: the simulation is the only thing that moves a fighter.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Combat/RKCCombatCore.h"
#include "Combat/RKCRoster.h"
#include "Visual/RKCArt.h"
#include "RKCFighter.generated.h"

class UPaperFlipbook;
class UPaperFlipbookComponent;
class USceneComponent;
class UStaticMeshComponent;

UCLASS()
class ROCKYKOMBAT_API ARKCFighter : public APawn
{
    GENERATED_BODY()

public:
    ARKCFighter();

    /** Attaches the fighter to a side of a live simulation and builds its rig. */
    void Init(RKC::FCombatSim* InSim, RKC::ESide InSide, RKC::ERoster InRoster, float InRingY);

    /** Reads the simulation and updates the visual rig. Called once per frame by
     *  the match director. Does nothing before Init(). */
    void SyncFromSim();

    RKC::ESide GetSide() const { return Side; }
    RKC::ERoster GetRoster() const { return Roster; }
    bool IsReady() const { return Sim != nullptr && bRigBuilt; }

    /** Training aid (E TrainingMode::ShowHitboxes). */
    void SetShowHitboxes(bool bInShow) { bShowHitboxes = bInShow; }
    bool IsShowingHitboxes() const { return bShowHitboxes; }

    /** Count of greybox parts created, used by tests/diagnostics to prove the rig
     *  exists rather than assuming it. */
    int32 GetPartCount() const { return PartCount; }

    // ---- Imported artwork ----------------------------------------------------
    // ART IS ADDITIVE, NEVER A REPLACEMENT. The greybox rig is always built and
    // always kept; when the imported flipbooks are present they are shown and the
    // greybox body parts are hidden by a visibility toggle. Deleting the art from
    // the project therefore restores the previous presentation with no code
    // change, and a clean source checkout still plays.

    /** True when imported flipbook art is driving this fighter's body. */
    bool IsUsingArtwork() const { return bArtActive; }

    /** How many of the eight animations resolved. 0 means no art at all. */
    int32 GetArtFlipbookCount() const { return ArtSet.Count(); }

    /** Name of the flipbook currently on screen, for diagnostics. */
    FString GetCurrentArtName() const;

    /** The component that draws the imported artwork, or null before Init() and
     *  when no art was found. Exposed so the in-engine self-test can read the
     *  material the sprite path actually shades with instead of inferring it. */
    UPaperFlipbookComponent* GetSpriteComponent() const { return Sprite; }

    /** Shows or hides the greybox body parts. A visibility flip, never a delete. */
    void SetGreyboxVisible(bool bVisible);

protected:
    virtual void BeginPlay() override;

private:
    void BuildRig();
    void UpdatePose(const RKC::FFighterState& S, const RKC::FFighterConfig& Cfg);

    /** Resolves the artwork and brings the sprite component up. Safe to call when
     *  no art is present, in which case it does nothing and the greybox stays. */
    void InitArt();

    /** Drives the sprite component from simulation state. Called after
     *  UpdatePose() so the two never fight over what is visible. */
    void UpdateArt(const RKC::FFighterState& S);

    UPROPERTY(VisibleAnywhere, Category = "RKC|Fighter")
    TObjectPtr<USceneComponent> Root;

    /** Everything visible hangs off Body, so knockdown/lean can move the whole
     *  figure without touching the actor transform the simulation owns. */
    UPROPERTY(VisibleAnywhere, Category = "RKC|Fighter")
    TObjectPtr<USceneComponent> Body;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> Legs;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> Trunks;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> Torso;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> Head;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> GloveLead;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> GloveRear;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> HitboxDebug;

    /** Paper2D flipbook that draws the imported artwork. Created in the
     *  constructor but left without a flipbook, and therefore invisible, until
     *  InitArt() finds one. */
    UPROPERTY(VisibleAnywhere, Category = "RKC|Fighter")
    TObjectPtr<UPaperFlipbookComponent> Sprite;

    /** Keeps every resolved flipbook alive for the fighter's lifetime. The set is
     *  held separately from the sprite component so that switching back to the
     *  greybox rig cannot drop the last reference mid-match. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UPaperFlipbook>> ArtKeepAlive;

    /** Resolved artwork for this fighter. Valid without art present. */
    RKCArt::FArtSet ArtSet;

    /** Greybox body parts, cached for the visibility toggle. HitboxDebug is
     *  deliberately NOT in this list: the training hitbox stays on screen
     *  whichever body is being drawn. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> GreyboxParts;

    bool bArtActive = false;

    /** The roster the currently presented artwork was resolved for, as an int32
     *  so that "no artwork loaded on this actor yet" has an unambiguous sentinel.
     *  -1 means nothing has been loaded here. A rematch reuses the fighter actors
     *  but may hand them a different roster, so InitArt() compares this against
     *  the requested roster instead of trusting bArtActive alone. */
    int32 LoadedArtRoster = -1;

    /** State and action of the previously presented flipbook, so a new animation
     *  is only restarted when the state actually changes. 255 means "none yet". */
    uint8 LastArtState = 255;
    uint8 LastArtAction = 255;

    RKC::FCombatSim* Sim = nullptr;
    RKC::ESide Side = RKC::ESide::A;
    RKC::ERoster Roster = RKC::ERoster::Rocky;

    float RingY = 0.0f;
    bool bRigBuilt = false;
    bool bShowHitboxes = false;
    int32 PartCount = 0;
    FLinearColor LastTorsoColour = FLinearColor(-1.0f, -1.0f, -1.0f, -1.0f);
};
