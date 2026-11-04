// Source/TMovement/Public/Enemy/EnemyPod.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Transfiguration/TransfigurationTypes.h"
#include "EnemyPod.generated.h"

class AEnemyObjectPool;
class ABaseEnemy;
class USphereComponent;
class UBoxComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPodStateChanged, EPodState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPodDefeated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPodReinforcing, int32, ReinforceCount);

UCLASS()
class TRANSFIGURE_API AEnemyPod : public AActor
{
    GENERATED_BODY()

public:
    AEnemyPod();

    // Configuration
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pod|Configuration")
        TArray<FEnemySpawnPoint> SpawnPoints;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pod|Configuration")
        int32 BaseEnemyCount = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pod|Configuration")
        int32 MaxEnemyCount = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pod|Configuration")
        float ActivationRadius = 2000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pod|Configuration")
        float ReinforcementDelay = 30.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pod|Configuration")
        int32 ReinforcementWaveSize = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pod|Configuration")
        float EscapeDetectionRadius = 3000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pod|Configuration")
        bool bReinforcesIfPlayerEscapes = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pod|Configuration")
        bool bLinksToNearbyPods = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pod|Configuration")
        float PodLinkRadius = 5000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pod|Destruction")
        bool bUsesCoverFromDestruction = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pod|Destruction")
        float CoverSearchRadius = 1000.f;

    // Components
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
        TObjectPtr<USphereComponent> ActivationTrigger;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
        TObjectPtr<UBoxComponent> PodVolume;

    // State
    UPROPERTY(BlueprintReadOnly, Category = "Pod|State")
        EPodState CurrentState = EPodState::Inactive;

    UPROPERTY(BlueprintReadOnly, Category = "Pod|State")
        int32 CurrentEnemyCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Pod|State")
        int32 DefeatedCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Pod|State")
        float TimeSinceLastEngagement = 0.f;

    // Delegates
    UPROPERTY(BlueprintAssignable)
        FOnPodStateChanged OnPodStateChanged;

    UPROPERTY(BlueprintAssignable)
        FOnPodDefeated OnPodDefeated;

    UPROPERTY(BlueprintAssignable)
        FOnPodReinforcing OnPodReinforcing;

    // Public methods
    UFUNCTION(BlueprintCallable, Category = "Pod")
        void InitializePod(AEnemyObjectPool* InEnemyPool, int32 InPodID);

    UFUNCTION(BlueprintCallable, Category = "Pod")
        void ActivatePod();

    UFUNCTION(BlueprintCallable, Category = "Pod")
        void DeactivatePod();

    UFUNCTION(BlueprintCallable, Category = "Pod")
        void OnEnemyDefeated(ABaseEnemy* DefeatedEnemy);

    UFUNCTION(BlueprintCallable, Category = "Pod")
        void OnEnemySpawned(ABaseEnemy* NewEnemy);

    UFUNCTION(BlueprintCallable, Category = "Pod")
        void TriggerReinforcements();

    UFUNCTION(BlueprintCallable, Category = "Pod")
        bool IsPlayerInEscapeRange(AActor* Player) const;

    UFUNCTION(BlueprintCallable, Category = "Pod")
        TArray<AEnemyPod*> GetNearbyPods() const;

    UFUNCTION(BlueprintPure, Category = "Pod")
        bool IsClear() const { return CurrentEnemyCount == 0; }

    UFUNCTION(BlueprintPure, Category = "Pod")
        float GetThreatLevel() const;

    UFUNCTION(BlueprintCallable, Category = "Pod|Destruction")
        void OnEnvironmentDestroyed(FVector DestructionLocation, float DestructionRadius);

    UFUNCTION(BlueprintCallable, Category = "Pod|Destruction")
        TArray<FVector> FindCoverPositions() const;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY()
        int32 PodID = -1;

    UPROPERTY()
        AEnemyObjectPool* EnemyPool = nullptr;

    UPROPERTY()
        TArray<ABaseEnemy*> ActiveEnemies;

    UPROPERTY()
        FTimerHandle ReinforcementTimerHandle;

    UPROPERTY()
        FTimerHandle EscapeCheckTimerHandle;

    UFUNCTION()
        void OnActivationOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
            UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
            const FHitResult& SweepResult);

    UFUNCTION()
        void OnActivationEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
            UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    UFUNCTION()
        void SpawnPodEnemies();

    UFUNCTION()
        void CheckEscapeConditions();

    UFUNCTION()
        void NotifyNearbyPods();

    UFUNCTION()
        void OnNeighborPodEngaged(int32 NeighborPodID);

    UFUNCTION()
        void ReinforcementTimeout();

    UFUNCTION()
        bool IsSpawnPointSafe(int32 SpawnPointIndex) const;

    UFUNCTION()
        ATMCharacter* GetPlayerInRange() const;

    UFUNCTION()
        void SetEnemyPatrolBehavior(ABaseEnemy* Enemy);

    UFUNCTION()
        void SendEnemyToLocation(ABaseEnemy* Enemy, FVector Location);

    UFUNCTION()
        void IncreaseEnemyAlertness(ABaseEnemy* Enemy);

    UFUNCTION()
        void UpdateEnemyCoverPositions();

    void TransitionToState(EPodState NewState);

#if WITH_EDITOR
    void DrawDebugInfo();
#endif
};