// Source/Transfigure/Transfiguration/Effects/SpellEffects.cpp

#include "Transfiguration/Effects/SpellEffects.h"
#include "Transfiguration/SpellHitLibrary.h"
#include "Transfiguration/SpellFX.h"
#include "Transfiguration/SpellZone.h"
#include "Transfiguration/SpellStructure.h"
#include "Transfiguration/TransfigurationDefinition.h"
#include "Transfiguration/BuffEffectComponent.h"
#include "Character/TMCharacter.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Enemy/BaseEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RingFrameUtils.h"

namespace
{
    FVector FeetOf(const AActor* Actor)
    {
        const ACharacter* C = Cast<ACharacter>(Actor);
        const FVector Up = URingFrameUtils::GetUpAt(Actor->GetActorLocation());
        return Actor->GetActorLocation() - Up * (C ? C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f);
    }
}

// ── Area ────────────────────────────────────────────────────────────────────

void USpellEffect_Area::Execute(const FVector& Origin, UTransfigurationDefinition* Def, AActor* Instigator, UWorld* World)
{
    if (!Def || !World) return;
    if (Def->Delay > 0.f)
    {
        ASpellZone::Start(World, ASpellZone::EMode::Delayed, Def, Instigator, Origin);
        return;
    }
    if (Def->Duration > 0.f)
    {
        ASpellZone::Start(World, ASpellZone::EMode::Aura, Def, Instigator, Def->bFollowCaster && Instigator ? Instigator->GetActorLocation() : Origin);
        return;
    }

    const FSpellHitParams P = SpellHit::FromDefinition(Def, Instigator);
    const int32 N = SpellHit::ApplyArea(World, Instigator, Origin, P);
    ASpellFX::SpawnBurst(World, Origin, P.OuterRadius, P.Color, 0.35f);
    if (Def->Launch > 0.f || Def->CasterLaunch > 0.f)
    {
        ASpellFX::SpawnColumn(World, Origin, URingFrameUtils::GetUpAt(Origin), P.InnerRadius * 0.6f, 1200.f, P.Color, 0.7f);
    }
    UE_LOG(LogTemp, Warning, TEXT("[TM] %s: burst %.0f dmg, r %.0f-%.0f, hit %d"), *Def->SpellID.ToString(), P.Damage, P.InnerRadius, P.OuterRadius, N);
}

// ── Line ────────────────────────────────────────────────────────────────────

void USpellEffect_Line::Execute(const FVector& Origin, UTransfigurationDefinition* Def, AActor* Instigator, UWorld* World)
{
    if (!Def || !World || !Instigator) return;
    // Start a couple of metres in front of the caster so the first strike doesn't hit them.
    const FVector Feet = FeetOf(Instigator);
    const FVector Dir = (Origin - Feet).GetSafeNormal();
    ASpellZone::Start(World, ASpellZone::EMode::Line, Def, Instigator, Origin, Feet + Dir * 200.f);
}

// ── Chain ───────────────────────────────────────────────────────────────────

void USpellEffect_Chain::Execute(const FVector& Origin, UTransfigurationDefinition* Def, AActor* Instigator, UWorld* World)
{
    if (!Def || !World) return;
    const FSpellHitParams P = SpellHit::FromDefinition(Def, Instigator);

    auto Nearest = [&](const FVector& From, float Range, const TSet<ABaseEnemy*>& Skip) -> ABaseEnemy*
    {
        ABaseEnemy* Best = nullptr;
        float BestD = Range;
        for (ABaseEnemy* E : SpellHit::FindEnemies(World, From, Range, Instigator, true))
        {
            const float D = FVector::Dist(From, E->GetActorLocation());
            if (!Skip.Contains(E) && D < BestD)
            {
                BestD = D;
                Best = E;
            }
        }
        return Best;
    };

    TSet<ABaseEnemy*> Hit;
    FVector From = Origin;
    float Scale = 1.f;
    ABaseEnemy* Current = Nearest(Origin, FMath::Max(P.OuterRadius, Def->ChainRange), Hit);
    for (int32 i = 0; i <= Def->ChainCount && Current; ++i)
    {
        const FVector To = Current->GetActorLocation();
        ASpellFX::SpawnBeam(World, From, To, P.Color, 14.f, 0.3f);
        ASpellFX::SpawnBurst(World, To, 90.f, P.Color, 0.2f);
        SpellHit::ApplySingle(Instigator, Current, To, P, Scale);
        Hit.Add(Current);
        From = To;
        Scale *= 0.85f;
        Current = Nearest(From, Def->ChainRange, Hit);
    }
    ASpellFX::SpawnBurst(World, Origin, 140.f, P.Color, 0.25f);
    if (Hit.Num() > 0 && P.Shake > 0.f)
    {
        if (UCombatFeedbackSubsystem* F = UCombatFeedbackSubsystem::Get(World)) F->AddShake(P.Shake);
    }
    UE_LOG(LogTemp, Warning, TEXT("[TM] %s: chained through %d enemies"), *Def->SpellID.ToString(), Hit.Num());
}

