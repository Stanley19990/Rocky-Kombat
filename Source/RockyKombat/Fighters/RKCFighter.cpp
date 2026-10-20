// RKCFighter.cpp
//
// Greybox fighter rig. The rig is posed purely from simulation state, so what is
// on screen is a direct reading of the rules: if the fighter looks like it is
// blocking, the simulation really does have it in EFighterState::Blocking.

#include "Fighters/RKCFighter.h"

// UE 5.8: the Paper2D plugin's Classes/ folder is itself a public include root,
// so the sprite headers are included without the "Components/" prefix. The
// prefixed form fails with C1083 (observed: build_editor.log, 2026-10-17).
#include "PaperFlipbookComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "PaperFlipbook.h"
#include "RockyKombat.h"
#include "Visual/RKCArt.h"
#include "Visual/RKCVisuals.h"

using namespace RKCVisuals;

namespace
{
constexpr float NeutralLeadX = 34.0f;
constexpr float NeutralRearX = 6.0f;
/** How far the striking glove travels on an active frame, as a fraction of the
 *  punch's own reach. The gameplay hitbox is authoritative; this only makes the
 *  active window visible. */
constexpr float ActiveExtensionFraction = 0.62f;
}

ARKCFighter::ARKCFighter()
{
    PrimaryActorTick.bCanEverTick = false;
    bUseControllerRotationYaw = false;
    AutoPossessPlayer = EAutoReceiveInput::Disabled;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("FighterRoot"));
    SetRootComponent(Root);

    Body = CreateDefaultSubobject<USceneComponent>(TEXT("Body"));
    Body->SetupAttachment(Root);

    // There is deliberately no world-space nameplate over the fighter. The old
    // UTextRenderComponent one sat at the actor root, i.e. on the floor between
    // the two boxers, and in a cooked build it resolved to a black mass instead
    // of the intended gold/blue text (see docs/06-known-issues.md section 4),
    // which obscured the artwork. The HUD already names both fighters in the
    // side panels and in the centre scoreboard, so identity never depended on it.

    // The artwork body. It hangs off Root rather than Body on purpose: the sprite
    // carries its own baked poses, so it must not inherit the greybox lean/bob
    // offsets that UpdatePose() applies to Body. It is born invisible and only
    // becomes visible once a real flipbook has been resolved.
    Sprite = CreateDefaultSubobject<UPaperFlipbookComponent>(TEXT("Sprite"));
    Sprite->SetupAttachment(Root);
    Sprite->SetMobility(EComponentMobility::Movable);
    Sprite->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Sprite->SetGenerateOverlapEvents(false);
    Sprite->SetCastShadow(false);
    Sprite->bCastDynamicShadow = false;
    Sprite->SetVisibility(false);
    // Paper2D draws sprites in the X/Z plane, which is the plane this project's
    // camera looks along, so no component rotation is required. The actor's own
    // 180-degree yaw turns the sprite to face the other way, exactly as the
    // simulation's Facing flag says it should.
}

void ARKCFighter::BeginPlay()
{
    Super::BeginPlay();
    BuildRig();
}

void ARKCFighter::Init(RKC::FCombatSim* InSim, RKC::ESide InSide, RKC::ERoster InRoster, float InRingY)
{
    Sim = InSim;
    Side = InSide;
    Roster = InRoster;
    RingY = InRingY;

    BuildRig();

    // Resolve the artwork only once the roster is known. BeginPlay() may already
    // have built the greybox with the default roster, so the rig colours are
    // corrected here before the artwork (which ignores them) takes over.
    // Explicit for both rosters, not just for Shadow: a rematch can reuse this
    // very actor with the other character selected, and the trunks colour was
    // baked at rig-build time for whatever roster was set back then.
    if (bRigBuilt && Trunks)
    {
        SetShapeColour(Trunks, Roster == RKC::ERoster::Shadow ? Palette::ShadowTrunks
                                                              : Palette::RockTrunks);
    }
    InitArt();

    SyncFromSim();
}

