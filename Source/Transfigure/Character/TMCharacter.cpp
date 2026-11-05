// Source/TMovement/Private/Character/TMCharacter.cpp

#include "Character/TMCharacter.h"
#include "States/TMState_Idle.h"
#include "States/TMState_Walk.h"
#include "States/TMState_Sprint.h"
#include "States/TMState_Slide.h"
#include "States/TMState_Jump.h"
#include "States/TMState_DoubleJump.h"
#include "States/TMState_WallRun.h"
#include "States/TMState_WallJump.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Transfiguration/TransfigurationCastingComponent.h"
#include "Transfiguration/TransfigurationComboDetector.h"
#include "Transfiguration/BuffEffectComponent.h"
#include "Grapple/GrappleComponent.h"
#include "Combat/WeaponComponent.h"
#include "Character/TMHUD.h"
#include "Enemy/BaseEnemy.h"
#include "Transfigure.h"
#include "Destruction/DestructionManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"
#include "RingFrameUtils.h"

ATMCharacter::ATMCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;

    // We drive actor rotation ourselves every tick from ring gravity + yaw
    // input (see UpdateRingGravity). ControlRotation stays untouched, and
    // AddControllerYawInput/PitchInput are no longer used for the actor.
    bUseControllerRotationYaw = false;
    bUseControllerRotationPitch = false;
    bUseControllerRotationRoll = false;
    GetCharacterMovement()->bOrientRotationToMovement = false;

    TransfigurationCasting = CreateDefaultSubobject<UTransfigurationCastingComponent>(
        TEXT("TransfigurationCasting"));
    ComboDetector = CreateDefaultSubobject<UTransfigurationComboDetector>(
        TEXT("ComboDetector"));
    GrappleComponent = CreateDefaultSubobject<UGrappleComponent>(
        TEXT("GrappleComponent"));
    WeaponComponent = CreateDefaultSubobject<UWeaponComponent>(
        TEXT("WeaponComponent"));
    BuffEffects = CreateDefaultSubobject<UBuffEffectComponent>(
        TEXT("BuffEffects"));
}

void ATMCharacter::BeginPlay()
{
    Super::BeginPlay();
    UE_LOG(LogTemp, Warning, TEXT("[TM] BeginPlay  Controller=%s  Mapping=%s  Loc=%s  Flat=%d"),
        *GetNameSafe(GetController()), *GetNameSafe(DefaultMappingContext),
        *GetActorLocation().ToString(), URingWorldSettings::GetSpec().bFlatWorldForTesting ? 1 : 0);


    // The camera pitches via CameraPitchTarget and yaws with the actor; it must
    // NOT follow ControlRotation (which this class never changes). If the
    // Blueprint didn't assign a target, use its SpringArm.
    if (!CameraPitchTarget)
    {
        CameraPitchTarget = FindComponentByClass<USpringArmComponent>();
    }
    if (USpringArmComponent* Arm = Cast<USpringArmComponent>(CameraPitchTarget))
    {
        Arm->bUsePawnControlRotation = false;
        Arm->bInheritPitch = true;
        Arm->bInheritYaw = true;
        Arm->bInheritRoll = true;
    }
    if (!CameraPitchTarget)
    {
        UE_LOG(LogTemp, Error, TEXT("[TM] No CameraPitchTarget and no SpringArm on %s - camera pitch will not work"), *GetName());
    }
    SetupFirstPersonCamera();
    if (!GetMesh() || !GetMesh()->GetSkeletalMeshAsset())
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] %s has no skeletal mesh assigned - the player will be invisible"), *GetName());
    }

    // Keep the Player Start's facing instead of snapping to world +X.
    CachedFacingForward = GetActorForwardVector();
    TransitionTo<TMState_Idle>();

    CurrentMana = StartingMana;
    StartManaRegen();
}

// Runs whenever a controller possesses/unpossesses this pawn. Possession can
// happen after BeginPlay, so the input mapping is added here, not in BeginPlay.
void ATMCharacter::NotifyControllerChanged()
{
    Super::NotifyControllerChanged();
    UE_LOG(LogTemp, Warning, TEXT("[TM] NotifyControllerChanged  Controller=%s"), *GetNameSafe(GetController()));

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        // Placeholder canvas HUD (crosshair, status, spell wheel). Installed here so the
        // Game Mode's HUD Class doesn't need changing.
        if (!Cast<ATMHUD>(PC->GetHUD()))
        {
            PC->ClientSetHUD(ATMHUD::StaticClass());
        }

        if (auto* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (DefaultMappingContext)
            {
                Sub->AddMappingContext(DefaultMappingContext, 0);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[TM] DefaultMappingContext is not set on %s"), *GetClass()->GetName());
            }
        }
    }
}

void ATMCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    UpdateCameraEffects(DeltaTime);
    UpdateRingGravity(DeltaTime);
    StateMachine.Tick(DeltaTime);
#if !UE_BUILD_SHIPPING
    // [TM] diagnostics. Remove once movement is confirmed.
    // Every state change, with jumps left, so mid-air relaunches can be traced.
    static FString LastLoggedState;
    if (CurrentStateName != LastLoggedState)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] State %s -> %s  JumpsRemaining=%d  Z=%.0f  Grounded=%d"),
            *LastLoggedState, *CurrentStateName, JumpsRemaining, GetActorLocation().Z,
            GetCharacterMovement()->IsMovingOnGround() ? 1 : 0);
        LastLoggedState = CurrentStateName;
    }

    // Once per second: position, velocity, movement mode, gravity.
    static double LastTMLog = -10.0;
    const double Now = GetWorld()->GetRealTimeSeconds();
    if (Now < LastTMLog) LastTMLog = -10.0; // new PIE session
    if (Now - LastTMLog > 1.0)
    {
        LastTMLog = Now;
        const UCharacterMovementComponent* CMC = GetCharacterMovement();
        UE_LOG(LogTemp, Warning, TEXT("[TM] Loc=%s  Vel=%s  Speed=%.0f  MaxWalk=%.0f  Mode=%s  GravDir=%s  Controller=%s  State=%s"),
            *GetActorLocation().ToString(), *GetVelocity().ToString(), GetVelocity().Size(), CMC->MaxWalkSpeed,
            *CMC->GetMovementName(), *CMC->GetGravityDirection().ToString(),
            *GetNameSafe(GetController()), *CurrentStateName);
    }
#endif
}

void ATMCharacter::UpdateRingGravity(float DeltaTime)
{
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();

    // Local up at wherever we physically are right now.
    CachedRingUp = -URingWorldGeometry::GetGravityDirection(Spec, GetActorLocation());

    // Tell CharacterMovementComponent about it. This is the UE 5.4+ native
    // custom-gravity path: CMC internally uses RotateWorldToGravity /
    // RotateGravityToWorld for floor detection, walkable-slope checks, and
    // gravity integration, so IsMovingOnGround() and normal walking physics
    // keep working correctly without us reimplementing floor traces.
    GetCharacterMovement()->SetGravityDirection(-CachedRingUp);

    // Apply accumulated yaw around the CURRENT up axis, then re-orthonormalize
    // against the (possibly just-changed) up so facing direction stays valid
    // as the surface curves underneath us.
    if (!FMath::IsNearlyZero(PendingYawInput))
    {
        const FQuat YawRot(CachedRingUp, FMath::DegreesToRadians(PendingYawInput));
        CachedFacingForward = YawRot.RotateVector(CachedFacingForward);
        PendingYawInput = 0.0f;
    }
    CachedFacingForward = (CachedFacingForward -
        CachedRingUp * FVector::DotProduct(CachedFacingForward, CachedRingUp)).GetSafeNormal();

    if (CachedFacingForward.IsNearlyZero())
    {
        // Degenerate only if we were somehow facing exactly along the axis.
        CachedFacingForward = FVector::CrossProduct(FVector::RightVector, CachedRingUp).GetSafeNormal();
    }

    // Set the actor's rotation directly from the (Forward, Right, Up) basis.
    // Every state function calls GetActorForwardVector()/GetActorRightVector()
    // already — this is the one change that makes all of them correct on the
    // ring without touching their bodies.
    SetActorRotation(FRotationMatrix::MakeFromXZ(CachedFacingForward, CachedRingUp).ToQuat());

    // Pitch is camera-only, never touches the actor.
    if (!FMath::IsNearlyZero(PendingPitchInput))
    {
        CameraPitch = FMath::Clamp(CameraPitch - PendingPitchInput, MinCameraPitch, MaxCameraPitch);
        PendingPitchInput = 0.0f;
    }
    if (CameraPitchTarget)
    {
        // Ease out any yaw offset left by a facing snap.
        CameraYawOffset = FMath::FInterpTo(CameraYawOffset, 0.0f, DeltaTime, CameraRecenterSpeed);
        CameraPitchTarget->SetRelativeRotation(FRotator(CameraPitch, CameraYawOffset, CameraRoll));
    }
}

