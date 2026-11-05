// Source/Transfigure/Limb/SeveredBodyPart.cpp

#include "Limb/SeveredBodyPart.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ProceduralMeshComponent.h"
#include "RingFrameUtils.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"

TArray<TWeakObjectPtr<ASeveredBodyPart>> ASeveredBodyPart::AllParts;

ASeveredBodyPart::ASeveredBodyPart()
{
    PrimaryActorTick.bCanEverTick = true;
    Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
    Mesh->SetGenerateOverlapEvents(false);
}

void ASeveredBodyPart::Init(USkeletalMesh* InMesh, const TArray<UMaterialInterface*>& SlotMaterials,
    const TArray<int32>& VisibleSlots, FName CutBone, const FTransform& CutBoneWorld, const FVector& LaunchVelocity)
{
    RootBone = CutBone;
    Mesh->SetSkeletalMesh(InMesh);
    for (int32 i = 0; i < SlotMaterials.Num(); ++i)
    {
        Mesh->SetMaterial(i, SlotMaterials[i]);
        Mesh->ShowMaterialSection(i, i, VisibleSlots.Contains(i), 0);
    }

    // Place the component so the cut bone (in its reference pose) lands on the zombie's bone.
    const FReferenceSkeleton& RefSkel = InMesh->GetRefSkeleton();
    const int32 BoneIdx = RefSkel.FindBoneIndex(CutBone);
    FTransform RefCS = FTransform::Identity;
    for (int32 B = BoneIdx; B != INDEX_NONE; B = RefSkel.GetParentIndex(B))
    {
        RefCS = RefCS * RefSkel.GetRefBonePose()[B];
    }
    FTransform Unscaled = CutBoneWorld;
    const FVector Scale = CutBoneWorld.GetScale3D();
    Unscaled.SetScale3D(FVector::OneVector);
    FTransform CompXf = RefCS.Inverse() * Unscaled;
    CompXf.SetScale3D(Scale);
    SetActorTransform(CompXf);

    // Physics: only the severed subtree simulates. The rest of the (hidden) skeleton keeps
    // its bodies kinematic and collision-free, so it can't snag the falling limb.
    Mesh->SetEnableGravity(false);              // ring gravity in Tick
    // SetSkeletalMesh already built the physics bodies. (Forcing a recreate here and then
    // walking Mesh->Bodies read freed bodies and crashed the editor.)
    if (!Mesh->IsPhysicsStateCreated())
    {
        Mesh->RecreatePhysicsState();
    }
    UPhysicsAsset* PhysAsset = Mesh->GetPhysicsAsset();
    if (!PhysAsset || BoneIdx == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] SeveredBodyPart: no physics asset or bone %s - limb won't simulate"), *CutBone.ToString());
        return;
    }
    TSet<int32> Subtree;
    for (int32 B = 0; B < RefSkel.GetNum(); ++B)
    {
        if (B == BoneIdx || RefSkel.BoneIsChildOf(B, BoneIdx)) Subtree.Add(B);
    }
    // Look bodies up by bone name through the physics asset (safe), not the raw array.
    for (const USkeletalBodySetup* Setup : PhysAsset->SkeletalBodySetups)
    {
        if (!Setup || Subtree.Contains(RefSkel.FindBoneIndex(Setup->BoneName))) continue;
        if (FBodyInstance* Body = Mesh->GetBodyInstance(Setup->BoneName))
        {
            Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }
    // Unreal's gore path: terminate the joint at the cut bone; everything below goes physical.
    Mesh->BreakConstraint(FVector::ZeroVector, CutBoneWorld.GetLocation(), CutBone);
    Mesh->SetAllBodiesBelowSimulatePhysics(CutBone, true, true);
    Mesh->SetAllBodiesBelowPhysicsBlendWeight(CutBone, 1.f, false, true);
    Mesh->AddImpulseToAllBodiesBelow(LaunchVelocity, CutBone, /*bVelChange=*/true, /*bIncludeSelf=*/true);
    Mesh->AddAngularImpulseInDegrees(FMath::VRand() * 360.f, CutBone, true);

    AllParts.RemoveAll([](const TWeakObjectPtr<ASeveredBodyPart>& P) { return !P.IsValid(); });
    AllParts.Add(this);
    while (AllParts.Num() > MaxParts)
    {
        if (ASeveredBodyPart* Oldest = AllParts[0].Get()) Oldest->Destroy();
        AllParts.RemoveAt(0);
    }
}

void ASeveredBodyPart::AddWound(const FLimbMesh& WoundMesh, UMaterialInterface* Material)
{
    UProceduralMeshComponent* PM = NewObject<UProceduralMeshComponent>(this);
    PM->SetupAttachment(Mesh, RootBone);
    PM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PM->RegisterComponent();
    PM->CreateMeshSection(0, WoundMesh.Vertices, WoundMesh.Triangles, WoundMesh.Normals, WoundMesh.UVs,
        WoundMesh.Colors, WoundMesh.Tangents, false);
    if (Material) PM->SetMaterial(0, Material);
}

void ASeveredBodyPart::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!Mesh || RootBone.IsNone()) return;

    const FVector Pos = Mesh->GetSocketLocation(RootBone);
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    const double Altitude = URingWorldGeometry::WorldToRing(Spec, Pos).Altitude;
    const float G = static_cast<float>(URingWorldGeometry::GetGravityAtAltitude(Spec, Altitude));
    Mesh->AddForceToAllBodiesBelow(-URingFrameUtils::GetUpAt(Pos) * G, RootBone, /*bAccelChange=*/true, /*bIncludeSelf=*/true);
}

void ASeveredBodyPart::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    AllParts.RemoveAll([this](const TWeakObjectPtr<ASeveredBodyPart>& P) { return !P.IsValid() || P.Get() == this; });
    Super::EndPlay(EndPlayReason);
}
