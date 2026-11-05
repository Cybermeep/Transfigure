// Source/Transfigure/Transfiguration/SpellTypes.h
//
// Types shared by the spell framework (Docs/SPELL_PLAN.md, section A).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Transfiguration/TransfigurationTypes.h"
#include "SpellTypes.generated.h"

/** Lasting effects a spell (or an infused shot) leaves on an enemy. See UStatusEffectComponent. */
UENUM(BlueprintType)
enum class ESpellStatus : uint8
{
    None        UMETA(DisplayName = "None"),
    Burning     UMETA(DisplayName = "Burning (damage over time)"),
    Chilled     UMETA(DisplayName = "Chilled (slows; freezes at 3 stacks)"),
    Shocked     UMETA(DisplayName = "Shocked (short stun)"),
    Marked      UMETA(DisplayName = "Marked (takes more damage)"),
    Staggered   UMETA(DisplayName = "Staggered (interrupt)"),
    Voided      UMETA(DisplayName = "Voided (weakened, slowed, can't attack)")
};

/** What a Self spell does to the caster. */
UENUM(BlueprintType)
enum class ESpellSelfAction : uint8
{
    None          UMETA(DisplayName = "None"),
    Buff          UMETA(DisplayName = "Buff"),
    DashToSigil   UMETA(DisplayName = "Dash to the sigil"),
    RecallToSigil UMETA(DisplayName = "Teleport to the sigil"),
    Heal          UMETA(DisplayName = "Heal")
};

/** Shape a Structure spell raises. */
UENUM(BlueprintType)
enum class ESpellStructureShape : uint8
{
    Wall    UMETA(DisplayName = "Wall (faces the caster)"),
    Pillar  UMETA(DisplayName = "Pillar"),
    Ramp    UMETA(DisplayName = "Ramp (from the caster to the sigil)"),
    Spike   UMETA(DisplayName = "Spike (shoots up, retracts)")
};

UCLASS()
class TRANSFIGURE_API USpellLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Signature colour of an element (HUD, sigils, effects, damage numbers). */
    UFUNCTION(BlueprintPure, Category = "Spells")
    static FLinearColor GetElementColor(ETransfigurationElement Element);

    UFUNCTION(BlueprintPure, Category = "Spells")
    static FString GetElementName(ETransfigurationElement Element);

    UFUNCTION(BlueprintPure, Category = "Spells")
    static FLinearColor GetStatusColor(ESpellStatus Status);

    UFUNCTION(BlueprintPure, Category = "Spells")
    static FString GetStatusName(ESpellStatus Status);
};
