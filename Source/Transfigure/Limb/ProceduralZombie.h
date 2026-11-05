// Source/Transfigure/Limb/ProceduralZombie.h
//
// Limb system: a zombie whose visible body is generated from math
// (FLimbMeshGenerator) and attached to a HIDDEN Mannequin skeleton that
// drives the animation.
//
// Each spawn picks a body form (FZombieArchetype: Husk / Bloat / Gaunt by
// default), rolls bounded mutations, grows fleshy sacs and tendrils, and
// sometimes carries robot parts (machine limbs + torso plates: corpses a
// robot stuffed with flesh). Every part has a capsule hit zone built from
// the same numbers. Damage arrives through TakePoolDamage / TakeAreaDamage;
// parts sever at 0 health into ASeveredLimb actors on ring gravity.
// Corpses ragdoll (ring gravity) and stay, still severable.

#pragma once

#include "CoreMinimal.h"
#include "Enemy/BaseEnemy.h"
#include "Limb/LimbTypes.h"
#include "ProceduralZombie.generated.h"

class UProceduralMeshComponent;
class UCapsuleComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Which material a generated mesh uses. */
enum class ELimbMaterial : uint8 { Skin, Growth, Metal };

/** Runtime state of one generated part. */
USTRUCT()
struct FLimbPartRuntime
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> MeshComp = nullptr;

    UPROPERTY()
    TObjectPtr<UCapsuleComponent> HitZone = nullptr;

    float Health = 0.f;
    bool bAttached = true;
    bool bMechanical = false;

    /** The (archetype-modified) spec this part was built from. */
    FLimbPartSpec Spec;

    /** Generated geometry in the start bone's space (kept so the severed copy matches exactly). */
    FLimbMesh Mesh;
    FLimbShape Shape;
};

/** A cosmetic extra (growth sac, tendril, machine plate) riding on a part. */
USTRUCT()
struct FLimbDecorRuntime
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> MeshComp = nullptr;

    /** Index into PartState: the decor goes with that part when it's severed. */
    int32 OwnerPart = INDEX_NONE;
    ELimbMaterial Material = ELimbMaterial::Growth;
    FLimbMesh Mesh;
};

UCLASS()
class TRANSFIGURE_API AProceduralZombie : public ABaseEnemy
{
    GENERATED_BODY()

public:
    AProceduralZombie();

    /** Skeleton that drives animation (hidden). Defaults to the UE5 Mannequin. */
    UPROPERTY(EditAnywhere, Category = "Zombie|Body")
    TSoftObjectPtr<USkeletalMesh> BodySkeleton;

    /** Animation Blueprint for the hidden skeleton. Defaults to the Mannequin's unarmed ABP. */
    UPROPERTY(EditAnywhere, Category = "Zombie|Body")
    TSoftClassPtr<UAnimInstance> BodyAnimClass;

    /** One entry per generated part. Defaults to a 10-part humanoid on Mannequin bones. */
    UPROPERTY(EditAnywhere, Category = "Zombie|Body")
    TArray<FLimbPartSpec> Parts;

    /** Body forms to pick from (by Weight). Defaults: Husk, Bloat, Gaunt. */
    UPROPERTY(EditAnywhere, Category = "Zombie|Body")
    TArray<FZombieArchetype> Archetypes;

    UPROPERTY(EditAnywhere, Category = "Zombie|Body")
    FLimbMutationRanges Mutations;

    /** 0 = random body every spawn; otherwise always build this exact body. */
    UPROPERTY(EditAnywhere, Category = "Zombie|Body")
    int32 BodySeed = 0;

    /** Machine parts take this much more damage before severing. */
    UPROPERTY(EditAnywhere, Category = "Zombie|Body")
    float MechanicalHealthMultiplier = 2.f;

    UPROPERTY(EditAnywhere, Category = "Zombie|Body")
    FLinearColor MetalColor = FLinearColor(0.16f, 0.17f, 0.21f);

    /** Hits further than this from every part's surface only damage the body. */
    UPROPERTY(EditAnywhere, Category = "Zombie|Damage")
    float PartHitTolerance = 30.f;

    /** Speed severed limbs are thrown at, along the hit direction. */
    UPROPERTY(EditAnywhere, Category = "Zombie|Damage")
    float SeverLaunchSpeed = 450.f;

    /** Rebuild the body (new form and mutations unless BodySeed is set). */
    UFUNCTION(BlueprintCallable, Category = "Zombie")
    void RebuildBody();

    UFUNCTION(BlueprintPure, Category = "Zombie")
    bool HasPart(ELimbPart Part) const;

    UFUNCTION(BlueprintPure, Category = "Zombie")
    FName GetArchetypeName() const { return CurrentArchetype; }

    // ABaseEnemy
    virtual void TakePoolDamage(float DamageAmount, FVector HitLocation) override;
    virtual void TakeAreaDamage(float Damage, FVector Origin, float InnerRadius, float OuterRadius) override;
    virtual void OnSpawnFromPool() override;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void Die() override;

private:
    void EnsureBodyAssets();
    const FZombieArchetype& PickArchetype(FRandomStream& Rng) const;
    void ApplyArchetypeBody(const FZombieArchetype& Arch);
    void BuildPart(int32 Index, const FLimbPartSpec& Spec, FRandomStream& Rng, float Fatness, bool bMechanical);
    void AddGrowths(const FZombieArchetype& Arch, FRandomStream& Rng);
    void AddDecor(int32 OwnerPart, const FLimbShape& Shape, ELimbMaterial Material);
    UMaterialInterface* GetMaterial(ELimbMaterial Kind) const;

    int32 FindHitPart(const FVector& HitLocation) const;
    /** Distance from Location to part Index's hit-zone surface. */
    float DistanceToPart(int32 Index, const FVector& Location) const;
    void SeverPart(int32 Index, const FVector& HitLocation);
    void UpdateCripple();
    void StopRagdoll();
    static TArray<FLimbPartSpec> MakeDefaultParts();
    static TArray<FZombieArchetype> MakeDefaultArchetypes();

    UPROPERTY()
    TArray<FLimbPartRuntime> PartState;

    UPROPERTY()
    TArray<FLimbDecorRuntime> Decor;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> SkinMaterial;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> GrowthMaterial;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> MetalMaterial;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> BaseMaterial;

    FName CurrentArchetype;
    bool bRagdoll = false;
    float OriginalWalkSpeed = 0.f;
    float OriginalAttackDamage = 0.f;
    float OriginalMaxHealth = 100.f;
    FVector MeshRelativeLocation = FVector::ZeroVector;
    FRotator MeshRelativeRotation = FRotator::ZeroRotator;
};