void ATMCharacter::SetupFirstPersonCamera()
{
    ViewCamera = FindComponentByClass<UCameraComponent>();
    if (ViewCamera)
    {
        ViewCamera->bUsePawnControlRotation = false;
        CurrentFOV = BaseFOV;
        ViewCamera->SetFieldOfView(BaseFOV);
    }
    if (!bFirstPerson) return;

    // Collapse the third-person boom onto the eyes. The spring arm stays as the pitch pivot.
    if (USpringArmComponent* Arm = Cast<USpringArmComponent>(CameraPitchTarget))
    {
        Arm->TargetArmLength = 0.f;
        Arm->SocketOffset = FVector::ZeroVector;
        Arm->TargetOffset = FVector::ZeroVector;
        Arm->bDoCollisionTest = false;
        Arm->bEnableCameraLag = false;
        Arm->bEnableCameraRotationLag = false;
    }
    if (CameraPitchTarget)
    {
        CameraPitchTarget->SetRelativeLocation(FVector(0.f, 0.f, FirstPersonEyeHeight));
    }
    if (ViewCamera && ViewCamera != CameraPitchTarget)
    {
        ViewCamera->SetRelativeLocation(FVector::ZeroVector);   // the Blueprint had it 400 cm back
        ViewCamera->SetRelativeRotation(FRotator::ZeroRotator);
    }
    if (GetMesh())
    {
        GetMesh()->SetOwnerNoSee(true);   // never see the inside of our own body
    }
    UE_LOG(LogTemp, Warning, TEXT("[TM] First-person camera: %s on %s, eye height %.0f, FOV %.0f"),
        *GetNameSafe(ViewCamera), *GetNameSafe(ViewCamera ? ViewCamera->GetAttachParent() : nullptr),
        FirstPersonEyeHeight, BaseFOV);
}

void ATMCharacter::UpdateCameraEffects(float DeltaTime)
{
    // Roll: lean away from the wall while wall running.
    const float TargetRoll = -WallRunSide * WallRunCameraTilt;
    CameraRoll = FMath::FInterpTo(CameraRoll, TargetRoll, DeltaTime, CameraTiltSpeed);

    // FOV: widen with ground speed so going fast feels fast.
    const float Speed = URingFrameUtils::StripVertical(GetVelocity(), CachedRingUp).Size();
    const float SpeedAlpha = FMath::Clamp((Speed - SpeedFOVStartSpeed) / FMath::Max(SpeedFOVFullSpeed - SpeedFOVStartSpeed, 1.f), 0.f, 1.f);
    const float TargetFOV = (bIsTemporalActive ? TemporalCameraFOV : BaseFOV) + SpeedFOVBoost * SpeedAlpha;
    // The clap snaps the view wide; leaving it (and speed changes) ease more gently.
    const float FOVSpeed = (bIsTemporalActive && TargetFOV > CurrentFOV) ? TemporalFOVChangeSpeed : FOVChangeSpeed;
    CurrentFOV = FMath::FInterpTo(CurrentFOV, TargetFOV, DeltaTime, FOVSpeed);
    if (ViewCamera)
    {
        ViewCamera->SetFieldOfView(CurrentFOV);
    }
}

void ATMCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    auto* EIC = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);

    // Movement Inputs
    EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &ATMCharacter::OnMove);
    EIC->BindAction(IA_Look, ETriggerEvent::Triggered, this, &ATMCharacter::OnLook);
    EIC->BindAction(IA_Jump, ETriggerEvent::Started, this, &ATMCharacter::OnJumpPressed);
    EIC->BindAction(IA_Sprint, ETriggerEvent::Started, this, &ATMCharacter::OnSprintStarted);
    EIC->BindAction(IA_Sprint, ETriggerEvent::Completed, this, &ATMCharacter::OnSprintEnded);
    EIC->BindAction(IA_Slide, ETriggerEvent::Started, this, &ATMCharacter::OnSlidePressed);

    // Magic Inputs
    EIC->BindAction(IA_DrawSigil, ETriggerEvent::Started, this, &ATMCharacter::OnDrawSigilPressed);
    // Q: tap = place the selected sigil at the crosshair, hold = spell wheel.
    EIC->BindAction(IA_PlaceSigil, ETriggerEvent::Started, this, &ATMCharacter::OnPlaceSigilPressed);
    EIC->BindAction(IA_PlaceSigil, ETriggerEvent::Completed, this, &ATMCharacter::OnPlaceSigilReleased);
    EIC->BindAction(IA_TriggerSigil, ETriggerEvent::Started, this, &ATMCharacter::OnTriggerSigilPressed);
    EIC->BindAction(IA_TriggerAll, ETriggerEvent::Started, this, &ATMCharacter::OnTriggerAllPressed);

    // Temporal Input
    EIC->BindAction(IA_TemporalClap, ETriggerEvent::Started, this, &ATMCharacter::OnTemporalClapPressed);

    // Grapple Inputs
    // Was bound to Started, Triggered (every frame held) and Completed, all firing the grapple.
    // Now: press to fire, release to let go.
    EIC->BindAction(IA_Grapple, ETriggerEvent::Started, this, &ATMCharacter::OnGrapplePressed);
    EIC->BindAction(IA_Grapple, ETriggerEvent::Completed, this, &ATMCharacter::OnGrappleReleased);
    EIC->BindAction(IA_GrappleKick, ETriggerEvent::Started, this, &ATMCharacter::OnGrappleKickPressed);
    EIC->BindAction(IA_GrappleSlam, ETriggerEvent::Started, this, &ATMCharacter::OnGrappleSlamPressed);
    EIC->BindAction(IA_GrappleToss, ETriggerEvent::Started, this, &ATMCharacter::OnGrappleTossPressed);
    EIC->BindAction(IA_GrappleEvade, ETriggerEvent::Started, this, &ATMCharacter::OnGrappleEvadePressed);
    EIC->BindAction(IA_GrapplePull, ETriggerEvent::Started, this, &ATMCharacter::OnGrapplePullPressed);
    EIC->BindAction(IA_WebTrap, ETriggerEvent::Started, this, &ATMCharacter::OnWebTrapPressed);

    // Combat Inputs
    if (IA_Fire)
    {
        EIC->BindAction(IA_Fire, ETriggerEvent::Started, this, &ATMCharacter::OnFirePressed);
    }
}

// ── MOVEMENT INPUT HANDLERS ──

void ATMCharacter::OnMove(const FInputActionValue& Value)
{
    FVector2D Vec = Value.Get<FVector2D>();
    AddMovementInput(GetActorForwardVector(), Vec.Y);
    AddMovementInput(GetActorRightVector(), Vec.X);
}

void ATMCharacter::OnLook(const FInputActionValue& Value)
{
    FVector2D Vec = Value.Get<FVector2D>();

    if (bSpellWheelOpen)
    {
        // Mouse drives the wheel cursor instead of the camera. Screen Y points down;
        // the look action already negates Y, so mouse-up arrives as negative Y.
        SpellWheelCursor = (SpellWheelCursor + FVector2D(Vec.X, Vec.Y) * SpellWheelSensitivity).GetClampedToMaxSize(1.f);
        if (SpellWheelCursor.Size() > 0.35f && SpellWheelOptions.Num() > 0)
        {
            const float Step = 2.f * PI / SpellWheelOptions.Num();
            float Angle = FMath::Atan2(SpellWheelCursor.Y, SpellWheelCursor.X) + PI * 0.5f; // 0 = top
            if (Angle < 0.f) Angle += 2.f * PI;
            SpellWheelHighlighted = FMath::RoundToInt(Angle / Step) % SpellWheelOptions.Num();
        }
        return;
    }

    // Accumulated and consumed in UpdateRingGravity, not applied to
    // ControlRotation — see the comment on CameraPitchTarget in the header
    // for why (ControlRotation is a world-axis rotator, not gravity-relative).
    PendingYawInput += Vec.X * LookYawSensitivity;
    PendingPitchInput += Vec.Y * LookPitchSensitivity;
}

void ATMCharacter::OnJumpPressed()
{
    UE_LOG(LogTemp, Warning, TEXT("[TM] Jump pressed  State=%s  JumpsRemaining=%d  Grounded=%d"),
        *CurrentStateName, JumpsRemaining, GetCharacterMovement()->IsMovingOnGround() ? 1 : 0);
    if (JumpsRemaining <= 0) return;

    --JumpsRemaining;

    if (GetCharacterMovement()->IsMovingOnGround())
    {
        // Standard ground jump
        TransitionTo<TMState_Jump>();
    }
    else if (TMState_WallRun::CanEnter(this))
    {
        // Mid-air near a wall — wall jump instead
        TransitionTo<TMState_WallJump>();
    }
    else if (JumpsRemaining > 0)
    {
        // Airborne with jumps left — double jump
        TransitionTo<TMState_DoubleJump>();
    }
}


// ── FACING ──

