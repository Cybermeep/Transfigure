// Source/TMovement/Private/Combat/WeaponComponent.cpp
// FULL REPLACEMENT — renames OnWeaponHit() to ProcessHit() to match header fix.

#include "Combat/WeaponComponent.h"
#include "Character/TMCharacter.h"
#include "Enemy/BaseEnemy.h"
#include "Kismet/GameplayStatics.h"

UWeaponComponent::UWeaponComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f;

    // Default elemental multipliers
    ElementalDamageMultipliers.Add(ETransfigurationElement::Fire, 1.2f);
    ElementalDamageMultipliers.Add(ETransfigurationElement::Ice, 1.0f);
    ElementalDamageMultipliers.Add(ETransfigurationElement::Lightning, 1.1f);
    ElementalDamageMultipliers.Add(ETransfigurationElement::Void, 1.3f);
    ElementalDamageMultipliers.Add(ETransfigurationElement::Earth, 1.0f);
}

void UWeaponComponent::BeginPlay()
{
    Super::BeginPlay();
    OwnerCharacter = Cast<ATMCharacter>(GetOwner());
}

void UWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UpdateInfusion(DeltaTime);
}

// ── BUG 4 FIX: Renamed from OnWeaponHit to ProcessHit ──
void UWeaponComponent::ProcessHit(FHitResult Hit, bool bWasHeadshot, bool bWasKill)
{
    if (!OwnerCharacter) return;

    FWeaponHitInfo HitInfo;
    HitInfo.HitLocation = Hit.ImpactPoint;
    HitInfo.HitActor = Hit.GetActor();
    HitInfo.bWasHeadshot = bWasHeadshot;
    HitInfo.bWasKill = bWasKill;

    // Calculate mana reward
    float ManaToAdd = 0.0f;

    if (bWasKill)
    {
        ManaToAdd = ManaPerKill;
        if (bWasHeadshot)
            ManaToAdd += ManaPerHeadshot;

        OnWeaponKill.Broadcast(Hit.GetActor());
    }
    else
    {
        ManaToAdd = ManaPerHit;
        if (bWasHeadshot)
            ManaToAdd += (ManaPerHeadshot - ManaPerHit); // Headshot bonus on non-kill
    }

    OwnerCharacter->AddMana(ManaToAdd);

    // Apply elemental damage if infused
    if (bIsInfused && Hit.GetActor())
    {
        ApplyElementalDamage(Hit.GetActor(), Hit, HitInfo.DamageDealt);
    }

    // Broadcast to Blueprint listeners
    OnWeaponHit.Broadcast(HitInfo);
}

void UWeaponComponent::OnMeleeKill(AActor* Target)
{
    if (!OwnerCharacter) return;

    OwnerCharacter->AddMana(ManaPerMelee);
    OnWeaponKill.Broadcast(Target);
}

void UWeaponComponent::InfuseWeaponWithElement(ETransfigurationElement Element)
{
    bIsInfused = true;
    CurrentInfusion = Element;
    InfusionTimeRemaining = ElementalInfusionDuration;

    GetWorld()->GetTimerManager().ClearTimer(InfusionTimerHandle);
    GetWorld()->GetTimerManager().SetTimer(
        InfusionTimerHandle,
        this, &UWeaponComponent::OnInfusionExpired,
        ElementalInfusionDuration,
        false);

    UE_LOG(LogTemp, Verbose, TEXT("WeaponComponent: Infused with element %d"), (int32)Element);
}

void UWeaponComponent::ClearInfusion()
{
    bIsInfused = false;
    InfusionTimeRemaining = 0.0f;
    GetWorld()->GetTimerManager().ClearTimer(InfusionTimerHandle);
}

void UWeaponComponent::UpdateInfusion(float DeltaTime)
{
    if (!bIsInfused) return;

    InfusionTimeRemaining -= DeltaTime;
    if (InfusionTimeRemaining <= 0.0f)
    {
        OnInfusionExpired();
    }
}
//basic version until i create the status effect infrastructure
void UWeaponComponent::ApplyElementalDamage(AActor* Target, const FHitResult& Hit, float InBaseDamage)
{
    ABaseEnemy* Enemy = Cast<ABaseEnemy>(Target);
    if (!Enemy) return;

    float DamageMultiplier = 1.0f;
    if (ElementalDamageMultipliers.Contains(CurrentInfusion))
    {
        DamageMultiplier = ElementalDamageMultipliers[CurrentInfusion];
    }

    float TotalDamage = InBaseDamage * DamageMultiplier;
    Enemy->TakePoolDamage(TotalDamage, Hit.ImpactPoint);
}

