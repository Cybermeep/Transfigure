// Source/TMovement/Public/Transfiguration/Effects/IceWallEffect.h

#pragma once

#include "Transfiguration/TransfigurationEffectBase.h"
#include "IceWallEffect.generated.h"

UCLASS()
class TRANSFIGURE_API UIceWallEffect : public UTransfigurationEffectBase
{
    GENERATED_BODY()

public:
    virtual void Execute(
        const FVector& Origin,
        UTransfigurationDefinition* SpellData,
        AActor* Instigator,
        UWorld* World) override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ice|Wall")
        TSubclassOf<AActor> IceWallClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ice|Wall")
        TSubclassOf<AActor> IceTrailClass;

private:
    void SpawnIceTrail(AActor* Instigator, float Duration);
    void CreateIceWall(const FVector& Origin, AActor* Instigator, UTransfigurationDefinition* SpellData);
};