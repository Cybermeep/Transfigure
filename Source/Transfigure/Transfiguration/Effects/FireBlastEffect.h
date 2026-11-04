// Source/TMovement/Public/Transfiguration/Effects/FireBlastEffect.h

#pragma once

#include "Transfiguration/TransfigurationEffectBase.h"
#include "FireBlastEffect.generated.h"

UCLASS()
class TRANSFIGURE_API UFireBlastEffect : public UTransfigurationEffectBase
{
    GENERATED_BODY()

public:
    virtual void Execute(
        const FVector& Origin,
        UTransfigurationDefinition* SpellData,
        AActor* Instigator,
        UWorld* World) override;

    UFUNCTION(BlueprintCallable, Category = "Fire|Movement")
        void SpawnFireTrail(AActor* Instigator, float Duration);

private:
    UPROPERTY()
        TSubclassOf<AActor> FireTrailClass;

    UPROPERTY()
        float FireTrailDamage = 10.f;

    UPROPERTY()
        float FireTrailDuration = 3.f;
};