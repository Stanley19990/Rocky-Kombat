// RKCVisuals.h
//
// Shared helpers for building the greybox presentation from engine primitives.
//
// WHY PRIMITIVES: the first playable increment must be reproducible from source
// alone, before any art asset is imported. Everything here uses engine basic
// shapes, so a clean checkout builds and plays with no binary content. Once the
// Rocky/Shadow sprite atlases and the Midnight Rooftop Ring backdrop are
// imported (milestone: asset integration), the fighters swap to paper sprites
// and the arena to a textured backdrop, while the gameplay code below is
// unchanged.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"

namespace RKCVisuals
{
/** Engine unit cube, 100x100x100 cm, centred on its origin. */
UStaticMesh* LoadCubeMesh();

/** Engine unit plane, 100x100 cm, facing +Z. */
UStaticMesh* LoadPlaneMesh();

/** Engine cylinder, 100 cm tall, 50 cm radius, centred on its origin. */
UStaticMesh* LoadCylinderMesh();

/** True when at least the cube mesh loaded; used to report a degraded-but-alive
 *  presentation instead of silently drawing nothing. */
bool ArePrimitivesAvailable();

/** Creates and registers a static mesh component on Owner, parented to Parent.
 *  @param SizeCm   desired world size in centimetres; the engine primitives are
 *                  100 cm across, so this is applied as a scale of SizeCm/100.
 *  @param Colour   base colour; applied through a dynamic material instance.
 *  @param bAdditiveDepth when true the component does not contribute depth, so
 *                  debug hitboxes can be drawn over the fighters. */
UStaticMeshComponent* AddShape(AActor* Owner, USceneComponent* Parent, const FName& Name,
                               const FVector& SizeCm, const FVector& RelativeLocationCm,
                               const FLinearColor& Colour, bool bDrawPlane = false,
                               bool bAdditiveDepth = false);

/** Re-tints an already-created component, reusing the cached material instance. */
void SetShapeColour(UStaticMeshComponent* Component, const FLinearColor& Colour);

/** Standard greybox palette, kept in one place so the whole presentation can be
 *  re-skinned without hunting for literals. */
namespace Palette
{
    const FLinearColor Canvas = FLinearColor(0.72f, 0.70f, 0.66f);
    const FLinearColor CanvasBackdrop = FLinearColor(0.06f, 0.07f, 0.10f);
    const FLinearColor RingSkirt = FLinearColor(0.16f, 0.18f, 0.24f);
    const FLinearColor Post = FLinearColor(0.90f, 0.20f, 0.16f);
    const FLinearColor PostTrim = FLinearColor(0.95f, 0.82f, 0.25f);
    const FLinearColor RopeRed = FLinearColor(0.78f, 0.14f, 0.16f);
    const FLinearColor RopeWhite = FLinearColor(0.90f, 0.90f, 0.92f);
    const FLinearColor RopeBlue = FLinearColor(0.16f, 0.32f, 0.78f);

    const FLinearColor RockSkin = FLinearColor(0.80f, 0.62f, 0.48f);
    const FLinearColor RockTrunks = FLinearColor(0.85f, 0.20f, 0.18f);
    const FLinearColor ShadowSkin = FLinearColor(0.62f, 0.52f, 0.52f);
    const FLinearColor ShadowTrunks = FLinearColor(0.18f, 0.22f, 0.42f);
    const FLinearColor Glove = FLinearColor(0.88f, 0.14f, 0.16f);
    const FLinearColor Boot = FLinearColor(0.12f, 0.12f, 0.14f);

    const FLinearColor HitboxActive = FLinearColor(1.0f, 0.15f, 0.10f, 0.35f);
    const FLinearColor StateTint = FLinearColor::White;
}
}