// ── Pull ────────────────────────────────────────────────────────────────────

void USpellEffect_Pull::Execute(const FVector& Origin, UTransfigurationDefinition* Def, AActor* Instigator, UWorld* World)
{
    if (!Def || !World) return;
    ASpellZone::Start(World, ASpellZone::EMode::Pull, Def, Instigator, Origin, FVector::ZeroVector, FMath::Max(Def->Duration, 1.5f));
}

// ── Structure ───────────────────────────────────────────────────────────────

void USpellEffect_Structure::Execute(const FVector& Origin, UTransfigurationDefinition* Def, AActor* Instigator, UWorld* World)
{
    if (!Def || !World) return;
    const FVector Up = URingFrameUtils::GetUpAt(Origin);
    FVector ToCaster = Instigator ? URingFrameUtils::StripVertical(Instigator->GetActorLocation() - Origin, Up) : FVector::ZeroVector;
    if (ToCaster.IsNearlyZero()) ToCaster = FVector::CrossProduct(Up, FVector::RightVector);
    ToCaster.Normalize();

    FVector Location = Origin;
    FRotator Rotation;
    FVector Size = Def->StructureSize;
    switch (Def->StructureShape)
    {
    case ESpellStructureShape::Wall:
        // Width runs across your view, thickness faces you.
        Rotation = FRotationMatrix::MakeFromXZ(FVector::CrossProduct(Up, ToCaster), Up).Rotator();
        break;
    case ESpellStructureShape::Ramp:
    {
        // From just ahead of you up to the sigil, rising by StructureSize.Z.
        const FVector Start = Instigator ? FeetOf(Instigator) - ToCaster * 150.f : Origin + ToCaster * 600.f;
        const FVector Flat = URingFrameUtils::StripVertical(Origin - Start, Up);
        const float Len = FMath::Max(Flat.Size() + 200.f, 400.f);
        const FVector Dir = Flat.GetSafeNormal();
        const float Rise = FMath::Max(Def->StructureSize.Z, 10.f);
        const float Pitch = FMath::Atan2(Rise, Len);
        const FVector RampX = Dir * FMath::Cos(Pitch) + Up * FMath::Sin(Pitch);
        const FVector RampZ = Up * FMath::Cos(Pitch) - Dir * FMath::Sin(Pitch);
        Location = Start + Dir * (Len * 0.5f) + Up * (Rise * 0.5f) - RampZ * 40.f;
        Rotation = FRotationMatrix::MakeFromXZ(RampX, RampZ).Rotator();
        Size = FVector(FMath::Sqrt(Len * Len + Rise * Rise), FMath::Max(Def->StructureSize.X, 200.f), 40.f);
        break;
    }
    default:   // Pillar, Spike
        Rotation = FRotationMatrix::MakeFromZX(Up, ToCaster).Rotator();
        break;
    }

    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (ASpellStructure* S = World->SpawnActor<ASpellStructure>(ASpellStructure::StaticClass(), Location, Rotation, SP))
    {
        const bool bSpike = Def->StructureShape == ESpellStructureShape::Spike;
        S->Init(Def->StructureShape, Size, Def->GetSpellColor() * 0.8f, bSpike ? FMath::Min(Def->StructureLifetime, 1.5f) : Def->StructureLifetime,
            Def->bStructureSlippery);
    }

    // Rising out of the ground hits whatever stands there (spikes launch, walls shove).
    const FSpellHitParams P = SpellHit::FromDefinition(Def, Instigator);
    if (P.Damage > 0.f || P.Knockback > 0.f || P.Launch > 0.f || P.Status != ESpellStatus::None)
    {
        SpellHit::ApplyArea(World, Instigator, Origin, P);
    }
    ASpellFX::SpawnDisc(World, Origin, Up, FMath::Max(Def->StructureSize.X, 200.f) * 0.6f, P.Color, 0.4f);
    UE_LOG(LogTemp, Warning, TEXT("[TM] %s: raised a %s"), *Def->SpellID.ToString(), *UEnum::GetValueAsString(Def->StructureShape));
}

