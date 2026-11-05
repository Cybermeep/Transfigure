// Source/Transfigure/Limb/ProceduralZombie.h
//
// First slice of the limb system: a zombie whose visible body is generated
// from math (FLimbMeshGenerator) and attached to a HIDDEN Mannequin skeleton
// that drives the animation. Each part has a capsule hit zone built from the
// same numbers. Damage arrives through TakePoolDamage; the part nearest the
// hit loses health and severs at 0, spawning an ASeveredLimb that falls along
// ring "down". Corpses ragdoll (ring gravity) and stay, still severable.

#pragma once

#include "CoreMinimal.h"
#include "Enemy/BaseEnemy.h"
#include "Limb/LimbTypes.h"
#include "ProceduralZombie.generated.h"

class UProceduralMeshComponent;
class UCapsuleComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

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

    /** Generated geometry in the start bone's space (kept so the severed copy matches exactly). */
    FLimbMesh Mesh;
    FLimbShape Shape;
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

    UPROPERTY(EditAnywhere, Category = "Zombie|Body")
    FLimbMutationRanges Mutations;

    /** 0 = random body every spawn; otherwise always build this exact body. */
    UPROPERTY(EditAnywhere, Category = "Zombie|Body")
    int32 BodySeed = 0;

    UPROPERTY(EditAnywhere, Category = "Zombie|Body")
    FLinearColor SkinColor = FLinearColor(0.32f, 0.40f, 0.27f);

    /** Hits further than this from every part's surface only damage the body. */
    UPROPERTY(EditAnywhere, Category = "Zombie|Damage")
    float PartHitTolerance = 30.f;

    /** Speed severed limbs are thrown at, along the hit direction. */
    UPROPERTY(EditAnywhere, Category = "Zombie|Damage")
    float SeverLaunchSpeed = 450.f;

    /** Rebuild the body (new mutations unless BodySeed is set). */
    UFUNCTION(BlueprintCallable, Category = "Zombie")
    void RebuildBody();

    UFUNCTION(BlueprintPure, Category = "Zombie")
    bool HasPart(ELimbPart Part) const;

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
    void BuildPart(int32 Index, FRandomStream& Rng, float Fatness);
    int32 FindHitPart(const FVector& HitLocation) const;
    /** Distance from Location to part Index's hit-zone surface. */
    float DistanceToPart(int32 Index, const FVector& Location) const;
    void SeverPart(int32 Index, const FVector& HitLocation);
    void UpdateCripple();
    void StopRagdoll();
    static TArray<FLimbPartSpec> MakeDefaultParts();

    UPROPERTY()
    TArray<FLimbPartRuntime> PartState;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> SkinMaterial;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> BaseMaterial;

    bool bRagdoll = false;
    float OriginalWalkSpeed = 0.f;
    float OriginalAttackDamage = 0.f;
    FVector MeshRelativeLocation = FVector::ZeroVector;
    FRotator MeshRelativeRotation = FRotator::ZeroRotator;
};
