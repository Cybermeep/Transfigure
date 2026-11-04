// Source/TMovement/Private/Destruction/DestructionManager.cpp

#include "Destruction/DestructionManager.h"
#include "Destruction/DestructibleSurface.h"
#include "Destruction/DestructibleChunk.h"
#include "ProceduralMeshComponent.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Chaos/ChaosEngineInterface.h"
#include "EngineUtils.h"
#include "RingFrameUtils.h"

TWeakObjectPtr<ADestructionManager> ADestructionManager::Instance = nullptr;

ADestructionManager::ADestructionManager()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.1f;
}

void ADestructionManager::BeginPlay()
{
    Super::BeginPlay();

    Instance = this;

    UWorld* World = GetWorld();
    if (!World) return;

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    for (int32 i = 0; i < ChunkPoolSize; i++)
    {
        // Was FVector(0,0,-10000) -- see EnemyObjectPool.cpp for the same fix
        FVector SpawnLoc = GetActorLocation() + URingFrameUtils::GetDownFor(this) * 10000.0f;
        ADestructibleChunk* Chunk = World->SpawnActor<ADestructibleChunk>(
            ADestructibleChunk::StaticClass(),
            SpawnLoc,
            FRotator::ZeroRotator,
            SpawnParams);

        if (Chunk)
        {
            Chunk->SetActorHiddenInGame(true);
            Chunk->SetActorEnableCollision(false);
            ChunkPool.Add(Chunk);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("DestructionManager: Initialized with %d chunks in pool"), ChunkPool.Num());
}

void ADestructionManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    for (int32 i = PendingGenerationTasks.Num() - 1; i >= 0; i--)
    {
        if (PendingGenerationTasks[i].bIsComplete)
        {
            SpawnChunkFromTask(PendingGenerationTasks[i]);
            PendingGenerationTasks.RemoveAt(i);
        }
    }

    CleanupOldChunks();
}

void ADestructionManager::QueueDestruction(FVector HitLocation, float DamageRadius, float DamageForce, AActor* Target)
{
    FDestructionRequest Request;
    Request.HitLocation = HitLocation;
    Request.DamageRadius = DamageRadius;
    Request.DamageForce = DamageForce;
    Request.TargetActor = Target;
    Request.RequestTime = FDateTime::Now();

    {
        FScopeLock Lock(&QueueLock);
        DestructionQueue.Add(Request);
    }

    UE_LOG(LogTemp, Verbose, TEXT("DestructionManager: Queued destruction at %s"), *HitLocation.ToString());
}

void ADestructionManager::ProcessDestructionQueue()
{
    TArray<FDestructionRequest> RequestsToProcess;

    {
        FScopeLock Lock(&QueueLock);
        RequestsToProcess = DestructionQueue;
        DestructionQueue.Empty();
    }

    if (RequestsToProcess.Num() == 0) return;

    UE_LOG(LogTemp, Log, TEXT("DestructionManager: Processing %d destruction requests"), RequestsToProcess.Num());

    for (const FDestructionRequest& Request : RequestsToProcess)
    {
        GenerateChunksForDestruction(Request);
        OnDestructionProcessed.Broadcast(Request.HitLocation, Request.DamageRadius);
    }
}

void ADestructionManager::GenerateChunksForDestruction(const FDestructionRequest& Request)
{
    if (!Request.TargetActor.IsValid()) return;

    FChunkGenerationTask NewTask;
    NewTask.HitLocation = Request.HitLocation;
    NewTask.ChunkSize = FMath::RandRange(MinChunkVolume, MaxChunkVolume);

    (new FAutoDeleteAsyncTask<FChunkGenerationAsyncTask>(NewTask))->StartBackgroundTask();
}

void ADestructionManager::SpawnChunkFromTask(FChunkGenerationTask& Task)
{
    ADestructibleChunk* Chunk = GetChunkFromPool();
    if (!Chunk) return;

    Chunk->SetActorLocation(Task.HitLocation + FVector(
        FMath::FRandRange(-50.f, 50.f),
        FMath::FRandRange(-50.f, 50.f),
        FMath::FRandRange(0.f, 100.f)
    ));

    Chunk->SetActorHiddenInGame(false);
    Chunk->SetActorEnableCollision(true);

    Chunk->ApplyDamage(Task.HitLocation, 100.f, 500.f);

    ActiveChunks.Add(Chunk);
}

