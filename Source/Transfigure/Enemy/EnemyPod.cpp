// Source/TMovement/Private/Enemy/EnemyPod.cpp

#include "Enemy/EnemyPod.h"
#include "Enemy/EnemyObjectPool.h"
#include "Enemy/BaseEnemy.h"
#include "Character/TMCharacter.h"
#include "Destruction/DestructionManager.h"
#include "Destruction/DestructibleSurface.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/OverlapResult.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "AIController.h"
#include "RingFrameUtils.h"

AEnemyPod::AEnemyPod()
{
    PrimaryActorTick.bCanEverTick = true;

    ActivationTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("ActivationTrigger"));
    ActivationTrigger->SetupAttachment(RootComponent);
    ActivationTrigger->SetSphereRadius(ActivationRadius);
    ActivationTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    ActivationTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    ActivationTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

    PodVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("PodVolume"));
    PodVolume->SetupAttachment(RootComponent);
    PodVolume->SetBoxExtent(FVector(1000, 1000, 500));
    PodVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    PodVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    PodVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AEnemyPod::BeginPlay()
{
    Super::BeginPlay();

    ActivationTrigger->OnComponentBeginOverlap.AddDynamic(this, &AEnemyPod::OnActivationOverlap);
    ActivationTrigger->OnComponentEndOverlap.AddDynamic(this, &AEnemyPod::OnActivationEndOverlap);

    // Start escape checks
    GetWorldTimerManager().SetTimer(
        EscapeCheckTimerHandle,
        this, &AEnemyPod::CheckEscapeConditions,
        1.0f, true, 1.0f);

    // Initialize spawn points if empty
    if (SpawnPoints.Num() == 0)
    {
        // Create default spawn points around the pod
        for (int32 i = 0; i < 8; i++)
        {
            float Angle = (360.0f / 8) * i;
            FVector Offset = FVector(
                FMath::Cos(FMath::DegreesToRadians(Angle)) * 500,
                FMath::Sin(FMath::DegreesToRadians(Angle)) * 500,
                0
            );

            FEnemySpawnPoint SpawnPoint;
            SpawnPoint.Location = GetActorLocation() + Offset;
            SpawnPoint.Rotation = (-Offset).Rotation();
            SpawnPoint.bIsPatrolPoint = (i % 2 == 0);

            SpawnPoints.Add(SpawnPoint);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: Initialized with %d spawn points"), PodID, SpawnPoints.Num());
}

void AEnemyPod::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (CurrentState == EPodState::Active && CurrentEnemyCount > 0)
    {
        TimeSinceLastEngagement += DeltaTime;

        // Update enemy awareness based on destruction
        if (bUsesCoverFromDestruction && (int32)(TimeSinceLastEngagement) % 5 == 0)
        {
            UpdateEnemyCoverPositions();
        }
    }
}

void AEnemyPod::InitializePod(AEnemyObjectPool* InEnemyPool, int32 InPodID)
{
    EnemyPool = InEnemyPool;
    PodID = InPodID;

    // Set trigger radius
    ActivationTrigger->SetSphereRadius(ActivationRadius);

    UE_LOG(LogTemp, Verbose, TEXT("EnemyPod %d: Initialized with pool"), PodID);
}

void AEnemyPod::ActivatePod()
{
    if (CurrentState != EPodState::Inactive) return;

    UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: Activating"), PodID);
    TransitionToState(EPodState::Spawning);
    SpawnPodEnemies();
}

void AEnemyPod::DeactivatePod()
{
    if (CurrentState == EPodState::Inactive) return;

    UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: Deactivating"), PodID);

    // Return all enemies to pool
    if (EnemyPool)
    {
        EnemyPool->ReturnPodEnemies(PodID);
    }

    ActiveEnemies.Empty();
    CurrentEnemyCount = 0;
    DefeatedCount = 0;

    // Clear timers
    GetWorldTimerManager().ClearTimer(ReinforcementTimerHandle);

    TransitionToState(EPodState::Inactive);
}

void AEnemyPod::SpawnPodEnemies()
{
    if (!EnemyPool || SpawnPoints.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("EnemyPod %d: Cannot spawn - no pool or spawn points"), PodID);
        return;
    }

    // Check if pool can handle this spawn
    if (!EnemyPool->CanSpawn(BaseEnemyCount))
    {
        UE_LOG(LogTemp, Warning, TEXT("EnemyPod %d: Pool exhausted, delaying spawn"), PodID);

        // Retry after delay
        GetWorldTimerManager().SetTimer(
            ReinforcementTimerHandle,
            this, &AEnemyPod::SpawnPodEnemies,
            2.0f, false);
        return;
    }

    // Determine how many enemies to spawn (don't exceed max)
    int32 SpawnCount = FMath::Min(BaseEnemyCount, SpawnPoints.Num());
    SpawnCount = FMath::Min(SpawnCount, MaxEnemyCount);

    // Randomize which spawn points to use
    TArray<int32> AvailableIndices;
    for (int32 i = 0; i < SpawnPoints.Num(); i++) AvailableIndices.Add(i);

    // Shuffle
    for (int32 i = AvailableIndices.Num() - 1; i > 0; i--)
    {
        int32 j = FMath::RandRange(0, i);
        AvailableIndices.Swap(i, j);
    }

    TArray<FVector> SpawnLocations;
    TArray<FRotator> SpawnRotations;

    for (int32 i = 0; i < SpawnCount; i++)
    {
        int32 Index = AvailableIndices[i % AvailableIndices.Num()];
        SpawnLocations.Add(SpawnPoints[Index].Location);
        SpawnRotations.Add(SpawnPoints[Index].Rotation);
    }

    // Request enemies from pool
    TArray<ABaseEnemy*> SpawnedEnemies = EnemyPool->RequestEnemies(SpawnLocations, PodID);

    for (int32 i = 0; i < SpawnedEnemies.Num(); i++)
    {
        ABaseEnemy* Enemy = SpawnedEnemies[i];
        if (Enemy)
        {
            Enemy->SetPodID(PodID);
            Enemy->SetOwningPool(EnemyPool);
            Enemy->SetActorRotation(SpawnRotations[i % SpawnRotations.Num()]);

            // Setup AI behavior based on spawn point type
            if (SpawnPoints[i % SpawnPoints.Num()].bIsPatrolPoint)
            {
                // Set enemy to patrol mode
                SetEnemyPatrolBehavior(Enemy);
            }

            ActiveEnemies.Add(Enemy);
            CurrentEnemyCount++;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: Spawned %d/%d enemies"),
        PodID, SpawnedEnemies.Num(), SpawnCount);

    if (SpawnedEnemies.Num() > 0)
    {
        TransitionToState(EPodState::Active);
        TimeSinceLastEngagement = 0.f;

        // Notify nearby pods
        if (bLinksToNearbyPods)
        {
            NotifyNearbyPods();
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("EnemyPod %d: Failed to spawn any enemies"), PodID);
        TransitionToState(EPodState::Inactive);
    }
}

void AEnemyPod::OnEnemyDefeated(ABaseEnemy* DefeatedEnemy)
{
    if (!DefeatedEnemy) return;

    ActiveEnemies.Remove(DefeatedEnemy);
    CurrentEnemyCount--;
    DefeatedCount++;

    UE_LOG(LogTemp, Verbose, TEXT("EnemyPod %d: Enemy defeated. %d remaining, %d total defeated"),
        PodID, CurrentEnemyCount, DefeatedCount);

    // Calculate defeat ratio
    float DefeatRatio = (float)DefeatedCount / (float)(DefeatedCount + CurrentEnemyCount);

    if (CurrentEnemyCount == 0)
    {
        // Pod cleared!
        TransitionToState(EPodState::Defeated);
        OnPodDefeated.Broadcast();

        UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: COMPLETELY CLEARED!"), PodID);

        // Reward player with mana
        ATMCharacter* Player = GetPlayerInRange();
        if (Player)
        {
            float RewardMana = BaseEnemyCount * 5.0f; // 5 mana per enemy
            Player->AddMana(RewardMana);
            UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: Awarded player %f mana for clearing pod"),
                PodID, RewardMana);
        }
    }
    else if (DefeatRatio > 0.5f && bLinksToNearbyPods)
    {
        // Lost half the pod, call for reinforcements
        UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: Lost 50%% of forces, calling reinforcements"), PodID);
        TriggerReinforcements();
    }
}

