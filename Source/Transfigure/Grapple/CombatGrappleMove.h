// Source/TMovement/Public/Grapple/CombatGrappleMove.h

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "GrappleTypes.h"
#include "CombatGrappleMove.generated.h"

class ATMCharacter;
class ABaseEnemy;

UCLASS(BlueprintType, Blueprintable)
class TRANSFIGURE_API UCombatGrappleMove : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatGrapple")
        ECombatGrappleMove MoveType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatGrapple")
        float BaseDamage = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatGrapple")
        float MomentumMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatGrapple")
        float ManaCost = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatGrapple|Elemental")
        TMap<ETransfigurationElement, float> ElementalDamageBonus;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatGrapple|Elemental")
        TMap<ETransfigurationElement, FName> ElementalEffectNames;

    UFUNCTION(BlueprintCallable, Category = "CombatGrapple")
        virtual void ExecuteMove(ATMCharacter* Instigator, ABaseEnemy* Target, ETransfigurationElement Element);

    UFUNCTION(BlueprintImplementableEvent, Category = "CombatGrapple|VFX")
        void OnMoveExecuted(ATMCharacter* Instigator, ABaseEnemy* Target, FVector ImpactPoint);

protected:
    UFUNCTION()
        void ApplyElementalDamage(ABaseEnemy* Target, ETransfigurationElement Element, float InDamage);

    UFUNCTION()
        void ApplyMomentumDamage(ATMCharacter* Instigator, ABaseEnemy* Target, float Speed);
};