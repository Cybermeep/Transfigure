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
struct FHitResult;

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

    // ── TAB INPUT: tap = boost, hold = anchor, double tap = pull (keep the 2nd tap held = stay stuck there) ──
    /** Hold Tab at least this long (s) to anchor instead of tapping. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Input")
    float HoldThreshold = 0.16f;

    /** A second tap within this long (s) of the first is a double tap (pull). A single tap's boost waits this long. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Input")
    float DoubleTapWindow = 0.22f;

    /** Tap: speed added toward the aim point (along the wall while wall running). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Boost")
    float TapBoostSpeed = 800.f;

    /** Tap: quick field-of-view punch, in degrees. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Boost")
    float TapBoostFOVKick = 4.f;

    /** No grapple move takes you faster than this (cm/s). Stops chained pulls stacking speed forever. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel")
    float MaxMomentumSpeed = 4500.f;

    /** Hold: an anchor on a wall counts as "the wall you're running on" (glue) when the normals match this closely. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Anchor", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float GlueMatchDot = 0.9f;

    /** Hold: longest an anchor swing lasts (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Anchor")
    float AnchorMaxTime = 8.f;

    /** Gravity while the rope carries a wall run round a corner (keeps you level). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Anchor")
    float OrbitGravityScale = 0.1f;

    /** Jumping off a spot you're stuck to: push away from the surface (cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Anchor")
    float PerchJumpOffSpeed = 700.f;

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
    float CornerSwingMaxDistance = 2500.f;

    /** How head-on you must be moving into the wall (1 = straight at it, 0.5 = within 60 degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|CornerSwing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CornerSwingMinApproach = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|CornerSwing")
    float CornerSwingMinSpeed = 300.f;

    /** Speed multiplier through the turn (your momentum is kept, plus this). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|CornerSwing")
    float CornerSwingSpeedBoost = 1.25f;

    /** The turn onto the wall: you dash straight at it, then snap round in a curve this tight (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|CornerSwing")
    float CornerTurnRadius = 350.f;

    // ── ANCHOR CONTROLS (while holding Tab) ──
    /** Holding the anchor with no movement keys: you're reeled in toward it (cm/s^2). W/A/D steer instead. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Anchor")
    float AnchorYankAcceleration = 5000.f;

    /** Hold + move away from the anchor (usually S): pinned in place, charging a slingshot.
     *  Let go of S or Tab to launch toward the anchor. Seconds to full charge. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Slingshot")
    float SlingChargeTime = 1.0f;

    /** While charging, how fast your speed bleeds away (higher = stops sooner; ~10 is about a quarter second). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Slingshot")
    float SlingBrakeRate = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Slingshot")
    float SlingMinSpeed = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Slingshot")
    float SlingMaxSpeed = 4500.f;

    /** Extra upward speed on launch. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Slingshot")
    float SlingLift = 250.f;

    /** The view narrows by up to this much while charging (tension), in degrees. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Slingshot")
    float SlingFOVPull = 8.f;

    /** FOV punch on launch at full charge, in degrees. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Slingshot")
    float SlingLaunchFOVKick = 12.f;

    /** Slingshots only charge from anchors on a floor or a ceiling: |anchor normal . up| at least
     *  this (0.7 = within about 45 degrees of flat). Wall anchors never charge, so S doesn't bleed speed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Slingshot")
    float SlingSurfaceMinDot = 0.7f;

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

    /** Tab pressed / released. The character passes its view trace. */
    void OnTabPressed(const FHitResult& Hit, bool bHit);
    void OnTabReleased();
    /** Wall-run state, while glued: a wall straight ahead (inside corner) - snap round onto it. */
    void OnGluedWallAhead(const FVector& Point, const FVector& Normal, const FVector& RunDir, float Speed, const FVector& OldWallNormal);
    /** Wall-run state, while glued: the wall just ended (outside corner) - whip round its end. */
    void OnGluedWallEnded(const FVector& LastWallPoint, const FVector& WallNormal, const FVector& RunDir, float Speed);
    /** Holding the anchor on the ground or a glued wall + S: bleed speed, hang there charging a slingshot. */
    void StartSurfaceCharge();
    /** The current anchor is on a floor or ceiling (see SlingSurfaceMinDot). */
    bool CanSlingshot() const;

    /** Glued wall run with the anchor still ahead of you: you're pulled along the wall toward it (cm/s^2). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Anchor")
    float GluedPullAcceleration = 1500.f;

    FVector GetAnchorPoint() const { return SwingAnchorPoint; }

    /** Glued wall run: how far ahead (seconds of travel) to look for a wall to snap round onto. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Anchor")
    float GluedLookAheadTime = 0.12f;

    /** Whipping round the end of a wall: give up after turning this far (degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Feel|Anchor")
    float EdgeWrapMaxAngle = 200.f;
    void JumpOffPerch();
    bool IsGluedToWall() const { return bWallGlue; }
    bool IsPerched() const { return bPerched; }
    /** The rope is carrying a wall run round a corner (the camera keeps its wall-run tilt). */
    bool IsCarryingWallRun() const { return bIsSwinging && (SwingMode == ESwingMode::CornerSwing || SwingMode == ESwingMode::EdgeWrap || bOrbit); }

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
    enum class ESwingMode : uint8 { Swing, WallPull, CornerSwing, CeilingPull, SurfacePull, EdgeWrap };
    ESwingMode SwingMode = ESwingMode::Swing;

    enum class ETabPhase : uint8 { Idle, FirstHeld, WaitSecond, Pull, Anchor };
    ETabPhase TabPhase = ETabPhase::Idle;
    double FirstPressTime = 0.0;
    double FirstReleaseTime = 0.0;
    FVector PendingPoint = FVector::ZeroVector;
    FVector PendingNormal = FVector::ZeroVector;
    bool bPullHeld = false;
    bool bWallGlue = false;
    bool bPerched = false;
    bool bPerchOnCeiling = false;
    FVector PerchNormal = FVector::ZeroVector;
    bool bOrbit = false;
    bool bCeilingRunEligible = false;
    bool bPressPaid = false;

    // Slingshot charge.
    bool bCharging = false;
    float ChargeTime = 0.f;
    /** Seconds since S was last seen while charging (input can read empty for a frame). */
    float SlingInputGone = 0.f;
    /** Real time movement input was last seen (so a one-frame gap doesn't count as "no keys"). */
    double LastMoveInputTime = 0.0;
    void Launch();

    // Corner snap path: straight dash P0 -> P1, then a quadratic curve P1 -(C)-> P2 along the wall.
    FVector CornerP0 = FVector::ZeroVector;
    FVector CornerP1 = FVector::ZeroVector;
    FVector CornerC = FVector::ZeroVector;
    FVector CornerP2 = FVector::ZeroVector;
    FVector CornerHeading = FVector::ZeroVector;
    float CornerStraight = 0.f;
    float CornerCurveLen = 0.f;
    float CornerTravel = 0.f;
    float CornerLift = 0.f;

    void UpdateTabTiming();
    bool StartBoost();
    void StartPull(bool bChargeMana);
    void StartAnchor();
    void AttachRope(const FVector& Point, const FVector& InitialVelocity, float MaxTime, bool bLift = true);
    void BeginCornerSnap(const FVector& WallPoint, const FVector& WallNormal, const FVector& Heading, const FVector& ExitDir, float Speed);

    // Holding an anchor while on the ground: you keep walking until you let it reel you in (no keys) or press S.
    bool bGroundAnchor = false;
    void UpdateGroundAnchor();

    // Whip round the end of a wall: orbit a pivot at the wall's end until another face is beside us.
    FVector EdgePivot = FVector::ZeroVector;
    FVector EdgeOffset = FVector::ZeroVector;      // horizontal, pivot -> player at the start
    FVector EdgeVertical = FVector::ZeroVector;    // vertical part of pivot -> player (kept)
    FVector EdgeStartNormal = FVector::ZeroVector;
    float EdgeAngle = 0.f;
    float EdgeSign = 1.f;
    float EdgeRadius = 100.f;
    float EdgeSpeed = 0.f;
    void UpdateEdgeWrap(float DeltaTime);
    void StopRope(bool bHop);
    void BeginPerch(const FVector& SurfaceNormal, bool bCeiling);
    void EndPerch(bool bJump);
    FVector ClampMomentum(const FVector& V) const;
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
    void ArriveAtWall(const FVector& Up, const FVector& WallNormal);
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