// RKCArena.h
//
// The arena: the supplied Midnight Rooftop Ring image, framed to fill the match
// camera's view.
//
// PRESENTATION ONLY. Every competitive rule the arena appears to enforce - the
// ropes, the arena bounds, the anti-overlap separation - is enforced by
// RKC::FCombatSim from FMatchRules. This actor only reads those rules (the
// procedural fallback ring is sized from ArenaMinX/ArenaMaxX), so nothing here can
// change a match result.
//
// WHAT IS ON SCREEN
// The supplied image already paints the complete ring - corner posts, three rope
// heights, canvas and apron - so the image IS the arena and the procedural greybox
// ring is not drawn. The greybox ring still exists, hidden, as a development
// fallback:
//
//   * The editor may put it back over the image with rkc.ArenaGreybox=1, for
//     comparing geometry against the artwork, with no rebuild and no map edit.
//   * No packaged build can draw it at all. The switch and the fallback are
//     compiled out of every non-editor target (see WITH_EDITOR in RKCArena.cpp),
//     Shipping and Development alike, so normal play shows exactly one arena with
//     no launch command and no console setting, and no value already saved in
//     Saved/Config/*/Engine.ini [ConsoleVariables] can bring the duplicate ring
//     back, even if the imported image failed to load.
//
// Nothing is deleted and no rule is touched: the procedural pieces are hidden by a
// visibility flip and stay in the world, and they never carried collision to begin
// with - RKCVisuals::AddShape() creates every one of them with
// ECollisionEnabled::NoCollision. There is no invisible collision here to
// preserve: the boundaries are the simulation's, and they are untouched.
//
// FRAMING
// "Fill the frame with the picture" and "put the fighters' feet on the line the
// pack documents as the painted floor" are two arithmetic requirements, not two
// taste values, and ApplyFraming() solves both:
//
//   * the image plane is placed so that its painted floor line projects onto
//     exactly the screen line the fighters' feet project onto, for the camera that
//     is really in the world;
//   * the plane is then scaled until it covers the whole frame at its own depth,
//     so no background, floor or placeholder is visible above, below or beside it.
//
// The framing follows the real camera and the real viewport, so a window resize, a
// different aspect ratio or a console override of the field of view is re-framed
// on the next frame instead of being baked in. Everything it computes is logged,
// including the difference between the two screen lines above, which must be zero.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/RKCCombatCore.h"
#include "RKCArena.generated.h"

class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;
class UTexture2D;

UCLASS()
class ROCKYKOMBAT_API ARKCArena : public AActor
{
    GENERATED_BODY()

public:
    ARKCArena();

    /** Builds the ring geometry. Safe to call more than once; the second call is
     *  ignored so a rebuilt ring cannot double up its components. */
    void BuildRing(const RKC::FMatchRules& Rules);

    bool IsBuilt() const { return bBuilt; }

    /** Y coordinate of the fight line (the axis the fighters move along). */
    float GetFightLineY() const { return 0.0f; }

    /** Z of the canvas surface; fighters stand their feet here. */
    float GetCanvasZ() const { return 0.0f; }

    /** Number of components the ring was built from, reported for diagnostics. */
    int32 GetPieceCount() const { return PieceCount; }

    // ---- The imported image -------------------------------------------------
    /** True when the imported arena image is what is on screen. */
    bool IsUsingArtBackdrop() const { return bArtBackdropActive; }

    /** Hides or shows the whole greybox ring. A visibility flip, never a delete,
     *  so the placeholder presentation can always be brought back. */
    void SetGreyboxVisible(bool bVisible);
    bool IsGreyboxVisible() const { return bGreyboxVisible; }

    /** How many procedural greybox pieces still have their visibility flag set.
     *  Zero is the requirement for the game that ships: the supplied image is the
     *  only visible arena, at every stage of the match. Reads the components' own
     *  flags, so a self-test can assert it instead of trusting this class's state. */
    int32 CountVisibleGreyboxPieces() const;

    /** The supplied image's own width/height ratio. The picture is always scaled by
     *  this one number, so a picture whose size does not divide down to it has been
     *  stretched, and a self-test can prove the framing preserved the image's ratio
     *  instead of trusting that it did. */
    float GetImageAspect() const;