void UWeaponComponent::OnInfusionExpired()
{
    CurrentInfusion = ETransfigurationElement::None;
    GetWorld()->GetTimerManager().ClearTimer(InfusionTimerHandle);
    UE_LOG(LogTemp, Verbose, TEXT("WeaponComponent: Infusion expired"));
}
/*
void UWeaponComponent::ApplyElementalDamage(AActor* Target, const FHitResult& Hit, float InBaseDamage)
{
    ABaseEnemy* Enemy = Cast<ABaseEnemy>(Target);
    if (!Enemy) return;

    float DamageMultiplier = 1.0f;
    if (ElementalDamageMultipliers.Contains(CurrentInfusion))
    {
        DamageMultiplier = ElementalDamageMultipliers[CurrentInfusion];
    }

    float TotalDamage = InBaseDamage * DamageMultiplier;

    switch (CurrentInfusion)
    {
    case ETransfigurationElement::Fire:
        // 3-second burning DoT: 10 damage/sec
        Enemy->ApplyStatusEffect(EStatusEffect::Burning, 3.0f, 10.0f);
        break;

    case ETransfigurationElement::Ice:
        // Slow to 40% movement speed for 2 seconds
        Enemy->ApplyStatusEffect(EStatusEffect::Slowed, 2.0f, 0.4f);
        break;

    case ETransfigurationElement::Lightning:
        // 0.5s stun, then chain 50% damage to nearest enemy within 300 units
        Enemy->ApplyStatusEffect(EStatusEffect::Stunned, 0.5f, 0.0f);
        if (OwnerCharacter)
        {
            TArray<FOverlapResult> Overlaps;
            FCollisionQueryParams Params;
            Params.AddIgnoredActor(OwnerCharacter);
            Params.AddIgnoredActor(Target);
            GetWorld()->OverlapMultiByChannel(
                Overlaps,
                Hit.ImpactPoint,
                FQuat::Identity,
                ECC_Pawn,
                FCollisionShape::MakeSphere(300.f),
                Params);

            for (const FOverlapResult& Overlap : Overlaps)
            {
                ABaseEnemy* ChainTarget = Cast<ABaseEnemy>(Overlap.GetActor());
                if (ChainTarget)
                {
                    ChainTarget->TakePoolDamage(TotalDamage * 0.5f, Hit.ImpactPoint);
                    break; // Chain to first valid target only
                }
            }
        }
        break;

    case ETransfigurationElement::Void:
        // Strip enemy buffs, return 5 mana to player
        Enemy->ClearAllBuffs();
        if (OwnerCharacter) OwnerCharacter->AddMana(5.0f);
        break;

    case ETransfigurationElement::Earth:
        // Knockback 400 units with upward component
    {
        FVector KnockDir = (Target->GetActorLocation() -
            OwnerCharacter->GetActorLocation()).GetSafeNormal();
        KnockDir.Z = 0.4f; // Upward kick
        KnockDir.Normalize();
        Enemy->LaunchCharacter(KnockDir * 400.f, true, true);
    }
    break;

    case ETransfigurationElement::Arcane:
        // Mark enemy — next sigil within proximity deals 3x damage
        Enemy->ApplyStatusEffect(EStatusEffect::SigilMarked, 8.0f, 3.0f);
        break;

    default:
        break;
    }

    Enemy->TakePoolDamage(TotalDamage, Hit.ImpactPoint);
}
*/
/*
void UWeaponComponent::InfuseWeaponWithElement(ETransfigurationElement Element)
{
    bIsInfused = true;
    CurrentInfusion = Element;
    InfusionTimeRemaining = ElementalInfusionDuration;

    GetWorld()->GetTimerManager().ClearTimer(InfusionTimerHandle);
    GetWorld()->GetTimerManager().SetTimer(
        InfusionTimerHandle,
        this, &UWeaponComponent::OnInfusionExpired,
        ElementalInfusionDuration,
        false);

    UE_LOG(LogTemp, Verbose, TEXT("WeaponComponent: Infused with element %d"), (int32)Element);
}
*/