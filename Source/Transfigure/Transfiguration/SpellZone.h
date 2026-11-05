// Source/Transfigure/Transfiguration/SpellZone.h
//
// Runs spells that play out over time, so the effect object (owned by a sigil that
// disappears half a second after firing) doesn't have to live that long.
//
//   Aura      hits every TickInterval for Duration (optionally one random target per
//             tick, optionally following the caster)
//   Pull      drags enemies to the centre for Duration, then collapses (damage)
//   Line      a wave of strikes travelling from the caster to the sigil
//   Delayed   a single impact after Delay (meteor falls in during the wait)
//   DashBurst follows the caster for a moment, then bursts where they end up

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Transfiguration/SpellHitLibrary.h"
#include "SpellZone.generated.h"

class UTransfigurationDefinition;
class ASpellFX;

UCLASS(NotPlaceable)
class TRANSFIGURE_API ASpellZone : public AActor
{
    GENERATED_BODY()

public:
    ASpellZone();

    enum class EMode : uint8 { Aura, Pull, Line, Delayed, DashBurst };

    static ASpellZone* Start(UWorld* World, EMode Mode, UTransfigurationDefinition* Def, AActor* Instigator,
        const FVector& Origin, const FVector& LineFrom = FVector::ZeroVector, float OverrideDuration = -1.f);

protected:
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void Hit(const FVector& At);
    FVector Center() const;

    UPROPERTY()
    TObjectPtr<UTransfigurationDefinition> Def;

    UPROPERTY()
    TObjectPtr<ASpellFX> Visual;

    TWeakObjectPtr<AActor> Instigator;
    FSpellHitParams Params;
    EMode Mode = EMode::Aura;
    FVector Origin = FVector::ZeroVector;
    FVector LineFrom = FVector::ZeroVector;
    float Duration = 1.f;
    float Age = 0.f;
    float NextTick = 0.f;
    float LineTravelled = 0.f;
    float NextLineStrike = 0.f;
};