void ATMCharacter::SetFacingDirection(const FVector& Direction)
{
    const FVector Flat = FVector::VectorPlaneProject(Direction, CachedRingUp).GetSafeNormal();
    if (Flat.IsNearlyZero()) return;

    // Signed yaw change around local up. The camera is offset by the opposite
    // amount so its world direction doesn't move this frame, then eases in.
    const float Cos = FVector::DotProduct(CachedFacingForward, Flat);
    const float Sin = FVector::DotProduct(FVector::CrossProduct(CachedFacingForward, Flat), CachedRingUp);
    const float DeltaYaw = FMath::RadiansToDegrees(FMath::Atan2(Sin, Cos));
    CameraYawOffset = FRotator::NormalizeAxis(CameraYawOffset - DeltaYaw);

    CachedFacingForward = Flat;
    // Apply now (not next tick) so side traces like wall-run detection see the new facing.
    SetActorRotation(FRotationMatrix::MakeFromXZ(Flat, CachedRingUp).ToQuat());
}

// ── DAMAGE ──

float ATMCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    // Our own spells (FireBlast radial damage) shouldn't hurt us.
    if (DamageCauser == this || EventInstigator == GetController()) return 0.f;

    const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    Health = FMath::Max(0.f, Health - Applied);
    UE_LOG(LogTemp, Warning, TEXT("[TM] Player took %.0f damage from %s, health %.0f"),
        Applied, *GetNameSafe(DamageCauser), Health);
    return Applied;
}

// ── COMBAT INPUT HANDLERS ──

void ATMCharacter::OnFirePressed()
{
    if (WeaponComponent) WeaponComponent->FireWeapon();
}

// ── MAGIC INPUT HANDLERS ──

void ATMCharacter::OnDrawSigilPressed()
{
    TransfigurationCasting->BeginDrawMode(DrawSigilSpellID);
}

void ATMCharacter::OnPlaceSigilPressed()
{
    // Start the hold timer; if Q is still down when it fires, the wheel opens.
    GetWorldTimerManager().SetTimer(SpellWheelOpenTimer, this, &ATMCharacter::OpenSpellWheel,
        SpellWheelHoldTime, false);
}

void ATMCharacter::OnPlaceSigilReleased()
{
    if (bSpellWheelOpen)
    {
        CloseSpellWheel();
        return;
    }

    // Short tap: place the selected sigil at the crosshair right now.
    GetWorldTimerManager().ClearTimer(SpellWheelOpenTimer);
    if (TransfigurationCasting)
    {
        TransfigurationCasting->CastAtAim(DrawSigilSpellID);
    }
}

void ATMCharacter::OpenSpellWheel()
{
    SpellWheelOptions = TransfigurationCasting ? TransfigurationCasting->GetAvailableSpellIDs() : TArray<FName>();
    SpellWheelHighlighted = SpellWheelOptions.IndexOfByKey(DrawSigilSpellID);
    SpellWheelCursor = FVector2D::ZeroVector;
    bSpellWheelOpen = true;
    UE_LOG(LogTemp, Warning, TEXT("[TM] Spell wheel opened (%d spells)"), SpellWheelOptions.Num());
}

void ATMCharacter::CloseSpellWheel()
{
    bSpellWheelOpen = false;
    if (SpellWheelOptions.IsValidIndex(SpellWheelHighlighted))
    {
        DrawSigilSpellID = SpellWheelOptions[SpellWheelHighlighted];
    }
    UE_LOG(LogTemp, Warning, TEXT("[TM] Spell wheel closed, selected %s"), *DrawSigilSpellID.ToString());
}

void ATMCharacter::OnTriggerSigilPressed()
{
    TransfigurationCasting->TriggerNearestManualSigil();
}

void ATMCharacter::OnTriggerAllPressed()
{
    TransfigurationCasting->TriggerAllSigils();
}

// ── GRAPPLE INPUT HANDLERS ──

bool ATMCharacter::TraceFromView(FHitResult& OutHit, float Range) const
{
    if (!GetController()) return false;

    FVector CamLoc;
    FRotator CamRot;
    GetController()->GetPlayerViewPoint(CamLoc, CamRot);
    const FVector Dir = CamRot.Vector();

    // Same idea as the gun: start level with the character so objects
    // between the camera and the player (e.g. a wall you're running on)
    // can't block the trace. Range is measured from the character.
    const float ToCharacter = FMath::Max(FVector::DotProduct(GetActorLocation() - CamLoc, Dir), 0.f);
    const FVector Start = CamLoc + Dir * ToCharacter;

    FCollisionQueryParams Params(SCENE_QUERY_STAT(PlayerViewTrace), false, this);
    return GetWorld()->LineTraceSingleByChannel(OutHit, Start, Start + Dir * Range, ECC_Weapon, Params);
}

