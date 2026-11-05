// Source/TMovement/Public/Enemy/BaseEnemy.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Transfiguration/TransfigurationTypes.h"
#include "BaseEnemy.generated.h"

class AEnemyObjectPool;
class UStatusEffectComponent;
class UWidgetComponent;
class ABaseEnemy;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyDied, ABaseEnemy*, Enemy);

UENUM(BlueprintType)
enum class EEnemyState : uint8
{
    Idle UMETA(DisplayName = "Idle"),
    Patrolling UMETA(DisplayName = "Patrolling"),
    Alerted UMETA(DisplayName = "Alerted"),
    Combat UMETA(DisplayName = "Combat"),
    Stunned UMETA(DisplayName = "Stunned"),
    Dead UMETA(DisplayName = "Dead")
};

UCLASS()
class TRANSFIGURE_API ABaseEnemy : public ACharacter
{
    GENERATED_BODY()

public:
    ABaseEnemy();

    // Pool lifecycle
    UFUNCTION(BlueprintCallable, Category = "Enemy|Pool")
    virtual void OnSpawnFromPool();

    UFUNCTION(BlueprintCallable, Category = "Enemy|Pool")
    virtual void OnReturnToPool();

    // Combat
    UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
    virtual void TakePoolDamage(float DamageAmount, FVector HitLocation);

    /**
     * Explosion-style damage. Full damage within InnerRadius of Origin, falling to half at
     * OuterRadius, none beyond. Base enemies use their distance; procedural zombies damage
     * every body part in range (so blasts can take several limbs at once).
     */
    virtual void TakeAreaDamage(float Damage, FVector Origin, float InnerRadius, float OuterRadius);

    /** Spell / elemental damage: like TakePoolDamage, scaled by this enemy's resistances and status. */
    virtual void TakeSpellDamage(float Damage, FVector HitLocation, ETransfigurationElement Element);

    /** Elemental TakeAreaDamage. */
    virtual void TakeSpellAreaDamage(float Damage, FVector Origin, float InnerRadius, float OuterRadius, ETransfigurationElement Element);

    /** Damage over time from a status (Burning). */
    virtual void TakeStatusDamage(float Damage, ETransfigurationElement Element);

    /** How much of an element's damage this enemy (or one of its machine parts) takes. 1 = normal. */
    virtual float GetElementScale(ETransfigurationElement Element, bool bMachinePart) const;

    /** Status effects (burning, chilled/frozen, shocked, marked, staggered, voided). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Status")
    TObjectPtr<UStatusEffectComponent> Status;

    /** Falloff used by TakeAreaDamage: 1 inside Inner, 0.5 at Outer, 0 beyond. */
    static float AreaFalloff(float Distance, float InnerRadius, float OuterRadius);

    /** Engine damage (ApplyDamage / ApplyRadialDamage / ApplyPointDamage) is routed into TakePoolDamage. */
    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;

    UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
    bool IsAlive() const { return bIsAlive; }

    UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
    bool IsElite() const { return bIsElite; }

    // Pod system
    UFUNCTION(BlueprintCallable, Category = "Enemy|Pod")
    void SetPodID(int32 NewPodID) { PodID = NewPodID; }

    UFUNCTION(BlueprintPure, Category = "Enemy|Pod")
    int32 GetPodID() const { return PodID; }

    UFUNCTION(BlueprintCallable, Category = "Enemy|Pod")
    void SetOwningPool(AEnemyObjectPool* Pool) { OwningPool = Pool; }

    // Awareness
    UFUNCTION(BlueprintCallable, Category = "Enemy|Awareness")
    void SetAlertnessLevel(float Level) { AlertnessLevel = Level; }

    UFUNCTION(BlueprintPure, Category = "Enemy|Awareness")
    float GetAlertnessLevel() const { return AlertnessLevel; }

    UFUNCTION(BlueprintCallable, Category = "Enemy|Awareness")
    void DetectGrappleLine(const FVector& LineStart, const FVector& LineEnd);

    UFUNCTION(BlueprintCallable, Category = "Enemy|Awareness")
    void ReactToDestruction(FVector DestructionLocation, float Radius);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
    float Health = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
    float MaxHealth = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
    float AwarenessRange = 2000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
    bool bIsElite = false;

    UPROPERTY(BlueprintReadOnly, Category = "Enemy|State")
    EEnemyState CurrentState = EEnemyState::Idle;

    /** Fires once when health reaches 0 (before the corpse is removed). Pods listen to this. */
    UPROPERTY(BlueprintAssignable, Category = "Enemy|Combat")
    FOnEnemyDied OnEnemyDied;

    /** Seconds the corpse stays before returning to the pool / being destroyed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
    float CorpseLifetime = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
    float AttackRange = 150.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
    float AttackDamage = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
    float AttackCooldown = 1.5f;

    /** Multiplies walk speed for Duration seconds. A new slow replaces the old one (never stacks). */
    UFUNCTION(BlueprintCallable, Category = "Enemy|Status")
    void ApplySlow(float SpeedMultiplier, float Duration);

    UFUNCTION(BlueprintCallable, Category = "Enemy|Status")
    void ApplyStun(float Duration);

    /** World-space local up at this enemy's current ring position. EnemyPod and others can read this instead of FVector::UpVector. */
    UFUNCTION(BlueprintPure, Category = "Enemy|RingGravity")
    FVector GetRingUp() const { return CachedRingUp; }

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    /**
     * Same purpose as ATMCharacter::UpdateRingGravity, without the yaw/pitch
     * camera handling the player needs. Enemies just need SetGravityDirection
     * every tick (so CMC's floor detection and nav-following behave) plus
     * actor rotation kept aligned to local up (so they visually stand
     * upright and don't tip over as the surface curves under them).
     */
    void UpdateRingGravity(float DeltaTime);

    // Simple chase/attack loop, run on a 0.5s timer while alive.
    void UpdateChase();
    void FinishDeath();
    void EndSlow();
    void EndStun();

    FTimerHandle ChaseTimerHandle;
    FTimerHandle DeathTimerHandle;
    FTimerHandle SlowTimerHandle;
    FTimerHandle StunTimerHandle;
    float BaseMaxWalkSpeed = 600.f;
    float LastAttackTime = -1000.f;

    /** Element of the damage being applied right now (set by TakeSpell*Damage), for per-part scaling. */
    ETransfigurationElement PendingElement = ETransfigurationElement::None;

    /** True when pathfinding failed (e.g. no nav mesh): walk straight at the player instead. */
    bool bDirectChase = false;

    UPROPERTY()
    int32 PodID = -1;

    FVector CachedRingUp = FVector::UpVector;

    UPROPERTY()
    AEnemyObjectPool* OwningPool = nullptr;

    UPROPERTY()
    bool bIsAlive = true;

    UPROPERTY()
    float AlertnessLevel = 1.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UWidgetComponent> HealthBarWidget;

    UFUNCTION()
    virtual void Die();

    UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|VFX")
    void OnSpawnEffects();

    UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|VFX")
    void OnDeathEffects();

    UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|VFX")
    void OnReturnEffects();

    UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|VFX")
    void OnDamageEffects(FVector HitLocation);

    UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|Awareness")
    void OnGrappleDetected(const FVector& LineLocation);

    UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|Awareness")
    void OnDestructionDetected(FVector DestructionLocation);
};