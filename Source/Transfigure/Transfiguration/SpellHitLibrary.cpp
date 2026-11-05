// Source/Transfigure/Transfiguration/SpellHitLibrary.cpp

#include "Transfiguration/SpellHitLibrary.h"
#include "Transfiguration/TransfigurationDefinition.h"
#include "Transfiguration/StatusEffectComponent.h"
#include "Enemy/BaseEnemy.h"
#include "Limb/ProceduralZombie.h"
#include "Character/TMCharacter.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Transfigure.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "RingFrameUtils.h"

static float GSpellDamageScale = 1.f;
static float GSpellRadiusScale = 1.f;

SpellHit::FScaleScope::FScaleScope(float DamageScale, float RadiusScale)
    : PrevDamage(GSpellDamageScale), PrevRadius(GSpellRadiusScale)
{
    GSpellDamageScale *= DamageScale;
    GSpellRadiusScale *= RadiusScale;
}

SpellHit::FScaleScope::~FScaleScope()
{
    GSpellDamageScale = PrevDamage;
    GSpellRadiusScale = PrevRadius;
}

FSpellHitParams SpellHit::FromDefinition(const UTransfigurationDefinition* Def, AActor* Instigator)
{
    FSpellHitParams P;
    if (!Def) return P;
    P.Damage = Def->BaseDamage;
    if (const ATMCharacter* Char = Cast<ATMCharacter>(Instigator))
    {
        P.Damage *= Char->GetDamageMultiplier();
        if (Def->bScalesWithSpeed) P.Damage *= Char->GetSpeedScalingFactor();
        if (Char->bIsTemporalActive) P.Damage *= Def->TemporalDamageMultiplier;
    }
    P.InnerRadius = Def->ImpactRadius;
    P.OuterRadius = FMath::Max(Def->FalloffRadius, Def->ImpactRadius);
    P.Element = Def->Element;
    P.Knockback = Def->Knockback;
    P.Launch = Def->Launch;
    P.Status = Def->Status;
    P.StatusStacks = Def->StatusStacks;
    P.StatusDuration = Def->StatusDuration;
    P.bSeverNearest = Def->bSeversNearestLimb;
    P.CasterLaunch = Def->CasterLaunch;
    P.Shake = Def->ImpactShake;
    P.Color = Def->GetSpellColor();
    P.Damage *= GSpellDamageScale;
    P.InnerRadius *= GSpellRadiusScale;
    P.OuterRadius *= GSpellRadiusScale;
    return P;
}

TArray<ABaseEnemy*> SpellHit::FindEnemies(UWorld* World, const FVector& Origin, float Radius, const AActor* Ignore, bool bAliveOnly)
{
    TArray<ABaseEnemy*> Out;
    if (!World || Radius <= 0.f) return Out;
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SpellHit), false, Ignore);
    // Weapon channel: finds enemy capsules and zombie hit zones, including on corpses.
    World->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Weapon, FCollisionShape::MakeSphere(Radius), Params);
    for (const FOverlapResult& O : Overlaps)
    {
        ABaseEnemy* Enemy = Cast<ABaseEnemy>(O.GetActor());
        if (!Enemy || (bAliveOnly && !Enemy->IsAlive())) continue;
        Out.AddUnique(Enemy);
    }
    return Out;
}

void SpellHit::LaunchUp(ACharacter* Character, float Speed)
{
    if (!Character || Speed <= 0.f) return;
    const FVector Up = URingFrameUtils::GetUpFor(Character);
    const FVector V = URingFrameUtils::SetVertical(Character->GetCharacterMovement()->Velocity, Up, Speed);
    Character->LaunchCharacter(V, true, true);
}

static void Knock(ABaseEnemy* Enemy, const FVector& Origin, float Knockback, float Launch, float Scale)
{
    if (!Enemy || !Enemy->IsAlive() || (Knockback <= 0.f && Launch <= 0.f)) return;
    const FVector Up = URingFrameUtils::GetUpFor(Enemy);
    const FVector Away = URingFrameUtils::StripVertical(Enemy->GetActorLocation() - Origin, Up).GetSafeNormal();
    const FVector V = Away * Knockback * Scale + Up * (Launch * Scale + (Knockback > 0.f ? 250.f : 0.f));
    Enemy->LaunchCharacter(V, true, true);
}