ADestructibleChunk* ADestructionManager::GetChunkFromPool()
{
    FScopeLock Lock(&PoolLock);

    if (ChunkPool.Num() == 0) return nullptr;

    ADestructibleChunk* Chunk = ChunkPool.Pop();
    return Chunk;
}

void ADestructionManager::ReturnChunkToPool(ADestructibleChunk* Chunk)
{
    if (!Chunk) return;

    Chunk->SetActorHiddenInGame(true);
    Chunk->SetActorEnableCollision(false);

    FScopeLock Lock(&PoolLock);
    ChunkPool.Add(Chunk);
    ActiveChunks.Remove(Chunk);
}

void ADestructionManager::CleanupOldChunks()
{
    float CurrentTime = GetWorld()->GetTimeSeconds();

    for (int32 i = ActiveChunks.Num() - 1; i >= 0; i--)
    {
        ADestructibleChunk* Chunk = ActiveChunks[i];
        // Was Location.Z < -5000.f -- see EnemyObjectPool.cpp for the same fix
        if (Chunk && URingFrameUtils::IsBelowSurface(Chunk->GetActorLocation(), 5000.0f))
        {
            ReturnChunkToPool(Chunk);
        }
    }
}

bool ADestructionManager::IsPositionDestructible(FVector Position) const
{
    UWorld* World = GetWorld();
    if (!World) return false;

    // Iterate all DestructibleSurface actors in the level
    for (TActorIterator<ADestructibleSurface> It(World); It; ++It)
    {
        ADestructibleSurface* Surface = *It;
        if (!Surface) continue;

        // Check if position is within the surface actor's bounding box
        FBox Bounds = Surface->GetComponentsBoundingBox(true);
        // Expand bounds slightly to catch edge cases
        Bounds = Bounds.ExpandBy(50.f);

        if (Bounds.IsInsideOrOn(Position))
        {
            // Also check surface hasn't already taken too much damage here
            float Integrity = Surface->GetSurfaceIntegrity(Position);
            return Integrity > 0.1f; // less than 90% destroyed
        }
    }

    return false;
}

ADestructionManager* ADestructionManager::GetInstance(UWorld* World)
{
    if (Instance.IsValid())
    {
        return Instance.Get();
    }

    if (World)
    {
        ADestructionManager* Manager = World->SpawnActor<ADestructionManager>();
        Instance = Manager;
        return Manager;
    }

    return nullptr;
}

void ADestructionManager::FChunkGenerationAsyncTask::DoWork()
{
    FPlatformProcess::Sleep(0.01f);

    Task.GeneratedVertices.Empty();
    Task.GeneratedTriangles.Empty();

    float Size = Task.ChunkSize;

    Task.GeneratedVertices.Add(FVector(-Size, -Size, -Size) * 0.5f);
    Task.GeneratedVertices.Add(FVector(Size, -Size, -Size) * 0.5f);
    Task.GeneratedVertices.Add(FVector(Size, Size, -Size) * 0.5f);
    Task.GeneratedVertices.Add(FVector(-Size, Size, -Size) * 0.5f);
    Task.GeneratedVertices.Add(FVector(-Size, -Size, Size) * 0.5f);
    Task.GeneratedVertices.Add(FVector(Size, -Size, Size) * 0.5f);
    Task.GeneratedVertices.Add(FVector(Size, Size, Size) * 0.5f);
    Task.GeneratedVertices.Add(FVector(-Size, Size, Size) * 0.5f);

    int32 Tris[] = {
        0,1,2, 0,2,3,
        4,6,5, 4,7,6,
        0,4,5, 0,5,1,
        2,6,7, 2,7,3,
        0,3,7, 0,7,4,
        1,5,6, 1,6,2
    };

    Task.GeneratedTriangles.Append(Tris, 36);

    Task.bIsComplete = true;
}