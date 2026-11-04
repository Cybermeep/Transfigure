// Source/TMovement/Private/Transfiguration/TransfigurationEffectBase.cpp

#include "Transfiguration/TransfigurationEffectBase.h"
#include "Character/TMCharacter.h"
#include "Destruction/DestructionManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UTransfigurationEffectBase::StoreInstigatorVelocity(AActor* Instigator)
{
    if (!Instigator) return;

    ACharacter* Char = Cast<ACharacter>(Instigator);
    if (Char && Char->GetCharacterMovement())
    {
        StoredVelocity = Char->GetCharacterMovement()->Velocity;
        bVelocityStored = true;
    }
}

void UTransfigurationEffectBase::RestoreInstigatorVelocity(AActor* Instigator)
{
    if (!bVelocityStored || !Instigator) return;

    ACharacter* Char = Cast<ACharacter>(Instigator);
    if (Char && Char->GetCharacterMovement())
    {
        Char->GetCharacterMovement()->Velocity = StoredVelocity;
    }
}

float UTransfigurationEffectBase::GetScaledDamage(UTransfigurationDefinition* SpellData, AActor* Instigator) const
{
    if (!SpellData || !SpellData->bScalesWithSpeed || !Instigator)
        return SpellData ? SpellData->BaseDamage : 0.f;

    ATMCharacter* TMChar = Cast<ATMCharacter>(Instigator);
    if (!TMChar) return SpellData->BaseDamage;

    float ScaleFactor = TMChar->GetSpeedScalingFactor();
    return SpellData->BaseDamage * ScaleFactor;
}

float UTransfigurationEffectBase::GetScaledCooldown(UTransfigurationDefinition* SpellData, AActor* Instigator) const
{
    if (!SpellData || !SpellData->bScalesWithSpeed || !Instigator)
        return SpellData ? SpellData->CooldownDuration : 0.f;

    ATMCharacter* TMChar = Cast<ATMCharacter>(Instigator);
    if (!TMChar) return SpellData->CooldownDuration;

    float Speed = TMChar->GetCurrentSpeed();
    float ScaleFactor = FMath::GetMappedRangeValueClamped(
        FVector2D(TMChar->MinSpeedForMagicScaling, TMChar->MaxSpeedForMagicScaling),
        FVector2D(1.0f, TMChar->MaxCooldownScale),
        Speed);

    return SpellData->CooldownDuration * ScaleFactor;
}

void UTransfigurationEffectBase::QueueEnvironmentalDestruction(FVector Origin, AActor* Instigator, UTransfigurationDefinition* SpellData)
{
    if (!SpellData || !SpellData->bCausesEnvironmentalDestruction) return;

    UWorld* World = Instigator ? Instigator->GetWorld() : nullptr;
    if (!World) return;

    ADestructionManager* DestructionManager = ADestructionManager::GetInstance(World);
    if (!DestructionManager) return;

    DestructionManager->QueueDestruction(Origin, SpellData->DestructionRadius, SpellData->DestructionForce, nullptr);
}

void UTransfigurationEffectBase::ApplyRadialDamage(
    UWorld* World,
    AActor* Instigator,
    FVector Origin,
    float Radius,
    float BaseDamage,
    TSubclassOf<UDamageType> DamageTypeClass) const
{
    if (!World) return;

    UGameplayStatics::ApplyRadialDamage(
        World,
        BaseDamage,
        Origin,
        Radius,
        DamageTypeClass,
        TArray<AActor*>{},
        Instigator,
        nullptr,
        /*bDoFullDamage=*/false);
}

TArray<AActor*> UTransfigurationEffectBase::GetActorsInRadius(
    UWorld* World,
    const FVector& Origin,
    float Radius) const
{
    TArray<AActor*> Result;
    if (!World) return Result;

    TArray<AActor*> IgnoredActors;
    TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
    ObjectTypes.Add(EObjectTypeQuery::ObjectTypeQuery3); // ECC_Pawn

    UKismetSystemLibrary::SphereOverlapActors(
        World,
        Origin,
        Radius,
        ObjectTypes,
        nullptr,
        IgnoredActors,
        Result);

    return Result;
}