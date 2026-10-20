// RKCArena.cpp

#include "Arena/RKCArena.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "RockyKombat.h"
#include "Visual/RKCArt.h"
#include "Visual/RKCVisuals.h"

using namespace RKCVisuals;

namespace
{
/** Pixel size of the supplied arena image. It fixes the picture's aspect ratio,
 *  which is preserved at every scale, and nothing else. */
constexpr float ArenaImageWidthPx = 1672.0f;
constexpr float ArenaImageHeightPx = 941.0f;
constexpr float ArenaImageAspect = ArenaImageWidthPx / ArenaImageHeightPx;

/** How high up the image the painted floor line sits, as a fraction of the
 *  picture's height, and the value shipped by default.
 *
 *  MEASURED, not assumed. The supplied picture's ring canvas runs from about 62%
 *  down the image (the base of the far ropes, the bright band) to the bottom edge,
 *  so the middle of the painted surface - where a fighter standing in the middle of
 *  the ring meets the floor - is about 78% down. The value used to be 0.12 (88%
 *  down), which is the near lip of the canvas: the fighters were placed at the front
 *  edge of the ring and, because of the frame-cover rule below, that put the whole
 *  pair in the bottom sixth of the screen with most of the ring empty above them.
 *  0.22 stands them on the middle of the painted canvas instead.
 *
 *  This is a presentation number only: it moves a backdrop plane, and the ring's
 *  real bounds stay the simulation's own Rules.ArenaMinX/MaxX. */
constexpr float DefaultArenaFloorLineUpFraction = 0.22f;

/** rkc.ArenaFloorLine - where up the supplied picture the fighters stand, as a
 *  fraction of the picture's height (0 = the picture's bottom edge, 1 = its top).
 *  Tunable so a composition can be judged on a running build instead of being
 *  rebuilt for every trial; the default above is what the game ships with. */
TAutoConsoleVariable<float> CVarArenaFloorLine(
    TEXT("rkc.ArenaFloorLine"), DefaultArenaFloorLineUpFraction,
    TEXT("Where up the supplied arena picture the fighters stand, as a fraction of the ")
    TEXT("picture's height. 0 = the picture's bottom edge, 1 = its top. Default 0.22, ")
    TEXT("the middle of the painted canvas."),
    ECVF_Default);

/** The floor-line fraction the framing actually uses, clamped to a range that
 *  still leaves a picture with a real height. */
float ArenaFloorLineUpFraction()
{
    return FMath::Clamp(CVarArenaFloorLine.GetValueOnGameThread(), 0.02f, 0.90f);
}

/** How far behind the fight line the picture stands, in centimetres. A close
 *  backdrop keeps the picture's scale relative to the fighters as close to 1:1 as
 *  the framing allows (each centimetre of the picture is worth about a centimetre
 *  of world), while still leaving the plane clear of everything a fighter draws. */
constexpr float ImagePlaneDistanceCm = 200.0f;

/** Safety margin on the picture's size, as a fraction. The picture is sized to
 *  cover the frame exactly, and this puts the frame's edge a little inside it, so
 *  a rounding difference between the projection computed here and the renderer's
 *  own cannot leave a line of empty background along an edge. */
constexpr float FramingOverscan = 1.05f;

// EDITOR BUILDS ONLY. The switch is compiled out of every packaged target, both
// Shipping and Development, so nothing a player can type at the console and nothing
// already saved in Saved/Config/*/Engine.ini [ConsoleVariables] can bring the
// duplicate ring back into the game that ships. That matters because a packaged
// build is normally a Development build, where !UE_BUILD_SHIPPING is still true:
// gating on shipping alone left the duplicate ring reachable at runtime.
#if WITH_EDITOR
/** rkc.ArenaGreybox - 0 (default) lets the supplied Midnight Rooftop Ring image BE
 *  the arena; 1 also draws the procedural greybox ring over it, which is only ever
 *  useful for diagnosis. It lives here so the arena owns this decision and no other
 *  file has to know the ring can be hidden. */
TAutoConsoleVariable<int32> CVarArenaGreybox(
    TEXT("rkc.ArenaGreybox"),
    0,
    TEXT("0: the imported arena image is the ring (default). ")
    TEXT("1: also draw the procedural greybox ring on top of it, for diagnosis."),
    ECVF_Default);
#endif

/** True when the procedural ring should be drawn IN ADDITION to the imported
 *  image. Only ever true in the editor, and only when asked for there: a packaged
 *  build has no switch at all and cannot reach this path, whatever the config. */
bool GreyboxRingRequested()
{
#if WITH_EDITOR
    return CVarArenaGreybox.GetValueOnGameThread() != 0;
#else
    return false;
#endif
}

/** True when this build may fall back to the placeholder ring if the imported
 *  image could not be loaded. Only the editor does, and it says so, because a
 *  playable picture beats an empty one while the import is being fixed. No packaged
 *  build ever does: the supplied image IS the arena, so a second ring on screen is a
 *  defect rather than a fallback, and nothing in the shipped game can ask for it. */
constexpr bool GreyboxFallbackAllowed()
{
#if WITH_EDITOR
    return true;
#else
    return false;
#endif
}
}

