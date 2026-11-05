// Source/TMovement/Private/Enemy/BaseEnemy.cpp

#include "Enemy/BaseEnemy.h"
#include "Enemy/EnemyObjectPool.h"
#include "Components/WidgetComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DamageEvents.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"
#include "Transfiguration/StatusEffectComponent.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Templates/UnrealTemplate.h"

ABaseEnemy::ABaseEnemy()
{
    PrimaryActorTick.bCanEverTick = true;

    HealthBarWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthBarWidget"));
    HealthBarWidget->SetupAttachment(RootComponent);
    HealthBarWidget->SetWidgetSpace(EWidgetSpace::Screen);
    HealthBarWidget->SetDrawSize(FVector2D(100, 20));

    Status = CreateDefaultSubobject<UStatusEffectComponent>(TEXT("Status"));

    // Get an AI controller whether placed in the map or spawned by a pool.
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    // Path following drives acceleration (not raw velocity), so animation BPs that check
    // acceleration (like the Mannequin's ABP_Unarmed) play the walk cycle.
    if (FNavMovementProperties* NavProps = GetCharacterMovement()->GetNavMovementProperties())
    {
        NavProps->bUseAccelerationForPaths = true;
    }
}

void ABaseEnemy::BeginPlay()
{
    Super::BeginPlay();
    Health = MaxHealth;
    BaseMaxWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;

    if (!OwningPool)
    {
        // Enemies placed directly in the map start chasing right away.
        // (Pool enemies get their timers cleared when parked, and restarted in OnSpawnFromPool.)
        GetWorldTimerManager().SetTimer(ChaseTimerHandle, this, &ABaseEnemy::UpdateChase, 0.5f, true);
    }
}

void ABaseEnemy::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    UpdateRingGravity(DeltaTime);

    // Fallback when there's no nav mesh path: walk straight at the player.
    if (bDirectChase && bIsAlive && CurrentState == EEnemyState::Combat)
    {
        if (const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
        {
            const FVector ToPlayer = FVector::VectorPlaneProject(
                Player->GetActorLocation() - GetActorLocation(), CachedRingUp);
            if (ToPlayer.Size() > AttackRange * 0.75f)
            {
                AddMovementInput(ToPlayer.GetSafeNormal());
            }
        }
    }

    // Update health bar visibility
    if (HealthBarWidget)
    {
        HealthBarWidget->SetVisibility(bIsAlive && Health < MaxHealth);
    }
}

void ABaseEnemy::UpdateRingGravity(float DeltaTime)
{
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    CachedRingUp = -URingWorldGeometry::GetGravityDirection(Spec, GetActorLocation());

    // Same native custom-gravity path as ATMCharacter: this is what makes
    // CMC's floor detection and nav-mesh-following behave correctly instead
    // of assuming world -Z.
    GetCharacterMovement()->SetGravityDirection(-CachedRingUp);

    // Keep the enemy standing upright relative to the ring surface. Unlike
    // the player, there's no player-controlled yaw to preserve here -- just
    // re-project the enemy's current forward onto the new tangent plane so
    // it doesn't visually tip over as the ground curves underneath it.
    FVector Forward = GetActorForwardVector();
    Forward = (Forward - CachedRingUp * FVector::DotProduct(Forward, CachedRingUp)).GetSafeNormal();
    if (Forward.IsNearlyZero())
    {
        Forward = FVector::CrossProduct(FVector::RightVector, CachedRingUp).GetSafeNormal();
    }

    SetActorRotation(FRotationMatrix::MakeFromXZ(Forward, CachedRingUp).ToQuat());
}

void ABaseEnemy::OnSpawnFromPool()
{
    bIsAlive = true;
    Health = MaxHealth;
    if (Status) Status->ClearAll();
    CurrentState = EEnemyState::Idle;
    AlertnessLevel = 1.0f;
    bDirectChase = false;

    SetActorHiddenInGame(false);
    SetActorEnableCollision(true);
    SetActorTickEnabled(true);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

    UCharacterMovementComponent* CMC = GetCharacterMovement();
    CMC->SetComponentTickEnabled(true);
    CMC->MaxWalkSpeed = BaseMaxWalkSpeed;
    CMC->SetMovementMode(MOVE_Walking);

    GetWorldTimerManager().SetTimer(ChaseTimerHandle, this, &ABaseEnemy::UpdateChase, 0.5f, true);
    OnSpawnEffects();

    UE_LOG(LogTemp, Warning, TEXT("[TM] %s spawned from pool (pod %d) at %s"),
        *GetName(), PodID, *GetActorLocation().ToString());
}

