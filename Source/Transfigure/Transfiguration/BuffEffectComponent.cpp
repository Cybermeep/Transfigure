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
        TMChar->GetCharacterMovement()->MaxWalkSpeed *= Multiplier;
        break;
    case EBuffType::Jump:
        TMChar->JumpZVelocity *= Multiplier;
        break;
    default:
        break;
    }

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
            switch (BuffType)
            {
            case EBuffType::Speed:
                TMChar->GetCharacterMovement()->MaxWalkSpeed /= ActiveBuffs[i].Multiplier;
                break;
            case EBuffType::Jump:
                TMChar->JumpZVelocity /= ActiveBuffs[i].Multiplier;
                break;
            default:
                break;
            }

            ActiveBuffs.RemoveAt(i);
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