ARKCArena::ARKCArena()
{
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("ArenaRoot"));
    SetRootComponent(Root);
}

void ARKCArena::BuildRing(const RKC::FMatchRules& Rules)
{
    if (bBuilt)
    {
        return;
    }

    if (!ArePrimitivesAvailable())
    {
        UE_LOG(LogRockyKombat, Error,
               TEXT("Ring geometry could not be built: the engine basic shape meshes are "
                    "missing. The match will still simulate correctly."));
        return;
    }

    // Ring dimensions follow the simulation's own fight-plane bounds, with a
    // margin so a fighter clamped to ArenaMinX/MaxX is still standing on canvas.
    const float Margin = 60.0f;
    const float HalfLengthCm = (Rules.ArenaMaxX - Rules.ArenaMinX) * 0.5f + Margin;
    const float HalfDepthCm = 240.0f;
    const float CanvasThickness = 25.0f;
    const float PostSize = 28.0f;
    const float PostHeight = 240.0f;
    const float RopeThickness = 7.0f;

    const float LengthCm = HalfLengthCm * 2.0f;
    const float DepthCm = HalfDepthCm * 2.0f;

    // --- Canvas and skirt -----------------------------------------------------
    Canvas = AddShape(this, Root, TEXT("Canvas"),
                      FVector(LengthCm, DepthCm, CanvasThickness),
                      FVector(0.0f, 0.0f, -CanvasThickness * 0.5f),
                      Palette::Canvas);

    AddShape(this, Root, TEXT("Skirt"),
             FVector(LengthCm + 40.0f, DepthCm + 40.0f, 90.0f),
             FVector(0.0f, 0.0f, -70.0f),
             Palette::RingSkirt);

    // Ringside floor, so the world does not end at the apron.
    AddShape(this, Root, TEXT("RingsideFloor"),
             FVector(LengthCm + 2600.0f, 3600.0f, 40.0f),
             FVector(0.0f, 600.0f, -130.0f),
             Palette::RingSkirt);

    // --- Corner posts and turnbuckle pads ------------------------------------
    const float PostZ = PostHeight * 0.5f;
    const FLinearColor RopeColours[3] = { Palette::RopeRed, Palette::RopeWhite, Palette::RopeBlue };
    const TCHAR* PostNames[4] = { TEXT("Post_FL"), TEXT("Post_FR"), TEXT("Post_BL"), TEXT("Post_BR") };

    int32 PostIndex = 0;
    for (int32 SideX = 0; SideX < 2; ++SideX)
    {
        for (int32 SideY = 0; SideY < 2; ++SideY)
        {
            const float X = (SideX == 0 ? -HalfLengthCm : HalfLengthCm);
            const float Y = (SideY == 0 ? -HalfDepthCm : HalfDepthCm);

            AddShape(this, Root, FName(PostNames[PostIndex]),
                     FVector(PostSize, PostSize, PostHeight),
                     FVector(X, Y, PostZ),
                     Palette::Post);

            // Padding at the second rope height, as on a real ring.
            AddShape(this, Root, FName(FString::Printf(TEXT("Turnbuckle_%d"), PostIndex)),
                     FVector(PostSize + 18.0f, PostSize + 18.0f, 66.0f),
                     FVector(X, Y, PostZ - 40.0f),
                     Palette::PostTrim);
            ++PostIndex;
        }
    }

    // --- Ropes: three heights on each of the four sides ----------------------
    const float RopeHeights[3] = { 62.0f, 122.0f, 182.0f };
    for (int32 Level = 0; Level < 3; ++Level)
    {
        const float Z = RopeHeights[Level];

        // Long ropes front and back.
        AddShape(this, Root, FName(FString::Printf(TEXT("RopeLongF_%d"), Level)),
                 FVector(LengthCm, RopeThickness, RopeThickness),
                 FVector(0.0f, -HalfDepthCm, Z), RopeColours[Level]);

        AddShape(this, Root, FName(FString::Printf(TEXT("RopeLongB_%d"), Level)),
                 FVector(LengthCm, RopeThickness, RopeThickness),
                 FVector(0.0f, HalfDepthCm, Z), RopeColours[Level]);

        // Short ropes left and right.
        AddShape(this, Root, FName(FString::Printf(TEXT("RopeShortL_%d"), Level)),
                 FVector(RopeThickness, DepthCm, RopeThickness),
                 FVector(-HalfLengthCm, 0.0f, Z), RopeColours[Level]);

        AddShape(this, Root, FName(FString::Printf(TEXT("RopeShortR_%d"), Level)),
                 FVector(RopeThickness, DepthCm, RopeThickness),
                 FVector(HalfLengthCm, 0.0f, Z), RopeColours[Level]);
    }

    // --- Backdrop -------------------------------------------------------------
    // Stands well behind the ring; the camera looks along +Y from -Y, so this
    // fills the frame behind the fight without occluding it.
    BackdropWall = AddShape(this, Root, TEXT("BackdropWall"),
                            FVector(LengthCm + 3600.0f, 60.0f, 1500.0f),
                            FVector(0.0f, 1900.0f, 600.0f),
                            Palette::CanvasBackdrop);

    BackdropFloor = AddShape(this, Root, TEXT("BackdropFloor"),
                            FVector(LengthCm + 3600.0f, 2200.0f, 60.0f),
                            FVector(0.0f, 1000.0f, -110.0f),
                            Palette::CanvasBackdrop);

    // Record every greybox piece before the artwork adds one of its own, so the
    // whole-ring toggle covers the placeholder rig and never the backdrop image.
    GreyboxPieces.Reset();
    for (UActorComponent* Component : GetComponents())
    {
        if (UStaticMeshComponent* Piece = Cast<UStaticMeshComponent>(Component))
        {
            GreyboxPieces.Add(Piece);
        }
    }

    EnableArtBackdrop();

    bBuilt = true;
    PieceCount = GetComponents().Num();

    UE_LOG(LogRockyKombat, Log,
           TEXT("Ring built: canvas %.0f x %.0f cm, fight plane X [%.0f, %.0f], %d components, ")
           TEXT("backdrop=%s."),
           LengthCm, DepthCm, Rules.ArenaMinX, Rules.ArenaMaxX, PieceCount,
           bArtBackdropActive ? TEXT("imported image") : TEXT("greybox placeholder"));
}

