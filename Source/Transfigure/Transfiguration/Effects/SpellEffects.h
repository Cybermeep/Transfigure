// Source/Transfigure/Transfiguration/Effects/SpellEffects.h
//
// The effect archetypes (SPELL_PLAN A1). Each spell is a data asset that picks one
// of these as its EffectClass and configures it with the Spell|Behaviour fields.
//
//   Area       burst at the sigil (Delay > 0: delayed strike; Duration > 0: aura)
//   Line       a wave of strikes from the caster to the sigil
//   Chain      hits the nearest enemy, then jumps ChainCount times
//   Pull       vortex: drags enemies in for Duration, then collapses
//   Structure  raises a wall / pillar / ramp / spike (solid: wall-run and grapple it)
//   Self       acts on the caster: buff, dash to the sigil, teleport to it, heal

#pragma once

#include "CoreMinimal.h"
#include "Transfiguration/TransfigurationEffectBase.h"
#include "SpellEffects.generated.h"

UCLASS()
class TRANSFIGURE_API USpellEffect_Area : public UTransfigurationEffectBase
{
    GENERATED_BODY()
public:
    virtual void Execute(const FVector& Origin, UTransfigurationDefinition* SpellData, AActor* Instigator, UWorld* World) override;
};

UCLASS()
class TRANSFIGURE_API USpellEffect_Line : public UTransfigurationEffectBase
{
    GENERATED_BODY()
public:
    virtual void Execute(const FVector& Origin, UTransfigurationDefinition* SpellData, AActor* Instigator, UWorld* World) override;
};

UCLASS()
class TRANSFIGURE_API USpellEffect_Chain : public UTransfigurationEffectBase
{
    GENERATED_BODY()
public:
    virtual void Execute(const FVector& Origin, UTransfigurationDefinition* SpellData, AActor* Instigator, UWorld* World) override;
};

UCLASS()
class TRANSFIGURE_API USpellEffect_Pull : public UTransfigurationEffectBase
{
    GENERATED_BODY()
public:
    virtual void Execute(const FVector& Origin, UTransfigurationDefinition* SpellData, AActor* Instigator, UWorld* World) override;
};

UCLASS()
class TRANSFIGURE_API USpellEffect_Structure : public UTransfigurationEffectBase
{
    GENERATED_BODY()
public:
    virtual void Execute(const FVector& Origin, UTransfigurationDefinition* SpellData, AActor* Instigator, UWorld* World) override;
};

UCLASS()
class TRANSFIGURE_API USpellEffect_Self : public UTransfigurationEffectBase
{
    GENERATED_BODY()
public:
    virtual void Execute(const FVector& Origin, UTransfigurationDefinition* SpellData, AActor* Instigator, UWorld* World) override;
};