void ATMCharacter::OnGrapplePressed()
{
    FHitResult Hit;
    if (GrappleComponent && TraceFromView(Hit, GrappleComponent->MaxGrappleRange))
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple pressed: hit %s"), *GetNameSafe(Hit.GetActor()));
        GrappleComponent->FireGrapple(Hit.ImpactPoint, Cast<ABaseEnemy>(Hit.GetActor()), Hit.ImpactNormal);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple pressed: nothing in range"));
    }
}

void ATMCharacter::OnGrappleReleased()
{
    if (GrappleComponent) GrappleComponent->ReleaseGrapple();
}

void ATMCharacter::TryCombatGrapple(ECombatGrappleMove Move)
{
    FHitResult Hit;
    if (GrappleComponent && TraceFromView(Hit, GrappleComponent->MaxGrappleRange))
    {
        GrappleComponent->ExecuteCombatMove(Move, Hit.GetActor());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Combat move %s: nothing in range"), *UEnum::GetValueAsString(Move));
    }
}

void ATMCharacter::OnGrappleKickPressed()  { TryCombatGrapple(ECombatGrappleMove::Kick); }
void ATMCharacter::OnGrappleSlamPressed()
{
    // Slam is a movement tech: aim anywhere (floor, wall, enemy). Aiming at
    // nothing in the air turns it into a ground-pound.
    if (!GrappleComponent) return;
    FHitResult Hit;
    const bool bHit = TraceFromView(Hit, GrappleComponent->MaxGrappleRange);
    GrappleComponent->StartSlam(Hit.ImpactPoint, Hit.ImpactNormal, Hit.GetActor(), bHit);
}
void ATMCharacter::OnGrappleTossPressed()  { TryCombatGrapple(ECombatGrappleMove::Toss); }

void ATMCharacter::OnGrappleEvadePressed()
{
    if (!GrappleComponent) return;

    // Use last movement input vector (Enhanced Input compatible).
    // Fall back to actor forward if no directional input is held.
    FVector Direction = GetLastMovementInputVector().GetSafeNormal();
    if (Direction.IsNearlyZero())
    {
        Direction = GetActorForwardVector();
    }

    // Pass direction to grapple component before executing the move
    GrappleComponent->SetEvadeDirection(Direction);
    GrappleComponent->ExecuteCombatMove(ECombatGrappleMove::Evade, nullptr);
}

void ATMCharacter::OnGrapplePullPressed()  { TryCombatGrapple(ECombatGrappleMove::Pull); }

void ATMCharacter::OnWebTrapPressed()
{
    if (!GrappleComponent) return;
    GrappleComponent->ExecuteCombatMove(ECombatGrappleMove::WebTrap, nullptr);
}

// ── TEMPORAL INPUT HANDLER ──

void ATMCharacter::OnTemporalClapPressed()
{
    ActivateTemporalClap();
}

// ── STATE QUERIES ──

bool ATMCharacter::IsWallRunning() const { return CurrentStateName == "WallRun"; }
bool ATMCharacter::IsSliding() const { return CurrentStateName == "Slide"; }
bool ATMCharacter::IsSprinting() const { return CurrentStateName == "Sprint"; }

float ATMCharacter::GetCurrentSpeed() const
{
    return GetVelocity().Size();
}

float ATMCharacter::GetSpeedScalingFactor() const
{
    float Speed = GetCurrentSpeed();
    return FMath::GetMappedRangeValueClamped(
        FVector2D(MinSpeedForMagicScaling, MaxSpeedForMagicScaling),
        FVector2D(MinDamageScale, MaxDamageScale),
        Speed);
}

// ── GRAPPLE QUERIES ──

bool ATMCharacter::IsGrappling() const
{
    return GrappleComponent && (GrappleComponent->GetCurrentState() != EGrappleState::Inactive
        || GrappleComponent->IsSlamming());
}

EGrappleTier ATMCharacter::GetGrappleTier() const
{
    return GrappleComponent ? GrappleComponent->GetCurrentTier() : EGrappleTier::Strand;
}

void ATMCharacter::UpgradeGrappleTier()
{
    if (GrappleComponent)
    {
        EGrappleTier CurrentTier = GrappleComponent->GetCurrentTier();
        if (CurrentTier < EGrappleTier::Nexus)
        {
            GrappleComponent->SetCurrentTier(static_cast<EGrappleTier>(static_cast<int32>(CurrentTier) + 1));
        }
    }
}

// ── DESTRUCTION QUERIES ──