void ARKCArena::EnableArtBackdrop()
{
    if (bArtBackdropActive)
    {
        return;
    }

    UMaterialInterface* const BaseMaterial = RKCArt::LoadBackdropMaterial();
    UTexture2D* const Image = RKCArt::LoadArenaTexture();
    if (!BaseMaterial || !Image)
    {
        // Without the picture there is no arena. A development build falls back to
        // the placeholder ring so the game stays playable while the import is
        // fixed, and says so; a shipping build draws nothing procedural at all,
        // because the supplied image is the arena and a second ring on screen is a
        // defect rather than a fallback.
        constexpr bool bFallbackAllowed = GreyboxFallbackAllowed();
        if (bFallbackAllowed)
        {
            UE_LOG(LogRockyKombat, Warning,
                   TEXT("No imported arena image (material %s, image %s). The greybox ")
                   TEXT("placeholder ring is standing in: a development fallback that must ")
                   TEXT("not ship."),
                   BaseMaterial ? TEXT("found") : TEXT("missing"),
                   Image ? TEXT("found") : TEXT("missing"));
        }
        else
        {
            UE_LOG(LogRockyKombat, Error,
                   TEXT("No imported arena image (material %s, image %s), and the greybox ")
                   TEXT("fallback is development-only, so no ring is drawn."),
                   BaseMaterial ? TEXT("found") : TEXT("missing"),
                   Image ? TEXT("found") : TEXT("missing"));
        }
        SetGreyboxVisible(bFallbackAllowed);
        return;
    }

    // The engine plane is a 100x100 cm quad lying in its own X/Y plane. Its size
    // and position are set by ApplyFraming() below, because they follow the camera;
    // the values here only have to be non-degenerate for the component to exist.
    ArtBackdrop = AddShape(this, Root, TEXT("ArtBackdrop"),
                           FVector(100.0f, 100.0f, 1.0f),
                           FVector(0.0f, ImagePlaneDistanceCm, 0.0f),
                           FLinearColor::White, /*bDrawPlane=*/true);
    if (!ArtBackdrop)
    {
        UE_LOG(LogRockyKombat, Warning,
               TEXT("Arena image is imported but the backdrop plane could not be created."));
        return;
    }

    // A roll of 90 degrees turns the plane's +Z normal to face -Y, which is the
    // direction the match camera looks from, and its +Y to world +Z, so its local Y
    // becomes the vertical axis of the picture. Its local X stays the width.
    ArtBackdrop->SetRelativeRotation(FRotator(0.0f, 0.0f, 90.0f));

    ArtBackdropMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
    if (ArtBackdropMaterial)
    {
        // The importer names the texture parameter SpriteTexture; the alternate is
        // set as well so a hand-authored material with the usual name also works.
        ArtBackdropMaterial->SetTextureParameterValue(TEXT("SpriteTexture"), Image);
        ArtBackdropMaterial->SetTextureParameterValue(TEXT("Texture"), Image);
        ArtBackdrop->SetMaterial(0, ArtBackdropMaterial);
    }

    ArtBackdrop->SetVisibility(true);

    // Hide the flat placeholder slabs behind the picture. Toggle, not delete.
    SetPlaceholderBackdropVisible(false);

    // The image already draws the ring - posts, three rope heights, canvas - so
    // leaving the procedural ring up would show a second ring at the wrong height
    // over the top of it. Hide it, by visibility rather than deletion, so every
    // component stays in the world and nothing about the fight plane changes.
    // GreyboxRingRequested() is false in every shipping build, so normal play needs
    // no launch command and no console setting to show exactly one arena.
    SetGreyboxVisible(GreyboxRingRequested());

    // Proof, in the run's own log, that the duplicate ring is really off: count the
    // procedural pieces whose visibility flag is still set. A packaged build can only
    // ever report zero - the switch does not exist there and the fallback is compiled
    // out - so a non-zero count here is an error worth failing loudly over rather
    // than something anyone has to notice in a screenshot.
    const int32 VisibleGreyboxPieces = CountVisibleGreyboxPieces();
    if (bGreyboxVisible)
    {
        UE_LOG(LogRockyKombat, Display,
               TEXT("rkc.ArenaGreybox is set: the procedural ring (%d piece(s)) is drawn over ")
               TEXT("the imported arena image as well. Editor diagnostic only."),
               VisibleGreyboxPieces);
    }
    else if (VisibleGreyboxPieces != 0)
    {
        UE_LOG(LogRockyKombat, Error,
               TEXT("Arena: %d of %d procedural ring piece(s) are still visible while the ")
               TEXT("imported image is up - that is the duplicate ring this arena must not show."),
               VisibleGreyboxPieces, GreyboxPieces.Num());
    }
    else
    {
        UE_LOG(LogRockyKombat, Log,
               TEXT("Arena: all %d procedural ring piece(s) are hidden; the imported image is ")
               TEXT("the only visible arena."),
               GreyboxPieces.Num());
    }

    bArtBackdropActive = true;

    // Size and place the picture so it fills the camera's frame with its painted
    // floor line on the fighters' feet.
    ApplyFraming();

    UE_LOG(LogRockyKombat, Log,
           TEXT("Arena backdrop: the imported image is the only visible ring ")
           TEXT("(placeholder backdrop hidden, procedural ring %s)."),
           bGreyboxVisible ? TEXT("shown for diagnosis") : TEXT("hidden"));
}

