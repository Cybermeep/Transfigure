// Source/TMovement/Private/Combat/WeaponComponent.cpp
// FULL REPLACEMENT — renames OnWeaponHit() to ProcessHit() to match header fix.

#include "Combat/WeaponComponent.h"
#include "Character/TMCharacter.h"
#include "Enemy/BaseEnemy.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Transfigure.h"

UWeaponComponent::UWeaponComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    // Ticks every frame so the debug aim marker doesn't flicker.

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

#if !UE_BUILD_SHIPPING
    // Debug aim marker until there is a crosshair: green on an enemy, white elsewhere.
    if (OwnerCharacter && OwnerCharacter->IsLocallyControlled())
    {
        FHitResult Hit;
        FVector Start, End;
        if (TraceAim(Hit, Start, End))
        {
            const bool bEnemy = Cast<ABaseEnemy>(Hit.GetActor()) != nullptr;
            DrawDebugSphere(GetWorld(), Hit.ImpactPoint, bEnemy ? 12.f : 6.f, 8,
                bEnemy ? FColor::Green : FColor::White, false, -1.f, 0, 1.5f);
        }
    }
#endif
}

bool UWeaponComponent::TraceAim(FHitResult& OutHit, FVector& OutStart, FVector& OutEnd) const
{
    if (!OwnerCharacter || !OwnerCharacter->GetController()) return false;

    FVector CamLoc;
    FRotator CamRot;
    OwnerCharacter->GetController()->GetPlayerViewPoint(CamLoc, CamRot);
    const FVector Dir = CamRot.Vector();

    // Move the start forward along the view ray to the point level with the
    // character, so nothing behind the player can block the shot.
    const float ToCharacter = FVector::DotProduct(OwnerCharacter->GetActorLocation() - CamLoc, Dir);
    OutStart = CamLoc + Dir * FMath::Max(ToCharacter, 0.f);
    OutEnd = CamLoc + Dir * MaxRange;

    FCollisionQueryParams Params(SCENE_QUERY_STAT(WeaponFire), false, OwnerCharacter);
    return GetWorld()->LineTraceSingleByChannel(OutHit, OutStart, OutEnd, ECC_Weapon, Params);
}

bool UWeaponComponent::FireWeapon()
{
    FHitResult Hit;
    FVector Start, End;
    const bool bHit = TraceAim(Hit, Start, End);

#if !UE_BUILD_SHIPPING
    // Stand-in for a gun model and tracer: line from the player to the impact.
    DrawDebugLine(GetWorld(), OwnerCharacter->GetActorLocation(), bHit ? Hit.ImpactPoint : End,
        FColor::Yellow, false, 0.5f, 0, 1.5f);
    UE_LOG(LogTemp, Warning, TEXT("[TM] Fire: %s"), bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("miss"));
#endif

    if (bHit) ProcessHit(Hit, BaseDamage);
    return bHit;
}

void UWeaponComponent::ProcessHit(const FHitResult& Hit, float HitDamage)
{
    if (!OwnerCharacter) return;

    ABaseEnemy* Enemy = Cast<ABaseEnemy>(Hit.GetActor());
    if (!Enemy) return;

    if (!Enemy->IsAlive())
    {
        // Corpse: still pass the hit on (procedural zombies can be dismembered
        // after death), but no mana, kill credit or hit events.
        Enemy->TakePoolDamage(HitDamage * OwnerCharacter->GetDamageMultiplier(), Hit.ImpactPoint);
        return;
    }

    // Headshots need per-bone hits, which arrive with the limb system.
    const bool bHeadshot = Hit.BoneName == HeadBoneName;
    float Damage = HitDamage * OwnerCharacter->GetDamageMultiplier();
    if (bHeadshot) Damage *= HeadshotMultiplier;

    if (bIsInfused)
    {
        if (const float* Multiplier = ElementalDamageMultipliers.Find(CurrentInfusion))
        {
            Damage *= *Multiplier;
        }
    }

    Enemy->TakePoolDamage(Damage, Hit.ImpactPoint);
    const bool bKill = !Enemy->IsAlive();

    float ManaToAdd = bKill ? ManaPerKill : ManaPerHit;
    if (bHeadshot) ManaToAdd += ManaPerHeadshot;
    OwnerCharacter->AddMana(ManaToAdd);

    FWeaponHitInfo HitInfo;
    HitInfo.HitLocation = Hit.ImpactPoint;
    HitInfo.HitActor = Enemy;
    HitInfo.bWasHeadshot = bHeadshot;
    HitInfo.bWasKill = bKill;
    HitInfo.DamageDealt = Damage;
    OnWeaponHit.Broadcast(HitInfo);

    if (bKill) OnWeaponKill.Broadcast(Enemy);
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
void UWeaponComponent::OnInfusionExpired()
{
    bIsInfused = false;
    InfusionTimeRemaining = 0.f;
    CurrentInfusion = ETransfigurationElement::None;
    GetWorld()->GetTimerManager().ClearTimer(InfusionTimerHandle);
    UE_LOG(LogTemp, Verbose, TEXT("WeaponComponent: Infusion expired"));
}
