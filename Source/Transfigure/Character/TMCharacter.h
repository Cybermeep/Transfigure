// Source/TMovement/Public/Character/TMCharacter.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "StateMachine/TMStateMachine.h"
#include "InputActionValue.h"
#include "Transfiguration/TransfigurationTypes.h"
#include "TMCharacter.generated.h"

class UInputMappingContext;
class UInputAction;
class UTransfigurationCastingComponent;
class UTransfigurationComboDetector;
class UBuffEffectComponent;
class UGrappleComponent;
class UWeaponComponent;
class UCameraComponent;
class URingLaunchComponent;

UCLASS()
class TRANSFIGURE_API ATMCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ATMCharacter();

    // ── MOVEMENT PARAMETERS ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement")
    float WalkSpeed = 600.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement")
    float SprintSpeed = 1000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement")
    float SlideImpulse = 1400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement")
    float JumpZVelocity = 700.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement")
    float WallRunGravityScale = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement")
    float WallRunSpeed = 850.f;

    /** Wall runs speed up the longer they last (cm/s per second). Rewards keeping momentum. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|WallRun")
    float WallRunAcceleration = 700.f;

    /** Wall run speed cap. Arriving faster than this (e.g. a wall grapple) keeps the arrival speed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|WallRun")
    float WallRunMaxSpeed = 2400.f;

    // ── CAMERA FEEL ──
    /** First person: the camera sits at eye height inside the capsule (no boom, no wall clipping). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Camera")
    bool bFirstPerson = true;

    /** Eye height above the capsule centre, in cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Camera")
    float FirstPersonEyeHeight = 64.f;

    /** Normal field of view, in degrees. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Camera")
    float BaseFOV = 90.f;

    /** Camera roll while wall running, tilted away from the wall. Negative tilts toward it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Camera")
    float WallRunCameraTilt = 15.f;

    /** How fast the tilt eases in and out (higher = snappier). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Camera")
    float CameraTiltSpeed = 6.f;

    /** Speed (cm/s) where the view starts widening. Walking is 600. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Camera")
    float SpeedFOVStartSpeed = 700.f;

    /** Speed (cm/s) where the extra FOV is fully applied. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Camera")
    float SpeedFOVFullSpeed = 2600.f;

    /** Extra FOV at full speed, in degrees (the "zoom out" at speed). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Camera")
    float SpeedFOVBoost = 20.f;

    /** How fast the FOV follows speed changes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Camera")
    float FOVChangeSpeed = 4.f;

    /** Set by the wall-run state: -1 wall on the left, +1 on the right, 0 when not wall running. */
    float WallRunSide = 0.f;
    /** Set by the wall-run state: the wall's normal. */
    FVector WallRunNormal = FVector::ZeroVector;

    /** Keep the wall-run tilt this long (s) after leaving the wall in the air, so quick hops don't snap it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Camera")
    float WallRunTiltGrace = 0.4f;

    /** Gun kick: the view climbs (degrees) and settles back by itself. */
    void AddRecoil(float PitchDegrees, float YawDegrees)
    {
        RecoilPitch = FMath::Min(RecoilPitch + PitchDegrees, 6.f);
        RecoilYaw = FMath::Clamp(RecoilYaw + YawDegrees, -3.f, 3.f);
    }

    /** Quick FOV punch (degrees) that fades out, e.g. on a grapple boost. */
    void AddFOVKick(float Degrees) { FOVKick += Degrees; }
    /** Camera shake (0..1 "trauma"; adds up, decays). Big spells, hits taken. */
    void AddCameraShake(float Trauma) { CameraShakeTrauma = FMath::Min(CameraShakeTrauma + Trauma, 1.f); }
    /** Real time the player last took damage (HUD hurt flash). */
    double GetLastHurtTime() const { return LastHurtTime; }

    /** Spellbook screen (key B): browse known spells, set the wheel. */
    bool IsSpellbookOpen() const { return bSpellbookOpen; }
    void ToggleSpellbook();

    /** Headless check: start the game with -TMSpellTest and the player casts every spell at
     *  spawned zombies (plus spellbook, wheel and infused shots), logs it, then quits. */
    void StartSpellSmokeTest();

    /** Health reached 0: input off, slow motion, death screen. Click / Space / R restarts the map. */
    bool IsDead() const { return bIsDead; }
    /** Real time the player died (the death screen fades in from it). */
    double GetDeathTime() const { return DeathTime; }
    /** Ignore all damage (headless tests). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Testing")
    bool bGodModeForTesting = false;

    /** Headless check: start L_CombatArena with -TMArenaTest. Fights the first waves with
     *  auto-aim, stops defending and dies, restarts once, then quits. */
    void StartArenaTest();
    /** Seconds since you were last wall running (0 while you are). */
    float GetTimeSinceWallRun() const { return TimeSinceWallRun; }
    /** Narrow the view by this much this frame (e.g. slingshot tension). Set every frame it applies. */
    void SetFOVTension(float Degrees) { FOVTension = Degrees; }

    // ── MOMENTUM ──
    /** On the ground faster than walk/sprint speed (after a grapple, wall run, slam...), you coast
     *  down at this rate (cm/s^2) instead of the normal hard stop, with or without input. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|Momentum")
    float OverspeedGroundDeceleration = 450.f;

    // ── CEILING RUN (grapple a ceiling at full speed) ──
    /** How far into the speed zoom-out you must be (1 = fully zoomed out) for a ceiling grapple to become a ceiling run. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|CeilingRun", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CeilingRunSpeedAlpha = 0.95f;

    /** Run momentum lost per second on the ceiling (cm/s^2). Mag boots keep you up there regardless. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|CeilingRun")
    float CeilingRunDeceleration = 500.f;

    /** Run momentum never goes above this (cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|CeilingRun")
    float CeilingRunMaxMomentum = 4500.f;

    /** Walking speed on the ceiling with WASD (cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|CeilingRun")
    float CeilingWalkSpeed = 550.f;

    /** Jump on the ceiling: hop away from it at this speed (the boots pull you back). Jump again mid-hop to drop. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|CeilingRun")
    float CeilingHopSpeed = 450.f;

    /** Jump on the ceiling: momentum added to the run. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|CeilingRun")
    float CeilingHopBoost = 500.f;

    /** How hard the mag boots pull you back during a hop (cm/s^2). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|CeilingRun")
    float CeilingMagPull = 1800.f;

    /** After dropping, no new ceiling run for this long (stops the view flipping back and forth). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|CeilingRun")
    float CeilingRunCooldown = 1.5f;

    /** How long the camera takes to roll over when you land on / leave the ceiling. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|CeilingRun")
    float CeilingFlipDuration = 0.45f;

    /** How hard you're pressed into the ceiling while running on it (cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|CeilingRun")
    float CeilingStickSpeed = 150.f;

    /** Push away from the ceiling when you drop off (cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement|CeilingRun")
    float CeilingDropSpeed = 300.f;

    /** 0..1: how far the current ground speed is into the speed zoom-out (1 = full effect). */
    float GetSpeedFOVAlpha() const;
    /** Fast enough and not cooling down from a previous ceiling run. */
    bool CanStartCeilingRun() const;
    bool IsCeilingRunning() const { return CurrentStateName == TEXT("CeilingRun"); }
    void StartCeilingRunCooldown();

    /**
     * Change which way is "up" for the character (bOverride=false: back to ring up).
     * Re-orients the actor immediately while keeping the camera's view exactly where
     * it was; the leftover roll then eases out over CeilingFlipDuration.
     */
    void SetSurfaceUp(const FVector& NewUp, bool bOverride);
    /** Same, but the camera turns with the body (used to blend "up" smoothly during a launch flight). */
    void SetSurfaceUpDirect(const FVector& NewUp, bool bOverride)
    {
        bSurfaceUpOverride = bOverride && !NewUp.IsNearlyZero();
        SurfaceUpOverride = NewUp.GetSafeNormal();
    }
    /** Ease facing and camera pitch toward a world point (Rate per second); mouse look still works on top. */
    void SteerViewToward(const FVector& WorldPoint, float DeltaTime, float Rate);
    FVector GetSurfaceUp() const { return bSurfaceUpOverride ? SurfaceUpOverride : CachedRingUp; }

    /** Filled in by the grapple right before it starts a ceiling run. */
    FVector CeilingRunNormal = FVector::DownVector;
    float CeilingRunEntrySpeed = 0.f;
    /** Momentum queued for the ceiling run (Tab taps), and a jump press for it to handle. */
    float CeilingRunBoost = 0.f;
    bool bCeilingJumpRequested = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Movement")
    float WallJumpLateralBoost = 500.f;

    // ── MAGIC SCALING ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic|Scaling")
    float MinSpeedForMagicScaling = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic|Scaling")
    float MaxSpeedForMagicScaling = 2000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic|Scaling")
    float MinDamageScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic|Scaling")
    float MaxDamageScale = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic|Scaling")
    float MinCooldownScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic|Scaling")
    float MaxCooldownScale = 0.3f;

    // ── PERSISTENT SIGILS ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic|Sigils")
    float HealingSigilRate = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic|Sigils")
    float SpeedBuffMultiplier = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic|Sigils")
    float SpeedBuffDuration = 3.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic|Sigils")
    float DamageBuffMultiplier = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic|Sigils")
    float DamageBuffDuration = 3.f;

    // ── MANA SYSTEM ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Mana")
    float MaxMana = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Mana")
    float StartingMana = 100.0f;

    UPROPERTY(BlueprintReadOnly, Category = "TM|Mana")
    float CurrentMana = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Mana|Regeneration")
    float ManaRegenRate = 15.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Mana|Regeneration")
    float ManaRegenDelay = 1.0f;

    // ── SPELL WHEEL (hold Q) ──
    /** Hold Q at least this long to open the spell wheel; a shorter tap places the sigil. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic")
    float SpellWheelHoldTime = 0.2f;

    /** How far the mouse has to move to sweep across the wheel (higher = more sensitive). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic")
    float SpellWheelSensitivity = 0.08f;

    bool IsSpellWheelOpen() const { return bSpellWheelOpen; }
    const TArray<FName>& GetSpellWheelOptions() const { return SpellWheelOptions; }
    int32 GetSpellWheelHighlighted() const { return SpellWheelHighlighted; }
    FVector2D GetSpellWheelCursor() const { return SpellWheelCursor; }

    /** Selected spell: tap Q places it, the wheel changes it. Must be a key in DA_SpellRegistry. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Magic")
    FName DrawSigilSpellID = TEXT("FireBlast");

    /** Testing only: abilities cost no mana. Untick for real balance testing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Mana")
    bool bInfiniteManaForTesting = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Mana|Combat")
    float ManaOnKill = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Mana|Combat")
    float ManaOnHeadshot = 15.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Mana|Combat")
    float ManaOnMelee = 20.0f;

    // ── TEMPORAL CLAP ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Temporal")
    bool bCanUseTemporalClap = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Temporal")
    float TemporalClapCooldown = 15.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Temporal")
    float TemporalTimeDilation = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Temporal")
    float TemporalClapDuration = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Temporal")
    float ClapAnimationDuration = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Temporal|Mana")
    float TemporalClapManaCost = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Temporal|Mana")
    bool bTemporalClapRefundOnNoSigils = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Temporal|Mana")
    float TemporalClapRefundPercent = 0.75f;

    /** Field of view while the temporal clap is active (the camera widens instead of tilting). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Temporal|Camera")
    float TemporalCameraFOV = 105.0f;

    /** How fast the view widens when the clap starts (higher = punchier). It eases back at FOVChangeSpeed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Temporal|Camera")
    float TemporalFOVChangeSpeed = 10.0f;

    UPROPERTY(BlueprintReadOnly, Category = "TM|Temporal")
    bool bIsTemporalActive = false;

    UPROPERTY(BlueprintReadOnly, Category = "TM|Temporal")
    float TemporalTimeRemaining = 0.0f;

    //new properties
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_Fire;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_Aim;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_Reload;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_Melee;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_Crouch;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_Temporal;

    // ── RING GRAVITY / CAMERA ──
    // Assign this in BP_TMCharacter to whatever scene component holds the
    // camera (the Camera component itself, or a parent socket). Bypasses
    // ControlRotation entirely: pitch applies only here, yaw is baked into
    // the actor's own rotation every tick alongside the ring-up alignment.
    // See RingFrameUtils.h for why ControlRotation doesn't work as-is on a
    // ring (it's a world-axis rotator, not gravity-relative).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|RingGravity")
    TObjectPtr<USceneComponent> CameraPitchTarget;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|RingGravity")
    float LookYawSensitivity = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|RingGravity")
    float LookPitchSensitivity = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|RingGravity", meta = (ClampMin = "-89.0", ClampMax = "0.0"))
    float MinCameraPitch = -85.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|RingGravity", meta = (ClampMin = "0.0", ClampMax = "89.0"))
    float MaxCameraPitch = 85.0f;

    /** World-space local up at the character's current ring position. States read this instead of FVector::UpVector. */
    UFUNCTION(BlueprintPure, Category = "TM|RingGravity")
    FVector GetRingUp() const { return CachedRingUp; }

    /**
     * Turn the player to face Direction (flattened to the ground plane) immediately.
     * The camera keeps looking where it was and eases round over CameraRecenterSpeed,
     * so gameplay snaps (e.g. grapple -> wall run) don't jerk the view.
     */
    void SetFacingDirection(const FVector& Direction);

    /** How fast the camera eases back behind the player after a facing snap (higher = quicker). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|RingGravity")
    float CameraRecenterSpeed = 8.0f;


    // ── STATE ──
    UPROPERTY(BlueprintReadOnly, Category = "TM|State")
    FString CurrentStateName = "Idle";

    // ── UFUNCTIONs ──
    UFUNCTION(BlueprintCallable, Category = "TM|Movement")
    bool IsWallRunning() const;

    UFUNCTION(BlueprintCallable, Category = "TM|Movement")
    bool IsSliding() const;

    UFUNCTION(BlueprintCallable, Category = "TM|Movement")
    bool IsSprinting() const;

    UFUNCTION(BlueprintCallable, Category = "TM|Movement")
    float GetCurrentSpeed() const;

    UFUNCTION(BlueprintCallable, Category = "TM|Movement")
    float GetSpeedScalingFactor() const;

    // ── MANA FUNCTIONS ──
    UFUNCTION(BlueprintCallable, Category = "TM|Mana")
    bool ConsumeMana(float ManaCost);

    UFUNCTION(BlueprintCallable, Category = "TM|Mana")
    void AddMana(float ManaAmount);

    UFUNCTION(BlueprintPure, Category = "TM|Mana")
    float GetManaPercentage() const { return CurrentMana / MaxMana; }

    UFUNCTION(BlueprintPure, Category = "TM|Mana")
    bool HasEnoughMana(float ManaCost) const { return CurrentMana >= ManaCost; }

    // ── TEMPORAL FUNCTIONS ──
    UFUNCTION(BlueprintCallable, Category = "TM|Temporal")
    void ActivateTemporalClap();

    UFUNCTION(BlueprintCallable, Category = "TM|Temporal")
    void EndTemporalClap();

    UFUNCTION(BlueprintPure, Category = "TM|Temporal")
    bool CanActivateTemporalClap() const;

    // ── BUFF FUNCTIONS ──
    UFUNCTION(BlueprintCallable, Category = "TM|Magic|Buffs")
    void ApplySpeedBuff(float Multiplier, float Duration);

    UFUNCTION(BlueprintCallable, Category = "TM|Magic|Buffs")
    void ApplyDamageBuff(float Multiplier, float Duration);

    /** Enemy hits arrive here. The player's own damage (e.g. their FireBlast) is ignored. No death yet. */
    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;

    /** Current damage buff multiplier (1 = no buff). Used by the gun and spells. */
    UFUNCTION(BlueprintPure, Category = "TM|Magic|Buffs")
    /** Damage buff from sigils (ApplyDamageBuff) times any Damage buff (Might). */
    float GetDamageMultiplier() const;

    UFUNCTION(BlueprintCallable, Category = "TM|Magic|Buffs")
    float GetHealth() const { return Health; }

    UFUNCTION(BlueprintCallable, Category = "TM|Magic|Buffs")
    float GetMaxHealth() const { return MaxHealth; }

    UFUNCTION(BlueprintCallable, Category = "TM|Magic|Buffs")
    void SetHealth(float NewHealth) { Health = NewHealth; }

    // ── GRAPPLE FUNCTIONS ──
    UFUNCTION(BlueprintPure, Category = "TM|Grapple")
    bool IsGrappling() const;

    UFUNCTION(BlueprintPure, Category = "TM|Grapple")
    EGrappleTier GetGrappleTier() const;

    UFUNCTION(BlueprintCallable, Category = "TM|Grapple")
    void UpgradeGrappleTier();

    // ── DESTRUCTION FUNCTIONS ──
    UFUNCTION(BlueprintCallable, Category = "TM|Destruction")
    bool IsLocationDestructible(FVector Location) const;

    UFUNCTION(BlueprintCallable, Category = "TM|Destruction")
    bool CanWallRunAtLocation(FVector Location) const;

    // ── DELEGATES ──
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnManaChanged, float, NewMana, float, MaxMana);
    UPROPERTY(BlueprintAssignable, Category = "TM|Mana")
    FOnManaChanged OnManaChanged;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnManaDepleted, float, TimeUntilRegen);
    UPROPERTY(BlueprintAssignable, Category = "TM|Mana")
    FOnManaDepleted OnManaDepleted;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnManaRegenStarted);
    UPROPERTY(BlueprintAssignable, Category = "TM|Mana")
    FOnManaRegenStarted OnManaRegenStarted;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTemporalClapDelegate);
    UPROPERTY(BlueprintAssignable, Category = "TM|Temporal")
    FTemporalClapDelegate OnTemporalClapStarted;

    UPROPERTY(BlueprintAssignable, Category = "TM|Temporal")
    FTemporalClapDelegate OnTemporalClapEnded;

    UPROPERTY(BlueprintAssignable, Category = "TM|Temporal")
    FTemporalClapDelegate OnClapAnimationComplete;

    // ── COMPONENTS ──
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Magic")
    TObjectPtr<UTransfigurationCastingComponent> TransfigurationCasting;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Magic")
    TObjectPtr<UTransfigurationComboDetector> ComboDetector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grapple")
    TObjectPtr<UGrappleComponent> GrappleComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    TObjectPtr<UWeaponComponent> WeaponComponent;

    /** Ring-world launch pads: aiming at the ring and the flight there. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ring World")
    TObjectPtr<URingLaunchComponent> LaunchComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Magic")
    TObjectPtr<UBuffEffectComponent> BuffEffects;

    /** Speed buff multiplier (1 = none). */
    float GetSpeedBuffMultiplier() const;

    /** Movement states call this with WalkSpeed / SprintSpeed; buffs are applied on top. */
    void SetBaseWalkSpeed(float NewBase);

    /** Re-apply MaxWalkSpeed = base speed x speed buff (called when buffs change). */
    void RefreshMovementSpeed();

    // ── STATE MANAGEMENT ──
    void SetStateName(const FString& Name) { CurrentStateName = Name; }

    template<typename T>
    void TransitionTo()
    {
        StateMachine.TransitionTo(std::make_unique<T>(this));
    }

    // ── RUNTIME FLAGS ──
    bool bSprintHeld = false;
    bool bSlidePressed = false;
    int32 JumpsRemaining = 2;
    bool bWantsToTeleport = false;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void NotifyControllerChanged() override;

    /**
     * Called every Tick before the state machine runs. Computes local up
     * from the character's world position, pushes it into
     * CharacterMovementComponent via SetGravityDirection (native custom
     * gravity support, UE 5.4+) so floor detection/walking happen correctly
     * in the rotated frame, then aligns the actor's own rotation so
     * GetActorForwardVector()/GetActorRightVector() — which every movement
     * state already calls — read correctly without any state-level changes.
     */
    void UpdateRingGravity(float DeltaTime);