int32 ARKCArena::CountVisibleGreyboxPieces() const
{
    // Reads each piece's own visibility flag rather than this class's intention, so
    // the answer is about the components that would actually be drawn.
    int32 VisiblePieces = 0;
    for (const TObjectPtr<UStaticMeshComponent>& Piece : GreyboxPieces)
    {
        if (Piece && Piece->GetVisibleFlag())
        {
            ++VisiblePieces;
        }
    }
    return VisiblePieces;
}

float ARKCArena::GetImageAspect() const
{
    // The supplied image's own ratio. PositionArtBackdrop() derives both the width
    // and the height of the picture from one number through this, so a picture whose
    // size does not divide down to it has been stretched.
    return ArenaImageAspect;
}

void ARKCArena::SetPlaceholderBackdropVisible(bool bVisible)
{
    if (BackdropWall)
    {
        BackdropWall->SetVisibility(bVisible);
    }
    if (BackdropFloor)
    {
        BackdropFloor->SetVisibility(bVisible);
    }
}

void ARKCArena::SetGreyboxVisible(bool bVisible)
{
#if !WITH_EDITOR
    // Defence in depth for the game that ships. Nothing in a packaged build has a
    // reason to ask for the procedural ring - the switch does not exist there and
    // EnableArtBackdrop() passes a constant false - so a request arriving here would
    // come from stale content, a leftover call site or a future edit. It is refused
    // and reported rather than honoured, because the supplied image is the arena and
    // a second ring on screen is the defect this code exists to prevent. Whoever
    // asks, and however they ask, a packaged build shows one arena.
    if (bVisible)
    {
        UE_LOG(LogRockyKombat, Error,
               TEXT("Arena: something asked for the procedural greybox ring in a packaged build. ")
               TEXT("The supplied image is the only visible arena, so the request is refused."));
        bVisible = false;
    }
#endif

    bGreyboxVisible = bVisible;
    for (const TObjectPtr<UStaticMeshComponent>& Piece : GreyboxPieces)
    {
        if (Piece)
        {
            Piece->SetVisibility(bVisible);
        }
    }

    // The placeholder backdrop stays hidden while the imported image is up, even
    // when the ring itself is shown again, so the two backgrounds never overlap.
    if (bVisible && bArtBackdropActive)
    {
        SetPlaceholderBackdropVisible(false);
    }

    UE_LOG(LogRockyKombat, Log, TEXT("Greybox ring visibility set to %s."),
           bVisible ? TEXT("shown") : TEXT("hidden"));
}

