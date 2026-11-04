// Source/TMovement/Public/Destruction/DestructibleSurface.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DestructibleSurface.generated.h"

UCLASS()
class TRANSFIGURE_API ADestructibleSurface : public AActor
{
    GENERATED_BODY()

public:
    ADestructibleSurface();

    UFUNCTION(BlueprintCallable, Category = "Destruction")
        bool CanPlaceSigilAtLocation(FVector Location) const;

    UFUNCTION(BlueprintCallable, Category = "Destruction")
        void RegisterDestruction(FVector Location, float Radius);

    UFUNCTION(BlueprintPure, Category = "Destruction")
        float GetSurfaceIntegrity(FVector Location) const;

    UFUNCTION(BlueprintCallable, Category = "Destruction")
        void RepairSurface(float RepairAmount = 1.0f);

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction")
        float MaxDamagePerLocation = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction")
        float RegenerationTime = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction")
        float RepairCooldown = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction|Visuals")
        TObjectPtr<UMaterialInterface> DamagedMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction|Visuals")
        TObjectPtr<UParticleSystem> DestructionEffect;

private:
    struct FDamageZone
    {
        FVector Location;
        float DamageAmount;
        float DamageTime;
        float Radius;
    };

    TArray<FDamageZone> DamageZones;  // removed UPROPERTY()

    UPROPERTY()
        TArray<UMaterialInstanceDynamic*> MaterialInstances;

    UPROPERTY()
        FTimerHandle RepairTimerHandle;

    UFUNCTION()
        void CleanupOldDamageZones();

    UFUNCTION()
        void ApplyDamageVisuals();

    UFUNCTION()
        void StartRepair();

    UFUNCTION()
        void CompleteRepair();
};