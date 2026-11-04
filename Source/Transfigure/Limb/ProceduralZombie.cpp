// Source/Transfigure/Limb/ProceduralZombie.cpp

#include "Limb/ProceduralZombie.h"
#include "Limb/LimbMeshGenerator.h"
#include "Limb/SeveredLimb.h"
#include "Transfigure.h"
#include "ProceduralMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "RingFrameUtils.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"
#include "UObject/ConstructorHelpers.h"

AProceduralZombie::AProceduralZombie()
{
    // Defaults for the UE5 Mannequin (migrated into /Game/Characters/Mannequins).
    BodySkeleton = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
    BodyAnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C")));
    Parts = MakeDefaultParts();

    // Corpses persist for a while (and can still be dismembered).
    CorpseLifetime = 30.f;
    GetCharacterMovement()->MaxWalkSpeed = 250.f;   // shamble

    GetCapsuleComponent()->InitCapsuleSize(34.f, 90.f);
    // Shots go through the movement capsule to the per-part hit zones.
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Weapon, ECR_Ignore);

    // Hidden skeleton: never rendered, but always animated so attached parts move.
    USkeletalMeshComponent* Skel = GetMesh();
    Skel->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -90.f), FRotator(0.f, -90.f, 0.f));
    Skel->SetHiddenInGame(true);
    Skel->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Skel->SetCollisionProfileName(TEXT("CharacterMesh"));

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (BasicMat.Succeeded()) BaseMaterial = BasicMat.Object;
}

TArray<FLimbPartSpec> AProceduralZombie::MakeDefaultParts()
{
    // Bone names are the UE5 Mannequin's (SKM_Manny / SKM_Quinn).
    auto P = [](ELimbPart Part, const TCHAR* Start, const TCHAR* End, float Extra,
                float R0, float R1, float InBulge, float PartHealth, bool bSever, float BodyMult, ELimbPart Parent)
    {
        FLimbPartSpec S;
        S.Part = Part; S.StartBone = Start; S.EndBone = End; S.ExtraLength = Extra;
        S.StartRadius = R0; S.EndRadius = R1; S.Bulge = InBulge;
        S.MaxHealth = PartHealth; S.bSeverable = bSever; S.BodyDamageMultiplier = BodyMult; S.ParentPart = Parent;
        return S;
    };
    return {
        P(ELimbPart::Torso,     TEXT("pelvis"),     TEXT("neck_01"),   0.f, 15.f, 14.f, 3.f, 9999.f, false, 1.f, ELimbPart::None),
        P(ELimbPart::Head,      TEXT("head"),       TEXT(""),         24.f,  8.f,  7.f, 2.f,   50.f, true,  2.f, ELimbPart::Torso),
        P(ELimbPart::UpperArmL, TEXT("upperarm_l"), TEXT("lowerarm_l"), 0.f, 5.5f, 4.5f, 1.f,  40.f, true,  1.f, ELimbPart::Torso),
        P(ELimbPart::LowerArmL, TEXT("lowerarm_l"), TEXT("hand_l"),   14.f, 4.5f, 3.5f, 0.5f,  30.f, true,  1.f, ELimbPart::UpperArmL),
        P(ELimbPart::UpperArmR, TEXT("upperarm_r"), TEXT("lowerarm_r"), 0.f, 5.5f, 4.5f, 1.f,  40.f, true,  1.f, ELimbPart::Torso),
        P(ELimbPart::LowerArmR, TEXT("lowerarm_r"), TEXT("hand_r"),   14.f, 4.5f, 3.5f, 0.5f,  30.f, true,  1.f, ELimbPart::UpperArmR),
        P(ELimbPart::ThighL,    TEXT("thigh_l"),    TEXT("calf_l"),    0.f, 8.f,  6.f,  1.5f,  50.f, true,  1.f, ELimbPart::Torso),
        P(ELimbPart::CalfL,     TEXT("calf_l"),     TEXT("foot_l"),    6.f, 6.f,  4.5f, 1.f,   40.f, true,  1.f, ELimbPart::ThighL),
        P(ELimbPart::ThighR,    TEXT("thigh_r"),    TEXT("calf_r"),    0.f, 8.f,  6.f,  1.5f,  50.f, true,  1.f, ELimbPart::Torso),
        P(ELimbPart::CalfR,     TEXT("calf_r"),     TEXT("foot_r"),    6.f, 6.f,  4.5f, 1.f,   40.f, true,  1.f, ELimbPart::ThighR),
    };
}