// ---------------------------------------------------------------------------
// Framing the supplied image
// ---------------------------------------------------------------------------

void ARKCArena::SetFramingCamera(float InFOVDegrees, bool bInFOVIsHorizontal,
                                 float InCameraDistanceCm, float InCameraHeightCm,
                                 float InCameraPitchDegrees)
{
    const float ClampedFOV = FMath::Clamp(InFOVDegrees, 5.0f, 170.0f);
    const float ClampedDistance = FMath::Max(InCameraDistanceCm, 1.0f);
    const float ClampedPitch = FMath::Clamp(InCameraPitchDegrees, -80.0f, 80.0f);

    // Re-framing rewrites a component transform and logs; the caller runs every
    // frame, so the work is only done when a number the framing depends on moved.
    const bool bChanged =
        !FMath::IsNearlyEqual(ClampedFOV, FramingFOV, 0.01f) ||
        !FMath::IsNearlyEqual(ClampedDistance, FramingDistance, 0.5f) ||
        !FMath::IsNearlyEqual(InCameraHeightCm, FramingHeight, 0.5f) ||
        !FMath::IsNearlyEqual(ClampedPitch, FramingPitch, 0.01f) ||
        (bInFOVIsHorizontal != bFOVIsHorizontal);

    FramingFOV = ClampedFOV;
    bFOVIsHorizontal = bInFOVIsHorizontal;
    FramingDistance = ClampedDistance;
    FramingHeight = InCameraHeightCm;
    FramingPitch = ClampedPitch;

    if (bChanged)
    {
        ApplyFraming();
    }
}