bool ATMCharacter::IsLocationDestructible(FVector Location) const
{
    ADestructionManager* DestructionManager = ADestructionManager::GetInstance(GetWorld());
    if (!DestructionManager) return true;

    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    // Was Location +/- FVector(0,0,100) — meaningless on a ring, where
    // "up" at this location isn't world +Z. MakeGroundTrace derives the
    // correct local vertical from Location itself.
    FVector Start, End;
    URingFrameUtils::MakeGroundTrace(Location, 100.0f, Start, End);

    return GetWorld()->LineTraceTestByChannel(Start, End, ECC_WorldStatic, Params);
}

bool ATMCharacter::CanWallRunAtLocation(FVector Location) const
{
    return true;
}

// ── MANA SYSTEM ──

bool ATMCharacter::ConsumeMana(float ManaCost)
{
    if (bInfiniteManaForTesting) return true;
    if (!HasEnoughMana(ManaCost)) return false;

    CurrentMana = FMath::Max(0.0f, CurrentMana - ManaCost);
    OnManaChanged.Broadcast(CurrentMana, MaxMana);

    StopManaRegen();

    GetWorldTimerManager().SetTimer(
        ManaRegenDelayHandle,
        this, &ATMCharacter::StartManaRegen,
        ManaRegenDelay,
        false);

    if (CurrentMana <= 0.0f)
    {
        OnManaDepleted.Broadcast(ManaRegenDelay);
    }

    return true;
}

void ATMCharacter::AddMana(float ManaAmount)
{
    CurrentMana = FMath::Min(MaxMana, CurrentMana + ManaAmount);
    OnManaChanged.Broadcast(CurrentMana, MaxMana);
}

void ATMCharacter::StartManaRegen()
{
    if (bManaRegenActive) return;

    bManaRegenActive = true;
    OnManaRegenStarted.Broadcast();

    GetWorldTimerManager().SetTimer(
        ManaRegenTickHandle,
        this, &ATMCharacter::RegenerateManaTick,
        0.1f,
        true);
}

void ATMCharacter::StopManaRegen()
{
    bManaRegenActive = false;

    GetWorldTimerManager().ClearTimer(ManaRegenDelayHandle);
    GetWorldTimerManager().ClearTimer(ManaRegenTickHandle);
}

void ATMCharacter::RegenerateManaTick()
{
    if (!bManaRegenActive) return;

    float RegenAmount = ManaRegenRate * 0.1f;
    CurrentMana = FMath::Min(MaxMana, CurrentMana + RegenAmount);
    OnManaChanged.Broadcast(CurrentMana, MaxMana);

    if (CurrentMana >= MaxMana)
    {
        StopManaRegen();
    }
}

// ── TEMPORAL CLAP SYSTEM ──

bool ATMCharacter::CanActivateTemporalClap() const
{
    if (!bCanUseTemporalClap) return false;
    if (bIsTemporalActive) return false;
    if (!TransfigurationCasting) return false;

    // CRITICAL: Require at least one armed sigil.
    // Without this, pressing V wastes mana for nothing.
    int32 SigilCount = TransfigurationCasting->GetActiveSigilCount();
    if (SigilCount == 0) return false;

    // Also require enough mana to actually activate.
    if (CurrentMana < TemporalClapManaCost) return false;

    return true;
}

void ATMCharacter::ActivateTemporalClap()
{
    if (!CanActivateTemporalClap()) return;

    if (!ConsumeMana(TemporalClapManaCost))
    {
        UE_LOG(LogTemp, Warning, TEXT("Not enough mana for Temporal Clap"));
        return;
    }

    int32 SigilCount = TransfigurationCasting ? TransfigurationCasting->GetActiveSigilCount() : 0;
    if (SigilCount == 0 && bTemporalClapRefundOnNoSigils)
    {
        AddMana(TemporalClapManaCost * TemporalClapRefundPercent);
        UE_LOG(LogTemp, Warning, TEXT("No sigils to trigger, refunding %f mana"),
            TemporalClapManaCost * TemporalClapRefundPercent);
        return;
    }

    bIsTemporalActive = true;
    TemporalTimeRemaining = TemporalClapDuration;

    APlayerController* PC = Cast<APlayerController>(GetController());
    if (PC)
    {
        OriginalCameraFOV = PC->PlayerCameraManager->GetFOVAngle();
        OriginalCameraRotation = PC->GetControlRotation();
    }

    StartTemporalEffects();
    OnTemporalClapStarted.Broadcast();

    TransfigurationCasting->PrepareSigilsForTemporalClap();

    ADestructionManager* DestructionManager = ADestructionManager::GetInstance(GetWorld());
    if (DestructionManager)
    {
        DestructionManager->ProcessDestructionQueue();
    }

    if (GrappleComponent)
    {
        for (auto& Line : GrappleComponent->GetActiveLines())
        {
            Line.Lifetime *= 2.0f;
        }
    }

    GetWorldTimerManager().SetTimer(
        ClapAnimationTimerHandle,
        this, &ATMCharacter::ExecuteClapSigilActivation,
        ClapAnimationDuration * TemporalTimeDilation,  // real seconds while the world is slowed
        false);

    GetWorldTimerManager().SetTimer(
        TemporalTimerHandle,
        this, &ATMCharacter::EndTemporalClap,
        TemporalClapDuration * TemporalTimeDilation,   // real seconds while the world is slowed
        false);

    UE_LOG(LogTemp, Warning, TEXT("[TM] Temporal Clap: started, %d sigils, %.1fs real time"),
        SigilCount, TemporalClapDuration);
}