void AProceduralZombie::EnsureBodyAssets()
{
    USkeletalMeshComponent* Skel = GetMesh();
    if (!Skel->GetSkeletalMeshAsset())
    {
        if (USkeletalMesh* SM = BodySkeleton.LoadSynchronous())
        {
            Skel->SetSkeletalMesh(SM);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[TM] %s: body skeleton %s not found - migrate the UE5 Mannequin into /Game/Characters/Mannequins"),
                *GetName(), *BodySkeleton.ToString());
        }
    }
    if (!Skel->GetAnimInstance())
    {
        if (UClass* Anim = BodyAnimClass.LoadSynchronous())
        {
            Skel->SetAnimInstanceClass(Anim);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[TM] %s: anim class %s not found - the body will stand still"),
                *GetName(), *BodyAnimClass.ToString());
        }
    }
}

void AProceduralZombie::BeginPlay()
{
    EnsureBodyAssets();
    Super::BeginPlay();

    OriginalWalkSpeed = BaseMaxWalkSpeed;
    OriginalAttackDamage = AttackDamage;
    MeshRelativeLocation = GetMesh()->GetRelativeLocation();
    MeshRelativeRotation = GetMesh()->GetRelativeRotation();

    if (BaseMaterial)
    {
        SkinMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
        SkinMaterial->SetVectorParameterValue(TEXT("Color"), SkinColor);
    }

    RebuildBody();
}

void AProceduralZombie::RebuildBody()
{
    USkeletalMeshComponent* Skel = GetMesh();
    if (!Skel->GetSkeletalMeshAsset()) return;

    // Throw away the previous body's components.
    for (FLimbPartRuntime& Old : PartState)
    {
        if (Old.MeshComp) Old.MeshComp->DestroyComponent();
        if (Old.HitZone) Old.HitZone->DestroyComponent();
    }
    PartState.Reset();
    PartState.SetNum(Parts.Num());

    FRandomStream Rng(BodySeed != 0 ? BodySeed : FMath::Rand());
    const float Fatness = Rng.FRandRange(Mutations.BodyFatness.X, Mutations.BodyFatness.Y);

    for (int32 i = 0; i < Parts.Num(); ++i)
    {
        BuildPart(i, Rng, Fatness);
    }

    BaseMaxWalkSpeed = OriginalWalkSpeed;
    GetCharacterMovement()->MaxWalkSpeed = BaseMaxWalkSpeed;
    AttackDamage = OriginalAttackDamage;
}

