// Source/Transfigure/Combat/CombatWaveDirector.cpp

#include "Combat/CombatWaveDirector.h"
#include "Enemy/BaseEnemy.h"
#include "Limb/ProceduralZombie.h"
#include "Character/TMCharacter.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "RingFrameUtils.h"

// Best score this session (survives restarting the map; a save file comes later).
static int32 GBestScore = 0;

ACombatWaveDirector::ACombatWaveDirector()
{
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    EnemyClass = AProceduralZombie::StaticClass();
}

ACombatWaveDirector* ACombatWaveDirector::Find(const UObject* WorldContext)
{
    UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    if (!World) return nullptr;
    for (TActorIterator<ACombatWaveDirector> It(World); It; ++It)
    {
        return *It;
    }
    return nullptr;
}

int32 ACombatWaveDirector::GetBestScore() const
{
    return FMath::Max(GBestScore, Score);
}

void ACombatWaveDirector::BeginPlay()
{
    Super::BeginPlay();
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        if (It->ActorHasTag(SpawnPointTag)) SpawnPoints.Add(It->GetActorLocation());
    }
    Phase = ERunPhase::Starting;
    PhaseTimeLeft = StartDelay;
    UE_LOG(LogTemp, Warning, TEXT("[TM] Waves: director ready, %d spawn points, wave 1 in %.0f s (best score %d)"),
        SpawnPoints.Num(), StartDelay, GBestScore);
}

void ACombatWaveDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Phase == ERunPhase::Over) return;

    const ATMCharacter* Player = Cast<ATMCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (Player && Player->IsDead())
    {
        EndRun();
        return;
    }

    // Enemies removed some other way (destroyed, pooled) no longer count.
    Alive.RemoveAll([](const TWeakObjectPtr<ABaseEnemy>& E) { return !E.IsValid() || !E->IsAlive(); });

    switch (Phase)
    {
    case ERunPhase::Starting:
    case ERunPhase::Breather:
        if (Phase == ERunPhase::Breather) RunTime += DeltaSeconds;
        PhaseTimeLeft -= DeltaSeconds;
        if (PhaseTimeLeft <= 0.f) StartWave();
        break;

    case ERunPhase::Fighting:
    {
        RunTime += DeltaSeconds;
        WaveTime += DeltaSeconds;
        UnstickEnemies(DeltaSeconds);
        const int32 MaxAlive = FMath::Min(FirstMaxAlive + Wave - 1, MaxAliveCap);
        NextSpawnIn -= DeltaSeconds;
        if (ToSpawn > 0 && Alive.Num() < MaxAlive && NextSpawnIn <= 0.f)
        {
            if (SpawnEnemy()) --ToSpawn;
            NextSpawnIn = FMath::Max(MinSpawnInterval, SpawnInterval * FMath::Pow(0.92f, static_cast<float>(Wave - 1)));
        }
        if (ToSpawn <= 0 && Alive.Num() == 0)
        {
            const int32 Bonus = 250 * Wave;
            Score += Bonus;
            UE_LOG(LogTemp, Warning, TEXT("[TM] WAVE %d cleared in %.0f s (+%d), score %d"), Wave, WaveTime, Bonus, Score);
            if (UCombatFeedbackSubsystem* Feedback = UCombatFeedbackSubsystem::Get(this))
            {
                Feedback->AddToast(FString::Printf(TEXT("WAVE %d CLEARED  +%d"), Wave, Bonus), FLinearColor(0.55f, 1.f, 0.6f));
            }
            Phase = ERunPhase::Breather;
            PhaseTimeLeft = BreatherTime;
        }
        break;
    }

    default:
        break;
    }
}

void ACombatWaveDirector::StartWave()
{
    ++Wave;
    ToSpawn = FMath::Min(FirstWaveCount + ExtraPerWave * (Wave - 1), MaxPerWave);
    NextSpawnIn = 0.f;
    WaveTime = 0.f;
    Phase = ERunPhase::Fighting;
    UE_LOG(LogTemp, Warning, TEXT("[TM] WAVE %d: %d enemies, up to %d at once, health x%.2f"), Wave, ToSpawn,
        FMath::Min(FirstMaxAlive + Wave - 1, MaxAliveCap), 1.f + HealthGrowthPerWave * (Wave - 1));
    if (UCombatFeedbackSubsystem* Feedback = UCombatFeedbackSubsystem::Get(this))
    {
        Feedback->AddToast(FString::Printf(TEXT("WAVE %d"), Wave), FLinearColor(1.f, 0.75f, 0.4f));
    }
}

void ACombatWaveDirector::EndRun()
{
    Phase = ERunPhase::Over;
    bNewBest = Score > GBestScore;
    GBestScore = FMath::Max(GBestScore, Score);
    UE_LOG(LogTemp, Warning, TEXT("[TM] RUN over: wave %d, %d kills, score %d, %.0f s (best %d%s)"),
        Wave, Kills, Score, RunTime, GBestScore, bNewBest ? TEXT(", new best") : TEXT(""));
}

