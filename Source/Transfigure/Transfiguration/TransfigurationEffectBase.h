// Source/TMovement/Public/Transfiguration/TransfigurationEffectBase.h

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TransfigurationDefinition.h"
#include "TransfigurationEffectBase.generated.h"

UCLASS(Abstract, BlueprintType, Blueprintable)
class TRANSFIGURE_API UTransfigurationEffectBase : public UObject
{
    GENERATED_BODY()

public:
    virtual void Execute(
        const FVector& Origin,
        UTransfigurationDefinition* SpellData,
        AActor* Instigator,
        UWorld* World) PURE_VIRTUAL(UTransfigurationEffectBase::Execute, );

    // Momentum conservation helpers
    UFUNCTION(BlueprintCallable, Category = "Transfiguration|Effect")
        virtual float GetScaledDamage(UTransfigurationDefinition* SpellData, AActor* Instigator) const;

    UFUNCTION(BlueprintCallable, Category = "Transfiguration|Effect")
        virtual float GetScaledCooldown(UTransfigurationDefinition* SpellData, AActor* Instigator) const;

    void StoreInstigatorVelocity(AActor* Instigator);
    void RestoreInstigatorVelocity(AActor* Instigator);

    // Destruction integration
    UFUNCTION(BlueprintCallable, Category = "Spell|Destruction")
    virtual void QueueEnvironmentalDestruction(FVector Origin, AActor* Instigator, UTransfigurationDefinition* SpellData);

protected:
    FVector StoredVelocity;
    bool bVelocityStored = false;

    void ApplyRadialDamage(
        UWorld* World,
        AActor* Instigator,
        FVector Origin,
        float Radius,
        float BaseDamage,
        TSubclassOf<UDamageType> DamageTypeClass) const;

    TArray<AActor*> GetActorsInRadius(
        UWorld* World,
        const FVector& Origin,
        float Radius) const;
};