void AProceduralZombie::BuildPart(int32 Index, FRandomStream& Rng, float Fatness)
{
    const FLimbPartSpec& Spec = Parts[Index];
    FLimbPartRuntime& State = PartState[Index];
    USkeletalMeshComponent* Skel = GetMesh();
    const FReferenceSkeleton& RefSkel = Skel->GetSkeletalMeshAsset()->GetRefSkeleton();

    // Component-space reference pose of a bone (walk up the parent chain).
    auto RefPoseCS = [&RefSkel](int32 Bone)
    {
        FTransform T = FTransform::Identity;
        while (Bone != INDEX_NONE)
        {
            T = T * RefSkel.GetRefBonePose()[Bone];
            Bone = RefSkel.GetParentIndex(Bone);
        }
        return T;
    };

    const int32 StartIdx = RefSkel.FindBoneIndex(Spec.StartBone);
    if (StartIdx == INDEX_NONE)
    {
        UE_LOG(LogTemp, Error, TEXT("[TM] %s: bone %s not on this skeleton"), *GetName(), *Spec.StartBone.ToString());
        State.bAttached = false;
        return;
    }
    const FTransform StartCS = RefPoseCS(StartIdx);

    // End point in the start bone's local space.
    FVector LocalEnd;
    const int32 EndIdx = Spec.EndBone.IsNone() ? INDEX_NONE : RefSkel.FindBoneIndex(Spec.EndBone);
    if (EndIdx != INDEX_NONE)
    {
        LocalEnd = StartCS.InverseTransformPosition(RefPoseCS(EndIdx).GetLocation());
        LocalEnd += LocalEnd.GetSafeNormal() * Spec.ExtraLength;
    }
    else
    {
        // No end bone: continue in the direction parent -> start (e.g. neck -> head -> top of head).
        const int32 ParentIdx = RefSkel.GetParentIndex(StartIdx);
        const FVector DirCS = ParentIdx != INDEX_NONE
            ? (StartCS.GetLocation() - RefPoseCS(ParentIdx).GetLocation()).GetSafeNormal()
            : FVector::UpVector;
        LocalEnd = StartCS.InverseTransformVectorNoScale(DirCS) * FMath::Max(Spec.ExtraLength, 5.f);
    }

    State.Shape = FLimbMeshGenerator::MakeShape(Spec, FVector::ZeroVector, LocalEnd, Mutations, Fatness, Rng);
    FLimbMeshGenerator::BuildTube(State.Shape, State.Mesh);
    State.Health = Spec.MaxHealth;
    State.bAttached = true;

    // Visible part, riding on the hidden bone.
    UProceduralMeshComponent* PM = NewObject<UProceduralMeshComponent>(this);
    PM->SetupAttachment(Skel, Spec.StartBone);
    PM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PM->SetCastShadow(true);
    PM->RegisterComponent();
    PM->CreateMeshSection(0, State.Mesh.Vertices, State.Mesh.Triangles, State.Mesh.Normals,
        State.Mesh.UVs, TArray<FColor>(), State.Mesh.Tangents, false);
    if (SkinMaterial) PM->SetMaterial(0, SkinMaterial);
    State.MeshComp = PM;

    // Hit zone from the same numbers: capsule along the part's axis.
    const FVector Axis = State.Shape.End - State.Shape.Start;
    const float Radius = State.Shape.MaxRadius();
    UCapsuleComponent* Zone = NewObject<UCapsuleComponent>(this);
    Zone->SetupAttachment(Skel, Spec.StartBone);
    Zone->InitCapsuleSize(Radius, FMath::Max(Axis.Size() * 0.5f, Radius));
    Zone->SetRelativeLocation((State.Shape.Start + State.Shape.End) * 0.5f);
    Zone->SetRelativeRotation(FRotationMatrix::MakeFromZ(Axis.GetSafeNormal()).Rotator());
    Zone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Zone->SetCollisionObjectType(ECC_WorldDynamic);
    Zone->SetCollisionResponseToAllChannels(ECR_Ignore);
    Zone->SetCollisionResponseToChannel(ECC_Weapon, ECR_Block);   // only shots/aim see it
    Zone->SetGenerateOverlapEvents(false);
    Zone->RegisterComponent();
    State.HitZone = Zone;
}

bool AProceduralZombie::HasPart(ELimbPart Part) const
{
    for (int32 i = 0; i < Parts.Num(); ++i)
    {
        if (Parts[i].Part == Part) return PartState.IsValidIndex(i) && PartState[i].bAttached;
    }
    return false;
}