private:
    TMStateMachine StateMachine;

    // ── INPUT ASSETS ──
    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_Move;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_Look;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_Jump;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_Sprint;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_Slide;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_DrawSigil;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_PlaceSigil;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_TriggerSigil;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_TriggerAll;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_TemporalClap;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_Grapple;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_GrappleKick;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_GrappleSlam;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_GrappleToss;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_GrappleEvade;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_GrapplePull;

    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_WebTrap;

    /** Spellbook (B). Loaded from /Game/Input/IA_Spellbook if left empty. */
    UPROPERTY(EditAnywhere, Category = "TM|Input")
    TObjectPtr<UInputAction> IA_Spellbook;

    // ── INPUT HANDLERS ──
    void OnMove(const FInputActionValue& Value);
    void OnLook(const FInputActionValue& Value);
    void OnJumpPressed();
    void OnSprintStarted();
    void OnSprintEnded();
    void OnSlidePressed();
    void OnDrawSigilPressed();
    void OnPlaceSigilPressed();
    void OnPlaceSigilReleased();
    void OpenSpellWheel();
    void CloseSpellWheel();

    bool bSpellWheelOpen = false;
    TArray<FName> SpellWheelOptions;
    int32 SpellWheelHighlighted = INDEX_NONE;
    FVector2D SpellWheelCursor = FVector2D::ZeroVector;
    FTimerHandle SpellWheelOpenTimer;
    void OnTriggerSigilPressed();
    void OnTriggerAllPressed();
    void OnTemporalClapPressed();
    void OnGrapplePressed();
    void OnGrappleReleased();

    /** View-direction trace on the Weapon channel, starting level with the character (not at the camera). */
    bool TraceFromView(FHitResult& OutHit, float Range) const;

    /** Shared body of the Kick/Slam/Toss/Pull handlers. */
    void TryCombatGrapple(ECombatGrappleMove Move);
    void OnGrappleKickPressed();
    void OnGrappleSlamPressed();
    void OnGrappleTossPressed();
    void OnGrappleEvadePressed();
    void OnGrapplePullPressed();
    void OnWebTrapPressed();
    void OnFirePressed();
    void OnFireReleased();

    // ── MANA SYSTEM ──
    FTimerHandle ManaRegenDelayHandle;
    FTimerHandle ManaRegenTickHandle;
    bool bManaRegenActive = false;

    void StartManaRegen();
    void StopManaRegen();
    void RegenerateManaTick();

    // ── TEMPORAL SYSTEM ──
    FTimerHandle TemporalTimerHandle;
    FTimerHandle ClapAnimationTimerHandle;
    FTimerHandle CooldownTimerHandle;

    float OriginalTimeDilation;
    float OriginalCameraFOV;
    FRotator OriginalCameraRotation;

    void StartTemporalEffects();
    void EndTemporalEffects();
    void ExecuteClapSigilActivation();
    void BeginTemporalCooldown();

    // ── RING GRAVITY STATE ──
    // Yaw is baked directly into actor rotation each tick (see UpdateRingGravity).
    // Pitch applies only to CameraPitchTarget. Neither uses ControlRotation.
    FVector CachedRingUp = FVector::UpVector;
    FVector CachedFacingForward = FVector::ForwardVector;
    float CameraPitch = 0.0f;

    /** Camera yaw offset (degrees) left over from a facing snap; decays to 0. */
    float CameraYawOffset = 0.0f;

    /** Current camera roll (wall-run tilt) and FOV, eased every tick. */
    float CameraRoll = 0.0f;
    float CurrentFOV = 90.0f;

    /** Extra roll left over from a ceiling flip, eased from CameraFlipStart to 0. */
    float CameraFlipRoll = 0.0f;
    float CameraFlipStart = 0.0f;
    float CameraFlipElapsed = 0.0f;

    bool bSurfaceUpOverride = false;
    bool bOverspeedBraking = false;
    float FOVKick = 0.f;
    float RecoilPitch = 0.f;
    float RecoilYaw = 0.f;
    float FOVTension = 0.f;
    float CameraShakeTrauma = 0.f;
    float CameraShakeTime = 0.f;
    FRotator CameraShakeRot = FRotator::ZeroRotator;
    double LastHurtTime = -100.0;
    bool bSpellbookOpen = false;
    bool bIsDead = false;
    double DeathTime = 0.0;
    void HandleDeath();
    void RestartRun();
    void ArenaTestTick();
    FTimerHandle ArenaTestTimer;
    double ArenaTestStart = 0.0;
    bool bOnSlipperyFloor = false;
    float SavedGroundFriction = 8.f;
    void SpellSmokeTestStep();
    FTimerHandle SmokeTestTimer;
    int32 SmokeStep = 0;
    float LastWallRunSide = 0.f;
    float TimeSinceWallRun = 100.f;
    float DefaultBrakingDecelerationWalking = 2048.f;
    void UpdateMomentumBraking();
    FVector SurfaceUpOverride = FVector::UpVector;
    float CeilingRunReadyTime = 0.0f;

    UPROPERTY()
    TObjectPtr<UCameraComponent> ViewCamera;

    void SetupFirstPersonCamera();
    void UpdateCameraEffects(float DeltaTime);

    float BaseWalkSpeed = 600.f;
    FTimerHandle DamageBuffTimerHandle;
    float PendingYawInput = 0.0f;
    float PendingPitchInput = 0.0f;

    // ── HEALTH ──
    UPROPERTY()
    float Health = 100.f;

    UPROPERTY()
    float MaxHealth = 100.f;

    UPROPERTY()
    float CurrentDamageMultiplier = 1.0f;
};