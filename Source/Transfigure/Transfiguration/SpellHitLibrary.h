// Source/Transfigure/Transfiguration/SpellHitLibrary.h
//
// The one way spells hurt things (SPELL_PLAN A2). Every effect archetype goes
// through here, so damage, resistances, status, knockback along ring-up, loose
// limbs and feedback behave the same for every spell.

#pragma once

#include "CoreMinimal.h"
#include "Transfiguration/SpellTypes.h"

class ABaseEnemy;
class UTransfigurationDefinition;

struct TRANSFIGURE_API FSpellHitParams
{
    float Damage = 0.f;
    float InnerRadius = 300.f;
    float OuterRadius = 600.f;
    ETransfigurationElement Element = ETransfigurationElement::None;
    float Knockback = 0.f;
    float Launch = 0.f;
    ESpellStatus Status = ESpellStatus::None;
    float StatusStacks = 1.f;
    float StatusDuration = 4.f;
    bool bSeverNearest = false;
    float CasterLaunch = 0.f;
    float Shake = 0.f;
    FLinearColor Color = FLinearColor::White;
};

namespace SpellHit
{
    /** Damage (with the caster's damage buff, speed scaling and the temporal clap bonus) and everything else from a spell. */
    TRANSFIGURE_API FSpellHitParams FromDefinition(const UTransfigurationDefinition* Def, AActor* Instigator);

    /** Every enemy (and corpse) in OuterRadius: falloff damage, status, knockback; loose limbs pushed. Returns enemies hit. */
    TRANSFIGURE_API int32 ApplyArea(UWorld* World, AActor* Instigator, const FVector& Origin, const FSpellHitParams& P,
        TArray<ABaseEnemy*>* OutHit = nullptr);

    /** One enemy, full damage (times DamageScale). */
    TRANSFIGURE_API void ApplySingle(AActor* Instigator, ABaseEnemy* Target, const FVector& HitLocation, const FSpellHitParams& P,
        float DamageScale = 1.f);

    /** Enemies whose capsule or hit zones are within Radius. */
    TRANSFIGURE_API TArray<ABaseEnemy*> FindEnemies(UWorld* World, const FVector& Origin, float Radius, const AActor* Ignore, bool bAliveOnly);

    /** Physics push for severed limbs and ragdoll corpses. */
    TRANSFIGURE_API void PushLoose(UWorld* World, const FVector& Origin, float Radius, float Strength);

    /** While one of these is alive, every spell's damage and radius are multiplied (Overcharge / Amplify). */
    struct TRANSFIGURE_API FScaleScope
    {
        FScaleScope(float DamageScale, float RadiusScale);
        ~FScaleScope();
    private:
        float PrevDamage, PrevRadius;
    };

    /** Launch a character along ring-up (keeps its sideways speed). */
    TRANSFIGURE_API void LaunchUp(ACharacter* Character, float Speed);
}
