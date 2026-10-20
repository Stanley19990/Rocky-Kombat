// RKCVisuals.cpp

#include "Visual/RKCVisuals.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "RockyKombat.h"

namespace RKCVisuals
{
namespace
{
/** BasicShapeMaterial is a plain engine material with a single colour vector
 *  parameter. Both the documented name and the common alternative are set, so
 *  the tint survives either spelling without a hard dependency. */
const TCHAR* ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

UMaterialInterface* LoadShapeMaterial()
{
    static TWeakObjectPtr<UMaterialInterface> Cached;
    if (Cached.IsValid())
    {
        return Cached.Get();
    }
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath);
    Cached = Material;
    return Material;
}
}

UStaticMesh* LoadCubeMesh()
{
    static TWeakObjectPtr<UStaticMesh> Cached;
    if (!Cached.IsValid())
    {
        Cached = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    }
    return Cached.Get();
}

UStaticMesh* LoadPlaneMesh()
{
    static TWeakObjectPtr<UStaticMesh> Cached;
    if (!Cached.IsValid())
    {
        Cached = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    }
    return Cached.Get();
}

UStaticMesh* LoadCylinderMesh()
{
    static TWeakObjectPtr<UStaticMesh> Cached;
    if (!Cached.IsValid())
    {
        Cached = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    }
    return Cached.Get();
}

bool ArePrimitivesAvailable()
{
    return LoadCubeMesh() != nullptr;
}

void SetShapeColour(UStaticMeshComponent* Component, const FLinearColor& Colour)
{
    if (!Component)
    {
        return;
    }

    UMaterialInterface* Base = LoadShapeMaterial();
    if (!Base)
    {
        // No tint available; the primitive still renders with its own material.
        return;
    }

    UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0));
    if (!Dynamic)
    {
        Dynamic = UMaterialInstanceDynamic::Create(Base, Component);
        if (!Dynamic)
        {
            return;
        }
        Component->SetMaterial(0, Dynamic);
    }

    Dynamic->SetVectorParameterValue(TEXT("Color"), Colour);
    Dynamic->SetVectorParameterValue(TEXT("BaseColor"), Colour);
}

UStaticMeshComponent* AddShape(AActor* Owner, USceneComponent* Parent, const FName& Name,
                               const FVector& SizeCm, const FVector& RelativeLocationCm,
                               const FLinearColor& Colour, bool bDrawPlane, bool bAdditiveDepth)
{
    if (!Owner)
    {
        return nullptr;
    }

    UStaticMesh* Mesh = bDrawPlane ? LoadPlaneMesh() : LoadCubeMesh();
    if (!Mesh)
    {
        UE_LOG(LogRockyKombat, Warning,
               TEXT("Basic shape mesh unavailable; component '%s' was not created."),
               *Name.ToString());
        return nullptr;
    }

    UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner, Name);
    if (!Component)
    {
        return nullptr;
    }

    Component->SetStaticMesh(Mesh);
    Component->SetMobility(EComponentMobility::Movable);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetGenerateOverlapEvents(false);
    Component->bCastDynamicShadow = false;
    Component->CastShadow = false;
    if (bAdditiveDepth)
    {
        Component->SetRenderCustomDepth(false);
    }

    Component->RegisterComponent();

    if (Parent && Parent != Owner->GetRootComponent())
    {
        Component->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
    }
    else if (Parent)
    {
        Component->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
    }

    // Engine basic shapes are 100 cm across, so centimetres map to a scale of /100.
    Component->SetRelativeScale3D(SizeCm / 100.0f);
    Component->SetRelativeLocation(RelativeLocationCm);

    SetShapeColour(Component, Colour);
    return Component;
}
}