void AEnemyPod::TriggerReinforcements()
{
    if (CurrentState != EPodState::Active && CurrentState != EPodState::Reinforcing) return;
    if (CurrentEnemyCount >= MaxEnemyCount) return;

    TransitionToState(EPodState::Reinforcing);

    // Calculate how many reinforcements to spawn
    int32 AvailableSlots = MaxEnemyCount - CurrentEnemyCount;
    int32 ReinforceCount = FMath::Min(ReinforcementWaveSize, AvailableSlots);

    // Scale reinforcement by nearby pods
    if (bLinksToNearbyPods)
    {
        TArray<AEnemyPod*> NearbyPods = GetNearbyPods();
        int32 NearbyActiveCount = 0;
        for (AEnemyPod* Pod : NearbyPods)
        {
            if (Pod && Pod->CurrentState == EPodState::Active)
            {
                NearbyActiveCount++;
            }
        }

        // More nearby pods = larger reinforcement wave
        ReinforceCount = FMath::Min(ReinforceCount + NearbyActiveCount, AvailableSlots);
    }

    OnPodReinforcing.Broadcast(ReinforceCount);

    UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: Triggering reinforcements: %d enemies"),
        PodID, ReinforceCount);

    // Find available spawn points (preferably ones not near players)
    TArray<FVector> SpawnLocations;
    TArray<int32> AvailableIndices;

    for (int32 i = 0; i < SpawnPoints.Num(); i++)
    {
        // Check if spawn point is safe (not in player LOS)
        if (IsSpawnPointSafe(i))
        {
            AvailableIndices.Add(i);
        }
    }

    if (AvailableIndices.Num() == 0)
    {
        // Fallback to any spawn point
        for (int32 i = 0; i < SpawnPoints.Num(); i++) AvailableIndices.Add(i);
    }

    for (int32 i = 0; i < ReinforceCount; i++)
    {
        int32 SpawnIndex = AvailableIndices[FMath::RandRange(0, AvailableIndices.Num() - 1)];
        SpawnLocations.Add(SpawnPoints[SpawnIndex].Location);
    }

    if (EnemyPool && EnemyPool->CanSpawn(ReinforceCount))
    {
        TArray<ABaseEnemy*> Reinforcements = EnemyPool->RequestEnemies(SpawnLocations, PodID);

        for (ABaseEnemy* Enemy : Reinforcements)
        {
            if (Enemy)
            {
                Enemy->SetPodID(PodID);
                Enemy->SetOwningPool(EnemyPool);
                ActiveEnemies.Add(Enemy);
                CurrentEnemyCount++;

                // Reinforcements are aggressive - send them to player location
                ATMCharacter* Player = GetPlayerInRange();
                if (Player)
                {
                    SendEnemyToLocation(Enemy, Player->GetActorLocation());
                }
            }
        }

        UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: Reinforcements arrived! +%d enemies, now %d total"),
            PodID, Reinforcements.Num(), CurrentEnemyCount);
    }

    TransitionToState(EPodState::Active);
}

