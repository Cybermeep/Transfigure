// Source/TMovement/Private/Enemy/EnemyObjectPool.cpp

#include "Enemy/EnemyObjectPool.h"
#include "RingFrameUtils.h"
#include "Enemy/BaseEnemy.h"
#include "Engine/World.h"
#include "TimerManager.h"

AEnemyObjectPool::AEnemyObjectPool()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.5f;
}

void AEnemyObjectPool::BeginPlay()
{
    Super::BeginPlay();

    GetWorldTimerManager().SetTimer(
        PoolMaintenanceTimer,
        this, &AEnemyObjectPool::PerformPoolMaintenance,
        5.0f, true, 5.0f);

    if (StartupEnemyClass)
    {
        InitializePool(StartupEnemyClass, StartupPoolSize);
    }
}

void AEnemyObjectPool::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

void AEnemyObjectPool::InitializePool(TSubclassOf<ABaseEnemy> InEnemyClass, int32 InPoolSize, int32 InMaxConcurrent)
{
    // Already built (e.g. by StartupEnemyClass): a second call from a Blueprint is ignored.
    if (Pool.Num() > 0) return;

    EnemyClass = InEnemyClass;
    PoolSize = InPoolSize;
    MaxConcurrent = InMaxConcurrent > 0 ? InMaxConcurrent : PoolSize;

    if (!EnemyClass || !GetWorld()) return;

    Pool.Reserve(PoolSize);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    for (int32 i = 0; i < PoolSize; i++)
    {
        // Was FVector(0,0,-1000) -- assumes down is world -Z. On the ring,
        // "below the pool actor" means along local down from wherever
        // the pool actor itself is placed.
        FVector SpawnLoc = GetActorLocation() + URingFrameUtils::GetDownFor(this) * 1000.0f;
        ABaseEnemy* Enemy = GetWorld()->SpawnActor<ABaseEnemy>(EnemyClass, SpawnLoc, FRotator::ZeroRotator, SpawnParams);

        if (Enemy)
        {
            Enemy->SetActorHiddenInGame(true);
            Enemy->SetActorEnableCollision(false);
            Enemy->SetLifeSpan(0.f);

            // Park it fully: timers, tick and movement off until requested.
            Enemy->SetOwningPool(this);
            Enemy->OnReturnToPool();

            Enemy->OnDestroyed.AddDynamic(this, &AEnemyObjectPool::OnEnemyDestroyed);

            FEnemyPoolEntry Entry;
            Entry.Enemy = Enemy;
            Entry.bIsActive = false;
            Entry.PodID = -1;

            Pool.Add(Entry);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[TM] EnemyObjectPool: Initialized pool with %d x %s"), Pool.Num(), *GetNameSafe(EnemyClass));
}

ABaseEnemy* AEnemyObjectPool::RequestEnemy(FVector SpawnLocation, FRotator SpawnRotation, int32 PodID)
{
    if (!CanSpawn())
    {
        UE_LOG(LogTemp, Warning, TEXT("EnemyObjectPool: Pool exhausted! Active: %d, Max: %d"),
            ActiveCount, MaxConcurrent);
        OnPoolExhausted.Broadcast(1);
        return nullptr;
    }

    for (FEnemyPoolEntry& Entry : Pool)
    {
        if (!Entry.bIsActive && Entry.Enemy && Entry.Enemy->IsValidLowLevel())
        {
            Entry.bIsActive = true;
            Entry.SpawnTime = GetWorld()->GetTimeSeconds();
            Entry.SpawnLocation = SpawnLocation;
            Entry.PodID = PodID >= 0 ? PodID : GeneratePodID();

            ABaseEnemy* Enemy = Entry.Enemy;
            Enemy->SetActorLocation(SpawnLocation);
            Enemy->SetActorRotation(SpawnRotation);
            Enemy->SetActorHiddenInGame(false);
            Enemy->SetActorEnableCollision(true);
            Enemy->SetPodID(Entry.PodID);
            Enemy->SetOwningPool(this);
            Enemy->OnSpawnFromPool();

            ActiveCount++;

            return Enemy;
        }
    }

    OnPoolExhausted.Broadcast(1);
    return nullptr;
}

TArray<ABaseEnemy*> AEnemyObjectPool::RequestEnemies(TArray<FVector> SpawnLocations, int32 PodID)
{
    TArray<ABaseEnemy*> Result;

    if (!CanSpawn(SpawnLocations.Num()))
    {
        OnPoolExhausted.Broadcast(SpawnLocations.Num());
        return Result;
    }

    int32 PodIDToUse = PodID >= 0 ? PodID : GeneratePodID();

    for (const FVector& Location : SpawnLocations)
    {
        ABaseEnemy* Enemy = RequestEnemy(Location, FRotator::ZeroRotator, PodIDToUse);
        if (Enemy)
        {
            Result.Add(Enemy);
        }
    }

    if (Result.Num() > 0)
    {
        PodSpawnTimes.Add(PodIDToUse, FDateTime::Now());
    }

    return Result;
}

void AEnemyObjectPool::ReturnEnemy(ABaseEnemy* Enemy)
{
    if (!Enemy) return;

    for (FEnemyPoolEntry& Entry : Pool)
    {
        if (Entry.Enemy == Enemy)
        {
            if (!Entry.bIsActive)
            {
                UE_LOG(LogTemp, Warning, TEXT("EnemyObjectPool: Enemy returned twice!"));
                return;
            }

            Entry.bIsActive = false;
            Entry.PodID = -1;

            Enemy->SetActorHiddenInGame(true);
            Enemy->SetActorEnableCollision(false);
            Enemy->OnReturnToPool();

            ActiveCount--;

            OnEnemyReturned.Broadcast(Enemy);
            return;
        }
    }

    UE_LOG(LogTemp, Error, TEXT("EnemyObjectPool: Tried to return enemy not from this pool!"));
    Enemy->Destroy();
}

void AEnemyObjectPool::ReturnPodEnemies(int32 PodID)
{
    int32 ReturnedCount = 0;

    for (FEnemyPoolEntry& Entry : Pool)
    {
        if (Entry.bIsActive && Entry.PodID == PodID && Entry.Enemy)
        {
            Entry.bIsActive = false;
            Entry.PodID = -1;

            Entry.Enemy->SetActorHiddenInGame(true);
            Entry.Enemy->SetActorEnableCollision(false);
            Entry.Enemy->OnReturnToPool();

            ActiveCount--;
            ReturnedCount++;

            OnEnemyReturned.Broadcast(Entry.Enemy);
        }
    }

    PodSpawnTimes.Remove(PodID);

    UE_LOG(LogTemp, Verbose, TEXT("EnemyObjectPool: Returned pod %d with %d enemies"), PodID, ReturnedCount);
}

TArray<ABaseEnemy*> AEnemyObjectPool::GetActiveEnemies() const
{
    TArray<ABaseEnemy*> Result;

    for (const FEnemyPoolEntry& Entry : Pool)
    {
        if (Entry.bIsActive && Entry.Enemy)
        {
            Result.Add(Entry.Enemy);
        }
    }

    return Result;
}

TArray<ABaseEnemy*> AEnemyObjectPool::GetPodEnemies(int32 PodID) const
{
    TArray<ABaseEnemy*> Result;

    for (const FEnemyPoolEntry& Entry : Pool)
    {
        if (Entry.bIsActive && Entry.PodID == PodID && Entry.Enemy)
        {
            Result.Add(Entry.Enemy);
        }
    }

    return Result;
}

int32 AEnemyObjectPool::GeneratePodID()
{
    return NextPodID++;
}

void AEnemyObjectPool::OnEnemyDestroyed(AActor* DestroyedActor)
{
    for (int32 i = 0; i < Pool.Num(); i++)
    {
        if (Pool[i].Enemy == DestroyedActor)
        {
            if (Pool[i].bIsActive)
            {
                ActiveCount--;
            }

            Pool.RemoveAt(i);

            if (EnemyClass && GetWorld())
            {
                FActorSpawnParameters SpawnParams;
                SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

                // Was FVector(0,0,-1000) -- assumes down is world -Z. On the ring,
        // "below the pool actor" means along local down from wherever
        // the pool actor itself is placed.
                FVector SpawnLoc = GetActorLocation() + URingFrameUtils::GetDownFor(this) * 1000.0f;
                ABaseEnemy* NewEnemy = GetWorld()->SpawnActor<ABaseEnemy>(EnemyClass, SpawnLoc, FRotator::ZeroRotator, SpawnParams);

                if (NewEnemy)
                {
                    NewEnemy->SetActorHiddenInGame(true);
                    NewEnemy->SetActorEnableCollision(false);
                    NewEnemy->SetOwningPool(this);
                    NewEnemy->OnReturnToPool();
                    NewEnemy->OnDestroyed.AddDynamic(this, &AEnemyObjectPool::OnEnemyDestroyed);

                    FEnemyPoolEntry NewEntry;
                    NewEntry.Enemy = NewEnemy;
                    NewEntry.bIsActive = false;
                    NewEntry.PodID = -1;

                    Pool.Add(NewEntry);
                }
            }

            break;
        }
    }
}

void AEnemyObjectPool::PerformPoolMaintenance()
{
    // No time limit (it made live enemies vanish mid-fight). Only return
    // enemies that fell out of the world. Collect first: ReturnEnemy edits entries.
    TArray<ABaseEnemy*> ToReturn;
    for (const FEnemyPoolEntry& Entry : Pool)
    {
        if (Entry.bIsActive && Entry.Enemy &&
            URingFrameUtils::IsBelowSurface(Entry.Enemy->GetActorLocation(), 5000.0f))
        {
            ToReturn.Add(Entry.Enemy);
        }
    }
    for (ABaseEnemy* Enemy : ToReturn)
    {
        UE_LOG(LogTemp, Warning, TEXT("EnemyObjectPool: %s fell out of world, returning to pool"), *GetNameSafe(Enemy));
        ReturnEnemy(Enemy);
    }
}