void ABaseEnemy::OnReturnToPool()
{
    if (Status) Status->ClearAll();
    bIsAlive = false;
    CurrentState = EEnemyState::Dead;
    GetWorldTimerManager().ClearAllTimersForObject(this);

    // Fully parked: hidden, no collision, no tick, no movement.
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
    SetActorTickEnabled(false);

    UCharacterMovementComponent* CMC = GetCharacterMovement();
    CMC->StopMovementImmediately();
    CMC->SetComponentTickEnabled(false);

    OnReturnEffects();
}

void ABaseEnemy::Die()
{
    if (!bIsAlive) return;

    bIsAlive = false;
    CurrentState = EEnemyState::Dead;
    GetWorldTimerManager().ClearTimer(ChaseTimerHandle);
    GetWorldTimerManager().ClearTimer(SlowTimerHandle);
    GetWorldTimerManager().ClearTimer(StunTimerHandle);

    if (AAIController* AI = Cast<AAIController>(GetController()))
    {
        AI->StopMovement();
    }

    // The corpse stops moving and no longer blocks shots or the player.
    GetCharacterMovement()->DisableMovement();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    UE_LOG(LogTemp, Warning, TEXT("[TM] %s died"), *GetName());
    OnDeathEffects();
    OnEnemyDied.Broadcast(this);

    GetWorldTimerManager().SetTimer(DeathTimerHandle, this, &ABaseEnemy::FinishDeath, CorpseLifetime, false);
}

void ABaseEnemy::FinishDeath()
{
    if (OwningPool)
    {
        OwningPool->ReturnEnemy(this);
    }
    else
    {
        Destroy();
    }
}

void ABaseEnemy::UpdateChase()
{
    if (!bIsAlive || CurrentState == EEnemyState::Stunned) return;

    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    AAIController* AI = Cast<AAIController>(GetController());
    if (!Player || !AI) return;

    const float Distance = FVector::Dist(GetActorLocation(), Player->GetActorLocation());
    if (Distance > AwarenessRange)
    {
        AI->StopMovement();
        bDirectChase = false;
        return;
    }

    if (CurrentState != EEnemyState::Combat)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] %s noticed the player (%.0f away)"), *GetName(), Distance);
    }
    CurrentState = EEnemyState::Combat;

    const EPathFollowingRequestResult::Type Result = AI->MoveToActor(Player, AttackRange * 0.75f);
    const bool bFailed = (Result == EPathFollowingRequestResult::Failed);
    if (bFailed && !bDirectChase)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] %s: no nav path to player, chasing in a straight line (nav mesh built?)"), *GetName());
    }
    bDirectChase = bFailed;

    const float Now = GetWorld()->GetTimeSeconds();
    if (Distance <= AttackRange && Now - LastAttackTime >= AttackCooldown && !(Status && Status->BlocksAttack()))
    {
        LastAttackTime = Now;
        UGameplayStatics::ApplyDamage(Player, AttackDamage, AI, this, UDamageType::StaticClass());
    }
}

void ABaseEnemy::ApplySlow(float SpeedMultiplier, float Duration)
{
    if (!bIsAlive) return;
    // Always relative to the base speed, so repeated slows can't stack down to zero.
    GetCharacterMovement()->MaxWalkSpeed = BaseMaxWalkSpeed * SpeedMultiplier;
    GetWorldTimerManager().SetTimer(SlowTimerHandle, this, &ABaseEnemy::EndSlow, Duration, false);
}

void ABaseEnemy::EndSlow()
{
    GetCharacterMovement()->MaxWalkSpeed = BaseMaxWalkSpeed;
}

void ABaseEnemy::ApplyStun(float Duration)
{
    if (!bIsAlive) return;
    CurrentState = EEnemyState::Stunned;
    // Stop chasing, but don't freeze physics: a stunned enemy that was knocked into
    // the air still has to fall (was DisableMovement, which left launched enemies hanging).
    if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
    bDirectChase = false;
    if (GetCharacterMovement()->IsMovingOnGround()) GetCharacterMovement()->StopMovementImmediately();
    // A longer stun is never cut short by a shorter one.
    const float Remaining = GetWorldTimerManager().IsTimerActive(StunTimerHandle) ? GetWorldTimerManager().GetTimerRemaining(StunTimerHandle) : 0.f;
    GetWorldTimerManager().SetTimer(StunTimerHandle, this, &ABaseEnemy::EndStun, FMath::Max(Duration, Remaining), false);
}

void ABaseEnemy::EndStun()
{
    if (!bIsAlive) return;
    CurrentState = EEnemyState::Combat;
}

