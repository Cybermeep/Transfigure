// Source/TMovement/Public/Grapple/ElementalGrappleEffect.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Transfiguration/TransfigurationTypes.h"
#include "ElementalGrappleEffect.generated.h"

class UNiagaraComponent;
class UBoxComponent;

UCLASS()
class TRANSFIGURE_API AElementalGrappleEffect : public AActor
{
    GENERATED_BODY()

public:
    AElementalGrappleEffect();

    UFUNCTION(BlueprintCallable, Category = "ElementalGrapple")
        void InitializeEffect(ETransfigurationElement Element, FVector StartLocation, FVector EndLocation, float Duration);

    UFUNCTION(BlueprintCallable, Category = "ElementalGrapple")
        void ApplyEffectToEnemy(AActor* Enemy);

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
        TObjectPtr<UNiagaraComponent> EffectParticle;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
        TObjectPtr<UBoxComponent> DamageVolume;

    UPROPERTY()
        ETransfigurationElement CurrentElement;

    UPROPERTY()
        float EffectDuration;

    UPROPERTY()
        float DamageInterval = 0.5f;

    UPROPERTY()
        FTimerHandle DamageTimerHandle;

    UFUNCTION()
        void ApplyDamageTick();

    UFUNCTION()
        void OnDamageVolumeOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
            UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
            const FHitResult& SweepResult);
};