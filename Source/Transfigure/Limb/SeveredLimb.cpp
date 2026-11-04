// Source/Transfigure/Limb/SeveredLimb.cpp

#include "Limb/SeveredLimb.h"
#include "ProceduralMeshComponent.h"
#include "RingFrameUtils.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"

TArray<TWeakObjectPtr<ASeveredLimb>> ASeveredLimb::AllLimbs;

ASeveredLimb::ASeveredLimb()
{
    PrimaryActorTick.bCanEverTick = true;

    Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    Mesh->bUseComplexAsSimpleCollision = false;    // simulate with the convex hulls
    Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
}

void ASeveredLimb::AddPartMesh(const FLimbMesh& PartMesh, UMaterialInterface* Material)
{
    const int32 Section = NextSection++;
    Mesh->CreateMeshSection(Section, PartMesh.Vertices, PartMesh.Triangles, PartMesh.Normals,
        PartMesh.UVs, TArray<FColor>(), PartMesh.Tangents, false);
    if (Material) Mesh->SetMaterial(Section, Material);

    // Convex hull for physics (every few vertices is plenty for a hull).
    TArray<FVector> Hull;
    for (int32 i = 0; i < PartMesh.Vertices.Num(); i += 3) Hull.Add(PartMesh.Vertices[i]);
    Mesh->AddCollisionConvexMesh(Hull);
}

void ASeveredLimb::FinishAndLaunch(const FVector& Impulse, ELimbPart InRootPart)
{
    RootPart = InRootPart;

    // Gravity is applied manually along ring "down" in Tick.
    Mesh->SetEnableGravity(false);
    Mesh->SetSimulatePhysics(true);
    Mesh->AddImpulse(Impulse, NAME_None, true);
    Mesh->AddAngularImpulseInDegrees(FMath::VRand() * 360.f, NAME_None, true);

    // Persist, but cap the total so long fights can't pile up forever.
    AllLimbs.RemoveAll([](const TWeakObjectPtr<ASeveredLimb>& L) { return !L.IsValid(); });
    AllLimbs.Add(this);
    while (AllLimbs.Num() > MaxSeveredLimbs)
    {
        if (ASeveredLimb* Oldest = AllLimbs[0].Get()) Oldest->Destroy();
        AllLimbs.RemoveAt(0);
    }
}

void ASeveredLimb::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (Mesh && Mesh->IsSimulatingPhysics())
    {
        // Ring-correct gravity: "down" from wherever the limb actually is.
        const FVector Pos = Mesh->GetComponentLocation();
        const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
        const double Altitude = URingWorldGeometry::WorldToRing(Spec, Pos).Altitude;
        const float G = static_cast<float>(URingWorldGeometry::GetGravityAtAltitude(Spec, Altitude));
        Mesh->AddForce(-URingFrameUtils::GetUpAt(Pos) * G, NAME_None, /*bAccelChange=*/true);
    }
}

void ASeveredLimb::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    AllLimbs.RemoveAll([this](const TWeakObjectPtr<ASeveredLimb>& L) { return !L.IsValid() || L.Get() == this; });
    Super::EndPlay(EndPlayReason);
}
