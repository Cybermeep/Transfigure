// Source/TMovement/Public/Transfiguration/SigilActor.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Transfiguration/TransfigurationDefinition.h"
#include "SigilActor.generated.h"

class UDecalComponent;
class USphereComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
FOnSigilTriggered, ASigilActor*, TriggeredSigil);

UCLASS()
class TRANSFIGURE_API ASigilActor : public AActor
{
    GENERATED_BODY()

public:
    ASigilActor();

    UFUNCTION(BlueprintCallable, Category = "Sigil")
        virtual void Initialize(UTransfigurationDefinition* InSpell, AActor* InCaster);

    UFUNCTION(BlueprintCallable, Category = "Sigil")
        void TriggerManual();

    UFUNCTION(BlueprintImplementableEvent, Category = "Sigil|VFX")
        void OnSigilPlaced();

    UFUNCTION(BlueprintImplementableEvent, Category = "Sigil|VFX")
        void OnSigilTriggeredVFX();

    UFUNCTION(BlueprintImplementableEvent, Category = "Sigil|VFX")
        void OnSigilExpired();

    UFUNCTION(BlueprintCallable, Category = "Sigil")
        bool IsArmed() const { return bArmed; }

    UFUNCTION(BlueprintCallable, Category = "Sigil")
        UTransfigurationDefinition* GetSpellDefinition() const { return SpellDef; }

    UPROPERTY(BlueprintAssignable, Category = "Sigil")
        FOnSigilTriggered OnTriggered;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY()
        TObjectPtr<UTransfigurationDefinition> SpellDef;

    UPROPERTY()
        TWeakObjectPtr<AActor> Caster;

    UPROPERTY(VisibleDefaultsOnly)
        TObjectPtr<USphereComponent> TriggerSphere;

    UPROPERTY(VisibleDefaultsOnly)
        TObjectPtr<UDecalComponent> SigilDecal;

    bool bArmed = false;

    void SetupProximityTrigger();
    void ExecuteSpellEffect();
    void OnTimedAutoFire();

    FTimerHandle TTLTimerHandle;
    FTimerHandle AutoFireTimerHandle;

    UFUNCTION()
        void OnProximityOverlap(
            UPrimitiveComponent* OverlappedComp,
            AActor* OtherActor,
            UPrimitiveComponent* OtherComp,
            int32 OtherBodyIndex,
            bool bFromSweep,
            const FHitResult& SweepResult);
};