void ATMCharacter::StartTemporalEffects()
{
    // Slow the world, keep the player at normal speed inside it.
    UGameplayStatics::SetGlobalTimeDilation(GetWorld(), TemporalTimeDilation);
    CustomTimeDilation = 1.f / FMath::Max(TemporalTimeDilation, 0.01f);

    // Camera tilt and FOV for the clap are applied in UpdateCameraEffects.
}

void ATMCharacter::ExecuteClapSigilActivation()
{
    OnClapAnimationComplete.Broadcast();

    if (TransfigurationCasting)
    {
        TransfigurationCasting->TriggerSigilsSynchronized();
    }
}

void ATMCharacter::EndTemporalClap()
{
    if (!bIsTemporalActive) return;

    bIsTemporalActive = false;
    TemporalTimeRemaining = 0.0f;

    UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.0f);
    CustomTimeDilation = 1.0f;
    UE_LOG(LogTemp, Warning, TEXT("[TM] Temporal Clap: ended"));

    EndTemporalEffects();
    OnTemporalClapEnded.Broadcast();
    BeginTemporalCooldown();
}

void ATMCharacter::EndTemporalEffects()
{
    // UpdateCameraEffects eases the tilt and FOV back once bIsTemporalActive is false.
}

void ATMCharacter::BeginTemporalCooldown()
{
    bCanUseTemporalClap = false;

    GetWorldTimerManager().SetTimer(
        CooldownTimerHandle,
        [this]() { bCanUseTemporalClap = true; },
        TemporalClapCooldown,
        false);
}

// ── BUFF FUNCTIONS ──

void ATMCharacter::ApplySpeedBuff(float Multiplier, float Duration)
{
    UBuffEffectComponent* BuffComp = FindComponentByClass<UBuffEffectComponent>();
    if (BuffComp)
    {
        BuffComp->ApplyBuff(EBuffType::Speed, Multiplier, Duration);
    }
}

void ATMCharacter::ApplyDamageBuff(float Multiplier, float Duration)
{
    CurrentDamageMultiplier = Multiplier;

    // Member handle: a new buff restarts the timer instead of an older one ending it early.
    GetWorldTimerManager().SetTimer(DamageBuffTimerHandle, [this]()
        {
            CurrentDamageMultiplier = 1.0f;
        }, Duration, false);
}

float ATMCharacter::GetSpeedBuffMultiplier() const
{
    return BuffEffects ? BuffEffects->GetBuffMultiplier(EBuffType::Speed) : 1.f;
}

void ATMCharacter::SetBaseWalkSpeed(float NewBase)
{
    BaseWalkSpeed = NewBase;
    RefreshMovementSpeed();
}

void ATMCharacter::RefreshMovementSpeed()
{
    // One place computes max speed, so state changes and buffs can't overwrite each other.
    GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed * GetSpeedBuffMultiplier();
}
void ATMCharacter::OnSprintStarted()
{
    bSprintHeld = true;
    if (GetCharacterMovement()->IsMovingOnGround())
    {
        TransitionTo<TMState_Sprint>();
    }
}

void ATMCharacter::OnSprintEnded()
{
    bSprintHeld = false;
    if (CurrentStateName == "Sprint")
    {
        TransitionTo<TMState_Walk>();
    }
}

void ATMCharacter::OnSlidePressed()
{
    // Only slide if grounded and moving fast enough.
    // Was GetVelocity().Size2D() — that's XY-plane speed, which on a ring
    // is only correct at exactly arc 0. GetPlanarSpeed projects onto the
    // actual local tangent plane instead.
    if (GetCharacterMovement() &&
        GetCharacterMovement()->IsMovingOnGround() &&
        URingFrameUtils::GetPlanarSpeed(this) > 200.f)
    {
        TransitionTo<TMState_Slide>();
    }
}