// ── Self ────────────────────────────────────────────────────────────────────

void USpellEffect_Self::Execute(const FVector& Origin, UTransfigurationDefinition* Def, AActor* Instigator, UWorld* World)
{
    ATMCharacter* Char = Cast<ATMCharacter>(Instigator);
    if (!Def || !World || !Char) return;
    const FLinearColor Color = Def->GetSpellColor();
    const FVector Up = URingFrameUtils::GetUpFor(Char);
    UCombatFeedbackSubsystem* Feedback = UCombatFeedbackSubsystem::Get(World);

    switch (Def->SelfAction)
    {
    case ESpellSelfAction::Buff:
        if (Def->SelfBuff == EBuffType::Speed) Char->ApplySpeedBuff(Def->SelfBuffMultiplier, Def->SelfBuffDuration);
        else if (Char->BuffEffects) Char->BuffEffects->ApplyBuff(Def->SelfBuff, Def->SelfBuffMultiplier, Def->SelfBuffDuration);
        ASpellFX::SpawnBurst(World, Char->GetActorLocation(), 160.f, Color, 0.4f);
        if (Feedback) Feedback->AddToast(FString::Printf(TEXT("%s  %.0fs"), *Def->DisplayName.ToString(), Def->SelfBuffDuration), Color);
        break;

    case ESpellSelfAction::DashToSigil:
    {
        // Burst where you leave, a fast dash keeping your speed, burst where you arrive.
        const FSpellHitParams P = SpellHit::FromDefinition(Def, Char);
        SpellHit::ApplyArea(World, Char, Char->GetActorLocation(), P);
        ASpellFX::SpawnBurst(World, Char->GetActorLocation(), P.OuterRadius, Color, 0.3f);
        const FVector To = Origin + Up * 120.f;
        const FVector Dir = (To - Char->GetActorLocation()).GetSafeNormal();
        const float Keep = FVector::DotProduct(Char->GetCharacterMovement()->Velocity, Dir);
        Char->LaunchCharacter(Dir * FMath::Max(Def->SelfAmount, Keep) + Up * 150.f, true, true);
        ASpellFX::SpawnBeam(World, Char->GetActorLocation(), To, Color, 22.f, 0.35f);
        ASpellZone::Start(World, ASpellZone::EMode::DashBurst, Def, Char, To,
            FVector::ZeroVector, FMath::Clamp(FVector::Dist(To, Char->GetActorLocation()) / FMath::Max(Def->SelfAmount, 500.f), 0.15f, 0.8f));
        break;
    }

    case ESpellSelfAction::RecallToSigil:
        ASpellFX::SpawnBurst(World, Char->GetActorLocation(), 180.f, Color, 0.4f);
        Char->TeleportTo(Origin + Up * (Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.f), Char->GetActorRotation(), false, true);
        ASpellFX::SpawnBurst(World, Char->GetActorLocation(), 180.f, Color, 0.4f);
        break;

    case ESpellSelfAction::Heal:
        Char->SetHealth(FMath::Min(Char->GetHealth() + Def->SelfAmount, Char->GetMaxHealth()));
        ASpellFX::SpawnBurst(World, Char->GetActorLocation(), 200.f, Color, 0.5f);
        if (Feedback) Feedback->AddToast(FString::Printf(TEXT("+%.0f health"), Def->SelfAmount), Color);
        break;

    default:
        break;
    }
    UE_LOG(LogTemp, Warning, TEXT("[TM] %s: self %s"), *Def->SpellID.ToString(), *UEnum::GetValueAsString(Def->SelfAction));
}
