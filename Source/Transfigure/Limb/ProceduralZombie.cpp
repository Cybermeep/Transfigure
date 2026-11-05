// Source/Transfigure/Limb/ProceduralZombie.cpp

#include "Limb/ProceduralZombie.h"
#include "Limb/LimbMeshGenerator.h"
#include "Limb/SeveredLimb.h"
#include "Limb/SeveredBodyPart.h"
#include "Limb/ZombieBodyVariant.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Transfigure.h"
#include "ProceduralMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Transfiguration/StatusEffectComponent.h"
#include "Templates/UnrealTemplate.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "RingFrameUtils.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr float BaseCapsuleRadius = 34.f;
    constexpr float BaseCapsuleHalfHeight = 90.f;

    bool IsArm(ELimbPart P) { return P == ELimbPart::UpperArmL || P == ELimbPart::LowerArmL || P == ELimbPart::UpperArmR || P == ELimbPart::LowerArmR; }
    bool IsForearm(ELimbPart P) { return P == ELimbPart::LowerArmL || P == ELimbPart::LowerArmR; }
    bool IsLeg(ELimbPart P) { return P == ELimbPart::ThighL || P == ELimbPart::CalfL || P == ELimbPart::ThighR || P == ELimbPart::CalfR; }
    bool IsLeftArm(ELimbPart P) { return P == ELimbPart::UpperArmL || P == ELimbPart::LowerArmL; }

    FLinearColor RandomHSV(FRandomStream& Rng, const FVector2D& H, const FVector2D& S, const FVector2D& V)
    {
        return FLinearColor(Rng.FRandRange(H.X, H.Y), Rng.FRandRange(S.X, S.Y), Rng.FRandRange(V.X, V.Y)).HSVToLinearRGB();
    }

    /** Random unit vector perpendicular to Axis. */
    FVector RandomPerpendicular(FRandomStream& Rng, const FVector& Axis)
    {
        FVector V = FVector::VectorPlaneProject(Rng.GetUnitVector(), Axis).GetSafeNormal();
        return V.IsNearlyZero() ? FVector::CrossProduct(Axis, FVector::UpVector).GetSafeNormal() : V;
    }
}

AProceduralZombie::AProceduralZombie()
{
    // Defaults for the UE5 Mannequin (in /Game/Characters/Mannequins).
    BodySkeleton = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
    BodyAnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C")));
    Parts = MakeDefaultParts();
    Archetypes = MakeDefaultArchetypes();
    FleshMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Zombie/Materials/M_ZombieFlesh.M_ZombieFlesh")));
    GrowthMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Zombie/Materials/M_ZombieGrowth.M_ZombieGrowth")));
    MetalMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Zombie/Materials/M_ZombieMetal.M_ZombieMetal")));

    // Corpses persist for a while (and can still be dismembered).
    CorpseLifetime = 30.f;
    GetCharacterMovement()->MaxWalkSpeed = 250.f;

    GetCapsuleComponent()->InitCapsuleSize(BaseCapsuleRadius, BaseCapsuleHalfHeight);
    // Shots go through the movement capsule to the per-part hit zones.
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Weapon, ECR_Ignore);

    // Hidden skeleton: never rendered, but always animated so attached parts move.
    USkeletalMeshComponent* Skel = GetMesh();
    Skel->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -BaseCapsuleHalfHeight), FRotator(0.f, -90.f, 0.f));
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
        P(ELimbPart::Torso,     TEXT("pelvis"),     TEXT("neck_01"),   0.f, 14.f, 13.f, 3.f, 9999.f, false, 1.f, ELimbPart::None),
        P(ELimbPart::Head,      TEXT("head"),       TEXT(""),         24.f,  8.f,  7.f, 2.f,   50.f, true,  2.f, ELimbPart::Torso),
        P(ELimbPart::UpperArmL, TEXT("upperarm_l"), TEXT("lowerarm_l"), 0.f, 5.5f, 4.5f, 1.5f, 40.f, true,  1.f, ELimbPart::Torso),
        P(ELimbPart::LowerArmL, TEXT("lowerarm_l"), TEXT("hand_l"),    4.f, 4.5f, 3.5f, 1.f,   30.f, true,  1.f, ELimbPart::UpperArmL),
        P(ELimbPart::UpperArmR, TEXT("upperarm_r"), TEXT("lowerarm_r"), 0.f, 5.5f, 4.5f, 1.5f, 40.f, true,  1.f, ELimbPart::Torso),
        P(ELimbPart::LowerArmR, TEXT("lowerarm_r"), TEXT("hand_r"),    4.f, 4.5f, 3.5f, 1.f,   30.f, true,  1.f, ELimbPart::UpperArmR),
        P(ELimbPart::ThighL,    TEXT("thigh_l"),    TEXT("calf_l"),    0.f, 8.f,  6.f,  2.f,   50.f, true,  1.f, ELimbPart::Torso),
        P(ELimbPart::CalfL,     TEXT("calf_l"),     TEXT("foot_l"),    6.f, 6.f,  4.5f, 1.5f,  40.f, true,  1.f, ELimbPart::ThighL),
        P(ELimbPart::ThighR,    TEXT("thigh_r"),    TEXT("calf_r"),    0.f, 8.f,  6.f,  2.f,   50.f, true,  1.f, ELimbPart::Torso),
        P(ELimbPart::CalfR,     TEXT("calf_r"),     TEXT("foot_r"),    6.f, 6.f,  4.5f, 1.5f,  40.f, true,  1.f, ELimbPart::ThighR),
    };
}

