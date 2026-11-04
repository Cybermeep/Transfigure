// Source/TMovement/Public/Transfiguration/PersistentSigilActor.h

#pragma once

#include "CoreMinimal.h"
#include "Transfiguration/SigilActor.h"
#include "PersistentSigilActor.generated.h"

class UBuffEffectComponent;

UCLASS()
class TRANSFIGURE_API APersistentSigilActor : public ASigilActor
{
    GENERATED_BODY()

public:
    APersistentSigilActor();

    virtual void Initialize(UTransfigurationDefinition* InSpell, AActor* InCaster) override;
    virtual void Tick(float DeltaTime) override;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
        TObjectPtr<UBuffEffectComponent> BuffComponent;

    UPROPERTY()
        TArray<AActor*> AffectedActors;

    UPROPERTY()
        FTimerHandle HealingTimerHandle;

    UFUNCTION()
        void ApplyHealingToOverlappingActors();

    UFUNCTION()
        void ApplyBuffToOverlappingActors();

    UFUNCTION()
        void OnTriggerVolumeOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
            UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
            const FHitResult& SweepResult);

    UFUNCTION()
        void OnTriggerVolumeEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
            UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    bool bIsHealingSigil;
    bool bIsBuffSigil;
    float HealRate;
    float BuffMultiplier;
    float BuffDuration;
};