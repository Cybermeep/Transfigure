// Source/TMovement/Public/Combat/WeaponComponent.h
// FULL REPLACEMENT — renames OnWeaponHit() function to ProcessHit() to resolve
// name collision with the OnWeaponHit BlueprintAssignable delegate.
// Also adds missing #include for ETransfigurationElement.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Transfiguration/TransfigurationTypes.h"   // ← NEW: required for ETransfigurationElement
#include "WeaponComponent.generated.h"

class ATMCharacter;
class UPointLightComponent;

USTRUCT(BlueprintType)
struct FWeaponHitInfo
{
    GENERATED_BODY()

    UPROPERTY()
    FVector HitLocation = FVector::ZeroVector;

    UPROPERTY()
    AActor* HitActor = nullptr;

    UPROPERTY()
    bool bWasHeadshot = false;

    UPROPERTY()
    bool bWasKill = false;

    UPROPERTY()
    float DamageDealt = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponHit, const FWeaponHitInfo&, HitInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponKill, AActor*, KilledEnemy);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TRANSFIGURE_API UWeaponComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UWeaponComponent();

    // Mana rewards
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Mana")
    float ManaPerHit = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Mana")
    float ManaPerKill = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Mana")
    float ManaPerHeadshot = 15.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Mana")
    float ManaPerMelee = 20.0f;

    // Elemental infusion
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Elemental")
    bool bEnableElementalInfusion = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Elemental")
    float ElementalInfusionDuration = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Elemental")
    TMap<ETransfigurationElement, float> ElementalDamageMultipliers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Elemental")
    TMap<ETransfigurationElement, TSubclassOf<UDamageType>> ElementalDamageTypes;

    // Delegates (BlueprintAssignable properties)
    UPROPERTY(BlueprintAssignable)
    FOnWeaponHit OnWeaponHit;

    UPROPERTY(BlueprintAssignable)
    FOnWeaponKill OnWeaponKill;

    // Hitscan settings
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
    float BaseDamage = 25.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
    float MaxRange = 10000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
    float HeadshotMultiplier = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
    FName HeadBoneName = TEXT("head");

    /** Hold to fire: shots per second while the trigger is held (the first shot is instant). 0 = one per click. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
    float FireRate = 9.f;

    /** Camera kick per shot (degrees up); it settles back by itself. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Feel")
    float RecoilPitchPerShot = 0.55f;

    /** Random sideways kick per shot (degrees, either way). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Feel")
    float RecoilYawPerShot = 0.18f;

    /** Speed (cm/s) a shot gives to severed limbs and ragdoll corpses. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Feel")
    float ShotImpulse = 420.f;

    /** Trigger pressed / released (the character's fire input). */
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void StartFiring();

    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void StopFiring();

    UFUNCTION(BlueprintPure, Category = "Weapon")
    bool IsFiring() const { return bTriggerHeld; }

    /** Shots fired since play started (the smoke test checks it). */
    int32 GetShotsFired() const { return ShotsFired; }

    /** Traces from the camera along the view direction. Returns true if something was hit. */
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    bool FireWeapon();

    /** Works out damage (buffs, headshot, infusion), applies it via TakePoolDamage, rewards mana and broadcasts. */
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void ProcessHit(const FHitResult& Hit, float HitDamage);

    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void OnMeleeKill(AActor* Target);

    UFUNCTION(BlueprintCallable, Category = "Weapon|Elemental")
    void InfuseWeaponWithElement(ETransfigurationElement Element);

    UFUNCTION(BlueprintPure, Category = "Weapon|Elemental")
    bool IsWeaponInfused() const { return bIsInfused; }

    UFUNCTION(BlueprintPure, Category = "Weapon|Elemental")
    ETransfigurationElement GetCurrentInfusion() const { return CurrentInfusion; }

    UFUNCTION(BlueprintCallable, Category = "Weapon|Elemental")
    void ClearInfusion();

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    UPROPERTY()
    ATMCharacter* OwnerCharacter;

    UPROPERTY()
    bool bIsInfused = false;

    UPROPERTY()
    ETransfigurationElement CurrentInfusion = ETransfigurationElement::Fire;

    UPROPERTY()
    float InfusionTimeRemaining = 0.0f;

    UPROPERTY()
    FTimerHandle InfusionTimerHandle;

    UFUNCTION()
    void UpdateInfusion(float DeltaTime);

    /**
     * Line trace along the camera's view, but starting level with the
     * character instead of at the camera. A third-person camera sits behind
     * the player, so starting there would hit walls/objects between the
     * camera and the player (e.g. the wall you're running on).
     */
    bool TraceAim(FHitResult& OutHit, FVector& OutStart, FVector& OutEnd) const;

    UFUNCTION()
    void OnInfusionExpired();

    bool bTriggerHeld = false;
    double LastShotTime = -100.0;
    int32 ShotsFired = 0;

    UPROPERTY()
    TObjectPtr<UPointLightComponent> MuzzleLight;
    float MuzzleFlash = 0.f;

    /** Where a gun held low on the right would be (tracers start here). */
    FVector GetMuzzleLocation() const;
    void PlayShotEffects(const FVector& Muzzle, const FVector& End, const FHitResult* Hit, bool bEnemy);
};
