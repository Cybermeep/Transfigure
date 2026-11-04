// Source/TMovement/Public/Destruction/DestructionManager.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Destruction/DestructibleChunk.h"
#include "Transfiguration/TransfigurationTypes.h" 
#include "DestructionManager.generated.h"

class ADestructibleChunk;
class UProceduralMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDestructionProcessed, FVector, Location, float, Radius);

UCLASS()
class TRANSFIGURE_API ADestructionManager : public AActor
{
    GENERATED_BODY()

public:
    ADestructionManager();

    // Queue destruction for processing
    UFUNCTION(BlueprintCallable, Category = "Destruction")
        void QueueDestruction(FVector HitLocation, float DamageRadius, float DamageForce, AActor* Target);

    // Process all queued destruction (called during Temporal Clap)
    UFUNCTION(BlueprintCallable, Category = "Destruction")
        void ProcessDestructionQueue();

    // Get singleton instance
    UFUNCTION(BlueprintPure, Category = "Destruction")
        static ADestructionManager* GetInstance(UWorld* World);

    // Configuration
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction|Performance")
        int32 MaxChunksPerFrame = 10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction|Performance")
        float ChunkGenerationTimeBudget = 0.005f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction|Pool")
        int32 ChunkPoolSize = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction|Physics")
        float ChunkLifetime = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction|Physics")
        float MinChunkVolume = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destruction|Physics")
        float MaxChunkVolume = 1000.0f;

    UPROPERTY(BlueprintAssignable)
        FOnDestructionProcessed OnDestructionProcessed;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

private:
    static TWeakObjectPtr<ADestructionManager> Instance;

    UPROPERTY()
        TArray<FDestructionRequest> DestructionQueue;

    UPROPERTY()
        TArray<FChunkGenerationTask> PendingGenerationTasks;

    UPROPERTY()
        TArray<ADestructibleChunk*> ChunkPool;

    UPROPERTY()
        TArray<ADestructibleChunk*> ActiveChunks;

    UPROPERTY()
        FTimerHandle ProcessingTimerHandle;

    FCriticalSection QueueLock;
    FCriticalSection PoolLock;

    class FChunkGenerationAsyncTask : public FNonAbandonableTask
    {
    public:
        FChunkGenerationAsyncTask(FChunkGenerationTask& InTask)
            : Task(InTask)
        {}

        void DoWork();

        FORCEINLINE TStatId GetStatId() const
        {
            RETURN_QUICK_DECLARE_CYCLE_STAT(FChunkGenerationAsyncTask, STATGROUP_ThreadPoolAsyncTasks);
        }

    private:
        FChunkGenerationTask& Task;
    };

    friend class FChunkGenerationAsyncTask;

    void GenerateChunksForDestruction(const FDestructionRequest& Request);
    void SpawnChunkFromTask(FChunkGenerationTask& Task);
    ADestructibleChunk* GetChunkFromPool();
    void ReturnChunkToPool(ADestructibleChunk* Chunk);
    bool IsPositionDestructible(FVector Position) const;
    void CleanupOldChunks();
};