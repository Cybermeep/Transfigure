// Source/TMovement/Private/Transfiguration/Effects/FireBlastEffect.cpp

#include "Transfiguration/Effects/FireBlastEffect.h"
#include "Character/TMCharacter.h"
#include "Destruction/DestructionManager.h"
#include "Kismet/GameplayStatics.h"
#include "Components/DecalComponent.h"
#include "Particles/ParticleSystemComponent.h"

void UFireBlastEffect::Execute(
    const FVector& Origin,
    UTransfigurationDefinition* SpellData,
    AActor* Instigator,
    UWorld* World)
{
    if (!SpellData || !World) return;

    // Store momentum if conserving
    if (SpellData->bConservesMomentum)
    {
        StoreInstigatorVelocity(Instigator);
    }

    float ScaledDamage = GetScaledDamage(SpellData, Instigator);

    // Apply damage to enemies.
    // Was ApplyRadialDamage with radius = TriggerRadius and linear falloff to 0:
    // the sigil fires when an enemy's capsule EDGE touches the trigger, so the
    // enemy's centre is always just outside that radius and took 0 damage.
    // Now: full damage inside the trigger radius, falling to half at 2x radius.
    const float Inner = SpellData->TriggerRadius;
    const float Outer = SpellData->TriggerRadius * 2.f;
    const bool bHitAnything = UGameplayStatics::ApplyRadialDamageWithFalloff(
        World,
        ScaledDamage,
        ScaledDamage * 0.5f,     // minimum damage at the outer edge
        Origin,
        Inner,
        Outer,
        1.f,                     // linear falloff between inner and outer
        UDamageType::StaticClass(),
        TArray<AActor*>{},
        Instigator,
        nullptr);

    UE_LOG(LogTemp, Warning, TEXT("[TM] FireBlast: %.0f damage, radius %.0f-%.0f, hit something: %d"),
        ScaledDamage, Inner, Outer, bHitAnything ? 1 : 0);

    // Queue environmental destruction
    if (SpellData->bCausesEnvironmentalDestruction)
    {
        ATMCharacter* TMChar = Cast<ATMCharacter>(Instigator);
        if (TMChar && TMChar->bIsTemporalActive)
        {
            ADestructionManager* DestructionManager = ADestructionManager::GetInstance(World);
            if (DestructionManager)
            {
                DestructionManager->ProcessDestructionQueue();
            }
        }
        else
        {
            QueueEnvironmentalDestruction(Origin, Instigator, SpellData);
        }
    }

    // Movement augmentation - Fire + Slide = Fire Trail
    ATMCharacter* TMChar = Cast<ATMCharacter>(Instigator);
    if (TMChar && TMChar->IsSliding() && SpellData->MovementAugmentations.Contains("Slide"))
    {
        SpawnFireTrail(Instigator, FireTrailDuration);
    }

    if (SpellData->bConservesMomentum)
    {
        RestoreInstigatorVelocity(Instigator);
    }
}

void UFireBlastEffect::SpawnFireTrail(AActor* Instigator, float Duration)
{
    if (!Instigator) return;

    UWorld* World = Instigator->GetWorld();
    if (!World) return;

    // Store the instigator's current velocity direction for trail orientation
    FVector TrailDirection = Instigator->GetVelocity().GetSafeNormal();
    FVector TrailStart = Instigator->GetActorLocation();

    // Apply radial fire damage in a sweep along the slide direction
    // using the existing ApplyRadialDamage helper from the base class
    int32 NumTrailPoints = FMath::Max(3, FMath::FloorToInt(Duration * 4.f));
    float StepDistance = 80.f;

    for (int32 i = 0; i < NumTrailPoints; i++)
    {
        FVector TrailPoint = TrailStart + (TrailDirection * StepDistance * i);

        ApplyRadialDamage(
            World,
            Instigator,
            TrailPoint,
            100.f,          // fire trail radius
            10.f,           // damage per point
            UDamageType::StaticClass());
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("FireBlastEffect: Spawned fire trail with %d points over %.1fs"),
        NumTrailPoints, Duration);

    // Visual trail spawned in Blueprint via OnSpellCast BlueprintImplementableEvent
    // when Niagara assets are available
}