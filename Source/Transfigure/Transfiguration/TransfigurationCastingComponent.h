// Source/TMovement/Public/Transfiguration/TransfigurationCastingComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TransfigurationTypes.h"
#include "TransfigurationDefinition.h"
#include "SigilActor.h"
#include "TransfigurationCastingComponent.generated.h"

class UTransfigurationRegistry;
class APersistentSigilActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCastingStateChanged, ECastingState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSigilTemporalActivate, ASigilActor*, Sigil);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TRANSFIGURE_API UTransfigurationCastingComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UTransfigurationCastingComponent();

    // Core casting
    UFUNCTION(BlueprintCallable, Category = "Casting")
        void BeginDrawMode(FName SpellID);

    UFUNCTION(BlueprintCallable, Category = "Casting")
        void ConfirmPlacement();

    UFUNCTION(BlueprintCallable, Category = "Casting")
        void CancelDrawMode();

    UFUNCTION(BlueprintCallable, Category = "Casting")
        void TriggerNearestManualSigil();

    UFUNCTION(BlueprintCallable, Category = "Casting")
        void TriggerAllSigils();

    // State queries
    UFUNCTION(BlueprintCallable, Category = "Casting")
        ECastingState GetCastingState() const { return CurrentState; }

    UFUNCTION(BlueprintCallable, Category = "Casting")
        bool IsInDrawMode() const { return CurrentState != ECastingState::Idle; }

    UFUNCTION(BlueprintCallable, Category = "Casting")
        int32 GetActiveSigilCount() const { return ActiveSigils.Num(); }

    // Mana integration
    UFUNCTION(BlueprintCallable, Category = "Casting|Mana")
        bool CanAffordSpell(FName SpellID) const;

    UFUNCTION(BlueprintCallable, Category = "Casting|Mana")
        float GetSpellManaCost(FName SpellID) const;

    // Temporal integration
    UFUNCTION(BlueprintCallable, Category = "Casting|Temporal")
        void PrepareSigilsForTemporalClap();

    UFUNCTION(BlueprintCallable, Category = "Casting|Temporal")
        void TriggerSigilsSynchronized();

    // Persistent sigils
    UFUNCTION(BlueprintCallable, Category = "Casting")
        void ConvertToPersistentSigil(ASigilActor* Sigil, float Duration);

    UFUNCTION(BlueprintCallable, Category = "Casting")
        TArray<APersistentSigilActor*> GetActivePersistentSigils() const;

    // Configuration
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Casting")
        float MaxPlacementDistance = 600.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Casting")
        int32 MaxActiveSigils = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Casting")
        float ManualTriggerScanRadius = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Casting|Temporal")
        float TemporalSigilActivationDelay = 0.2f;

    // Delegates
    UPROPERTY(BlueprintAssignable)
        FOnCastingStateChanged OnCastingStateChanged;

    UPROPERTY(BlueprintAssignable)
        FOnCastingStateChanged OnAimUpdated;

    UPROPERTY(BlueprintAssignable)
        FOnSigilTemporalActivate OnSigilTemporalActivate;

    // Blueprint implementable events
    UFUNCTION(BlueprintImplementableEvent, Category = "Casting|VFX")
        void OnDrawModeEntered(FName SpellID);

    UFUNCTION(BlueprintImplementableEvent, Category = "Casting|VFX")
        void OnPlacementConfirmed(ASigilActor* PlacedSigil);

    UFUNCTION(BlueprintImplementableEvent, Category = "Casting|VFX")
        void OnDrawModeCancelled();

    UPROPERTY(BlueprintReadOnly, Category = "Casting|Mana")
        float LastSpellManaCost = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Casting|Temporal")
        TArray<ASigilActor*> PendingTemporalSigils;

    UPROPERTY()
        TArray<TObjectPtr<APersistentSigilActor>> PersistentSigils;

    UPROPERTY()
        TArray<TObjectPtr<ASigilActor>> ActiveSigils;

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    ECastingState CurrentState = ECastingState::Idle;
    FName ActiveSpellID;
    FHitResult CurrentAimHit;

    void TransitionToState(ECastingState NewState);
    bool PerformAimTrace(FHitResult& OutHit) const;
    void CleanupDestroyedSigils();
    UTransfigurationRegistry* GetSpellRegistry() const;
    void NotifySpellHitResult(bool bHitEnemy);
};