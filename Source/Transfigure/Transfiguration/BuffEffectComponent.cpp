// Source/TMovement/Private/Transfiguration/BuffEffectComponent.cpp

#include "Transfiguration/BuffEffectComponent.h"
#include "Character/TMCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

UBuffEffectComponent::UBuffEffectComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UBuffEffectComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    for (int32 i = ActiveBuffs.Num() - 1; i >= 0; i--)
    {
        FActiveBuff& Buff = ActiveBuffs[i];
        Buff.TimeRemaining -= DeltaTime;

        if (Buff.TimeRemaining <= 0.f)
        {
            RemoveBuff(Buff.Type);
        }
    }
}

void UBuffEffectComponent::ApplyBuff(EBuffType BuffType, float Multiplier, float Duration)
{
    ATMCharacter* TMChar = Cast<ATMCharacter>(GetOwner());
    if (!TMChar) return;

    RemoveBuff(BuffType);

    FActiveBuff NewBuff;
    NewBuff.Type = BuffType;
    NewBuff.Multiplier = Multiplier;
    NewBuff.TimeRemaining = Duration;
    ActiveBuffs.Add(NewBuff);

    switch (BuffType)
    {
    case EBuffType::Speed:
        // Was MaxWalkSpeed *= Multiplier, which movement states then overwrote
        // (and the matching /= on expiry left the player permanently slow).
        TMChar->RefreshMovementSpeed();
        break;
    case EBuffType::Jump:
        TMChar->JumpZVelocity *= Multiplier;
        break;
    default:
        break;
    }

    UE_LOG(LogTemp, Warning, TEXT("[TM] Buff applied: %s x%.2f for %.1fs"),
        *UEnum::GetValueAsString(BuffType), Multiplier, Duration);
    OnBuffApplied.Broadcast(BuffType, Duration);
}

void UBuffEffectComponent::RemoveBuff(EBuffType BuffType)
{
    ATMCharacter* TMChar = Cast<ATMCharacter>(GetOwner());
    if (!TMChar) return;

    for (int32 i = 0; i < ActiveBuffs.Num(); i++)
    {
        if (ActiveBuffs[i].Type == BuffType)
        {
            const float Multiplier = ActiveBuffs[i].Multiplier;
            ActiveBuffs.RemoveAt(i);

            switch (BuffType)
            {
            case EBuffType::Speed:
                TMChar->RefreshMovementSpeed();  // buff is gone now, so this restores base speed
                break;
            case EBuffType::Jump:
                if (!FMath::IsNearlyZero(Multiplier)) TMChar->JumpZVelocity /= Multiplier;
                break;
            default:
                break;
            }

            UE_LOG(LogTemp, Warning, TEXT("[TM] Buff expired: %s"), *UEnum::GetValueAsString(BuffType));
            OnBuffExpired.Broadcast(BuffType);
            return;
        }
    }
}

bool UBuffEffectComponent::HasBuff(EBuffType BuffType) const
{
    for (const FActiveBuff& Buff : ActiveBuffs)
    {
        if (Buff.Type == BuffType)
            return true;
    }
    return false;
}

float UBuffEffectComponent::GetBuffMultiplier(EBuffType BuffType) const
{
    for (const FActiveBuff& Buff : ActiveBuffs)
    {
        if (Buff.Type == BuffType)
            return Buff.Multiplier;
    }
    return 1.0f;
}