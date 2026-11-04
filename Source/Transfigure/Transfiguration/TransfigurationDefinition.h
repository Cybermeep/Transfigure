// Source/TMovement/Public/Transfiguration/TransfigurationDefinition.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TransfigurationTypes.h"
#include "TransfigurationDefinition.generated.h"

class UTransfigurationEffectBase;
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