void AEnemyPod::CheckEscapeConditions()
{
    if (CurrentState != EPodState::Active) return;

    ATMCharacter* Player = GetPlayerInRange();

    if (!Player)
    {
        // No player in area - increment idle time
        TimeSinceLastEngagement += 1.0f;

        // If idle too long, consider deactivating
        if (TimeSinceLastEngagement > 30.0f) // 30 seconds idle
        {
            UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: Idle for 30s, deactivating"), PodID);
            DeactivatePod();
        }
        return;
    }

    // Check if player escaped
    bool bPlayerEscaped = IsPlayerInEscapeRange(Player);

    if (bPlayerEscaped)
    {
        UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: Player escaped! Distance: %f"),
            PodID, FVector::Dist(GetActorLocation(), Player->GetActorLocation()));

        if (bReinforcesIfPlayerEscapes)
        {
            // Player escaped - they'll face a bigger pod later
            BaseEnemyCount = FMath::Min(BaseEnemyCount + 2, MaxEnemyCount);
            UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: Increased base count to %d for next encounter"),
                PodID, BaseEnemyCount);
        }

        // Reset timer but don't deactivate - wait for player return
        TimeSinceLastEngagement = 0.f;
    }
    else
    {
        // Player is engaged
        TimeSinceLastEngagement = 0.f;
    }
}

