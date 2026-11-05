// Source/TMovement/Public/Grapple/GrappleComponent.h
// FULL REPLACEMENT — adds SetEvadeDirection() and EvadeDirection member

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GrappleTypes.h"
#include "GrappleComponent.generated.h"

class AGrappleLine;
class ATMCharacter;
class UCombatGrappleMove;
class ABaseEnemy;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGrappleStateChanged, EGrappleState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGrappleLineCreated, const FGrappleLineData&, LineData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCombatGrappleExecuted, ECombatGrappleMove, MoveType);
/** Slam landed. SurfaceActor is what was hit (floor, wall, enemy...). Hook for future environment changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGrappleSlamImpact, FVector, Location, FVector, SurfaceNormal, AActor*, SurfaceActor);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TRANSFIGURE_API UGrappleComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGrappleComponent();

    // Configuration
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Config")
    float MaxGrappleRange = 15000.0f;

    /** Enemies further than this can't be combat-grappled (kick, pull...). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Config")
    float CombatGrappleRange = 4000.0f;

    /** Swing: if you're moving mostly AWAY from the anchor when the rope goes tight (you passed a
     *  ground anchor, say), let go and keep the speed instead of being stopped dead. 1 = never. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Swing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SwingReleaseAwayDot = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Config")
    float SwingStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Config")
    float MomentumConservation = 0.95f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Config")
    float MaxSwingTime = 3.0f;

    /** Ceilings / non-wall surfaces swing (pendulum) instead of pulling you in. Small tug toward the anchor on attach. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Swing")
    float SwingAttachTug = 250.f;

    /** Swing: how fast the rope shortens (cm/s). 0 = fixed length, a true pendulum. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Swing")
    float SwingReelInSpeed = 0.f;

    // ── CORNER SWING: grapple the wall you're running into, hold, swing round into a wall run along it ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|CornerSwing")
    bool bEnableCornerSwing = true;

    /** Only when you're at most this far from the wall you hit (further away = the normal yank). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|CornerSwing")
    float CornerSwingMaxDistance = 1000.f;

    /** How head-on you must be moving into the wall (1 = straight at it, 0.5 = within 60 degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|CornerSwing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CornerSwingMinApproach = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|CornerSwing")
    float CornerSwingMinSpeed = 400.f;

    /** Speed multiplier through the turn (your momentum is kept, plus this). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|CornerSwing")
    float CornerSwingSpeedBoost = 1.15f;

    /** Tightest turn radius (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|CornerSwing")
    float CornerSwingMinRadius = 150.f;

    /** Gravity while swinging round (like a wall run, so you don't drop). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|CornerSwing")
    float CornerSwingGravityScale = 0.1f;

    // ── CEILING ──
    /** A surface whose normal points down at least this much counts as a ceiling. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Ceiling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CeilingMinDownDot = 0.7f;

    /** Ceiling grapple at full speed: start the ceiling run once your capsule is this close to the ceiling (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Ceiling")
    float CeilingArrivalSlack = 40.f;

    /** Wall anchors (vertical surfaces): yank speed. Faster, to feed wall running. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Wall")
    float WallPullSpeed = 3000.f;

    /** Wall anchors: reel-in speed (cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Wall")
    float WallReelInSpeed = 1500.f;

    /** Wall anchors: switch to a wall run once your centre is this close to the wall surface.
     *  Keep below ~80 (how far wall-run detection reaches sideways). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Wall")
    float WallArrivalDistance = 75.f;

    /** On reaching a wall anchor, turn to face along the wall so a wall run can start. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Wall")
    bool bFaceAlongWallOnArrival = true;

    /** A surface counts as a wall when its normal is within this many degrees of horizontal. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Wall")
    float WallMaxTiltDegrees = 30.f;

    /** Extra upward speed when a grapple starts from the ground, so you don't scrape along the floor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel")
    float GroundLiftSpeed = 500.f;

    /** The rope never reels shorter than this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel")
    float MinRopeLength = 300.f;

    /** How strongly WASD steers / pumps the swing (cm/s^2). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel")
    float SwingSteerAcceleration = 3000.f;

    /** Extra upward speed when you let go, so a release flows into a jump instead of a drop. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel")
    float ReleaseHopSpeed = 450.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Config")
    int32 MaxSimultaneousLines = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Tier")
    EGrappleTier CurrentTier = EGrappleTier::Strand;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Mana")
    float SwingManaCost = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Mana")
    float CombatGrappleManaCost = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Mana")
    float EvadeManaCost = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Mana")
    float WebLineManaCost = 10.0f;

    /** Optional Blueprint subclass per move. Moves without one use UCombatGrappleMove itself. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Combat")
    TMap<ECombatGrappleMove, TSubclassOf<UCombatGrappleMove>> CombatMoveClasses;

    /** Seconds a combat move lasts before you can grapple again. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Combat")
    float CombatMoveDuration = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Combat")
    float MaxCombatPullSpeed = 2500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Combat")
    float EvadeSpeed = 1800.f;

    /** Minimum seconds between evades (replaces the old "grapple busy" lockout). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Combat")
    float EvadeCooldown = 0.35f;

    // State
    UPROPERTY(BlueprintReadOnly, Category = "Grapple|State")
    EGrappleState CurrentState = EGrappleState::Inactive;

    UPROPERTY(BlueprintReadOnly, Category = "Grapple|State")
    ETransfigurationElement CurrentElement = ETransfigurationElement::Fire;

    UPROPERTY(BlueprintReadOnly, Category = "Grapple|State")
    TArray<FGrappleLineData> ActiveLines;

    UPROPERTY(BlueprintReadOnly, Category = "Grapple|State")
    int32 CurrentLineCount = 0;

    // Delegates
    UPROPERTY(BlueprintAssignable)
    FOnGrappleStateChanged OnGrappleStateChanged;

    UPROPERTY(BlueprintAssignable)
    FOnGrappleLineCreated OnGrappleLineCreated;

    UPROPERTY(BlueprintAssignable)
    FOnCombatGrappleExecuted OnCombatGrappleExecuted;

    UPROPERTY(BlueprintAssignable)
    FOnGrappleSlamImpact OnSlamImpact;

    // ── SLAM (movement tech: dive at any aimed point, area damage on impact) ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Slam")
    float SlamSpeed = 3200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Slam")
    float SlamDamage = 40.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Slam")
    float SlamRadius = 400.f;

    /** Speed enemies in the radius are knocked away (and up) at. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Slam")
    float SlamKnockback = 900.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Slam")
    float SlamManaCost = 10.f;

    /** Speed (when V is pressed) at or below which the slam is normal strength. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Slam|Momentum")
    float SlamMomentumMinSpeed = 600.f;

    /** Speed at or above which the slam reaches SlamMaxMomentumScale. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Slam|Momentum")
    float SlamMomentumMaxSpeed = 2500.f;

    /** Multiplier on radius, damage and knockback at full momentum. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Slam|Momentum")
    float SlamMaxMomentumScale = 2.0f;

    /** Dive speed multiplier at full momentum (dive is also never slower than 1.15x your entry speed). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Slam|Momentum")
    float SlamMaxDiveSpeedScale = 1.8f;

    /** A slam that hasn't hit anything after this long impacts where it is. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Slam")
    float SlamMaxTime = 1.0f;

    /** Dive toward Target. bHasTarget=false (aimed at nothing) means ground-pound straight down. */
    UFUNCTION(BlueprintCallable, Category = "Grapple|Slam")
    void StartSlam(FVector Target, FVector TargetNormal, AActor* TargetActor, bool bHasTarget);

    UFUNCTION(BlueprintPure, Category = "Grapple|Slam")
    bool IsSlamming() const { return bIsSlamming; }

    // Public methods
    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void StartAiming(ETransfigurationElement Element);

    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void FireGrapple(FVector AimLocation, AActor* TargetEnemy = nullptr, FVector AimNormal = FVector::ZeroVector);

    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void ExecuteCombatMove(ECombatGrappleMove MoveType, AActor* TargetEnemy);

    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void ReleaseGrapple();

    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void CancelAiming();

    UFUNCTION(BlueprintPure, Category = "Grapple")
    EGrappleState GetCurrentState() const { return CurrentState; }

    // ── BUG 3 FIX: SetEvadeDirection so TMCharacter can pass input direction ──
    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void SetEvadeDirection(FVector Direction) { EvadeDirection = Direction.GetSafeNormal(); }

    UFUNCTION(BlueprintCallable, Category = "Grapple|Web")
    void CreateWebLine(FVector StartPoint, FVector EndPoint);

    UFUNCTION(BlueprintCallable, Category = "Grapple|Web")
    bool CanCreateWebLine() const;

    UFUNCTION(BlueprintCallable, Category = "Grapple|Web")
    TArray<FVector> FindWebConnectionPoints() const;

    UFUNCTION(BlueprintCallable, Category = "Grapple|Combat")
    bool IsEnemyGrappleable(AActor* Enemy) const;

    UFUNCTION(BlueprintCallable, Category = "Grapple|Query")
    float GetCurrentTierLines() const;

    UFUNCTION(BlueprintCallable, Category = "Grapple|Query")
    float GetMomentumPreservation() const;

    UFUNCTION(BlueprintPure, Category = "Grapple")
    EGrappleTier GetCurrentTier() const { return CurrentTier; }

    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void SetCurrentTier(EGrappleTier NewTier) { CurrentTier = NewTier; }

    UFUNCTION(BlueprintPure, Category = "Grapple")
    TArray<FGrappleLineData>& GetActiveLines() { return ActiveLines; }

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    UPROPERTY()
    ATMCharacter* OwnerCharacter;

    UPROPERTY()
    TArray<AGrappleLine*> SpawnedLines;

    UPROPERTY()
    FGrappleLineData CurrentSwingData;

    UPROPERTY()
    FTimerHandle SwingTimerHandle;

    // Swing physics state (Verlet integration)
    UPROPERTY()
    FVector SwingAnchorPoint = FVector::ZeroVector;

    UPROPERTY()
    FVector SwingVelocity = FVector::ZeroVector;

    UPROPERTY()
    float SwingRopeLength = 0.f;

    UPROPERTY()
    bool bIsSwinging = false;

    /** Current swing is anchored on a wall (vertical surface). */
    bool bWallAnchor = false;

    /** What the rope does: pendulum, yank to a wall, swing round a wall, or yank to a ceiling run. */
    enum class ESwingMode : uint8 { Swing, WallPull, CornerSwing, CeilingPull };
    ESwingMode SwingMode = ESwingMode::Swing;
    FVector CornerExitDir = FVector::ZeroVector;
    float CornerSpeed = 0.f;
    float CeilingEntrySpeed = 0.f;
    void UpdateCornerSwing(float DeltaTime);
    void ArriveAtCeiling();
    FVector AnchorNormal = FVector::ZeroVector;

    // ── BUG 3 FIX: Stores evade direction set from input handler ──
    FVector EvadeDirection = FVector::ForwardVector;

    FTimerHandle CombatMoveTimerHandle;
    float LastEvadeTime = -100.f;
    void FinishCombatMove();
    void UpdateSlam(float DeltaTime);
    void SlamImpact(const FVector& Location, const FVector& Normal, AActor* Surface);

    bool bIsSlamming = false;
    float SlamMomentumScale = 1.f;
    float SlamDiveSpeed = 0.f;
    float SlamElapsed = 0.f;
    FVector SlamTarget = FVector::ZeroVector;
    FVector SlamTargetNormal = FVector::ZeroVector;
    FVector SlamDir = FVector::ZeroVector;
    TWeakObjectPtr<AActor> SlamTargetActor;
    void ArriveAtWall(const FVector& Up);
    void PerformEvade();

    void PerformGrappleSwing(FVector AnchorPoint);
    void PerformCombatGrapple(AActor* TargetEnemy, ECombatGrappleMove MoveType);
    void UpdateSwingPhysics(float DeltaTime);
    void ApplyElementalEffect(FGrappleLineData& LineData);
    void UpdateLineLifecycles(float DeltaTime);
    void CleanupExpiredLines();
    bool HasLineTierCapability(FString Capability) const;
    void PreserveMomentum(FVector PreGrappleVelocity);
};