void ARKCArena::UpdatePresentation(float DeltaSeconds, float RenderAspect)
{
    if (!bBuilt)
    {
        return;
    }

    // ---- Make sure the supplied image is the thing on screen -----------------
    // A picture that could not be loaded when the ring was built would otherwise
    // leave the placeholder ring as the arena for the whole session, which is
    // exactly the second ring this arena must not show. Bounded, one second apart,
    // and it stops the moment the picture is up.
    if (!bArtBackdropActive && ArtLoadRetriesLeft > 0)
    {
        ArtLoadRetryCountdown -= FMath::Max(DeltaSeconds, 0.0f);
        if (ArtLoadRetryCountdown <= 0.0f)
        {
            ArtLoadRetryCountdown = 1.0f;
            --ArtLoadRetriesLeft;
            UE_LOG(LogRockyKombat, Log,
                   TEXT("Arena: the imported image is not up yet; retrying (%d attempt(s) left)."),
                   ArtLoadRetriesLeft);
            EnableArtBackdrop();
        }
    }

    // ---- Follow the shape of the viewport ------------------------------------
    // Only on a real change: the framing below rewrites the picture's transform and
    // logs, and a resize is rare. A window that has not been measured yet reports an
    // aspect of 0, which is ignored rather than framed for.
    if (RenderAspect > 0.05f && !FMath::IsNearlyEqual(RenderAspect, FramedAspect, 0.0005f))
    {
        ViewportAspect = RenderAspect;
        ApplyFraming();
    }
}