bool AEnemyPod::IsPlayerInEscapeRange(AActor* Player) const
{
    if (!Player) return true;

    float Distance = FVector::Dist(GetActorLocation(), Player->GetActorLocation());
    return Distance > EscapeDetectionRadius;
}

ATMCharacter* AEnemyPod::GetPlayerInRange() const
{
    TArray<AActor*> OverlappingActors;
    ActivationTrigger->GetOverlappingActors(OverlappingActors, ATMCharacter::StaticClass());

    for (AActor* Actor : OverlappingActors)
    {
        ATMCharacter* Player = Cast<ATMCharacter>(Actor);
        if (Player && Player->GetHealth() > 0.f)
        {
            return Player;
        }
    }

    return nullptr;
}

TArray<AEnemyPod*> AEnemyPod::GetNearbyPods() const
{
    TArray<AEnemyPod*> Result;

    if (!GetWorld()) return Result;

    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    GetWorld()->OverlapMultiByObjectType(
        Overlaps,
        GetActorLocation(),
        FQuat::Identity,
        FCollisionObjectQueryParams(ECC_GameTraceChannel1), // Custom channel for pods
        FCollisionShape::MakeSphere(PodLinkRadius),
        Params);

    for (const FOverlapResult& Overlap : Overlaps)
    {
        AEnemyPod* Pod = Cast<AEnemyPod>(Overlap.GetActor());
        if (Pod && Pod != this && Pod->CurrentState == EPodState::Active)
        {
            Result.Add(Pod);
        }
    }

    return Result;
}

float AEnemyPod::GetThreatLevel() const
{
    // Base threat from enemy count
    float BaseThreat = CurrentEnemyCount * 10.0f;

    // Add bonus for elite enemies (you'd need to track this)
    float EliteBonus = 0.0f;
    for (ABaseEnemy* Enemy : ActiveEnemies)
    {
        if (Enemy && Enemy->IsElite()) // Assuming IsElite() exists
        {
            EliteBonus += 20.0f;
        }
    }

    // Proximity to player (if player in area)
    ATMCharacter* Player = GetPlayerInRange();
    if (Player)
    {
        float Distance = FVector::Dist(GetActorLocation(), Player->GetActorLocation());
        float ProximityBonus = 50.0f * (1.0f - (Distance / ActivationRadius));
        BaseThreat += ProximityBonus;
    }

    return BaseThreat + EliteBonus;
}

void AEnemyPod::NotifyNearbyPods()
{
    TArray<AEnemyPod*> NearbyPods = GetNearbyPods();

    for (AEnemyPod* Pod : NearbyPods)
    {
        if (Pod && Pod->CurrentState == EPodState::Active)
        {
            // Notify that this pod is under attack
            Pod->OnNeighborPodEngaged(PodID);
            UE_LOG(LogTemp, Verbose, TEXT("EnemyPod %d: Notifying pod %d of engagement"),
                PodID, Pod->PodID);
        }
    }
}

void AEnemyPod::OnNeighborPodEngaged(int32 NeighborPodID)
{
    // Neighbor is fighting - increase alert level
    if (CurrentState == EPodState::Active)
    {
        // Make enemies more aggressive
        for (ABaseEnemy* Enemy : ActiveEnemies)
        {
            if (Enemy)
            {
                IncreaseEnemyAlertness(Enemy);
            }
        }

        UE_LOG(LogTemp, Verbose, TEXT("EnemyPod %d: Alert increased due to pod %d engagement"),
            PodID, NeighborPodID);
    }
}

void AEnemyPod::OnActivationOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
    const FHitResult& SweepResult)
{
    ATMCharacter* Player = Cast<ATMCharacter>(OtherActor);
    if (!Player) return;

    UE_LOG(LogTemp, Verbose, TEXT("EnemyPod %d: Player entered activation zone"), PodID);

    if (CurrentState == EPodState::Inactive)
    {
        ActivatePod();
    }
    else if (CurrentState == EPodState::Active)
    {
        // Player re-entered - reset escape timer
        TimeSinceLastEngagement = 0.f;
    }
}

void AEnemyPod::OnActivationEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    ATMCharacter* Player = Cast<ATMCharacter>(OtherActor);
    if (!Player) return;

    UE_LOG(LogTemp, Verbose, TEXT("EnemyPod %d: Player left activation zone"), PodID);

    // Player left the area - start escape timer (handled in CheckEscapeConditions)
    TimeSinceLastEngagement = 0.f;
}