    // ---- Framing ------------------------------------------------------------
    /** Tells the arena which camera will look at it: a field of view, the axis that
     *  angle describes (the engine's aspect-ratio axis constraint decides that, so
     *  the caller reads it rather than this class assuming it), the distance back
     *  along -Y, the height, and the pitch in degrees. Re-frames only when a value
     *  actually changed, so it is safe to call every frame. */
    void SetFramingCamera(float InFOVDegrees, bool bInFOVIsHorizontal,
                          float InCameraDistanceCm, float InCameraHeightCm,
                          float InCameraPitchDegrees);

    /** Per-frame entry point: keeps the supplied image the only visible arena by
     *  retrying the import if it was not loadable when the ring was built, and
     *  re-framing when the rendered aspect ratio changes. Costs a float comparison
     *  when neither is needed.
     *  @param RenderAspect  the aspect ratio actually being rendered, after any
     *                       aspect-ratio constraint the director applied. */
    void UpdatePresentation(float DeltaSeconds, float RenderAspect);

    /** The rendered aspect ratio the picture is currently framed for; 0 before the
     *  first frame. */
    float GetFramedAspect() const { return FramedAspect; }

    /** Picture size in centimetres, for the evidence record. */
    FVector2D GetPictureSizeCm() const { return PictureSizeCm; }

    /** World Z of the painted floor line the fighters stand on, at the picture's
     *  own depth. */
    float GetFloorLineZ() const { return FloorLineZ; }

    /** Screen position of the fighters' feet and of the picture's painted floor
     *  line, in units of the vertical half-frame (0 = centre, -1 = bottom edge).
     *  These two numbers being equal is the proof that the alignment is real, and
     *  they are logged on every re-frame for exactly that reason. */
    float GetFeetScreenPosition() const { return FeetScreenPosition; }
    float GetImageFloorScreenPosition() const { return ImageFloorScreenPosition; }

    /** How far the picture reaches past the frame on its tightest edge, in
     *  centimetres. Positive means the frame is filled on every side, which is the
     *  "no empty space below or around the image" requirement as a number rather
     *  than as a judgement. */
    float GetCoverMarginCm() const { return CoverMarginCm; }

private:
    /** Builds the textured backdrop plane from the imported arena image and hides
     *  the two placeholder slabs behind it. Does nothing, leaving the greybox
     *  backdrop in place, when the image is not present. */
    void EnableArtBackdrop();

    /** Hides or shows the flat placeholder backdrop slabs only. */
    void SetPlaceholderBackdropVisible(bool bVisible);

    /** Sizes and places the picture for the current camera and viewport aspect. */
    void ApplyFraming();

    /** Sets the picture component's size and centre height. */
    void PositionArtBackdrop(float PictureWidthCm, float PictureHeightCm, float CentreZ);

    UPROPERTY(VisibleAnywhere, Category = "RKC|Arena")
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere, Category = "RKC|Arena")
    TObjectPtr<UStaticMeshComponent> Canvas;

    /** Placeholder backdrop slabs, kept so they can be toggled off when the
     *  imported arena image takes over the background. */
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> BackdropWall;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> BackdropFloor;

    /** The imported arena image, drawn on a plane facing the match camera. */
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> ArtBackdrop;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> ArtBackdropMaterial;

    /** Every greybox piece, for the whole-ring visibility toggle. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> GreyboxPieces;

    bool bBuilt = false;
    bool bArtBackdropActive = false;
    bool bGreyboxVisible = true;
    int32 PieceCount = 0;

    // ---- Framing state ------------------------------------------------------
    /** The camera the picture was framed for. Defaults match the director's own
     *  defaults so the first frame is already framed before it reports anything. */
    float FramingFOV = 32.0f;
    bool bFOVIsHorizontal = false;
    float FramingDistance = 1500.0f;
    float FramingHeight = 300.0f;
    float FramingPitch = 0.0f;

    /** Rendered aspect ratio from the director (0 until it has been measured) and
     *  the one the picture currently reflects. */
    float ViewportAspect = 0.0f;
    float FramedAspect = 0.0f;

    /** Results of the last framing pass, kept for the evidence record. */
    FVector2D PictureSizeCm = FVector2D::ZeroVector;
    float FloorLineZ = 0.0f;
    float FeetScreenPosition = 0.0f;
    float ImageFloorScreenPosition = 0.0f;
    float CoverMarginCm = 0.0f;

    /** Bounded retries for the case where the image was not loadable when the ring
     *  was built, so a late or failed import cannot leave the placeholder ring as
     *  the arena for a whole session. */
    int32 ArtLoadRetriesLeft = 8;
    float ArtLoadRetryCountdown = 0.0f;
};