void ARKCFighter::BuildRig()
{
    if (bRigBuilt)
    {
        return;
    }
    if (!ArePrimitivesAvailable())
    {
        UE_LOG(LogRockyKombat, Error,
               TEXT("Fighter rig could not be built: engine basic shapes are missing."));
        return;
    }

    const bool bIsShadow = (Roster == RKC::ERoster::Shadow);
    const FLinearColor SkinColour = bIsShadow ? Palette::ShadowSkin : Palette::RockSkin;
    const FLinearColor TrunksColour = bIsShadow ? Palette::ShadowTrunks : Palette::RockTrunks;

    // The rig stands with its feet on Z = 0 and faces +X. Facing -X is the actor's
    // own 180 degree yaw, never a mirrored rig, so both fighters are identical.
    Legs = AddShape(this, Body, TEXT("Legs"), FVector(46.0f, 38.0f, 92.0f),
                    FVector(0.0f, 0.0f, 46.0f), Palette::Boot);
    Trunks = AddShape(this, Body, TEXT("Trunks"), FVector(62.0f, 42.0f, 30.0f),
                      FVector(0.0f, 0.0f, 101.0f), TrunksColour);
    Torso = AddShape(this, Body, TEXT("Torso"), FVector(60.0f, 40.0f, 72.0f),
                     FVector(0.0f, 0.0f, 131.0f), SkinColour);
    Head = AddShape(this, Body, TEXT("Head"), FVector(38.0f, 38.0f, 38.0f),
                    FVector(0.0f, 0.0f, 186.0f), SkinColour);
    GloveLead = AddShape(this, Body, TEXT("GloveLead"), FVector(28.0f, 28.0f, 28.0f),
                         FVector(NeutralLeadX, -20.0f, 148.0f), Palette::Glove);
    GloveRear = AddShape(this, Body, TEXT("GloveRear"), FVector(28.0f, 28.0f, 28.0f),
                         FVector(NeutralRearX, 22.0f, 144.0f), Palette::Glove);

    // Debug hitbox is parented to the actor root, not the body, so a lean or a
    // knockdown cannot drag the gameplay hitbox picture out of alignment.
    HitboxDebug = AddShape(this, Root, TEXT("HitboxDebug"), FVector(92.0f, 130.0f, 123.0f),
                           FVector((26.0f + 118.0f) * 0.5f, 0.0f, (55.0f + 178.0f) * 0.5f),
                           Palette::HitboxActive);
    if (HitboxDebug)
    {
        HitboxDebug->SetVisibility(false);
    }

    PartCount = 6;
    if (HitboxDebug)
    {
        ++PartCount;
    }

    // Cache the body parts so the artwork can hide them with a visibility flip.
    // They are kept, not destroyed, so the greybox remains a complete fallback.
    GreyboxParts.Reset();
    GreyboxParts.Add(Legs);
    GreyboxParts.Add(Trunks);
    GreyboxParts.Add(Torso);
    GreyboxParts.Add(Head);
    GreyboxParts.Add(GloveLead);
    GreyboxParts.Add(GloveRear);
    GreyboxParts.RemoveAll([](const TObjectPtr<UStaticMeshComponent>& C) { return C == nullptr; });

    bRigBuilt = true;
    UE_LOG(LogRockyKombat, Log, TEXT("%s rig built from %d greybox parts."),
           *RKC::GetRosterDisplayName(Roster), PartCount);
}