void AEnemyPod::TransitionToState(EPodState NewState)
{
    if (CurrentState == NewState) return;

    EPodState OldState = CurrentState;
    CurrentState = NewState;

    UE_LOG(LogTemp, Verbose, TEXT("EnemyPod %d: State changed from %d to %d"),
        PodID, (int32)OldState, (int32)NewState);

    OnPodStateChanged.Broadcast(NewState);

    // State-specific behaviors
    switch (NewState)
    {
    case EPodState::Defeated:
        // Clean up any remaining timers
        GetWorldTimerManager().ClearTimer(ReinforcementTimerHandle);
        break;

    case EPodState::Reinforcing:
        // Set timer to revert to Active if reinforcement fails
        GetWorldTimerManager().SetTimer(
            ReinforcementTimerHandle,
            this, &AEnemyPod::ReinforcementTimeout,
            5.0f, false);
        break;

    default:
        break;
    }
}

void AEnemyPod::ReinforcementTimeout()
{
    if (CurrentState == EPodState::Reinforcing)
    {
        UE_LOG(LogTemp, Warning, TEXT("EnemyPod %d: Reinforcement timeout"), PodID);
        TransitionToState(EPodState::Active);
    }
}

bool AEnemyPod::IsSpawnPointSafe(int32 SpawnPointIndex) const
{
    if (!SpawnPoints.IsValidIndex(SpawnPointIndex)) return false;

    const FEnemySpawnPoint& Point = SpawnPoints[SpawnPointIndex];

    // Check if player can see this point
    ATMCharacter* Player = GetPlayerInRange();
    if (!Player) return true;

    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Player);

    bool bHasLineOfSight = !GetWorld()->LineTraceTestByChannel(
        Player->GetActorLocation(),
        Point.Location,
        ECC_Visibility,
        Params);

    return !bHasLineOfSight;
}

void AEnemyPod::SetEnemyPatrolBehavior(ABaseEnemy* Enemy)
{
    if (!Enemy) return;

    // Find nearby patrol points
    TArray<FVector> PatrolPoints;
    for (const FEnemySpawnPoint& Point : SpawnPoints)
    {
        if (Point.bIsPatrolPoint)
        {
            PatrolPoints.Add(Point.Location);
        }
    }

    // Set enemy to patrol (implementation depends on AI system)
    AAIController* AIController = Cast<AAIController>(Enemy->GetController());
    if (AIController && PatrolPoints.Num() > 0)
    {
        // This would use your AI system's patrol functionality
        // AIController->StartPatrolling(PatrolPoints);
    }
}

void AEnemyPod::SendEnemyToLocation(ABaseEnemy* Enemy, FVector Location)
{
    if (!Enemy) return;

    AAIController* AIController = Cast<AAIController>(Enemy->GetController());
    if (AIController)
    {
        // Move to location (implementation depends on AI system)
        // AIController->MoveToLocation(Location);
    }
}

void AEnemyPod::IncreaseEnemyAlertness(ABaseEnemy* Enemy)
{
    if (!Enemy) return;

    // Increase enemy awareness (implementation depends on AI system)
    // Enemy->SetAlertnessLevel(2.0f);
}

void AEnemyPod::UpdateEnemyCoverPositions()
{
    if (!bUsesCoverFromDestruction) return;

    // Get destruction manager to find new cover
    ADestructionManager* DestructionManager = ADestructionManager::GetInstance(GetWorld());
    if (!DestructionManager) return;

    TArray<FVector> CoverPositions = FindCoverPositions();

    // Update enemy cover assignments
    for (ABaseEnemy* Enemy : ActiveEnemies)
    {
        if (Enemy && CoverPositions.Num() > 0)
        {
            // Find closest cover
            FVector BestCover = CoverPositions[0];
            float BestDist = FVector::Dist(Enemy->GetActorLocation(), BestCover);

            for (const FVector& Cover : CoverPositions)
            {
                float Dist = FVector::Dist(Enemy->GetActorLocation(), Cover);
                if (Dist < BestDist)
                {
                    BestDist = Dist;
                    BestCover = Cover;
                }
            }

            // Send enemy to cover
            SendEnemyToLocation(Enemy, BestCover);
        }
    }
}