bool ACombatWaveDirector::PickSpawnPoint(FVector& Out) const
{
    const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    const APawn* Player = PC ? PC->GetPawn() : nullptr;
    if (!Player) return false;
    const FVector PlayerLoc = Player->GetActorLocation();
    FVector ViewLoc = PlayerLoc;
    FVector ViewDir = Player->GetActorForwardVector();
    if (PC->PlayerCameraManager)
    {
        ViewLoc = PC->PlayerCameraManager->GetCameraLocation();
        ViewDir = PC->PlayerCameraManager->GetCameraRotation().Vector();
    }

    // Far enough away, and behind or beside you if possible (nothing pops in on screen).
    TArray<FVector> Hidden, Visible;
    for (const FVector& P : SpawnPoints)
    {
        if (FVector::Dist(P, PlayerLoc) < MinSpawnDistance) continue;
        const bool bInView = FVector::DotProduct((P - ViewLoc).GetSafeNormal(), ViewDir) > 0.5f;
        (bInView ? Visible : Hidden).Add(P);
    }
    const TArray<FVector>& Pool = Hidden.Num() > 0 ? Hidden : Visible;
    if (Pool.Num() > 0)
    {
        Out = Pool[FMath::RandRange(0, Pool.Num() - 1)];
    }
    else
    {
        // No spawn points (or all too close): somewhere on a ring around the player.
        const FVector Up = URingFrameUtils::GetUpAt(PlayerLoc);
        const FVector Side = FVector::CrossProduct(Up, FMath::Abs(Up.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
        const FQuat Spin(Up, FMath::FRandRange(0.f, 2.f * PI));
        const FVector Guess = PlayerLoc + Spin.RotateVector(Side) * (MinSpawnDistance + 600.f);
        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(WaveSpawn), false, Player);
        if (!GetWorld()->LineTraceSingleByChannel(Hit, Guess + Up * 800.f, Guess - Up * 3000.f, ECC_WorldStatic, Params)) return false;
        Out = Hit.ImpactPoint;
    }
    // Spread them out a little.
    const FVector Up = URingFrameUtils::GetUpAt(Out);
    Out += URingFrameUtils::StripVertical(FMath::VRand(), Up).GetSafeNormal() * FMath::FRandRange(0.f, 220.f);
    return true;
}

bool ACombatWaveDirector::SpawnEnemy()
{
    FVector P;
    if (!PickSpawnPoint(P)) return false;
    const FVector Up = URingFrameUtils::GetUpAt(P);
    const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    FVector Face = Player ? URingFrameUtils::StripVertical(Player->GetActorLocation() - P, Up).GetSafeNormal() : FVector::ZeroVector;
    if (Face.IsNearlyZero()) Face = FVector::CrossProduct(Up, FVector::RightVector).GetSafeNormal();

    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    UClass* Class = EnemyClass ? EnemyClass.Get() : AProceduralZombie::StaticClass();
    ABaseEnemy* E = GetWorld()->SpawnActor<ABaseEnemy>(Class, P + Up * 110.f, FRotationMatrix::MakeFromXZ(Face, Up).Rotator(), SP);
    if (!E) return false;

    E->AwarenessRange = 1.0e6f;   // wave enemies always come for you
    E->MaxHealth *= 1.f + HealthGrowthPerWave * (Wave - 1);
    E->Health = E->MaxHealth;
    E->OnEnemyDied.AddDynamic(this, &ACombatWaveDirector::HandleEnemyDied);
    Alive.Add(E);
    return true;
}

void ACombatWaveDirector::UnstickEnemies(float DeltaSeconds)
{
    // An enemy that hasn't moved for a while, far from you, is stuck on the level: put it
    // back at a spawn point so a wave can't stall. The log says where, so the map can be fixed.
    const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Player) return;
    for (auto It = Stuck.CreateIterator(); It; ++It)
    {
        if (!It->Key.IsValid() || !It->Key->IsAlive()) It.RemoveCurrent();
    }
    for (const TWeakObjectPtr<ABaseEnemy>& Weak : Alive)
    {
        ABaseEnemy* E = Weak.Get();
        if (!E) continue;
        FStuckTrack& T = Stuck.FindOrAdd(Weak);
        const FVector Pos = E->GetActorLocation();
        const bool bBusy = E->CurrentState == EEnemyState::Stunned;
        if (FVector::Dist(Pos, T.LastPos) > 60.f || bBusy || FVector::Dist(Pos, Player->GetActorLocation()) < 800.f)
        {
            T.LastPos = Pos;
            T.StillTime = 0.f;
            continue;
        }
        T.StillTime += DeltaSeconds;
        if (T.StillTime < 6.f) continue;

        FVector P;
        if (!PickSpawnPoint(P)) continue;
        UE_LOG(LogTemp, Warning, TEXT("[TM] Waves: %s stuck at %s for 6 s, moved to a spawn point"), *E->GetName(), *Pos.ToString());
        E->TeleportTo(P + URingFrameUtils::GetUpAt(P) * 110.f, E->GetActorRotation());
        T.LastPos = E->GetActorLocation();
        T.StillTime = 0.f;
    }
}

void ACombatWaveDirector::HandleEnemyDied(ABaseEnemy* Enemy)
{
    if (Phase == ERunPhase::Over) return;
    ++Kills;
    Score += 100 + 10 * Wave;
    Alive.RemoveAll([Enemy](const TWeakObjectPtr<ABaseEnemy>& E) { return E.Get() == Enemy; });
}
