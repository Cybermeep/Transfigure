// Source/Transfigure/Combat/WeaponComponent.cpp
//
// Hitscan gun: hold to fire, tracer + impact spark + muzzle flash (from the
// spell glow material), camera kick that recovers by itself, headshots on the
// procedural zombies' head hit zone, and a shove for loose limbs and corpses.

#include "Combat/WeaponComponent.h"
#include "Character/TMCharacter.h"
#include "Enemy/BaseEnemy.h"
#include "Kismet/GameplayStatics.h"
#include "Transfigure.h"
#include "Transfiguration/StatusEffectComponent.h"
#include "Transfiguration/SpellFX.h"
#include "Transfiguration/SpellTypes.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Components/PointLightComponent.h"
#include "RingFrameUtils.h"

UWeaponComponent::UWeaponComponent()
{
    PrimaryComponentTick.bCanEverTick = true;

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

    // Muzzle flash light: off until a shot, then a quick bright blink.
    if (OwnerCharacter)
    {
        MuzzleLight = NewObject<UPointLightComponent>(OwnerCharacter, TEXT("MuzzleLight"));
        MuzzleLight->SetupAttachment(OwnerCharacter->GetRootComponent());
        MuzzleLight->SetMobility(EComponentMobility::Movable);
        MuzzleLight->SetCastShadows(false);
        MuzzleLight->SetAttenuationRadius(550.f);
        MuzzleLight->SetIntensity(0.f);
        MuzzleLight->RegisterComponent();
    }
}

void UWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UpdateInfusion(DeltaTime);

    // Holding the trigger: keep firing at FireRate (the leftover time carries over, so the
    // rate doesn't depend on the frame rate).
    const double Now = GetWorld()->GetTimeSeconds();
    const double Interval = FireRate > 0.f ? 1.0 / FireRate : 0.0;
    if (bTriggerHeld && Interval > 0.0 && Now - LastShotTime >= Interval)
    {
        const double Due = FMath::Max(LastShotTime + Interval, Now - Interval);
        FireWeapon();
        LastShotTime = Due;
    }

    if (MuzzleLight && MuzzleFlash > 0.f)
    {
        MuzzleFlash = FMath::Max(MuzzleFlash - DeltaTime / 0.06f, 0.f);
        MuzzleLight->SetIntensity(9000.f * MuzzleFlash);
        MuzzleLight->SetWorldLocation(GetMuzzleLocation());
    }
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

FVector UWeaponComponent::GetMuzzleLocation() const
{
    if (!OwnerCharacter) return FVector::ZeroVector;
    if (!OwnerCharacter->GetController()) return OwnerCharacter->GetActorLocation();
    FVector CamLoc;
    FRotator CamRot;
    OwnerCharacter->GetController()->GetPlayerViewPoint(CamLoc, CamRot);
    // Where a gun held low on the right would be.
    const FRotationMatrix M(CamRot);
    return CamLoc + M.GetUnitAxis(EAxis::X) * 55.f + M.GetUnitAxis(EAxis::Y) * 18.f - M.GetUnitAxis(EAxis::Z) * 14.f;
}

void UWeaponComponent::StartFiring()
{
    bTriggerHeld = true;
    if (FireRate <= 0.f || GetWorld()->GetTimeSeconds() - LastShotTime >= 1.0 / FireRate) FireWeapon();
}

void UWeaponComponent::StopFiring()
{
    bTriggerHeld = false;
}

bool UWeaponComponent::FireWeapon()
{
    if (!OwnerCharacter || !GetWorld()) return false;
    FHitResult Hit;
    FVector Start, End;
    const bool bHit = TraceAim(Hit, Start, End);
    LastShotTime = GetWorld()->GetTimeSeconds();
    ++ShotsFired;

    ABaseEnemy* Enemy = bHit ? Cast<ABaseEnemy>(Hit.GetActor()) : nullptr;
    PlayShotEffects(GetMuzzleLocation(), bHit ? Hit.ImpactPoint : End, bHit ? &Hit : nullptr, Enemy != nullptr);
    UE_LOG(LogTemp, Log, TEXT("[TM] Fire: %s"), bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("miss"));

    if (bHit)
    {
        // Severed limbs and ragdoll corpses get knocked about by the shot.
        UPrimitiveComponent* Comp = Hit.GetComponent();
        if (ShotImpulse > 0.f && Comp && Comp->IsSimulatingPhysics(Hit.BoneName))
        {
            const FVector Dir = (End - Start).GetSafeNormal();
            const FVector Up = URingFrameUtils::GetUpAt(Hit.ImpactPoint);
            Comp->AddImpulse((Dir + Up * 0.25f) * ShotImpulse, Hit.BoneName, true);
        }
        ProcessHit(Hit, BaseDamage);
    }
    return bHit;
}

