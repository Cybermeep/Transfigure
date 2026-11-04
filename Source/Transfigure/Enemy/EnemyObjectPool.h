// Source/TMovement/Public/Enemy/EnemyObjectPool.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Transfiguration/TransfigurationTypes.h"
#include "EnemyObjectPool.generated.h"

class ABaseEnemy;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPoolExhausted, int32, RequestedCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyReturned, ABaseEnemy*, Enemy);

UCLASS()
class TRANSFIGURE_API AEnemyObjectPool : public AActor
{
    GENERATED_BODY()

public:
    AEnemyObjectPool();

    UFUNCTION(BlueprintCallable, Category = "Enemy|Pool")
        void InitializePool(TSubclassOf<ABaseEnemy> EnemyClass, int32 PoolSize, int32 MaxConcurrent = -1);

    UFUNCTION(BlueprintCallable, Category = "Enemy|Pool")
        ABaseEnemy* RequestEnemy(FVector SpawnLocation, FRotator SpawnRotation, int32 PodID = -1);

    UFUNCTION(BlueprintCallable, Category = "Enemy|Pool")
        TArray<ABaseEnemy*> RequestEnemies(TArray<FVector> SpawnLocations, int32 PodID = -1);

    UFUNCTION(BlueprintCallable, Category = "Enemy|Pool")
        void ReturnEnemy(ABaseEnemy* Enemy);

    UFUNCTION(BlueprintCallable, Category = "Enemy|Pool")
        void ReturnPodEnemies(int32 PodID);

    UFUNCTION(BlueprintPure, Category = "Enemy|Pool")
        TArray<ABaseEnemy*> GetActiveEnemies() const;

    UFUNCTION(BlueprintPure, Category = "Enemy|Pool")
        TArray<ABaseEnemy*> GetPodEnemies(int32 PodID) const;

    UFUNCTION(BlueprintPure, Category = "Enemy|Pool")
        int32 GetActiveCount() const { return ActiveCount; }

    UFUNCTION(BlueprintPure, Category = "Enemy|Pool")
        int32 GetPoolSize() const { return PoolSize; }

    UFUNCTION(BlueprintPure, Category = "Enemy|Pool")
        int32 GetAvailableCount() const { return PoolSize - ActiveCount; }

    UFUNCTION(BlueprintPure, Category = "Enemy|Pool")
        bool CanSpawn(int32 Count = 1) const { return (ActiveCount + Count) <= PoolSize; }

    UPROPERTY(BlueprintAssignable)
        FOnPoolExhausted OnPoolExhausted;

    UPROPERTY(BlueprintAssignable)
        FOnEnemyReturned OnEnemyReturned;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

private:
    UPROPERTY()
        TSubclassOf<ABaseEnemy> EnemyClass;

    UPROPERTY()
        TArray<FEnemyPoolEntry> Pool;

    UPROPERTY()
        int32 PoolSize = 0;

    UPROPERTY()
        int32 MaxConcurrent = -1;

    UPROPERTY()
        int32 ActiveCount = 0;

    UPROPERTY()
        int32 NextPodID = 0;

    UPROPERTY()
        TMap<int32, FDateTime> PodSpawnTimes;

    UPROPERTY()
        FTimerHandle PoolMaintenanceTimer;

    UFUNCTION()
        void OnEnemyDestroyed(AActor* DestroyedActor);

    UFUNCTION()
        void PerformPoolMaintenance();

    int32 GeneratePodID();
};