TArray<FZombieArchetype> AProceduralZombie::MakeDefaultArchetypes()
{
    TArray<FZombieArchetype> Out;

    // Husk: the baseline reanimated corpse.
    FZombieArchetype Husk;
    Husk.Name = TEXT("Husk");
    Husk.Weight = 5.f;
    Husk.GrowthCount = FIntPoint(1, 3);
    Husk.TendrilCount = FIntPoint(0, 2);
    Out.Add(Husk);

    // Bloat: swollen gut, one massive arm, small head. Slow and tough.
    FZombieArchetype Bloat;
    Bloat.Name = TEXT("Bloat");
    Bloat.Weight = 2.f;
    Bloat.BodyScale = 1.08f;
    Bloat.TorsoThickness = 1.5f;
    Bloat.ArmThickness = 1.15f;
    Bloat.LegThickness = 1.25f;
    Bloat.HeadScale = 0.8f;
    Bloat.Belly = 8.f;
    Bloat.BigArmChance = 1.f;
    Bloat.BigArmScale = 1.9f;
    Bloat.GrowthCount = FIntPoint(4, 7);
    Bloat.GrowthRadius = FVector2D(4.f, 10.f);
    Bloat.TendrilCount = FIntPoint(0, 1);
    Bloat.WalkSpeed = 170.f;
    Bloat.HealthMultiplier = 1.8f;
    Bloat.PartHealthMultiplier = 1.6f;
    Bloat.SkinHue = FVector2D(285.f, 310.f);
    Bloat.SkinSaturation = FVector2D(0.3f, 0.5f);
    Out.Add(Bloat);

    // Gaunt: tall and starved, long blade-like forearms, tendrils. Fast and fragile.
    FZombieArchetype Gaunt;
    Gaunt.Name = TEXT("Gaunt");
    Gaunt.Weight = 2.f;
    Gaunt.BodyScale = 1.12f;
    Gaunt.TorsoThickness = 0.78f;
    Gaunt.ArmThickness = 0.7f;
    Gaunt.LegThickness = 0.75f;
    Gaunt.HeadScale = 0.9f;
    Gaunt.ForearmExtraLength = 38.f;   // blades instead of hands
    Gaunt.bHasFingers = false;
    Gaunt.ForearmTipScale = 0.35f;
    Gaunt.GrowthCount = FIntPoint(0, 2);
    Gaunt.GrowthRadius = FVector2D(2.f, 5.f);
    Gaunt.TendrilCount = FIntPoint(2, 4);
    Gaunt.WalkSpeed = 360.f;
    Gaunt.HealthMultiplier = 0.7f;
    Gaunt.PartHealthMultiplier = 0.7f;
    Gaunt.SkinHue = FVector2D(255.f, 280.f);
    Gaunt.SkinSaturation = FVector2D(0.15f, 0.35f);
    Gaunt.SkinValue = FVector2D(0.22f, 0.34f);
    Out.Add(Gaunt);

    return Out;
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
            UE_LOG(LogTemp, Error, TEXT("[TM] %s: body skeleton %s not found - add the UE5 Mannequin to /Game/Characters/Mannequins"),
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
    ProceduralSkeleton = GetMesh()->GetSkeletalMeshAsset();
    Super::BeginPlay();

    OriginalAttackDamage = AttackDamage;
    OriginalMaxHealth = MaxHealth;
    MeshRelativeRotation = GetMesh()->GetRelativeRotation();

    auto MakeMID = [this](const TSoftObjectPtr<UMaterialInterface>& Asset) -> UMaterialInstanceDynamic*
    {
        UMaterialInterface* Parent = Asset.LoadSynchronous();
        if (!Parent)
        {
            UE_LOG(LogTemp, Warning, TEXT("[TM] %s: material %s missing (run Tools/create_zombie_materials.py) - using plain material"),
                *GetName(), *Asset.ToString());
            Parent = BaseMaterial;
        }
        return Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
    };
    SkinMaterial = MakeMID(FleshMaterialAsset);
    GrowthMaterial = MakeMID(GrowthMaterialAsset);
    MetalMaterial = MakeMID(MetalMaterialAsset);
    BoneMaterial = MakeMID(FleshMaterialAsset);
    if (BoneMaterial)
    {
        BoneMaterial->SetVectorParameterValue(TEXT("Color"), BoneColor);
        BoneMaterial->SetVectorParameterValue(TEXT("BruiseColor"), BoneColor * 0.75f);
        BoneMaterial->SetVectorParameterValue(TEXT("VeinColor"), BoneColor * 0.6f);
        BoneMaterial->SetScalarParameterValue(TEXT("SubsurfaceAmount"), 0.2f);
    }

    RebuildBody();
}

const FZombieArchetype& AProceduralZombie::PickArchetype(FRandomStream& Rng) const
{
    static const FZombieArchetype Fallback;
    float Total = 0.f;
    for (const FZombieArchetype& A : Archetypes) Total += FMath::Max(A.Weight, 0.f);
    if (Total <= 0.f) return Archetypes.Num() > 0 ? Archetypes[0] : Fallback;

    float Roll = Rng.FRandRange(0.f, Total);
    for (const FZombieArchetype& A : Archetypes)
    {
        Roll -= FMath::Max(A.Weight, 0.f);
        if (Roll <= 0.f) return A;
    }
    return Archetypes.Last();
}

void AProceduralZombie::ApplyArchetypeBody(const FZombieArchetype& Arch)
{
    // Whole-body scale: skeleton (and everything riding on it) plus the movement capsule.
    const float Scale = FMath::Max(Arch.BodyScale, 0.5f);
    const float Wide = Arch.TorsoThickness > 1.2f ? 1.25f : 1.f;
    GetCapsuleComponent()->SetCapsuleSize(BaseCapsuleRadius * Scale * Wide, BaseCapsuleHalfHeight * Scale);
    MeshRelativeLocation = FVector(0.f, 0.f, -BaseCapsuleHalfHeight * Scale);
    if (!bRagdoll)
    {
        GetMesh()->SetRelativeLocation(MeshRelativeLocation);
    }
    GetMesh()->SetRelativeScale3D(FVector(Scale));

    // Stats. ABaseEnemy::OnSpawnFromPool reads MaxHealth / BaseMaxWalkSpeed afterwards.
    OriginalWalkSpeed = Arch.WalkSpeed;
    BaseMaxWalkSpeed = OriginalWalkSpeed;
    GetCharacterMovement()->MaxWalkSpeed = BaseMaxWalkSpeed;
    MaxHealth = OriginalMaxHealth * Arch.HealthMultiplier;
    if (bIsAlive) Health = MaxHealth;
    AttackDamage = OriginalAttackDamage;
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
    for (FLimbDecorRuntime& Old : Decor)
    {
        if (Old.MeshComp) Old.MeshComp->DestroyComponent();
    }
    PartState.Reset();
    Decor.Reset();
    PartState.SetNum(Parts.Num());
    Skel->ShowAllMaterialSections(0);   // un-hide anything a previous life severed

    FRandomStream Rng(BodySeed != 0 ? BodySeed : FMath::Rand());
    const FZombieArchetype& Arch = PickArchetype(Rng);
    CurrentArchetype = Arch.Name;
    ApplyArchetypeBody(Arch);

    // Colours: purple flesh, brighter magenta growths, steel machine parts.
    const FLinearColor Skin = RandomHSV(Rng, Arch.SkinHue, Arch.SkinSaturation, Arch.SkinValue);
    const FLinearColor Growth = RandomHSV(Rng,
        FVector2D(Arch.SkinHue.X + 20.f, Arch.SkinHue.Y + 35.f), FVector2D(0.55f, 0.75f), FVector2D(0.45f, 0.62f));
    if (SkinMaterial)
    {
        const FLinearColor SkinHSV = Skin.LinearRGBToHSV();
        SkinMaterial->SetVectorParameterValue(TEXT("Color"), Skin);
        SkinMaterial->SetVectorParameterValue(TEXT("BruiseColor"),
            FLinearColor(SkinHSV.R + 12.f, FMath::Min(SkinHSV.G + 0.2f, 1.f), SkinHSV.B * 0.38f).HSVToLinearRGB());
        SkinMaterial->SetVectorParameterValue(TEXT("VeinColor"),
            FLinearColor(Rng.FRandRange(250.f, 275.f), 0.65f, 0.09f).HSVToLinearRGB());
        SkinMaterial->SetVectorParameterValue(TEXT("SubsurfaceColor"), Growth * 0.8f);
    }
    if (GrowthMaterial)
    {
        GrowthMaterial->SetVectorParameterValue(TEXT("Color"), Growth);
        GrowthMaterial->SetVectorParameterValue(TEXT("SubsurfaceColor"), Growth);
    }
    if (MetalMaterial) MetalMaterial->SetVectorParameterValue(TEXT("Color"), MetalColor);
    if (SkinMaterial) SkinMaterial->SetVectorParameterValue(TEXT("GlowColor"), Growth);
    if (MetalMaterial) MetalMaterial->SetVectorParameterValue(TEXT("GlowColor"), Growth);

    // Pre-generated skinned body for this form? Use it (and stop here).
    bBakedBody = false;
    if (bUseBakedBodies)
    {
        if (UZombieBodyVariant* Variant = PickVariant(Arch.Name, Rng))
        {
            ApplyBakedBody(Variant, Rng);
            return;
        }
    }
    // Procedural fallback: plain Mannequin skeleton, hidden, with generated parts riding on it.
    if (ProceduralSkeleton && Skel->GetSkeletalMeshAsset() != ProceduralSkeleton)
    {
        Skel->SetSkeletalMesh(ProceduralSkeleton);
    }
    Skel->SetHiddenInGame(true);
    CurrentVariant = nullptr;

    const float Fatness = Rng.FRandRange(Mutations.BodyFatness.X, Mutations.BodyFatness.Y);

    // Robot-stuffed corpse? Pick 1-2 limbs to be machinery.
    TSet<int32> MachineParts;
    const bool bRobot = Rng.FRand() < Arch.RobotChance;
    if (bRobot)
    {
        TArray<int32> Candidates;
        for (int32 i = 0; i < Parts.Num(); ++i)
        {
            if (IsArm(Parts[i].Part) || IsLeg(Parts[i].Part)) Candidates.Add(i);
        }
        const int32 Count = FMath::Min(Rng.RandRange(1, 2), Candidates.Num());
        for (int32 n = 0; n < Count; ++n)
        {
            MachineParts.Add(Candidates[Rng.RandRange(0, Candidates.Num() - 1)]);
        }
    }

    // Big arm side (Bloat): 0 = left, 1 = right.
    const bool bBigArm = Rng.FRand() < Arch.BigArmChance;
    const bool bBigLeft = Rng.FRand() < 0.5f;

    for (int32 i = 0; i < Parts.Num(); ++i)
    {
        // Apply the archetype's proportions to a copy of the spec.
        FLimbPartSpec Spec = Parts[i];
        float Thick = 1.f;
        if (Spec.Part == ELimbPart::Torso)
        {
            Thick = Arch.TorsoThickness;
            Spec.Bulge += Arch.Belly;
        }
        else if (Spec.Part == ELimbPart::Head)
        {
            Thick = Arch.HeadScale;
            Spec.ExtraLength *= Arch.HeadScale;
        }
        else if (IsArm(Spec.Part))
        {
            Thick = Arch.ArmThickness;
            if (bBigArm && IsLeftArm(Spec.Part) == bBigLeft) Thick *= Arch.BigArmScale;
            if (IsForearm(Spec.Part))
            {
                Spec.ExtraLength += Arch.ForearmExtraLength;
                Spec.EndRadius *= Arch.ForearmTipScale;
            }
        }
        else if (IsLeg(Spec.Part))
        {
            Thick = Arch.LegThickness;
        }
        Spec.StartRadius *= Thick;
        Spec.EndRadius *= Thick;
        Spec.Bulge *= Thick;
        Spec.MaxHealth *= Arch.PartHealthMultiplier;

        BuildPart(i, Spec, Rng, Fatness, MachineParts.Contains(i));
    }

    AddGrowths(Arch, Rng);
    AddHandsFeetFace(Arch, Rng);

    // Machine plates embedded in the torso of robot-stuffed corpses.
    if (bRobot)
    {
        const int32 TorsoIdx = Parts.IndexOfByPredicate([](const FLimbPartSpec& S) { return S.Part == ELimbPart::Torso; });
        if (PartState.IsValidIndex(TorsoIdx) && PartState[TorsoIdx].MeshComp)
        {
            const FLimbShape& Torso = PartState[TorsoIdx].Shape;
            const int32 Plates = Rng.RandRange(1, 3);
            for (int32 n = 0; n < Plates; ++n)
            {
                const float T = Rng.FRandRange(0.3f, 0.85f);
                const FVector Out = RandomPerpendicular(Rng, FLimbMeshGenerator::SpineDirection(Torso, T));
                const FVector Surface = FLimbMeshGenerator::SpinePoint(Torso, T) + Out * FLimbMeshGenerator::RadiusAt(Torso, T) * 0.9f;
                FLimbShape Plate;
                Plate.bMechanical = true;
                Plate.Segments = 1;
                Plate.CapExponent = 0.05f;
                Plate.Start = Surface - Out * 1.5f;
                Plate.End = Surface + Out * 3.f;
                Plate.StartRadius = Plate.EndRadius = Rng.FRandRange(4.f, 7.5f);
                AddDecor(TorsoIdx, Plate, ELimbMaterial::Metal);
            }
        }
    }

    UpdateCripple();

    FString Machine;
    for (int32 i : MachineParts) Machine += UEnum::GetValueAsString(Parts[i].Part).Replace(TEXT("ELimbPart::"), TEXT("")) + TEXT(" ");
    UE_LOG(LogTemp, Warning, TEXT("[TM] %s built as %s: %d growth/tendril/plate extras, machine parts: %s"),
        *GetName(), *CurrentArchetype.ToString(), Decor.Num(), Machine.IsEmpty() ? TEXT("none") : *Machine);
}

void AProceduralZombie::BuildPart(int32 Index, const FLimbPartSpec& Spec, FRandomStream& Rng, float Fatness, bool bMechanical,
    bool bBaked, float BakedRadius)
{
    FLimbPartRuntime& State = PartState[Index];
    State.Spec = Spec;
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

    if (Spec.Part == ELimbPart::Torso)
    {
        // Wider shoulder-to-shoulder than front-to-back.
        const int32 L = RefSkel.FindBoneIndex(TEXT("upperarm_l"));
        const int32 R = RefSkel.FindBoneIndex(TEXT("upperarm_r"));
        if (L != INDEX_NONE && R != INDEX_NONE)
        {
            State.Shape.WideAxis = StartCS.InverseTransformVectorNoScale(RefPoseCS(R).GetLocation() - RefPoseCS(L).GetLocation());
        }
        State.Shape.Width = Rng.FRandRange(1.15f, 1.3f);
        State.Shape.Depth = Rng.FRandRange(0.75f, 0.9f);
        State.Shape.Pinch *= 0.5f;
    }

    State.bMechanical = bMechanical;
    if (bMechanical)
    {
        // Machinery: ribbed, hard-edged, no lumps, flatter ends, a little slimmer.
        State.Shape.bMechanical = true;
        State.Shape.Segments = Rng.RandRange(2, 4);
        State.Shape.CapExponent = 0.12f;
        State.Shape.Lumpiness = 0.f;
        State.Shape.BendOffset = FVector::ZeroVector;
        State.Shape.StartRadius *= 0.85f;
        State.Shape.EndRadius = FMath::Max(State.Shape.EndRadius * 0.85f, State.Shape.StartRadius * 0.6f);
    }

    if (bBaked)
    {
        // The visible body is the baked skinned mesh: only keep a clean capsule shape
        // (measured radius) for the hit zone and the wound caps.
        const float R = BakedRadius > 0.f ? BakedRadius : State.Shape.StartRadius;
        State.Shape.StartRadius = State.Shape.EndRadius = R;
        State.Shape.Bulge = State.Shape.Lumpiness = 0.f;
        State.Shape.Width = State.Shape.Depth = 1.f;
        State.Shape.bMechanical = false;
    }
    else
    {
        FLimbMeshGenerator::BuildTube(State.Shape, State.Mesh);
    }
    State.Health = Spec.MaxHealth * (bMechanical ? MechanicalHealthMultiplier : 1.f);
    State.bAttached = true;

    // Visible part, riding on the hidden bone (procedural body only).
    if (!bBaked)
    {
    UProceduralMeshComponent* PM = NewObject<UProceduralMeshComponent>(this);
    PM->SetupAttachment(Skel, Spec.StartBone);
    PM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PM->SetCastShadow(true);
    PM->RegisterComponent();
    PM->CreateMeshSection(0, State.Mesh.Vertices, State.Mesh.Triangles, State.Mesh.Normals,
        State.Mesh.UVs, State.Mesh.Colors, State.Mesh.Tangents, false);
    PM->SetMaterial(0, GetMaterial(bMechanical ? ELimbMaterial::Metal : ELimbMaterial::Skin));
    State.MeshComp = PM;

    }

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
    Zone->SetCollisionResponseToChannel(ECC_Weapon, ECR_Block);   // only shots/aim/blasts see it
    Zone->SetGenerateOverlapEvents(false);
    Zone->RegisterComponent();
    State.HitZone = Zone;
}

void AProceduralZombie::AddGrowths(const FZombieArchetype& Arch, FRandomStream& Rng)
{
    // Candidate flesh parts (machine parts don't sprout growths). Torso counts 3x.
    TArray<int32> Flesh;
    int32 TorsoIdx = INDEX_NONE;
    TArray<int32> Shoulders;   // tendrils hang from the upper torso and upper arms
    for (int32 i = 0; i < PartState.Num(); ++i)
    {
        if (!PartState[i].MeshComp || PartState[i].bMechanical) continue;
        const ELimbPart P = Parts[i].Part;
        Flesh.Add(i);
        if (P == ELimbPart::Torso)
        {
            TorsoIdx = i;
            Flesh.Add(i);
            Flesh.Add(i);
        }
        if (P == ELimbPart::UpperArmL || P == ELimbPart::UpperArmR) Shoulders.Add(i);
    }
    if (Flesh.Num() == 0) return;

    // Sacs: lumpy blobs pushed half out of the surface.
    const int32 Sacs = Rng.RandRange(Arch.GrowthCount.X, FMath::Max(Arch.GrowthCount.X, Arch.GrowthCount.Y));
    for (int32 n = 0; n < Sacs; ++n)
    {
        const int32 Host = Flesh[Rng.RandRange(0, Flesh.Num() - 1)];
        const FLimbShape& HostShape = PartState[Host].Shape;
        const float T = Rng.FRandRange(0.2f, 0.8f);
        const FVector Out = RandomPerpendicular(Rng, FLimbMeshGenerator::SpineDirection(HostShape, T));
        const FVector Surface = FLimbMeshGenerator::SpinePoint(HostShape, T) + Out * FLimbMeshGenerator::RadiusAt(HostShape, T) * 0.75f;
        const float R = Rng.FRandRange(Arch.GrowthRadius.X, Arch.GrowthRadius.Y);

        FLimbShape Sac;
        Sac.Start = Surface - Out * R * 0.4f;
        Sac.End = Surface + Out * R * 1.1f;
        Sac.StartRadius = R * 0.55f;
        Sac.EndRadius = R * 0.45f;
        Sac.Bulge = R * 0.55f;
        Sac.BulgeCenter = Rng.FRandRange(0.4f, 0.6f);
        Sac.Lumpiness = R * 0.12f;
        Sac.CapExponent = 0.5f;
        Sac.Rings = 10;
        Sac.Sides = 12;
        Sac.NoiseSeed = Rng.RandRange(0, 100000);
        AddDecor(Host, Sac, ELimbMaterial::Growth);
    }

    // Tendrils: thin, strongly curled strands hanging from shoulders and upper back.
    TArray<int32> TendrilHosts = Shoulders;
    if (TorsoIdx != INDEX_NONE) { TendrilHosts.Add(TorsoIdx); TendrilHosts.Add(TorsoIdx); }
    if (TendrilHosts.Num() == 0) return;

    const int32 Tendrils = Rng.RandRange(Arch.TendrilCount.X, FMath::Max(Arch.TendrilCount.X, Arch.TendrilCount.Y));
    for (int32 n = 0; n < Tendrils; ++n)
    {
        const int32 Host = TendrilHosts[Rng.RandRange(0, TendrilHosts.Num() - 1)];
        const FLimbShape& HostShape = PartState[Host].Shape;
        const bool bTorso = Host == TorsoIdx;
        const float T = bTorso ? Rng.FRandRange(0.65f, 0.95f) : Rng.FRandRange(0.1f, 0.4f);
        const FVector Spine = FLimbMeshGenerator::SpineDirection(HostShape, T);
        const FVector Out = RandomPerpendicular(Rng, Spine);
        const FVector Root = FLimbMeshGenerator::SpinePoint(HostShape, T) + Out * FLimbMeshGenerator::RadiusAt(HostShape, T) * 0.7f;

        // Hang outward and back toward the part's start (down the torso / back toward the shoulder).
        const FVector Dir = (Out - Spine * 0.8f).GetSafeNormal();
        const float Len = Rng.FRandRange(30.f, 70.f);

        FLimbShape Strand;
        Strand.Start = Root;
        Strand.End = Root + Dir * Len;
        Strand.BendOffset = RandomPerpendicular(Rng, Dir) * Len * Rng.FRandRange(0.15f, 0.35f);
        Strand.StartRadius = Rng.FRandRange(2.f, 3.5f);
        Strand.EndRadius = 0.5f;
        Strand.Bulge = 0.5f;
        Strand.Lumpiness = 0.5f;
        Strand.CapExponent = 0.25f;
        Strand.Rings = 16;
        Strand.Sides = 8;
        Strand.NoiseSeed = Rng.RandRange(0, 100000);
        AddDecor(Host, Strand, ELimbMaterial::Growth);
    }
}

void AProceduralZombie::AddDecor(int32 OwnerPart, const FLimbShape& InShape, ELimbMaterial Material, FName Bone)
{
    if (!PartState.IsValidIndex(OwnerPart) || !PartState[OwnerPart].bAttached) return;

    FLimbShape Shape = InShape;
    Shape.Style = Material == ELimbMaterial::Growth ? FLimbShape::EStyle::Growth
                : Material == ELimbMaterial::Metal  ? FLimbShape::EStyle::Metal
                : FLimbShape::EStyle::Flesh;

    FLimbDecorRuntime& D = Decor.AddDefaulted_GetRef();
    D.OwnerPart = OwnerPart;
    D.Material = Material;
    FLimbMeshGenerator::BuildTube(Shape, D.Mesh);

    // Rides on Bone (default: the host part's bone), and its vertices are in that bone's space.
    UProceduralMeshComponent* PM = NewObject<UProceduralMeshComponent>(this);
    PM->SetupAttachment(GetMesh(), Bone.IsNone() ? PartState[OwnerPart].Spec.StartBone : Bone);
    PM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PM->RegisterComponent();
    PM->CreateMeshSection(0, D.Mesh.Vertices, D.Mesh.Triangles, D.Mesh.Normals, D.Mesh.UVs,
        D.Mesh.Colors, D.Mesh.Tangents, false);
    PM->SetMaterial(0, GetMaterial(Material));
    D.MeshComp = PM;
}

int32 AProceduralZombie::PartIndex(ELimbPart Part) const
{
    return Parts.IndexOfByPredicate([Part](const FLimbPartSpec& S) { return S.Part == Part; });
}

FTransform AProceduralZombie::RefPoseCS(int32 Bone) const
{
    const FReferenceSkeleton& RefSkel = GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    FTransform T = FTransform::Identity;
    while (Bone != INDEX_NONE)
    {
        T = T * RefSkel.GetRefBonePose()[Bone];
        Bone = RefSkel.GetParentIndex(Bone);
    }
    return T;
}

bool AProceduralZombie::BoneLocalPoint(FName From, FName To, FVector& Out) const
{
    const FReferenceSkeleton& RefSkel = GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    const int32 A = RefSkel.FindBoneIndex(From);
    const int32 B = RefSkel.FindBoneIndex(To);
    if (A == INDEX_NONE || B == INDEX_NONE) return false;
    Out = RefPoseCS(A).InverseTransformPosition(RefPoseCS(B).GetLocation());
    return true;
}

void AProceduralZombie::AddHandsFeetFace(const FZombieArchetype& Arch, FRandomStream& Rng)
{
    const FReferenceSkeleton& RefSkel = GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();

    for (int32 SideIdx = 0; SideIdx < 2; ++SideIdx)
    {
        const TCHAR* Sfx = SideIdx == 0 ? TEXT("_l") : TEXT("_r");
        auto B = [Sfx](const TCHAR* Name) { return FName(*(FString(Name) + Sfx)); };

        // ---- Hand: palm + five fingers on the real finger bones (so they animate).
        const int32 Fore = PartIndex(SideIdx == 0 ? ELimbPart::LowerArmL : ELimbPart::LowerArmR);
        if (Arch.bHasFingers && PartState.IsValidIndex(Fore) && PartState[Fore].bAttached)
        {
            const FLimbPartRuntime& F = PartState[Fore];
            const float Scale = Parts[Fore].StartRadius > 0.f ? F.Spec.StartRadius / Parts[Fore].StartRadius : 1.f;
            const ELimbMaterial Mat = F.bMechanical ? ELimbMaterial::Metal : ELimbMaterial::Skin;

            FVector KnuckleMid, IndexBase, PinkyBase;
            if (BoneLocalPoint(B(TEXT("hand")), B(TEXT("middle_01")), KnuckleMid) &&
                BoneLocalPoint(B(TEXT("hand")), B(TEXT("index_01")), IndexBase) &&
                BoneLocalPoint(B(TEXT("hand")), B(TEXT("pinky_01")), PinkyBase))
            {
                FLimbShape Palm;
                Palm.Start = -KnuckleMid.GetSafeNormal() * 1.5f;
                Palm.End = KnuckleMid * 1.05f;
                Palm.WideAxis = IndexBase - PinkyBase;
                Palm.Width = 1.5f; Palm.Depth = 0.6f;
                Palm.StartRadius = 3.2f * Scale; Palm.EndRadius = 3.0f * Scale;
                Palm.Bulge = 0.4f * Scale; Palm.Lumpiness = 0.35f; Palm.CapExponent = 0.4f;
                Palm.Rings = 12; Palm.Sides = 14; Palm.NoiseSeed = Rng.RandRange(0, 100000);
                Palm.bMechanical = F.bMechanical; Palm.Segments = 1;
                AddDecor(Fore, Palm, Mat, B(TEXT("hand")));
            }

            static const TCHAR* Fingers[] = { TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky"), TEXT("thumb") };
            for (const TCHAR* Finger : Fingers)
            {
                const FName Base = B(*(FString(Finger) + TEXT("_01")));
                FVector Tip;
                if (!BoneLocalPoint(Base, B(*(FString(Finger) + TEXT("_03"))), Tip)) continue;

                const bool bThumb = FCString::Strcmp(Finger, TEXT("thumb")) == 0;
                const FVector Dir = Tip.GetSafeNormal();
                FLimbShape Digit;
                Digit.Start = -Dir * 0.8f;
                Digit.End = Tip + Dir * (1.6f + Arch.ClawLength * Rng.FRandRange(0.7f, 1.3f));
                Digit.BendOffset = RandomPerpendicular(Rng, Dir) * Rng.FRandRange(0.f, 1.2f);
                Digit.StartRadius = (bThumb ? 1.45f : 1.1f) * Scale;
                Digit.EndRadius = 0.45f * Scale;
                Digit.Bulge = 0.2f * Scale; Digit.Lumpiness = 0.18f; Digit.Pinch = 0.12f; Digit.CapExponent = 0.3f;
                Digit.Rings = 12; Digit.Sides = 8; Digit.NoiseSeed = Rng.RandRange(0, 100000);
                Digit.bMechanical = F.bMechanical; Digit.Segments = 3;
                AddDecor(Fore, Digit, Mat, Base);
            }
        }

        // ---- Foot: flattened, toes spread left-right.
        const int32 Calf = PartIndex(SideIdx == 0 ? ELimbPart::CalfL : ELimbPart::CalfR);
        FVector Ball;
        if (PartState.IsValidIndex(Calf) && PartState[Calf].bAttached && BoneLocalPoint(B(TEXT("foot")), B(TEXT("ball")), Ball))
        {
            const FLimbPartRuntime& C = PartState[Calf];
            const float Scale = Parts[Calf].StartRadius > 0.f ? C.Spec.StartRadius / Parts[Calf].StartRadius : 1.f;
            const int32 FootBone = RefSkel.FindBoneIndex(B(TEXT("foot")));
            FLimbShape Foot;
            Foot.Start = -Ball.GetSafeNormal() * 3.f;
            Foot.End = Ball + Ball.GetSafeNormal() * 7.f;
            Foot.WideAxis = RefPoseCS(FootBone).InverseTransformVectorNoScale(FVector(1.f, 0.f, 0.f));   // body left-right
            Foot.Width = 1.35f; Foot.Depth = 0.65f;
            Foot.StartRadius = 4.2f * Scale; Foot.EndRadius = 3.0f * Scale;
            Foot.Bulge = 0.5f; Foot.Lumpiness = 0.5f; Foot.CapExponent = 0.35f;
            Foot.Rings = 14; Foot.Sides = 14; Foot.NoiseSeed = Rng.RandRange(0, 100000);
            Foot.bMechanical = C.bMechanical; Foot.Segments = 2;
            AddDecor(Calf, Foot, C.bMechanical ? ELimbMaterial::Metal : ELimbMaterial::Skin, B(TEXT("foot")));
        }
    }

    // ---- Face: two glowing eyes on the front of the head.
    const int32 HeadIdx = PartIndex(ELimbPart::Head);
    const int32 HeadBone = RefSkel.FindBoneIndex(Parts.IsValidIndex(HeadIdx) ? Parts[HeadIdx].StartBone : NAME_None);
    if (PartState.IsValidIndex(HeadIdx) && PartState[HeadIdx].bAttached && HeadBone != INDEX_NONE)
    {
        const FLimbShape& Head = PartState[HeadIdx].Shape;
        // The Mannequin faces +Y in component space.
        const FVector Face = RefPoseCS(HeadBone).InverseTransformVectorNoScale(FVector(0.f, 1.f, 0.f)).GetSafeNormal();
        const float T = 0.52f;
        const FVector Spine = FLimbMeshGenerator::SpineDirection(Head, T);
        const FVector Front = FVector::VectorPlaneProject(Face, Spine).GetSafeNormal();
        const FVector Across = FVector::CrossProduct(Spine, Front).GetSafeNormal();
        const float R = FLimbMeshGenerator::RadiusAt(Head, T);
        const FVector Mid = FLimbMeshGenerator::SpinePoint(Head, T) + Front * R * 0.8f;
        const float EyeR = FMath::Clamp(R * 0.14f, 0.8f, 1.6f);

        for (int32 Eye = -1; Eye <= 1; Eye += 2)
        {
            const FVector P = Mid + Across * (R * 0.36f * Eye);
            FLimbShape Orb;
            Orb.Start = P - Front * EyeR * 0.6f;
            Orb.End = P + Front * EyeR * 1.0f;
            Orb.StartRadius = Orb.EndRadius = EyeR * 0.7f;
            Orb.Bulge = EyeR * 0.4f; Orb.CapExponent = 0.6f; Orb.Rings = 8; Orb.Sides = 10;
            Orb.NoiseSeed = Rng.RandRange(0, 100000);
            AddDecor(HeadIdx, Orb, ELimbMaterial::Growth);
        }
    }
}

void AProceduralZombie::MakeWound(const FLimbPartRuntime& P, bool bOnBody, FLimbShape& Cap, FLimbShape& BoneStub) const
{
    // In the severed part's start-bone space; the joint is at the origin.
    const FVector Axis = (P.Shape.End - P.Shape.Start).GetSafeNormal();
    const float R = P.Shape.StartRadius * 0.5f * (P.Shape.Width + P.Shape.Depth);
    const float Out = bOnBody ? 1.f : -1.f;   // which way is "out of the wound"

    Cap = FLimbShape();
    Cap.Start = -Axis * Out * 1.5f;
    Cap.End = Axis * Out * 2.5f;
    Cap.StartRadius = Cap.EndRadius = R * 1.05f;
    Cap.Bulge = 0.6f; Cap.Lumpiness = 0.7f; Cap.CapExponent = 0.12f;
    Cap.Rings = 6; Cap.Sides = 18; Cap.NoiseSeed = FMath::Rand();

    BoneStub = FLimbShape();
    BoneStub.Start = -Axis * Out * 1.f;
    BoneStub.End = Axis * Out * FMath::Max(R * 1.1f, 5.f);
    BoneStub.StartRadius = R * 0.3f; BoneStub.EndRadius = R * 0.24f;
    BoneStub.Bulge = R * 0.08f; BoneStub.Lumpiness = 0.15f; BoneStub.CapExponent = 0.6f;
    BoneStub.Rings = 10; BoneStub.Sides = 10; BoneStub.NoiseSeed = FMath::Rand();
    if (P.bMechanical)
    {
        // Machine joints tear into a ragged metal rod instead of bone.
        Cap.bMechanical = BoneStub.bMechanical = true;
        Cap.Segments = BoneStub.Segments = 1;
    }
}

UMaterialInterface* AProceduralZombie::GetMaterial(ELimbMaterial Kind) const
{
    switch (Kind)
    {
    case ELimbMaterial::Growth: return GrowthMaterial ? GrowthMaterial.Get() : BaseMaterial.Get();
    case ELimbMaterial::Metal:  return MetalMaterial ? MetalMaterial.Get() : BaseMaterial.Get();
    case ELimbMaterial::Bone:   return BoneMaterial ? BoneMaterial.Get() : BaseMaterial.Get();
    default:                    return SkinMaterial ? SkinMaterial.Get() : BaseMaterial.Get();
    }
}

bool AProceduralZombie::HasPart(ELimbPart Part) const
{
    for (int32 i = 0; i < Parts.Num(); ++i)
    {
        if (Parts[i].Part == Part) return PartState.IsValidIndex(i) && PartState[i].bAttached;
    }
    return false;
}

float AProceduralZombie::DistanceToPart(int32 Index, const FVector& Location) const
{
    const FLimbPartRuntime& S = PartState[Index];
    if (!S.HitZone) return BIG_NUMBER;

    const FTransform& T = S.HitZone->GetComponentTransform();
    const float Radius = S.HitZone->GetScaledCapsuleRadius();
    const float Half = S.HitZone->GetScaledCapsuleHalfHeight() - Radius;
    const FVector Up = T.GetUnitAxis(EAxis::Z);
    return FMath::PointDistToSegment(Location, T.GetLocation() - Up * Half, T.GetLocation() + Up * Half) - Radius;
}

int32 AProceduralZombie::FindHitPart(const FVector& HitLocation) const
{
    int32 Best = INDEX_NONE;
    float BestDist = PartHitTolerance;
    for (int32 i = 0; i < PartState.Num(); ++i)
    {
        if (!PartState[i].bAttached) continue;
        const float D = DistanceToPart(i, HitLocation);
        if (D < BestDist)
        {
            BestDist = D;
            Best = i;
        }
    }
    return Best;
}

void AProceduralZombie::TakeAreaDamage(float Damage, FVector Origin, float InnerRadius, float OuterRadius)
{
    // Every attached part in range takes its own falloff damage; the body takes
    // damage once, from the closest part. Parts are snapshotted first because
    // severing one detaches its children.
    float Closest = BIG_NUMBER;
    TArray<TPair<int32, float>> InRange;
    for (int32 i = 0; i < PartState.Num(); ++i)
    {
        if (!PartState[i].bAttached) continue;
        const float D = FMath::Max(DistanceToPart(i, Origin), 0.f);
        Closest = FMath::Min(Closest, D);
        const float Scale = AreaFalloff(D, InnerRadius, OuterRadius);
        if (Scale > 0.f) InRange.Emplace(i, Scale);
    }
    if (InRange.Num() == 0) return;

    int32 Severed = 0;
    bool bLostHead = false;
    for (const TPair<int32, float>& Hit : InRange)
    {
        FLimbPartRuntime& State = PartState[Hit.Key];
        if (!State.bAttached) continue;   // already taken with a severed parent
        State.Health -= Damage * Hit.Value * PartDamageScale(State);
        if (State.Health <= 0.f && State.Spec.bSeverable)
        {
            bLostHead |= State.Spec.Part == ELimbPart::Head;
            SeverPart(Hit.Key, Origin);
            ++Severed;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[TM] %s: area damage %.0f hit %d part(s), severed %d"),
        *GetName(), Damage, InRange.Num(), Severed);

    if (!bIsAlive) return;
    const float BodyDamage = Damage * AreaFalloff(Closest, InnerRadius, OuterRadius);
    // Body damage directly (skip our nearest-part logic, the parts are already done).
    ABaseEnemy::TakePoolDamage(bLostHead ? FMath::Max(BodyDamage, Health) : BodyDamage, Origin);
}

void AProceduralZombie::TakePoolDamage(float DamageAmount, FVector HitLocation)
{
    const int32 PartIdx = FindHitPart(HitLocation);
    float BodyDamage = DamageAmount;

    if (PartIdx != INDEX_NONE)
    {
        FLimbPartRuntime& State = PartState[PartIdx];
        BodyDamage *= State.Spec.BodyDamageMultiplier;
        const float PartDamage = DamageAmount * PartDamageScale(State);
        State.Health -= PartDamage;

        UE_LOG(LogTemp, Warning, TEXT("[TM] %s: %s%s hit for %.0f (part health %.0f)"), *GetName(),
            State.bMechanical ? TEXT("machine ") : TEXT(""),
            *UEnum::GetValueAsString(State.Spec.Part), PartDamage, FMath::Max(State.Health, 0.f));

        // Frozen solid: whatever limb you hit shatters.
        const bool bShatter = Status && Status->IsFrozen() && State.Spec.Part != ELimbPart::Torso;
        if ((State.Health <= 0.f || bShatter) && State.Spec.bSeverable)
        {
            if (bShatter) UE_LOG(LogTemp, Warning, TEXT("[TM] %s: frozen %s SHATTERED"), *GetName(), *UEnum::GetValueAsString(State.Spec.Part));
            SeverPart(PartIdx, HitLocation);
        }
    }

    // Corpses can still be dismembered, but only living zombies take body damage.
    if (!bIsAlive) return;

    if (PartIdx != INDEX_NONE && PartState[PartIdx].Spec.Part == ELimbPart::Head && !PartState[PartIdx].bAttached)
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
    if (!Root.bAttached || (!Root.MeshComp && !bBakedBody)) return;

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

    if (bBakedBody)
    {
        SeverBakedPart(Index, HitLocation, Take);
        UpdateCripple();
        return;
    }

    // Severed actor sits exactly where the root part is; everything else is re-expressed in its space.
    const FTransform RootXf = Root.MeshComp->GetComponentTransform();
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ASeveredLimb* Limb = GetWorld()->SpawnActor<ASeveredLimb>(ASeveredLimb::StaticClass(), RootXf, Params);

    auto CopyInto = [&](const FLimbMesh& SrcMesh, const UProceduralMeshComponent* From, UMaterialInterface* Mat)
    {
        if (!Limb || !From) return;
        const FTransform Xf = From->GetComponentTransform();
        FLimbMesh Copy = SrcMesh;
        for (FVector& V : Copy.Vertices) V = RootXf.InverseTransformPosition(Xf.TransformPosition(V));
        for (FVector& N : Copy.Normals) N = RootXf.InverseTransformVectorNoScale(Xf.TransformVectorNoScale(N));
        Limb->AddPartMesh(Copy, Mat);
    };

    for (int32 i : Take)
    {
        FLimbPartRuntime& S = PartState[i];
        CopyInto(S.Mesh, S.MeshComp, GetMaterial(S.bMechanical ? ELimbMaterial::Metal : ELimbMaterial::Skin));

        S.bAttached = false;
        S.Health = 0.f;
        if (S.MeshComp) S.MeshComp->SetVisibility(false);
        if (S.HitZone) S.HitZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // Growths, tendrils and plates go with the part they're on.
    for (FLimbDecorRuntime& D : Decor)
    {
        if (!Take.Contains(D.OwnerPart) || !D.MeshComp || !D.MeshComp->IsVisible()) continue;
        CopyInto(D.Mesh, D.MeshComp, GetMaterial(D.Material));
        D.MeshComp->SetVisibility(false);
    }

    // Wounds: infected cap + bone (or torn metal) on the limb's root...
    FLimbShape CapShape, BoneShape;
    const ELimbMaterial CapMat = Root.bMechanical ? ELimbMaterial::Metal : ELimbMaterial::Growth;
    const ELimbMaterial BoneMat = Root.bMechanical ? ELimbMaterial::Metal : ELimbMaterial::Bone;
    if (Limb)
    {
        MakeWound(Root, /*bOnBody=*/false, CapShape, BoneShape);
        CapShape.Style = Root.bMechanical ? FLimbShape::EStyle::Metal : FLimbShape::EStyle::Growth;
        FLimbMesh WoundMesh;
        FLimbMeshGenerator::BuildTube(CapShape, WoundMesh);
        Limb->AddPartMesh(WoundMesh, GetMaterial(CapMat));
        FLimbMeshGenerator::BuildTube(BoneShape, WoundMesh);
        Limb->AddPartMesh(WoundMesh, GetMaterial(BoneMat));
    }

    // ...and a stump on the body, riding on the severed part's (still animated) bone.
    const int32 ParentIdx = PartIndex(Root.Spec.ParentPart);
    if (PartState.IsValidIndex(ParentIdx) && PartState[ParentIdx].bAttached)
    {
        MakeWound(Root, /*bOnBody=*/true, CapShape, BoneShape);
        AddDecor(ParentIdx, CapShape, CapMat, Root.Spec.StartBone);
        AddDecor(ParentIdx, BoneShape, BoneMat, Root.Spec.StartBone);
    }

    if (Limb)
    {
        // Thrown away from the hit, plus a little "up" relative to the ring.
        const FVector Away = (RootXf.GetLocation() - HitLocation).GetSafeNormal();
        const FVector Up = URingFrameUtils::GetUpFor(this);
        Limb->FinishAndLaunch((Away + Up * 0.5f).GetSafeNormal() * SeverLaunchSpeed + GetVelocity(), Root.Spec.Part);
    }

    UE_LOG(LogTemp, Warning, TEXT("[TM] %s: SEVERED %s%s (%d part(s))"), *GetName(),
        Root.bMechanical ? TEXT("machine ") : TEXT(""), *UEnum::GetValueAsString(Root.Spec.Part), Take.Num());

    UpdateCripple();
}

float AProceduralZombie::PartDamageScale(const FLimbPartRuntime& State) const
{
    float Scale = GetElementScale(PendingElement, State.bMechanical);
    if (PendingElement == ETransfigurationElement::Earth && IsLeg(State.Spec.Part)) Scale *= 1.5f;   // crushes legs
    if (Status && Status->IsFrozen()) Scale *= 3.f;                                                  // brittle
    return Scale;
}

FVector AProceduralZombie::PartLocation(int32 Index) const
{
    if (PartState.IsValidIndex(Index) && PartState[Index].HitZone) return PartState[Index].HitZone->GetComponentLocation();
    return GetActorLocation();
}

float AProceduralZombie::GetElementScale(ETransfigurationElement Element, bool bMachinePart) const
{
    float Scale = Super::GetElementScale(Element, bMachinePart);
    if (CurrentArchetype == TEXT("Bloat") && Element == ETransfigurationElement::Fire) Scale *= 1.5f;
    else if (CurrentArchetype == TEXT("Gaunt") && Element == ETransfigurationElement::Ice) Scale *= 0.6f;
    else if (CurrentArchetype == TEXT("Gaunt") && Element == ETransfigurationElement::Lightning) Scale *= 1.2f;
    else if (CurrentArchetype == TEXT("Husk") && Element == ETransfigurationElement::Void) Scale *= 1.2f;
    return Scale;
}

void AProceduralZombie::TakeStatusDamage(float Damage, ETransfigurationElement Element)
{
    // Burning eats at a random limb as well, so a burning zombie starts falling apart.
    TArray<int32> Limbs;
    for (int32 i = 0; i < PartState.Num(); ++i)
    {
        if (PartState[i].bAttached && PartState[i].Spec.bSeverable && PartState[i].Spec.Part != ELimbPart::Torso) Limbs.Add(i);
    }
    TGuardValue<ETransfigurationElement> Guard(PendingElement, Element);
    if (Limbs.Num() > 0)
    {
        const int32 Idx = Limbs[FMath::RandRange(0, Limbs.Num() - 1)];
        FLimbPartRuntime& State = PartState[Idx];
        State.Health -= Damage * 0.6f * PartDamageScale(State);
        if (State.Health <= 0.f)
        {
            const bool bHead = State.Spec.Part == ELimbPart::Head;
            SeverPart(Idx, PartLocation(Idx));
            if (bHead && bIsAlive) ABaseEnemy::TakePoolDamage(Health, GetActorLocation());
        }
    }
    // Body damage directly (not through our part logic, which would hit the torso again).
    ABaseEnemy::TakePoolDamage(Damage, GetActorLocation());
}

bool AProceduralZombie::SeverNearestLimb(const FVector& From)
{
    int32 Best = INDEX_NONE;
    float BestDist = BIG_NUMBER;
    for (int32 i = 0; i < PartState.Num(); ++i)
    {
        const FLimbPartRuntime& State = PartState[i];
        if (!State.bAttached || !State.Spec.bSeverable || State.Spec.Part == ELimbPart::Torso) continue;
        const float D = DistanceToPart(i, From);
        if (D < BestDist)
        {
            BestDist = D;
            Best = i;
        }
    }
    if (Best == INDEX_NONE) return false;
    const bool bHead = PartState[Best].Spec.Part == ELimbPart::Head;
    UE_LOG(LogTemp, Warning, TEXT("[TM] %s: limb torn off by the void"), *GetName());
    SeverPart(Best, From);
    if (bHead && bIsAlive) ABaseEnemy::TakePoolDamage(Health, From);
    return true;
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
        DisableSeveredBodies();
    }
}

void AProceduralZombie::DisableSeveredBodies()
{
    if (!bBakedBody) return;
    USkeletalMeshComponent* Skel = GetMesh();
    UPhysicsAsset* PhysAsset = Skel->GetPhysicsAsset();
    USkeletalMesh* BodyMesh = Skel->GetSkeletalMeshAsset();
    if (!PhysAsset || !BodyMesh || !Skel->IsPhysicsStateCreated()) return;
    const FReferenceSkeleton& RefSkel = BodyMesh->GetRefSkeleton();
    for (int32 i = 0; i < PartState.Num(); ++i)
    {
        if (PartState[i].bAttached) continue;
        const int32 Cut = RefSkel.FindBoneIndex(Parts[i].StartBone);
        if (Cut == INDEX_NONE) continue;
        for (const USkeletalBodySetup* Setup : PhysAsset->SkeletalBodySetups)
        {
            if (!Setup) continue;
            const int32 B = RefSkel.FindBoneIndex(Setup->BoneName);
            if (B != Cut && !RefSkel.BoneIsChildOf(B, Cut)) continue;
            if (FBodyInstance* Body = Skel->GetBodyInstance(Setup->BoneName))
            {
                Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
        }
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

FName AProceduralZombie::PartName(ELimbPart Part)
{
    FString S = UEnum::GetValueAsString(Part);
    S.RemoveFromStart(TEXT("ELimbPart::"));
    return FName(*S);
}

UZombieBodyVariant* AProceduralZombie::PickVariant(FName Archetype, FRandomStream& Rng) const
{
    // Search by the asset's searchable Archetype tag, without loading every variant.
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FAssetData> All;
    Registry.GetAssetsByClass(UZombieBodyVariant::StaticClass()->GetClassPathName(), All);

    TArray<FAssetData> Match;
    for (const FAssetData& A : All)
    {
        FName Tag;
        if (A.GetTagValue(TEXT("Archetype"), Tag) && Tag == Archetype) Match.Add(A);
    }
    if (Match.Num() == 0) return nullptr;
    return Cast<UZombieBodyVariant>(Match[Rng.RandRange(0, Match.Num() - 1)].GetAsset());
}

void AProceduralZombie::ApplyBakedBody(UZombieBodyVariant* Variant, FRandomStream& Rng)
{
    USkeletalMesh* BodyMesh = Variant->Mesh.LoadSynchronous();
    if (!BodyMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] %s: variant %s has no mesh"), *GetName(), *Variant->GetName());
        return;
    }
    CurrentVariant = Variant;
    bBakedBody = true;

    USkeletalMeshComponent* Skel = GetMesh();
    Skel->SetSkeletalMesh(BodyMesh);
    Skel->SetHiddenInGame(false);
    Skel->ShowAllMaterialSections(0);
    if (UPhysicsAsset* PhysAsset = Skel->GetPhysicsAsset(); PhysAsset && Skel->IsPhysicsStateCreated())
    {
        for (const USkeletalBodySetup* Setup : PhysAsset->SkeletalBodySetups)
        {
            if (!Setup) continue;
            if (FBodyInstance* Body = Skel->GetBodyInstance(Setup->BoneName))
            {
                Body->SetCollisionEnabled(Skel->GetCollisionEnabled());
            }
        }
    }

    // Region slots get flesh, "<Region>_Metal" slots get metal.
    const TArray<FSkeletalMaterial>& Slots = BodyMesh->GetMaterials();
    for (int32 i = 0; i < Slots.Num(); ++i)
    {
        const bool bMetal = Slots[i].MaterialSlotName.ToString().EndsWith(TEXT("_Metal"));
        Skel->SetMaterial(i, GetMaterial(bMetal ? ELimbMaterial::Metal : ELimbMaterial::Skin));
    }

    // Hit zones from the measured limb thickness.
    for (int32 i = 0; i < Parts.Num(); ++i)
    {
        const float* R = Variant->PartRadius.Find(PartName(Parts[i].Part));
        BuildPart(i, Parts[i], Rng, 1.f, /*bMechanical=*/false, /*bBaked=*/true, R ? *R : 0.f);

        // A region with a "<Region>_Metal" slot is machinery: tougher, sparks instead of bone.
        const FName MetalSlot(*(PartName(Parts[i].Part).ToString() + TEXT("_Metal")));
        PartState[i].bMechanical = Slots.ContainsByPredicate([&](const FSkeletalMaterial& M) { return M.MaterialSlotName == MetalSlot; });
        if (PartState[i].bMechanical) PartState[i].Health *= MechanicalHealthMultiplier;
    }
    UpdateCripple();

    UE_LOG(LogTemp, Warning, TEXT("[TM] %s using baked body %s (%s)"), *GetName(), *Variant->GetName(), *CurrentArchetype.ToString());
}

void AProceduralZombie::SeverBakedPart(int32 Index, const FVector& HitLocation, const TArray<int32>& Take)
{
    USkeletalMeshComponent* Skel = GetMesh();
    USkeletalMesh* BodyMesh = Skel->GetSkeletalMeshAsset();
    FLimbPartRuntime& Root = PartState[Index];
    const FName CutBone = Root.Spec.StartBone;

    // Which material slots belong to the severed parts (flesh + their metal).
    TSet<FName> SlotNames;
    for (int32 i : Take)
    {
        const FName N = PartName(Parts[i].Part);
        SlotNames.Add(N);
        SlotNames.Add(FName(*(N.ToString() + TEXT("_Metal"))));
    }
    TArray<int32> Visible;
    TArray<UMaterialInterface*> SlotMats;
    const TArray<FSkeletalMaterial>& Slots = BodyMesh->GetMaterials();
    for (int32 i = 0; i < Slots.Num(); ++i)
    {
        SlotMats.Add(Skel->GetMaterial(i));
        if (SlotNames.Contains(Slots[i].MaterialSlotName))
        {
            Visible.Add(i);
            Skel->ShowMaterialSection(i, i, false, 0);   // gone from the zombie
        }
    }

    for (int32 i : Take)
    {
        FLimbPartRuntime& S = PartState[i];
        S.bAttached = false;
        S.Health = 0.f;
        if (S.HitZone) S.HitZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // The cut piece: same mesh, only these sections, joint broken, ring gravity.
    const FTransform BoneWorld = Skel->GetSocketTransform(CutBone, RTS_World);
    const FVector Away = (BoneWorld.GetLocation() - HitLocation).GetSafeNormal();
    const FVector Up = URingFrameUtils::GetUpFor(this);
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (ASeveredBodyPart* Gib = GetWorld()->SpawnActor<ASeveredBodyPart>(ASeveredBodyPart::StaticClass(), BoneWorld, Params))
    {
        Gib->Init(BodyMesh, SlotMats, Visible, CutBone, BoneWorld,
            (Away + Up * 0.5f).GetSafeNormal() * SeverLaunchSpeed + GetVelocity(), Skel);

        FLimbShape CapShape, BoneShape;
        MakeWound(Root, /*bOnBody=*/false, CapShape, BoneShape);
        CapShape.Style = Root.bMechanical ? FLimbShape::EStyle::Metal : FLimbShape::EStyle::Growth;
        FLimbMesh W;
        FLimbMeshGenerator::BuildTube(CapShape, W);
        Gib->AddWound(W, GetMaterial(Root.bMechanical ? ELimbMaterial::Metal : ELimbMaterial::Growth));
        FLimbMeshGenerator::BuildTube(BoneShape, W);
        Gib->AddWound(W, GetMaterial(Root.bMechanical ? ELimbMaterial::Metal : ELimbMaterial::Bone));
    }

    // Stump on the body.
    const int32 ParentIdx = PartIndex(Root.Spec.ParentPart);
    if (PartState.IsValidIndex(ParentIdx) && PartState[ParentIdx].bAttached)
    {
        FLimbShape CapShape, BoneShape;
        MakeWound(Root, /*bOnBody=*/true, CapShape, BoneShape);
        AddDecor(ParentIdx, CapShape, Root.bMechanical ? ELimbMaterial::Metal : ELimbMaterial::Growth, CutBone);
        AddDecor(ParentIdx, BoneShape, Root.bMechanical ? ELimbMaterial::Metal : ELimbMaterial::Bone, CutBone);
    }

    DisableSeveredBodies();

    UE_LOG(LogTemp, Warning, TEXT("[TM] %s: SEVERED %s%s from baked body (%d part(s), %d section(s))"), *GetName(),
        Root.bMechanical ? TEXT("machine ") : TEXT(""), *PartName(Root.Spec.Part).ToString(), Take.Num(), Visible.Num());
}

void AProceduralZombie::OnSpawnFromPool()
{
    // A pooled zombie comes back whole: new form and mutations unless BodySeed is set.
    StopRagdoll();
    RebuildBody();
    Super::OnSpawnFromPool();
}
