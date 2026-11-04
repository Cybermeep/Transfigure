// Source/TMovement/Public/Transfiguration/BuffEffectComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TransfigurationTypes.h"
#include "BuffEffectComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBuffApplied, EBuffType, BuffType, float, Duration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuffExpired, EBuffType, BuffType);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TRANSFIGURE_API UBuffEffectComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UBuffEffectComponent();

    UFUNCTION(BlueprintCallable, Category = "Buff")
        void ApplyBuff(EBuffType BuffType, float Multiplier, float Duration);

    UFUNCTION(BlueprintCallable, Category = "Buff")
        void RemoveBuff(EBuffType BuffType);

    UFUNCTION(BlueprintCallable, Category = "Buff")
        bool HasBuff(EBuffType BuffType) const;

    UFUNCTION(BlueprintCallable, Category = "Buff")
        float GetBuffMultiplier(EBuffType BuffType) const;

    UPROPERTY(BlueprintAssignable)
        FOnBuffApplied OnBuffApplied;

    UPROPERTY(BlueprintAssignable)
        FOnBuffExpired OnBuffExpired;

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    struct FActiveBuff
    {
        EBuffType Type;
        float Multiplier;
        float TimeRemaining;
    };

    TArray<FActiveBuff> ActiveBuffs;
};