// All engine damage funnels into TakePoolDamage, so there is exactly one
// place that changes enemy health (the limb system will hook in there).
float ABaseEnemy::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    if (Applied <= 0.f) return 0.f;

    FVector HitLocation = GetActorLocation();
    if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
    {
        HitLocation = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.ImpactPoint;
    }
    else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
    {
        HitLocation = static_cast<const FRadialDamageEvent&>(DamageEvent).Origin;
    }

    TakePoolDamage(Applied, HitLocation);
    return Applied;
}

float ABaseEnemy::AreaFalloff(float Distance, float InnerRadius, float OuterRadius)
{
    if (Distance > OuterRadius) return 0.f;
    if (Distance <= InnerRadius || OuterRadius <= InnerRadius) return 1.f;
    return FMath::GetMappedRangeValueClamped(FVector2D(InnerRadius, OuterRadius), FVector2D(1.f, 0.5f), Distance);
}

void ABaseEnemy::TakeAreaDamage(float Damage, FVector Origin, float InnerRadius, float OuterRadius)
{
    const float Scale = AreaFalloff(FVector::Dist(Origin, GetActorLocation()), InnerRadius, OuterRadius);
    if (Scale > 0.f) TakePoolDamage(Damage * Scale, Origin);
}

void ABaseEnemy::TakePoolDamage(float DamageAmount, FVector HitLocation)
{
    if (!bIsAlive) return;

    // Resistances (body, not machine parts) and Marked / Voided.
    DamageAmount *= GetElementScale(PendingElement, false);
    if (Status) DamageAmount *= Status->GetDamageTakenMultiplier();

    Health -= DamageAmount;
    CurrentState = EEnemyState::Combat;
    OnDamageEffects(HitLocation);
    UE_LOG(LogTemp, Warning, TEXT("[TM] %s took %.1f damage, health %.1f / %.1f"),
        *GetName(), DamageAmount, Health, MaxHealth);

    if (Health <= 0.0f)
    {
        Die();
    }
    if (UCombatFeedbackSubsystem* Feedback = UCombatFeedbackSubsystem::Get(this))
    {
        Feedback->ReportDamage(GetActorLocation() + GetRingUp() * 100.f, DamageAmount, PendingElement, !bIsAlive);
    }
}

void ABaseEnemy::TakeSpellDamage(float Damage, FVector HitLocation, ETransfigurationElement Element)
{
    TGuardValue<ETransfigurationElement> Guard(PendingElement, Element);
    TakePoolDamage(Damage, HitLocation);
}

void ABaseEnemy::TakeSpellAreaDamage(float Damage, FVector Origin, float InnerRadius, float OuterRadius, ETransfigurationElement Element)
{
    TGuardValue<ETransfigurationElement> Guard(PendingElement, Element);
    TakeAreaDamage(Damage, Origin, InnerRadius, OuterRadius);
}

void ABaseEnemy::TakeStatusDamage(float Damage, ETransfigurationElement Element)
{
    TakeSpellDamage(Damage, GetActorLocation(), Element);
}

float ABaseEnemy::GetElementScale(ETransfigurationElement Element, bool bMachinePart) const
{
    if (!bMachinePart) return 1.f;
    // Robot parts: fire barely scorches them, lightning fries them, cold makes them brittle-ish.
    switch (Element)
    {
    case ETransfigurationElement::Fire:      return 0.5f;
    case ETransfigurationElement::Lightning: return 2.0f;
    case ETransfigurationElement::Ice:       return 0.75f;
    default:                                 return 1.f;
    }
}

void ABaseEnemy::DetectGrappleLine(const FVector& LineStart, const FVector& LineEnd)
{
    if (!bIsAlive || CurrentState == EEnemyState::Stunned) return;

    float DistanceToLine = FMath::PointDistToSegment(
        GetActorLocation(),
        LineStart,
        LineEnd
    );

    if (DistanceToLine < 200.0f)
    {
        OnGrappleDetected(LineStart + (LineEnd - LineStart) * 0.5f);

        if (CurrentState == EEnemyState::Idle || CurrentState == EEnemyState::Patrolling)
        {
            CurrentState = EEnemyState::Alerted;
        }
    }
}

void ABaseEnemy::ReactToDestruction(FVector DestructionLocation, float Radius)
{
    if (!bIsAlive) return;

    float Distance = FVector::Dist(GetActorLocation(), DestructionLocation);
    if (Distance < Radius * 1.5f)
    {
        OnDestructionDetected(DestructionLocation);

        if (Distance < Radius)
        {
            // Take damage from destruction
            float DestructionDamage = (1.0f - (Distance / Radius)) * 50.0f;
            TakePoolDamage(DestructionDamage, DestructionLocation);
        }

        CurrentState = EEnemyState::Alerted;
    }
}