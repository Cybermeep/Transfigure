// Source/TMovement/Private/Transfiguration/Effects/IceWallEffect.cpp

#include "Transfiguration/Effects/IceWallEffect.h"
#include "Character/TMCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Destruction/DestructionManager.h"
#include "Kismet/GameplayStatics.h"
#include "Transfiguration/SigilActor.h"
#include "Enemy/BaseEnemy.h"
#include "Engine/OverlapResult.h"
#include "RingFrameUtils.h"



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

    // Face the caster across the local ground plane, standing upright on the ring.
    const FVector Up = URingFrameUtils::GetUpAt(Origin);
    FVector ToInstigator = URingFrameUtils::StripVertical(Instigator->GetActorLocation() - Origin, Up).GetSafeNormal();
    if (ToInstigator.IsNearlyZero()) ToInstigator = FVector::CrossProduct(Up, FVector::RightVector).GetSafeNormal();
    const FRotator WallRotation = FRotationMatrix::MakeFromXZ(ToInstigator, Up).Rotator();

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

            // Was: save the (possibly already slowed) speed and restore it later,
            // which stacked down to a crawl. ApplySlow is always relative to base speed.
            Enemy->ApplySlow(0.4f, 3.f);
        }
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("IceWallEffect: Spawned ice trail with %d points over %.1fs"),
        NumTrailPoints, Duration);
}