int32 AProceduralZombie::FindHitPart(const FVector& HitLocation) const
{
    int32 Best = INDEX_NONE;
    float BestDist = PartHitTolerance;

    for (int32 i = 0; i < PartState.Num(); ++i)
    {
        const FLimbPartRuntime& S = PartState[i];
        if (!S.bAttached || !S.HitZone) continue;

        // Distance from the hit to the capsule's surface.
        const FTransform& T = S.HitZone->GetComponentTransform();
        const float Half = S.HitZone->GetScaledCapsuleHalfHeight() - S.HitZone->GetScaledCapsuleRadius();
        const FVector Up = T.GetUnitAxis(EAxis::Z);
        const FVector A = T.GetLocation() - Up * Half;
        const FVector B = T.GetLocation() + Up * Half;
        const float D = FMath::PointDistToSegment(HitLocation, A, B) - S.HitZone->GetScaledCapsuleRadius();

        if (D < BestDist)
        {
            BestDist = D;
            Best = i;
        }
    }
    return Best;
}

void AProceduralZombie::TakePoolDamage(float DamageAmount, FVector HitLocation)
{
    const int32 PartIdx = FindHitPart(HitLocation);
    float BodyDamage = DamageAmount;

    if (PartIdx != INDEX_NONE)
    {
        const FLimbPartSpec& Spec = Parts[PartIdx];
        FLimbPartRuntime& State = PartState[PartIdx];
        BodyDamage *= Spec.BodyDamageMultiplier;
        State.Health -= DamageAmount;

        UE_LOG(LogTemp, Warning, TEXT("[TM] %s: %s hit for %.0f (part health %.0f)"), *GetName(),
            *UEnum::GetValueAsString(Spec.Part), DamageAmount, FMath::Max(State.Health, 0.f));

        if (State.Health <= 0.f && Spec.bSeverable)
        {
            SeverPart(PartIdx, HitLocation);
        }
    }

    // Corpses can still be dismembered, but only living zombies take body damage.
    if (!bIsAlive) return;

    if (PartIdx != INDEX_NONE && Parts[PartIdx].Part == ELimbPart::Head && !PartState[PartIdx].bAttached)
    {
        // Lost its head: dead regardless of remaining health.
        Super::TakePoolDamage(FMath::Max(BodyDamage, Health), HitLocation);
        return;
    }
    Super::TakePoolDamage(BodyDamage, HitLocation);
}