void ARKCFighter::SyncFromSim()
{
    if (!Sim || !bRigBuilt)
    {
        return;
    }

    const RKC::FFighterState& S = Sim->GetFighter(Side);
    const RKC::FFighterConfig& Cfg = Sim->GetConfig(Side);

    // The simulation works in 1-D along X on a single fight line.
    SetActorLocation(FVector(S.PositionX, RingY, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
    SetActorRotation(FRotator(0.0f, (S.Facing >= 0 ? 0.0f : 180.0f), 0.0f), ETeleportType::TeleportPhysics);

    UpdatePose(S, Cfg);
    // After the pose, never before: UpdatePose decides what the greybox looks
    // like, and UpdateArt decides whether that greybox is on screen at all.
    UpdateArt(S);
}

void ARKCFighter::InitArt()
{
    if (!Sprite)
    {
        return;
    }

    // "Artwork is already on screen" is NOT enough to skip the load. A rematch
    // reuses the fighter actors (RKCGameMode's per-restart path re-Init()s them),
    // and the players can pick a different character in between. Without the
    // roster comparison the reused actor keeps presenting the previous
    // character's flipbooks, while its name banner, division, hitboxes and
    // combat data have already switched to the newly selected one - the fighter
    // is then drawn as one character and labelled as another.
    if (bArtActive && LoadedArtRoster == static_cast<int32>(Roster))
    {
        return;
    }

    // Switching rosters: release the previous character's artwork first, so that
    // a failed or partial re-resolve shows the greybox rig rather than silently
    // leaving the stale character on screen.
    if (bArtActive)
    {
        ArtKeepAlive.Reset();
        ArtSet = RKCArt::FArtSet();
        Sprite->SetFlipbook(nullptr);
        Sprite->SetVisibility(false);
        SetGreyboxVisible(true);
        bArtActive = false;
        LoadedArtRoster = -1;
    }

    ArtSet = RKCArt::LoadArtSet(Roster);
    if (!ArtSet.bValid)
    {
        // No artwork in this project. The greybox rig is already built and stays
        // on screen; nothing else to do.
        UE_LOG(LogRockyKombat, Log,
               TEXT("%s has no imported artwork; presenting the greybox rig."),
               *RKC::GetRosterDisplayName(Roster));
        return;
    }

    // Hold a reference to every resolved flipbook for the fighter's lifetime, so
    // that the set cannot be collected if the sprite component is ever cleared.
    ArtKeepAlive.Reset();
    UPaperFlipbook* const All[] = { ArtSet.Idle, ArtSet.Walk, ArtSet.Jab, ArtSet.HeavyPunch,
                                    ArtSet.Block, ArtSet.Dodge, ArtSet.Knockdown, ArtSet.Victory };
    for (UPaperFlipbook* Fb : All)
    {
        if (Fb)
        {
            ArtKeepAlive.Add(Fb);
        }
    }

    Sprite->SetMobility(EComponentMobility::Movable);
    Sprite->SetFlipbook(ArtSet.Idle);
    Sprite->SetLooping(true);
    Sprite->SetPlayRate(1.0f);
    Sprite->SetPlaybackPositionInFrames(0, false);
    Sprite->Play();
    Sprite->SetVisibility(true);

    bArtActive = true;
    LoadedArtRoster = static_cast<int32>(Roster);
    LastArtState = 255;
    LastArtAction = 255;

    // Hide the placeholder body. A toggle, so the rig is still there to return to.
    SetGreyboxVisible(false);

    UE_LOG(LogRockyKombat, Log,
           TEXT("%s is presenting imported artwork (%d/8 flipbooks); greybox body hidden."),
           *RKC::GetRosterDisplayName(Roster), ArtSet.Count());
}

void ARKCFighter::SetGreyboxVisible(bool bVisible)
{
    for (const TObjectPtr<UStaticMeshComponent>& Part : GreyboxParts)
    {
        if (Part)
        {
            Part->SetVisibility(bVisible);
        }
    }
}

FString ARKCFighter::GetCurrentArtName() const
{
    if (!Sprite)
    {
        return TEXT("none");
    }
    const UPaperFlipbook* Fb = Sprite->GetFlipbook();
    return Fb ? Fb->GetName() : TEXT("none");
}

void ARKCFighter::UpdateArt(const RKC::FFighterState& S)
{
    if (!Sprite || !ArtSet.bValid || !bArtActive)
    {
        return;
    }

    UPaperFlipbook* const Desired = RKCArt::SelectFlipbook(ArtSet, S);
    if (!Desired)
    {
        return;
    }

    const uint8 StateId = static_cast<uint8>(S.State);
    const uint8 ActionId = static_cast<uint8>(S.CurrentAction);
    const bool bStateChanged = (StateId != LastArtState) || (ActionId != LastArtAction);
    const bool bAssetChanged = (Sprite->GetFlipbook() != Desired);

    // Only restart on a real transition. Restarting every frame would reset the
    // animation continuously and nothing would ever appear to play.
    if (!bStateChanged && !bAssetChanged)
    {
        return;
    }

    Sprite->SetFlipbook(Desired);
    Sprite->SetPlayRate(1.0f);
    LastArtState = StateId;
    LastArtAction = ActionId;

    switch (S.State)
    {
    case RKC::EFighterState::Blocking:
        // A block is sustained input, not a one-shot. Hold the guard pose on the
        // first frame instead of looping a sequence that drops the guard.
        Sprite->SetLooping(false);
        Sprite->SetPlaybackPositionInFrames(0, false);
        Sprite->Stop();
        break;

    case RKC::EFighterState::HitStun:
        // The pack ships no hit-reaction animation and no get-up sequence. Its
        // documented stand-in is Knockdown frame 0 as a temporary hit response.
        Sprite->SetLooping(false);
        Sprite->SetPlaybackPositionInFrames(0, false);
        Sprite->Stop();
        break;

    case RKC::EFighterState::KnockedDown:
    case RKC::EFighterState::Defeated:
        // Play down to the floor once, then hold there: non-looping playback
        // leaves the final frame on screen, which is where a floored fighter is.
        Sprite->SetLooping(false);
        Sprite->SetPlaybackPositionInFrames(0, false);
        Sprite->Play();
        break;

    case RKC::EFighterState::AttackStartup:
    case RKC::EFighterState::AttackActive:
    case RKC::EFighterState::AttackRecovery:
    {
        // Stretch the four-pose animation across the attack's real duration so the
        // punch on screen lands in the same tick as the hitbox the rules use.
        // AttackTotalTicks is the simulation's own figure, never a guess.
        Sprite->SetLooping(false);
        const int32 Frames = FMath::Max(1, Desired->GetNumFrames());
        const float DurationSeconds =
            (S.AttackTotalTicks > 0)
                ? static_cast<float>(S.AttackTotalTicks) * RKC::FCombatSim::FixedTickSeconds
                : 0.0f;
        const float Rate = (DurationSeconds > 0.01f)
                               ? static_cast<float>(Frames) / DurationSeconds
                               : 1.0f;
        Sprite->SetPlayRate(Rate);
        Sprite->SetPlaybackPositionInFrames(0, false);
        Sprite->Play();
        break;
    }

    case RKC::EFighterState::Dodging:
    {
        // A slip is a one-shot too: play the evade and hold its end pose rather
        // than looping it back into a standing frame mid-dodge.
        Sprite->SetLooping(false);
        const float DurationSeconds =
            (S.StateTicksRemaining > 0)
                ? static_cast<float>(S.StateTicksRemaining) * RKC::FCombatSim::FixedTickSeconds
                : 0.0f;
        const int32 Frames = FMath::Max(1, Desired->GetNumFrames());
        const float Rate = (DurationSeconds > 0.01f)
                               ? static_cast<float>(Frames) / DurationSeconds
                               : 1.0f;
        Sprite->SetPlayRate(Rate);
        Sprite->SetPlaybackPositionInFrames(0, false);
        Sprite->Play();
        break;
    }

    case RKC::EFighterState::Victory:
        Sprite->SetLooping(true);
        Sprite->SetPlaybackPositionInFrames(0, false);
        Sprite->Play();
        break;

    case RKC::EFighterState::Idle:
    case RKC::EFighterState::Moving:
    default:
        Sprite->SetLooping(true);
        Sprite->SetPlaybackPositionInFrames(0, false);
        Sprite->Play();
        break;
    }
}

void ARKCFighter::UpdatePose(const RKC::FFighterState& S, const RKC::FFighterConfig& Cfg)
{
    const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
    const bool bIsShadow = (Roster == RKC::ERoster::Shadow);

    FVector LeadPosition(NeutralLeadX, -20.0f, 148.0f);
    FVector RearPosition(NeutralRearX, 22.0f, 144.0f);
    float BodyPitch = 0.0f;
    float BodyHeight = 0.0f;
    float LegPitch = 0.0f;
    FLinearColor TorsoColour = bIsShadow ? Palette::ShadowSkin : Palette::RockSkin;

    const float IdleBob = FMath::Sin(Time * 3.0f) * 2.0f;
    const float WalkCycle = FMath::Sin(Time * 9.0f);
    const bool bHeavy = (S.CurrentAction == RKC::EAction::HeavyPunch);
    const float Reach = bHeavy ? Cfg.HeavyPunch.ReachFar : Cfg.Jab.ReachFar;
    const float Extension = Reach * ActiveExtensionFraction;

    switch (S.State)
    {
    case RKC::EFighterState::Idle:
        BodyHeight = IdleBob;
        LeadPosition.Z += IdleBob;
        RearPosition.Z += IdleBob;
        break;

    case RKC::EFighterState::Moving:
        // Alternating steps, with the body rising slightly at each contact.
        LegPitch = WalkCycle * 16.0f;
        BodyHeight = FMath::Abs(WalkCycle) * 2.5f;
        LeadPosition.Z += BodyHeight;
        RearPosition.Z += BodyHeight;
        break;

    case RKC::EFighterState::AttackStartup:
        // Wind-up: the striking glove draws back and the torso loads.
        BodyPitch = 6.0f;
        if (bHeavy)
        {
            RearPosition = FVector(-8.0f, 26.0f, 146.0f);
        }
        else
        {
            LeadPosition = FVector(20.0f, -26.0f, 150.0f);
        }
        break;

    case RKC::EFighterState::AttackActive:
        BodyPitch = bHeavy ? 12.0f : 8.0f;
        if (bHeavy)
        {
            RearPosition = FVector(Extension, 14.0f, 150.0f);
            LeadPosition = FVector(18.0f, -22.0f, 146.0f);
        }
        else
        {
            LeadPosition = FVector(Extension, -12.0f, 152.0f);
            RearPosition = FVector(2.0f, 24.0f, 146.0f);
        }
        break;

    case RKC::EFighterState::AttackRecovery:
        // Coming back to guard; the hand is not yet home.
        BodyPitch = 4.0f;
        LeadPosition = bHeavy ? FVector(24.0f, -20.0f, 150.0f) : FVector(28.0f, -20.0f, 148.0f);
        break;

    case RKC::EFighterState::Blocking:
        BodyPitch = -5.0f;
        LeadPosition = FVector(46.0f, -12.0f, 176.0f);
        RearPosition = FVector(40.0f, 14.0f, 172.0f);
        break;

    case RKC::EFighterState::Dodging:
        // A slip: dip and lean away from the incoming punch.
        BodyPitch = (S.DodgeDir >= 0 ? -20.0f : 20.0f);
        BodyHeight = -22.0f;
        break;

    case RKC::EFighterState::HitStun:
        BodyPitch = -14.0f;
        BodyHeight = -4.0f;
        TorsoColour = FLinearColor(1.0f, 0.36f, 0.30f);
        break;

    case RKC::EFighterState::KnockedDown:
        BodyPitch = -80.0f;
        BodyHeight = -34.0f;
        break;

    case RKC::EFighterState::Victory:
        LeadPosition = FVector(26.0f, -20.0f, 206.0f + IdleBob);
        RearPosition = FVector(14.0f, 22.0f, 206.0f - IdleBob);
        break;

    case RKC::EFighterState::Defeated:
        BodyPitch = -88.0f;
        BodyHeight = -58.0f;
        break;

    default:
        break;
    }

    if (Body)
    {
        Body->SetRelativeLocation(FVector(0.0f, 0.0f, BodyHeight));
        Body->SetRelativeRotation(FRotator(BodyPitch, 0.0f, 0.0f));
    }
    if (Legs)
    {
        Legs->SetRelativeRotation(FRotator(LegPitch, 0.0f, 0.0f));
    }
    if (GloveLead)
    {
        GloveLead->SetRelativeLocation(LeadPosition);
    }
    if (GloveRear)
    {
        GloveRear->SetRelativeLocation(RearPosition);
    }
    if (Torso && !(TorsoColour == LastTorsoColour))
    {
        SetShapeColour(Torso, TorsoColour);
        LastTorsoColour = TorsoColour;
    }

    if (HitboxDebug)
    {
        const bool bShow = bShowHitboxes && (S.State == RKC::EFighterState::AttackActive);
        HitboxDebug->SetVisibility(bShow);
        if (bShow)
        {
            const RKC::FAttackDef& Def = bHeavy ? Cfg.HeavyPunch : Cfg.Jab;
            const float SpanX = Def.ReachFar - Def.ReachNear;
            const float SpanZ = Def.HitboxHighZ - Def.HitboxLowZ;
            HitboxDebug->SetRelativeScale3D(FVector(SpanX, 130.0f, SpanZ) / 100.0f);
            HitboxDebug->SetRelativeLocation(FVector((Def.ReachNear + Def.ReachFar) * 0.5f, 0.0f,
                                                     (Def.HitboxLowZ + Def.HitboxHighZ) * 0.5f));
        }
    }
}