static void ApplyStatusTo(ABaseEnemy* Enemy, const FSpellHitParams& P, AActor* Instigator)
{
    if (P.Status != ESpellStatus::None && Enemy && Enemy->IsAlive() && Enemy->Status)
    {
        Enemy->Status->ApplyStatus(P.Status, P.StatusStacks, P.StatusDuration, Instigator);
    }
}

int32 SpellHit::ApplyArea(UWorld* World, AActor* Instigator, const FVector& Origin, const FSpellHitParams& P, TArray<ABaseEnemy*>* OutHit)
{
    if (!World) return 0;
    const TArray<ABaseEnemy*> Enemies = FindEnemies(World, Origin, P.OuterRadius, Instigator, false);
    for (ABaseEnemy* Enemy : Enemies)
    {
        if (P.Damage > 0.f)
        {
            Enemy->TakeSpellAreaDamage(P.Damage, Origin, P.InnerRadius, P.OuterRadius, P.Element);
        }
        if (P.bSeverNearest)
        {
            if (AProceduralZombie* Z = Cast<AProceduralZombie>(Enemy)) Z->SeverNearestLimb(Origin);
        }
        const float Scale = ABaseEnemy::AreaFalloff(FVector::Dist(Enemy->GetActorLocation(), Origin), P.InnerRadius, P.OuterRadius);
        ApplyStatusTo(Enemy, P, Instigator);
        Knock(Enemy, Origin, P.Knockback, P.Launch, FMath::Max(Scale, 0.5f));
        if (OutHit) OutHit->Add(Enemy);
    }

    PushLoose(World, Origin, P.OuterRadius, FMath::Max(P.Knockback + P.Launch, 600.f));

    // Ride your own spell (Flame Pillar, Leap).
    if (ACharacter* Caster = Cast<ACharacter>(Instigator))
    {
        if (P.CasterLaunch > 0.f && FVector::Dist(Caster->GetActorLocation(), Origin) <= P.InnerRadius + 120.f)
        {
            LaunchUp(Caster, P.CasterLaunch);
        }
        if (P.Shake > 0.f && Caster->IsPlayerControlled())
        {
            const float Near = FMath::GetMappedRangeValueClamped(FVector2D(500.f, 4000.f), FVector2D(1.f, 0.25f),
                FVector::Dist(Caster->GetActorLocation(), Origin));
            if (UCombatFeedbackSubsystem* Feedback = UCombatFeedbackSubsystem::Get(World)) Feedback->AddShake(P.Shake * Near);
        }
    }
    return Enemies.Num();
}

void SpellHit::ApplySingle(AActor* Instigator, ABaseEnemy* Target, const FVector& HitLocation, const FSpellHitParams& P, float DamageScale)
{
    if (!Target) return;
    if (P.Damage > 0.f) Target->TakeSpellDamage(P.Damage * DamageScale, HitLocation, P.Element);
    if (P.bSeverNearest)
    {
        if (AProceduralZombie* Z = Cast<AProceduralZombie>(Target)) Z->SeverNearestLimb(HitLocation);
    }
    ApplyStatusTo(Target, P, Instigator);
    Knock(Target, HitLocation, P.Knockback, P.Launch, 1.f);
}

void SpellHit::PushLoose(UWorld* World, const FVector& Origin, float Radius, float Strength)
{
    if (!World || Radius <= 0.f || FMath::IsNearlyZero(Strength)) return;   // negative = pull inward
    TArray<FOverlapResult> Overlaps;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
    World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius));
    TSet<UPrimitiveComponent*> Done;
    for (const FOverlapResult& O : Overlaps)
    {
        UPrimitiveComponent* Comp = O.GetComponent();
        // Severed limbs simulate only below their cut bone, so a skeletal mesh may report "not
        // simulating" for its root: push those anyway (kinematic bodies just ignore it).
        if (!Comp || Done.Contains(Comp) || (!Comp->IsSimulatingPhysics() && !Comp->IsA<USkeletalMeshComponent>())) continue;
        Done.Add(Comp);
        Comp->AddRadialImpulse(Origin, Radius, Strength, ERadialImpulseFalloff::RIF_Linear, /*bVelChange=*/true);
    }
}
