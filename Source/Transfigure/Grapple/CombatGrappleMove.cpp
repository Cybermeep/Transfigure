// Source/TMovement/Private/Grapple/CombatGrappleMove.cpp

#include "Grapple/CombatGrappleMove.h"
#include "Character/TMCharacter.h"
#include "Enemy/BaseEnemy.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UCombatGrappleMove::ExecuteMove(ATMCharacter* Instigator, ABaseEnemy* Target, ETransfigurationElement Element)
{
    if (!Instigator || !Target) return;

    // Calculate damage with elemental bonus
    float TotalDamage = BaseDamage;
    if (ElementalDamageBonus.Contains(Element))
    {
        TotalDamage *= ElementalDamageBonus[Element];
    }

    // Apply momentum-based bonus
    float Speed = Instigator->GetCharacterMovement()->Velocity.Size();
    ApplyMomentumDamage(Instigator, Target, Speed);

    // Apply elemental effect
    ApplyElementalDamage(Target, Element, TotalDamage);

    // Broadcast event for VFX
    OnMoveExecuted(Instigator, Target, Target->GetActorLocation());

    UE_LOG(LogTemp, Verbose, TEXT("CombatGrappleMove: Executed %s on %s for %f damage"),
        *UEnum::GetValueAsString(MoveType), *Target->GetName(), TotalDamage);
}

void UCombatGrappleMove::ApplyElementalDamage(ABaseEnemy* Target, ETransfigurationElement Element, float InDamage)
{
    if (!Target) return;

    // Apply base damage first using the passed InDamage (not BaseDamage directly)
    Target->TakePoolDamage(InDamage, Target->GetActorLocation());

    UWorld* World = Target->GetWorld();
    if (!World) return;

    UCharacterMovementComponent* CMC =
        Target->FindComponentByClass<UCharacterMovementComponent>();

    switch (Element)
    {
    case ETransfigurationElement::Fire:
    {
        // Burning DOT — deal InDamage * 0.2 every 0.5s for 3 ticks
        float DotDamage = InDamage * 0.2f;
        FVector ImpactLoc = Target->GetActorLocation();

        for (int32 i = 1; i <= 3; i++)
        {
            FTimerHandle DotTimer;
            FTimerDelegate DotDelegate;
            TWeakObjectPtr<ABaseEnemy> WeakTarget(Target);
            DotDelegate.BindLambda([WeakTarget, DotDamage, ImpactLoc]()
                {
                    if (WeakTarget.IsValid())
                        WeakTarget->TakePoolDamage(DotDamage, ImpactLoc);
                });
            World->GetTimerManager().SetTimer(DotTimer, DotDelegate, 0.5f * i, false);
        }
        break;
    }

    case ETransfigurationElement::Ice:
    {
        // Slow — reduce movement speed by 60% for 2 seconds
        if (CMC)
        {
            float OriginalSpeed = CMC->MaxWalkSpeed;
            CMC->MaxWalkSpeed = OriginalSpeed * 0.4f;

            FTimerHandle SlowTimer;
            FTimerDelegate RestoreDelegate;
            RestoreDelegate.BindLambda([CMC, OriginalSpeed]()
                {
                    if (CMC) CMC->MaxWalkSpeed = OriginalSpeed;
                });
            World->GetTimerManager().SetTimer(SlowTimer, RestoreDelegate, 2.f, false);
        }
        break;
    }

    case ETransfigurationElement::Lightning:
    {
        // Stun — disable movement entirely for 0.75 seconds
        if (CMC)
        {
            CMC->DisableMovement();

            FTimerHandle StunTimer;
            FTimerDelegate StunDelegate;
            StunDelegate.BindLambda([CMC]()
                {
                    if (CMC) CMC->SetMovementMode(MOVE_Walking);
                });
            World->GetTimerManager().SetTimer(StunTimer, StunDelegate, 0.75f, false);
        }

        // Chain to nearby enemies for 30% of damage
        TArray<FOverlapResult> Overlaps;
        FCollisionShape Sphere = FCollisionShape::MakeSphere(300.f);
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(Target);

        World->OverlapMultiByChannel(
            Overlaps,
            Target->GetActorLocation(),
            FQuat::Identity,
            ECC_Pawn,
            Sphere,
            Params);

        float ChainDamage = InDamage * 0.3f;
        for (const FOverlapResult& Overlap : Overlaps)
        {
            ABaseEnemy* ChainTarget = Cast<ABaseEnemy>(Overlap.GetActor());
            if (ChainTarget && ChainTarget != Target)
            {
                ChainTarget->TakePoolDamage(ChainDamage, ChainTarget->GetActorLocation());
            }
        }
        break;
    }

    case ETransfigurationElement::Void:
    {
        // Pull nearby enemies toward the target (gravity well on impact)
        TArray<FOverlapResult> Overlaps;
        FCollisionShape Sphere = FCollisionShape::MakeSphere(400.f);
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(Target);

        World->OverlapMultiByChannel(
            Overlaps,
            Target->GetActorLocation(),
            FQuat::Identity,
            ECC_Pawn,
            Sphere,
            Params);

        for (const FOverlapResult& Overlap : Overlaps)
        {
            ACharacter* NearbyChar = Cast<ACharacter>(Overlap.GetActor());
            if (!NearbyChar) continue;

            FVector PullDir = (Target->GetActorLocation() - NearbyChar->GetActorLocation()).GetSafeNormal();
            UCharacterMovementComponent* NearCMC = NearbyChar->GetCharacterMovement();
            if (NearCMC)
            {
                NearCMC->AddImpulse(PullDir * 800.f, true);
            }
        }
        break;
    }

    case ETransfigurationElement::Earth:
    {
        // Knockback — launch enemy away from impact direction
        if (CMC)
        {
            // Get the instigator direction from velocity context — 
            // knock the enemy away from where they were hit
            FVector KnockDir = Target->GetActorForwardVector() * -1.f;
            KnockDir.Z = 0.5f;
            KnockDir.Normalize();

            CMC->AddImpulse(KnockDir * 1500.f, true);
        }

        // Heavy bonus damage on top of base — earth hits hardest
        Target->TakePoolDamage(InDamage * 0.5f, Target->GetActorLocation());
        break;
    }

    case ETransfigurationElement::Arcane:
    {
        // Arcane — amplifies next hit, apply a brief damage vulnerability window
        // Deal full damage once more after a short delay (echo damage)
        float EchoDamage = InDamage * 0.75f;
        FVector ImpactLoc = Target->GetActorLocation();

        FTimerHandle EchoTimer;
        FTimerDelegate EchoDelegate;
        TWeakObjectPtr<ABaseEnemy> WeakTarget(Target);
        EchoDelegate.BindLambda([WeakTarget, EchoDamage, ImpactLoc]()
            {
                if (WeakTarget.IsValid())
                    WeakTarget->TakePoolDamage(EchoDamage, ImpactLoc);
            });
        World->GetTimerManager().SetTimer(EchoTimer, EchoDelegate, 0.3f, false);
        break;
    }

    default:
        break;
    }
}

void UCombatGrappleMove::ApplyMomentumDamage(ATMCharacter* Instigator, ABaseEnemy* Target, float Speed)
{
    if (!Instigator || !Target) return;

    // Bonus damage based on how fast you're going
    float MomentumBonus = Speed / 1000.0f * 0.5f; // Up to 50% bonus at 1000 speed
    float BonusDamage = BaseDamage * MomentumBonus;

    if (BonusDamage > 0)
    {
        Target->TakePoolDamage(BonusDamage, Target->GetActorLocation());
    }
}