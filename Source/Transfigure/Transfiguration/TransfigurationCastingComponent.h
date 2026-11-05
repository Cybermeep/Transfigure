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

    /** One press: start the spell and place it at the crosshair immediately. Returns true if placed. */
    UFUNCTION(BlueprintCallable, Category = "Casting")
        bool CastAtAim(FName SpellID);

    /** The spells on the wheel (equipped slots, in order). Falls back to every known spell. */
    UFUNCTION(BlueprintCallable, Category = "Casting")
        TArray<FName> GetAvailableSpellIDs() const;

    // ── Spellbook (SPELL_PLAN C1) ──
    /** Spells you know. With bAllSpellsKnownForTesting the whole registry is known. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Casting|Spellbook")
        TArray<FName> KnownSpells;

    /** The spell wheel: WheelSlots entries, None = empty slot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Casting|Spellbook")
        TArray<FName> EquippedSpells;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Casting|Spellbook")
        bool bAllSpellsKnownForTesting = true;

    static constexpr int32 WheelSlots = 8;

    /** Known spells, sorted by element then name (spellbook order). */
    UFUNCTION(BlueprintCallable, Category = "Casting|Spellbook")
        TArray<FName> GetKnownSpellIDs() const;

    /** Put a known spell in a wheel slot (moves it if it's already in another slot). None clears the slot. */
    UFUNCTION(BlueprintCallable, Category = "Casting|Spellbook")
        void EquipSpell(int32 Slot, FName SpellID);

    /** Learn a spell (fills a free wheel slot). False if unknown to the registry or already known. */
    UFUNCTION(BlueprintCallable, Category = "Casting|Spellbook")
        bool LearnSpell(FName SpellID);

    UFUNCTION(BlueprintPure, Category = "Casting")
        float GetCooldownRemaining(FName SpellID) const;

    UFUNCTION(BlueprintPure, Category = "Casting")
        UTransfigurationDefinition* GetSpellDefinition(FName SpellID) const;

    /** Place a spell's sigil at an exact spot (no aim trace). Same rules as CastAtAim (mana, cooldown). */
    UFUNCTION(BlueprintCallable, Category = "Casting")
        bool CastAtLocation(FName SpellID, FVector Location, FVector SurfaceNormal);

    /** Testing: forget all cooldowns. */
    void ClearCooldowns() { CooldownReadyAt.Reset(); }

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
        float MaxPlacementDistance = 1500.f;

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
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    /** Real time each spell is ready again (real time, so the temporal clap's slow-mo doesn't stretch it). */
    TMap<FName, double> CooldownReadyAt;
    double NowSeconds() const;
    void Toast(const FString& Text, const FLinearColor& Color) const;

    ECastingState CurrentState = ECastingState::Idle;
    FName ActiveSpellID;
    FHitResult CurrentAimHit;

    void TransitionToState(ECastingState NewState);
    bool PerformAimTrace(FHitResult& OutHit) const;
    void CleanupDestroyedSigils();
    UTransfigurationRegistry* GetSpellRegistry() const;
    void NotifySpellHitResult(bool bHitEnemy);
};