TArray<FVector> AEnemyPod::FindCoverPositions() const
{
    TArray<FVector> Result;

    // Get destruction manager
    ADestructionManager* DestructionManager = ADestructionManager::GetInstance(GetWorld());
    if (!DestructionManager) return Result;

    // Check each spawn point for destruction status
    for (const FEnemySpawnPoint& Point : SpawnPoints)
    {
        // Check if this point is near destroyed geometry
        // Was Location +/- FVector(0,0,100) -- meaningless on a ring, see RingFrameUtils.h
        FVector Start, End;
        URingFrameUtils::MakeGroundTrace(Point.Location, 100.0f, Start, End);

        FHitResult Hit;
        FCollisionQueryParams Params;
        Params.bTraceComplex = true;

        if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
        {
            // Hit something - could be cover
            ADestructibleSurface* Surface = Cast<ADestructibleSurface>(Hit.GetActor());
            if (Surface && Surface->GetSurfaceIntegrity(Hit.ImpactPoint) < 0.3f)
            {
                // Damaged surface provides cover
                Result.Add(Hit.ImpactPoint + Hit.ImpactNormal * 50);
            }
        }
    }

    return Result;
}

void AEnemyPod::OnEnvironmentDestroyed(FVector DestructionLocation, float DestructionRadius)
{
    if (!bUsesCoverFromDestruction) return;

    UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: Environment destroyed at %s"),
        PodID, *DestructionLocation.ToString());

    // Check if any of our spawn points are affected
    for (int32 i = 0; i < SpawnPoints.Num(); i++)
    {
        float Distance = FVector::Dist(DestructionLocation, SpawnPoints[i].Location);
        if (Distance < DestructionRadius)
        {
            UE_LOG(LogTemp, Log, TEXT("EnemyPod %d: Spawn point %d destroyed"), PodID, i);

            // Mark spawn point as damaged (you'd want to persist this)
            // For now, just notify enemies near that point
            for (ABaseEnemy* Enemy : ActiveEnemies)
            {
                if (Enemy && FVector::Dist(Enemy->GetActorLocation(), SpawnPoints[i].Location) < 200.f)
                {
                    // Enemy needs to find new cover
                    TArray<FVector> CoverPoints = FindCoverPositions();
                    if (CoverPoints.Num() > 0)
                    {
                        FVector BestCover = CoverPoints[0];
                        float BestDist = FVector::Dist(Enemy->GetActorLocation(), BestCover);

                        for (const FVector& Cover : CoverPoints)
                        {
                            float Dist = FVector::Dist(Enemy->GetActorLocation(), Cover);
                            if (Dist < BestDist)
                            {
                                BestDist = Dist;
                                BestCover = Cover;
                            }
                        }

                        SendEnemyToLocation(Enemy, BestCover);
                    }
                }
            }
        }
    }
}

void AEnemyPod::OnEnemySpawned(ABaseEnemy* NewEnemy)
{
    if (!NewEnemy) return;

    ActiveEnemies.Add(NewEnemy);
    CurrentEnemyCount++;

    UE_LOG(LogTemp, Verbose, TEXT("EnemyPod %d: Enemy spawned, total: %d"),
        PodID, CurrentEnemyCount);
}

// Debug visualization
void AEnemyPod::DrawDebugInfo()
{
#if WITH_EDITOR
    if (GetWorld() && GetWorld()->WorldType == EWorldType::PIE)
    {
        // Draw activation radius
        DrawDebugSphere(GetWorld(), GetActorLocation(), ActivationRadius, 32,
            FColor::Red, false, 0.1f);

        // Draw spawn points
        for (const FEnemySpawnPoint& Point : SpawnPoints)
        {
            FColor PointColor = Point.bIsPatrolPoint ? FColor::Green : FColor::Yellow;
            DrawDebugSphere(GetWorld(), Point.Location, 50, 8, PointColor, false, 0.1f);
            DrawDebugLine(GetWorld(), Point.Location, Point.Location + Point.Rotation.Vector() * 100,
                PointColor, false, 0.1f);
        }

        // Draw pod ID
        DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 200),
            FString::Printf(TEXT("Pod %d - %d/%d"), PodID, CurrentEnemyCount, MaxEnemyCount),
            nullptr, FColor::White, 0.1f);
    }
#endif
}