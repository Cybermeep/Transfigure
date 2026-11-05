// Source/Transfigure/Transfiguration/StatusEffectComponent.h
//
// Status effects on an enemy (SPELL_PLAN A3). Added to every ABaseEnemy.
//
//   Burning   damage over time per stack (max 3); fire thaws ice
//   Chilled   slows per stack; at FreezeAtStacks the enemy is Frozen solid (stunned,
//             and its limbs shatter when hit - see AProceduralZombie)
//   Shocked   short stun
//   Marked    takes MarkMultiplier x damage
//   Staggered interrupt (short stun)
//   Voided    takes VoidMultiplier x damage, slowed, can't attack
//
// The glowing parts of the enemy's materials ("GlowColor") take the status colour.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Transfiguration/SpellTypes.h"
#include "StatusEffectComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TRANSFIGURE_API UStatusEffectComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UStatusEffectComponent();

    /** Stacks add up (Burning/Chilled); Duration refreshes to the longer one. */
    UFUNCTION(BlueprintCallable, Category = "Status")
    void ApplyStatus(ESpellStatus Status, float Stacks, float Duration, AActor* Instigator);

    UFUNCTION(BlueprintPure, Category = "Status")
    bool HasStatus(ESpellStatus Status) const;

    UFUNCTION(BlueprintPure, Category = "Status")
    float GetStacks(ESpellStatus Status) const;

    UFUNCTION(BlueprintPure, Category = "Status")
    bool IsFrozen() const { return FrozenTime > 0.f; }

    /** Multiplier on all damage this enemy takes (Marked, Voided). */
    float GetDamageTakenMultiplier() const;

    /** Voided enemies can't attack. */
    bool BlocksAttack() const { return HasStatus(ESpellStatus::Voided); }

    /** Statuses currently on, for the HUD (enemy bars). */
    TArray<ESpellStatus> GetActiveStatuses() const;

    void ClearAll();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Tuning")
    float BurnDamagePerSecond = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Tuning")
    float ChillSlowPerStack = 0.22f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Tuning")
    int32 FreezeAtStacks = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Tuning")
    float FreezeDuration = 2.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Tuning")
    float ShockStun = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Tuning")
    float StaggerStun = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Tuning")
    float MarkMultiplier = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Tuning")
    float VoidMultiplier = 1.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status|Tuning")
    float VoidSlow = 0.6f;

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    struct FActiveStatus
    {
        float Stacks = 0.f;
        float TimeLeft = 0.f;
        TWeakObjectPtr<AActor> Instigator;
    };
    TMap<ESpellStatus, FActiveStatus> Active;
    float FrozenTime = 0.f;
    float BurnTimer = 0.f;

    // Material glow tint.
    void UpdateTint();
    bool bTinted = false;
    bool bHaveOriginalGlow = false;
    FLinearColor OriginalGlow = FLinearColor::Black;
    float OriginalGlowStrength = 4.f;
    ESpellStatus TintedAs = ESpellStatus::None;
};