void ARKCArena::ApplyFraming()
{
    if (!ArtBackdrop)
    {
        return;
    }

    // 16:9 until the director has measured the window, so the first frame is already
    // framed for the common case rather than left at the placeholder size.
    const float Aspect = (ViewportAspect > 0.05f) ? ViewportAspect : (16.0f / 9.0f);

    const float PitchRadians = FMath::DegreesToRadians(FramingPitch);
    const float SinPitch = FMath::Sin(PitchRadians);
    const float CosPitch = FMath::Cos(PitchRadians);

    // The camera's FieldOfView is one half-angle of the frame; which axis it
    // describes is the engine's aspect-ratio axis constraint, so the director reads
    // that and hands the answer in rather than this code assuming it. With both
    // half-tangents known for the rendered aspect ratio, the frame at any depth is
    // arithmetic.
    const float TanHalfFOV = FMath::Tan(FMath::DegreesToRadians(FramingFOV * 0.5f));
    const float TanHorizontal = bFOVIsHorizontal ? TanHalfFOV : (TanHalfFOV * Aspect);
    const float TanVertical = bFOVIsHorizontal ? (TanHalfFOV / Aspect) : TanHalfFOV;
    if (TanVertical <= 0.0001f || TanHorizontal <= 0.0001f)
    {
        return;
    }

    const float DepthToFightLine = FramingDistance;
    const float DepthToPicture = FramingDistance + ImagePlaneDistanceCm;

    // Screen-vertical position of a world point, in units of the vertical
    // half-frame: 0 is the centre line, -1 the bottom edge, +1 the top edge. The
    // camera looks along +Y, so the whole frame is one depth slice and this is the
    // camera's own projection written out; that is what makes the numbers below
    // checkable against the frame that is actually rendered.
    const auto ScreenY = [SinPitch, CosPitch, TanVertical, this](float DepthCm, float WorldZcm)
    {
        const float Up = (-DepthCm * SinPitch) + ((WorldZcm - FramingHeight) * CosPitch);
        const float Forward = (DepthCm * CosPitch) + ((WorldZcm - FramingHeight) * SinPitch);
        return (FMath::Abs(Forward) > 0.0001f) ? ((Up / Forward) / TanVertical) : 0.0f;
    };

    // 1. Put the fighters' feet on the picture's painted floor line. The line is
    //    wherever, at the picture's own depth, the ray from the camera through the
    //    feet lands, so the projection above is solved for that world Z.
    const float FeetScreen = ScreenY(DepthToFightLine, GetCanvasZ());
    const float FeetTan = FeetScreen * TanVertical;
    const float FloorLineZAtDepth =
        FramingHeight + (DepthToPicture * (SinPitch + (FeetTan * CosPitch)) /
                                            (CosPitch - (FeetTan * SinPitch)));

    // 2. Cover the frame at that same depth, so no background, floor or placeholder
    //    shows above, below or beside the picture.
    const float FrameTopZ =
        FramingHeight + (DepthToPicture * (SinPitch + (TanVertical * CosPitch)) /
                                             (CosPitch - (TanVertical * SinPitch)));
    const float FrameBottomZ =
        FramingHeight + (DepthToPicture * (SinPitch - (TanVertical * CosPitch)) /
                                             (CosPitch + (TanVertical * SinPitch)));

    // The picture has to satisfy three things at once: put its floor line on the
    // feet (which fixes where its bottom edge is relative to that line), and reach
    // the top and bottom edges of the frame and its full width. Growing the picture
    // can only ever add coverage, so taking the largest requirement satisfies all
    // of them; the binding one is whichever the camera's framing makes the
    // tightest, and it is reported in the log.
    const float FloorLineFraction = ArenaFloorLineUpFraction();
    const float HeightForBottomEdge = (FloorLineZAtDepth - FrameBottomZ) / FloorLineFraction;
    const float HeightForTopEdge = (FrameTopZ - FloorLineZAtDepth) / (1.0f - FloorLineFraction);
    const float HeightForWidth = (2.0f * DepthToPicture * TanHorizontal) / ArenaImageAspect;

    const float PictureHeightCm =
        FMath::Max3(HeightForBottomEdge, HeightForTopEdge, HeightForWidth) * FramingOverscan;
    const float PictureWidthCm = PictureHeightCm * ArenaImageAspect;

    // The component is placed by its centre, which the floor line's height up the
    // picture and the picture's own height together fix.
    const float CentreZ = FloorLineZAtDepth + ((0.5f - FloorLineFraction) * PictureHeightCm);

    PositionArtBackdrop(PictureWidthCm, PictureHeightCm, CentreZ);

    // How far the picture reaches past the frame on its tightest edge. Any positive
    // number means the frame is filled; it is logged and recorded so a run's own
    // output shows whether its picture covered the view, without anyone having to
    // judge that from a screenshot.
    const float PictureBottomZ = CentreZ - (PictureHeightCm * 0.5f);
    const float PictureTopZ = CentreZ + (PictureHeightCm * 0.5f);
    const float FrameHalfWidthCm = DepthToPicture * TanHorizontal;
    CoverMarginCm = FMath::Min(FMath::Min(FrameBottomZ - PictureBottomZ, PictureTopZ - FrameTopZ),
                               ((PictureWidthCm * 0.5f) - FrameHalfWidthCm));

    FloorLineZ = FloorLineZAtDepth;
    PictureSizeCm = FVector2D(PictureWidthCm, PictureHeightCm);
    FeetScreenPosition = FeetScreen;
    FramedAspect = Aspect;

    // 3. Re-derive the screen position of the floor line from where the picture was
    //    actually just put and compare it with the feet. The alignment is arithmetic
    //    rather than tuned, so it can be checked, and the numbers are logged: a run's
    //    own log then shows whether its frame really had the feet on the painted
    //    floor line, without anyone having to judge it by eye.
    const float PlacedFloorLineZ = CentreZ + ((FloorLineFraction - 0.5f) * PictureHeightCm);
    ImageFloorScreenPosition = ScreenY(DepthToPicture, PlacedFloorLineZ);
    const float AlignmentDelta = FMath::Abs(FeetScreen - ImageFloorScreenPosition);

    UE_LOG(LogRockyKombat, Log,
           TEXT("Arena framing: aspect %.4f, FOV %.1f deg on the %s axis, camera %.0f cm back, ")
           TEXT("%.0f cm up, pitch %.1f deg -> picture %.0f x %.0f cm at Y %.0f; ")
           TEXT("frame Z [%.0f, %.0f] and painted floor line Z %.0f at that depth; ")
           TEXT("feet at %+.4f of the half-frame, picture floor line at %+.4f (delta %.5f); ")
           TEXT("narrowest cover margin %.0f cm."),
           Aspect, FramingFOV, bFOVIsHorizontal ? TEXT("horizontal") : TEXT("vertical"),
           FramingDistance, FramingHeight, FramingPitch, PictureWidthCm, PictureHeightCm,
           ImagePlaneDistanceCm, FrameBottomZ, FrameTopZ, PlacedFloorLineZ, FeetScreen,
           ImageFloorScreenPosition, AlignmentDelta, CoverMarginCm);

    if (AlignmentDelta > 0.001f)
    {
        UE_LOG(LogRockyKombat, Error,
               TEXT("Arena framing is inconsistent: the picture's painted floor line and the ")
               TEXT("fighters' feet project to different screen lines (%+.4f vs %+.4f)."),
               ImageFloorScreenPosition, FeetScreen);
    }
}

void ARKCArena::PositionArtBackdrop(float PictureWidthCm, float PictureHeightCm, float CentreZ)
{
    if (!ArtBackdrop)
    {
        return;
    }

    // The component was rolled 90 degrees, so its local X is the picture's width,
    // its local Y is the picture's height (which is also the axis the image is
    // sampled along) and its front face looks back at the camera. Setting the scale
    // in the component's own space therefore sets the picture's size, whatever
    // world axis those turn into. Thickness stays at 1 cm to keep the scale
    // non-degenerate; the plane is a backdrop, never a surface anyone stands on.
    ArtBackdrop->SetRelativeScale3D(FVector(PictureWidthCm, PictureHeightCm, 1.0f) / 100.0f);
    ArtBackdrop->SetRelativeLocation(FVector(0.0f, ImagePlaneDistanceCm, CentreZ));
}