void AProceduralZombie::SeverPart(int32 Index, const FVector& HitLocation)
{
    FLimbPartRuntime& Root = PartState[Index];
    if (!Root.bAttached || !Root.MeshComp) return;

    // This part plus everything hanging off it (upper arm takes the forearm).
    TArray<int32> Take = { Index };
    for (int32 Scan = 0; Scan < Take.Num(); ++Scan)
    {
        for (int32 i = 0; i < Parts.Num(); ++i)
        {
            if (Parts[i].ParentPart == Parts[Take[Scan]].Part && PartState[i].bAttached && !Take.Contains(i))
            {
                Take.Add(i);
            }
        }
    }

    // Severed actor sits exactly where the root part is; other parts' vertices are re-expressed in its space.
    const FTransform RootXf = Root.MeshComp->GetComponentTransform();
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ASeveredLimb* Limb = GetWorld()->SpawnActor<ASeveredLimb>(ASeveredLimb::StaticClass(), RootXf, Params);

    for (int32 i : Take)
    {
        FLimbPartRuntime& S = PartState[i];
        if (Limb && S.MeshComp)
        {
            const FTransform PartXf = S.MeshComp->GetComponentTransform();
            FLimbMesh Copy = S.Mesh;
            for (FVector& V : Copy.Vertices) V = RootXf.InverseTransformPosition(PartXf.TransformPosition(V));
            for (FVector& N : Copy.Normals) N = RootXf.InverseTransformVectorNoScale(PartXf.TransformVectorNoScale(N));
            Limb->AddPartMesh(Copy, SkinMaterial);
        }

        S.bAttached = false;
        S.Health = 0.f;
        if (S.MeshComp) S.MeshComp->SetVisibility(false);
        if (S.HitZone) S.HitZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    if (Limb)
    {
        // Thrown away from the hit, plus a little "up" relative to the ring.
        const FVector Away = (RootXf.GetLocation() - HitLocation).GetSafeNormal();
        const FVector Up = URingFrameUtils::GetUpFor(this);
        Limb->FinishAndLaunch((Away + Up * 0.5f).GetSafeNormal() * SeverLaunchSpeed + GetVelocity(), Parts[Index].Part);
    }

    UE_LOG(LogTemp, Warning, TEXT("[TM] %s: SEVERED %s (%d part(s))"), *GetName(),
        *UEnum::GetValueAsString(Parts[Index].Part), Take.Num());

    UpdateCripple();
}

void AProceduralZombie::UpdateCripple()
{
    // Legs: each missing leg halves speed.
    const bool bLeftLeg = HasPart(ELimbPart::ThighL) && HasPart(ELimbPart::CalfL);
    const bool bRightLeg = HasPart(ELimbPart::ThighR) && HasPart(ELimbPart::CalfR);
    const int32 Legs = (bLeftLeg ? 1 : 0) + (bRightLeg ? 1 : 0);
    const float SpeedScale = Legs == 2 ? 1.f : (Legs == 1 ? 0.5f : 0.25f);
    BaseMaxWalkSpeed = OriginalWalkSpeed * SpeedScale;
    GetCharacterMovement()->MaxWalkSpeed = FMath::Min(GetCharacterMovement()->MaxWalkSpeed, BaseMaxWalkSpeed);

    // Arms: no forearms left = no attack.
    const bool bAnyArm = HasPart(ELimbPart::LowerArmL) || HasPart(ELimbPart::LowerArmR);
    AttackDamage = bAnyArm ? OriginalAttackDamage : 0.f;
}

void AProceduralZombie::Die()
{
    const bool bWasAlive = bIsAlive;
    Super::Die();
    if (!bWasAlive || bRagdoll) return;

    // Ragdoll the hidden skeleton; the generated parts ride along on its bones.
    USkeletalMeshComponent* Skel = GetMesh();
    if (Skel->GetSkeletalMeshAsset() && Skel->GetPhysicsAsset())
    {
        Skel->SetCollisionProfileName(TEXT("Ragdoll"));
        Skel->SetCollisionResponseToChannel(ECC_Weapon, ECR_Ignore);   // hit zones still take the shots
        Skel->SetEnableGravity(false);                                 // ring gravity applied in Tick
        Skel->SetSimulatePhysics(true);
        Skel->SetAllBodiesPhysicsBlendWeight(1.f);
        bRagdoll = true;
    }
}

void AProceduralZombie::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (bRagdoll)
    {
        // Ring "down" at the corpse's actual position, not world -Z.
        const FVector Pos = GetMesh()->GetComponentLocation();
        const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
        const double Altitude = URingWorldGeometry::WorldToRing(Spec, Pos).Altitude;
        const float G = static_cast<float>(URingWorldGeometry::GetGravityAtAltitude(Spec, Altitude));
        // "pelvis" is the ancestor of every body in the Mannequin's physics asset.
        GetMesh()->AddForceToAllBodiesBelow(-URingFrameUtils::GetUpAt(Pos) * G, TEXT("pelvis"), /*bAccelChange=*/true, /*bIncludeSelf=*/true);
    }
}

void AProceduralZombie::StopRagdoll()
{
    if (!bRagdoll) return;
    bRagdoll = false;

    USkeletalMeshComponent* Skel = GetMesh();
    Skel->SetSimulatePhysics(false);
    Skel->SetAllBodiesPhysicsBlendWeight(0.f);
    Skel->SetCollisionProfileName(TEXT("CharacterMesh"));
    Skel->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    Skel->SetRelativeLocationAndRotation(MeshRelativeLocation, MeshRelativeRotation);
}

void AProceduralZombie::OnSpawnFromPool()
{
    // A pooled zombie comes back whole (fresh mutation unless BodySeed is set).
    StopRagdoll();
    RebuildBody();
    Super::OnSpawnFromPool();
}
