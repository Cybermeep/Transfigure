// Source/TMovement/Private/Transfiguration/Effects/IceWallEffect.cpp

#include "Transfiguration/Effects/IceWallEffect.h"
#include "Character/TMCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Destruction/DestructionManager.h"
#include "Kismet/GameplayStatics.h"
#include "Transfiguration/SigilActor.h"
#include "Enemy/BaseEnemy.h"
#include "Engine/OverlapResult.h"



void UIceWallEffect::Execute(
    const FVector& Origin,
    UTransfigurationDefinition* SpellData,
    AActor* Instigator,
    UWorld* World)
{
    if (!SpellData || !World) return;

    ATMCharacter* TMChar = Cast<ATMCharacter>(Instigator);

    // Check for movement augmentation - Ice + Wall Run = Ice Trail
    if (TMChar && TMChar->IsWallRunning() && SpellData->MovementAugmentations.Contains("WallRun"))
    {
        SpawnIceTrail(Instigator, 5.0f);
    }

    if (SpellData->bConservesMomentum)
    {
        StoreInstigatorVelocity(Instigator);
    }

    // Queue environmental destruction (ice shatters)
    if (SpellData->bCausesEnvironmentalDestruction)
    {
        QueueEnvironmentalDestruction(Origin, Instigator, SpellData);
    }

    // Create the ice wall
    CreateIceWall(Origin, Instigator, SpellData);

    if (SpellData->bConservesMomentum)
    {
        RestoreInstigatorVelocity(Instigator);
    }
}

void UIceWallEffect::CreateIceWall(const FVector& Origin, AActor* Instigator, UTransfigurationDefinition* SpellData)
{
    if (!IceWallClass || !GetWorld()) return;

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = Instigator;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    FVector ToInstigator = (Instigator->GetActorLocation() - Origin);
    ToInstigator.Z = 0.f;
    ToInstigator.Normalize();
    FRotator WallRotation = ToInstigator.Rotation();

    AActor* Wall = GetWorld()->SpawnActor<AActor>(
        IceWallClass, Origin, WallRotation, SpawnParams);

    if (Wall && SpellData)
    {
        Wall->SetLifeSpan(SpellData->TimeToLive);
    }
}

void UIceWallEffect::SpawnIceTrail(AActor* Instigator, float Duration)
{
    if (!Instigator) return;

    UWorld* World = Instigator->GetWorld();
    if (!World) return;

    FVector TrailDirection = Instigator->GetVelocity().GetSafeNormal();
    FVector TrailStart = Instigator->GetActorLocation();

    // Ice trail slows and damages enemies along the wall run path
    int32 NumTrailPoints = FMath::Max(3, FMath::FloorToInt(Duration * 5.f));
    float StepDistance = 60.f;

    for (int32 i = 0; i < NumTrailPoints; i++)
    {
        FVector TrailPoint = TrailStart + (TrailDirection * StepDistance * i);

        // Find enemies near each trail point and slow them
        TArray<FOverlapResult> Overlaps;
        FCollisionShape Sphere = FCollisionShape::MakeSphere(80.f);
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(Instigator);

        World->OverlapMultiByChannel(
            Overlaps,
            TrailPoint,
            FQuat::Identity,
            ECC_Pawn,
            Sphere,
            Params);

        for (const FOverlapResult& Overlap : Overlaps)
        {
            ABaseEnemy* Enemy = Cast<ABaseEnemy>(Overlap.GetActor());
            if (!Enemy) continue;

            Enemy->TakePoolDamage(5.f, TrailPoint);

            UCharacterMovementComponent* CMC =
                Enemy->FindComponentByClass<UCharacterMovementComponent>();
            if (CMC && CMC->MaxWalkSpeed > 100.f)
            {
                float SlowedSpeed = CMC->MaxWalkSpeed * 0.4f;
                CMC->MaxWalkSpeed = SlowedSpeed;

                FTimerHandle RestoreTimer;
                FTimerDelegate RestoreDelegate;
                TWeakObjectPtr<UCharacterMovementComponent> WeakCMC(CMC);
                float OrigSpeed = CMC->MaxWalkSpeed / 0.4f;
                RestoreDelegate.BindLambda([WeakCMC, OrigSpeed]()
                    {
                        if (WeakCMC.IsValid())
                            WeakCMC->MaxWalkSpeed = OrigSpeed;
                    });
                World->GetTimerManager().SetTimer(RestoreTimer, RestoreDelegate, 3.f, false);
            }
        }
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("IceWallEffect: Spawned ice trail with %d points over %.1fs"),
        NumTrailPoints, Duration);
}