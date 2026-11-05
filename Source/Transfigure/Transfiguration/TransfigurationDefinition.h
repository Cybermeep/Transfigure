// Source/TMovement/Public/Transfiguration/TransfigurationDefinition.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TransfigurationTypes.h"
#include "Transfiguration/SpellTypes.h"
#include "Transfiguration/BuffEffectComponent.h"
#include "TransfigurationDefinition.generated.h"

class UTransfigurationEffectBase;
class ASigilActor;
class UNiagaraSystem;
class USoundBase;

UCLASS(BlueprintType)
class TRANSFIGURE_API UTransfigurationDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    // Identity
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Identity")
        FName SpellID;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Identity")
        FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Identity")
        ETransfigurationElement Element = ETransfigurationElement::Fire;

    /** Shown in the spellbook. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Identity", meta = (MultiLine = true))
        FText Description;

    // ── Behaviour (read by the effect archetypes in SpellEffects.h) ─────────────
    /** Full damage within this radius of the sigil (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float ImpactRadius = 300.f;

    /** Damage falls to half at this radius and stops beyond it (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float FalloffRadius = 600.f;

    /** Push enemies away from the centre (cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float Knockback = 0.f;

    /** Throw enemies straight up along ring-up (cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float Launch = 0.f;

    /** If the caster is inside ImpactRadius, launch them up too (cm/s): ride your own spell. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float CasterLaunch = 0.f;

    /** Seconds between triggering and the impact (Meteor). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float Delay = 0.f;

    /** > 0: keeps hitting every TickInterval for this long (auras, storms, pulls). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float Duration = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float TickInterval = 0.5f;

    /** Aura: each tick zaps one random enemy in range instead of everything. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        bool bOneRandomTargetPerTick = false;

    /** Aura: centred on the caster and moves with them (trails, charges). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        bool bFollowCaster = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        ESpellStatus Status = ESpellStatus::None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float StatusStacks = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float StatusDuration = 4.f;

    /** Chain: how many more enemies it jumps to, and how far each jump reaches (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        int32 ChainCount = 4;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float ChainRange = 900.f;

    /** Pull: how hard enemies are dragged to the centre (cm/s^2). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float PullStrength = 2500.f;

    /** Line: gap between strikes, and how fast the wave travels from you to the sigil (cm, cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float LineSpacing = 160.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float LineSpeed = 3000.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        ESpellStructureShape StructureShape = ESpellStructureShape::Wall;

    /** Structure size in cm: width (along the surface), thickness, height. Ramps: width, length unused (spans to you), rise. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        FVector StructureSize = FVector(800.f, 80.f, 500.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float StructureLifetime = 12.f;

    /** Ice: no friction or braking while you stand on it, so it keeps all your speed. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        bool bStructureSlippery = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        ESpellSelfAction SelfAction = ESpellSelfAction::None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        EBuffType SelfBuff = EBuffType::Speed;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float SelfBuffMultiplier = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float SelfBuffDuration = 6.f;

    /** Dash to the sigil at this speed (cm/s). Heal: health restored. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float SelfAmount = 3500.f;

    /** Tears the nearest limb off every zombie it hits (Severing Void). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        bool bSeversNearestLimb = false;

    /** Damage multiplier when the temporal clap sets it off. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float TemporalDamageMultiplier = 1.5f;

    /** Camera shake when it hits (0..1). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Behaviour")
        float ImpactShake = 0.3f;

    /** Combo spell: fires when two sigils of ComboElementA and ComboElementB go off close together.
     *  Not castable on its own (never in the spellbook's castable list). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Combo")
        bool bIsCombo = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Combo", meta = (EditCondition = "bIsCombo"))
        ETransfigurationElement ComboElementA = ETransfigurationElement::None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Combo", meta = (EditCondition = "bIsCombo"))
        ETransfigurationElement ComboElementB = ETransfigurationElement::None;

    /** Alpha 0 = use the element's colour. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Visuals")
        FLinearColor ColorOverride = FLinearColor(0.f, 0.f, 0.f, 0.f);

    FLinearColor GetSpellColor() const
    {
        return ColorOverride.A > 0.f ? ColorOverride : USpellLibrary::GetElementColor(bIsHealingSigil || bIsBuffSigil ? ETransfigurationElement::None : Element);
    }

    // Persistent Sigil Types
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Identity")
        bool bIsPersistentSigil = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Identity",
        meta = (EditCondition = "bIsPersistentSigil"))
        float PersistentDuration = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Identity")
        bool bIsBuffSigil = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Identity")
        bool bIsHealingSigil = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Identity")
        bool bIsTeleportSigil = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Identity",
        meta = (EditCondition = "bIsTeleportSigil"))
        FName LinkedTeleportID;

    // Portal Properties
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Portal")
        bool bIsPortalSigil = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Portal",
        meta = (EditCondition = "bIsPortalSigil"))
        EPortalType PortalType = EPortalType::TwoWay;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Portal",
        meta = (EditCondition = "bIsPortalSigil"))
        EPortalActivation PortalActivation = EPortalActivation::Immediate;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Portal",
        meta = (EditCondition = "bIsPortalSigil"))
        float PortalCooldown = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Portal",
        meta = (EditCondition = "bIsPortalSigil"))
        float PortalActivationDelay = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Portal",
        meta = (EditCondition = "bIsPortalSigil"))
        bool bMaintainsMomentum = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Portal",
        meta = (EditCondition = "bIsPortalSigil"))
        bool bRequiresLineOfSight = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Portal",
        meta = (EditCondition = "bIsPortalSigil"))
        FString AllowedMovementStateForPortal;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Portal")
        float PortalVisualScale = 1.0f;

    // Mana Costs
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Mana",
        meta = (ClampMin = "0", ClampMax = "100"))
        float ManaCost = 25.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Mana")
        bool bCostScalesWithSpeed = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Mana",
        meta = (EditCondition = "bCostScalesWithSpeed"))
        float MaxSpeedManaMultiplier = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Mana")
        bool bRefundOnMiss = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Mana")
        float RefundPercentage = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Mana")
        bool bRequiresManaRegenDelay = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Mana")
        float CustomRegenDelay = -1.0f;

    // Sigil Behavior
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Sigil")
        ESigilTriggerType TriggerType = ESigilTriggerType::Manual;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Sigil",
        meta = (ClampMin = "50.0", ClampMax = "2000.0"))
        float TriggerRadius = 200.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Sigil")
        float TimeToLive = 30.f;

    /** Optional override. Empty uses the native class that fits this spell
     *  (persistent sigil if Is Persistent Sigil, otherwise the plain sigil). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Sigil")
        TSubclassOf<ASigilActor> SigilActorClass;

    // Spell Power
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Power")
        float BaseDamage = 50.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Power")
        float CooldownDuration = 2.f;

    // Movement Synergy
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|MovementSynergy")
        bool bScalesWithSpeed = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|MovementSynergy")
        bool bConservesMomentum = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|MovementSynergy")
        bool bCanCastWhileMoving = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|MovementSynergy")
        TArray<FString> AllowedMovementStates;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|MovementSynergy")
        TMap<FString, FName> MovementAugmentations;

    // Destruction
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Destruction")
        bool bCausesEnvironmentalDestruction = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Destruction")
        float DestructionRadius = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Destruction")
        float DestructionForce = 1000.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Destruction")
        bool bDestructionRequiresTemporalClap = true;

    // Effect Class
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Effect")
        TSubclassOf<UTransfigurationEffectBase> EffectClass;

    // Visuals
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Visuals")
        TObjectPtr<UNiagaraSystem> ImpactFX;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Visuals")
        TObjectPtr<USoundBase> ImpactSound;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Visuals")
        TObjectPtr<UMaterialInterface> SigilDecalMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Visuals",
        meta = (EditCondition = "bScalesWithSpeed"))
        TObjectPtr<UNiagaraSystem> SpeedScaledImpactFX;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Visuals")
        TObjectPtr<UNiagaraSystem> IdleLoopFX;
};