void UWeaponComponent::PlayShotEffects(const FVector& Muzzle, const FVector& End, const FHitResult* Hit, bool bEnemy)
{
    UWorld* World = GetWorld();
    const bool bElement = bIsInfused && CurrentInfusion != ETransfigurationElement::None;
    const FLinearColor Color = bElement ? USpellLibrary::GetElementColor(CurrentInfusion) : FLinearColor(1.f, 0.8f, 0.5f);

    // Thin tracer (fades in a few frames), starting a little ahead of the gun so the near end
    // isn't a fat streak across the screen, and a spark where it lands.
    const FVector TracerStart = Muzzle + (End - Muzzle).GetSafeNormal() * 40.f;
    if (ASpellFX* Tracer = ASpellFX::SpawnBeam(World, TracerStart, End, Color, bElement ? 1.4f : 1.0f, 0.07f))
    {
        Tracer->SetIntensity(bElement ? 7.f : 5.f);
        Tracer->SetNearFade(0.6f);
    }
    if (Hit)
    {
        ASpellFX::SpawnBurst(World, Hit->ImpactPoint + Hit->ImpactNormal * 3.f, bEnemy ? 26.f : 14.f, Color, 0.12f);
    }

    MuzzleFlash = 1.f;
    if (MuzzleLight)
    {
        MuzzleLight->SetLightColor(Color);
        MuzzleLight->SetWorldLocation(Muzzle);
        MuzzleLight->SetIntensity(9000.f);
    }

    // Kick: the view climbs a little per shot and settles back by itself.
    OwnerCharacter->AddRecoil(RecoilPitchPerShot, FMath::FRandRange(-RecoilYawPerShot, RecoilYawPerShot));
    OwnerCharacter->AddFOVKick(-0.35f);
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

    const bool bHeadshot = Enemy->IsHeadHit(Hit);
    float Damage = HitDamage * OwnerCharacter->GetDamageMultiplier();
    if (bHeadshot) Damage *= HeadshotMultiplier;

    if (bIsInfused)
    {
        if (const float* Multiplier = ElementalDamageMultipliers.Find(CurrentInfusion))
        {
            Damage *= *Multiplier;
        }
    }

    // Headshot: the damage number and hit marker show it (gold).
    UCombatFeedbackSubsystem* Feedback = UCombatFeedbackSubsystem::Get(this);
    if (bHeadshot && Feedback) Feedback->MarkNextHitCrit();
    if (bHeadshot) UE_LOG(LogTemp, Log, TEXT("[TM] Headshot: %s"), *Enemy->GetName());

    if (bIsInfused && CurrentInfusion != ETransfigurationElement::None)
    {
        // Infused shot: elemental damage plus a light dose of the element's status.
        Enemy->TakeSpellDamage(Damage, Hit.ImpactPoint, CurrentInfusion);
        if (Enemy->IsAlive() && Enemy->Status)
        {
            switch (CurrentInfusion)
            {
            case ETransfigurationElement::Fire:      Enemy->Status->ApplyStatus(ESpellStatus::Burning, 1.f, 3.f, OwnerCharacter); break;
            case ETransfigurationElement::Ice:       Enemy->Status->ApplyStatus(ESpellStatus::Chilled, 1.f, 4.f, OwnerCharacter); break;
            case ETransfigurationElement::Lightning: Enemy->Status->ApplyStatus(ESpellStatus::Shocked, 1.f, 0.3f, OwnerCharacter); break;
            case ETransfigurationElement::Arcane:    Enemy->Status->ApplyStatus(ESpellStatus::Marked, 1.f, 2.f, OwnerCharacter); break;
            case ETransfigurationElement::Earth:     if (bHeadshot) Enemy->Status->ApplyStatus(ESpellStatus::Staggered, 1.f, 0.5f, OwnerCharacter); break;
            case ETransfigurationElement::Void:      Enemy->Status->ApplyStatus(ESpellStatus::Voided, 1.f, 1.f, OwnerCharacter); break;
            default: break;
            }
        }
    }
    else
    {
        Enemy->TakePoolDamage(Damage, Hit.ImpactPoint);
    }
    if (Feedback) Feedback->ClearNextHitCrit();
    const bool bKill = !Enemy->IsAlive();
    if (bKill) OwnerCharacter->AddCameraShake(bHeadshot ? 0.22f : 0.14f);

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
