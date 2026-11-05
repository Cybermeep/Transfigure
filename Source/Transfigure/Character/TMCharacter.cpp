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
#include "States/TMState_CeilingRun.h"
#include "Ringworld/RingLaunchComponent.h"
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
#include "Transfiguration/SpellStructure.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Enemy/BaseEnemy.h"
#include "Transfigure.h"
#include "Destruction/DestructionManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Limb/ProceduralZombie.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"
#include "RingFrameUtils.h"
#include "EngineUtils.h"
#include "Combat/CombatWaveDirector.h"
#include "Ringworld/RingRegions.h"
#include "Components/CapsuleComponent.h"
#include "Ringworld/RingContentStreamer.h"
#include "Ringworld/RingNearField.h"

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
    LaunchComponent = CreateDefaultSubobject<URingLaunchComponent>(
        TEXT("LaunchComponent"));
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
    DefaultBrakingDecelerationWalking = GetCharacterMovement()->BrakingDecelerationWalking;
    if (FParse::Param(FCommandLine::Get(), TEXT("TMSpellTest")))
    {
        StartSpellSmokeTest();
    }
    else if (FParse::Param(FCommandLine::Get(), TEXT("TMArenaTest")))
    {
        StartArenaTest();
    }
    else if (FParse::Param(FCommandLine::Get(), TEXT("TMRingTest")))
    {
        StartRingTest();
    }
    // The selected spell has to be on the wheel (the casting component set the wheel up in Super::BeginPlay).
    if (TransfigurationCasting)
    {
        const TArray<FName> Wheel = TransfigurationCasting->GetAvailableSpellIDs();
        if (Wheel.Num() > 0 && !Wheel.Contains(DrawSigilSpellID)) DrawSigilSpellID = Wheel[0];
    }
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
    if (bIsDead)
    {
        // Death screen: after a moment, click / Space / R / Enter restarts.
        APlayerController* PC = Cast<APlayerController>(GetController());
        if (PC && GetWorld()->GetRealTimeSeconds() - DeathTime > 1.2
            && (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) || PC->WasInputKeyJustPressed(EKeys::SpaceBar)
                || PC->WasInputKeyJustPressed(EKeys::R) || PC->WasInputKeyJustPressed(EKeys::Enter)))
        {
            RestartRun();
        }
    }
    UpdateMomentumBraking();
    UpdateCameraEffects(DeltaTime);
    UpdateRingGravity(DeltaTime);
    SettleOntoTerrain();
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
    const FVector SurfUp = GetSurfaceUp();
    GetCharacterMovement()->SetGravityDirection(-SurfUp);

    // Apply accumulated yaw around the CURRENT up axis, then re-orthonormalize
    // against the (possibly just-changed) up so facing direction stays valid
    // as the surface curves underneath us.
    if (!FMath::IsNearlyZero(PendingYawInput))
    {
        const FQuat YawRot(SurfUp, FMath::DegreesToRadians(PendingYawInput));
        CachedFacingForward = YawRot.RotateVector(CachedFacingForward);
        PendingYawInput = 0.0f;
    }
    CachedFacingForward = (CachedFacingForward -
        SurfUp * FVector::DotProduct(CachedFacingForward, SurfUp)).GetSafeNormal();

    if (CachedFacingForward.IsNearlyZero())
    {
        // Degenerate only if we were somehow facing exactly along the axis.
        CachedFacingForward = FVector::CrossProduct(FVector::RightVector, SurfUp).GetSafeNormal();
    }

    // Set the actor's rotation directly from the (Forward, Right, Up) basis.
    // Every state function calls GetActorForwardVector()/GetActorRightVector()
    // already — this is the one change that makes all of them correct on the
    // ring without touching their bodies.
    SetActorRotation(FRotationMatrix::MakeFromXZ(CachedFacingForward, SurfUp).ToQuat());

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
        CameraPitchTarget->SetRelativeRotation(FRotator(CameraPitch + RecoilPitch, CameraYawOffset + RecoilYaw, CameraRoll + CameraFlipRoll) + CameraShakeRot);
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
    // Roll: lean away from the wall while wall running. Kept for a moment after leaving the
    // wall (and while the rope carries the run round a corner) so it doesn't snap off and on.
    if (WallRunSide != 0.f)
    {
        LastWallRunSide = WallRunSide;
        TimeSinceWallRun = 0.f;
    }
    else
    {
        TimeSinceWallRun += DeltaTime;
    }
    const bool bAroundCorner = GrappleComponent && GrappleComponent->IsCarryingWallRun() && TimeSinceWallRun < 3.f;
    const bool bKeepTilt = WallRunSide == 0.f && !GetCharacterMovement()->IsMovingOnGround()
        && (TimeSinceWallRun < WallRunTiltGrace || bAroundCorner);
    const float Side = WallRunSide != 0.f ? WallRunSide : (bKeepTilt ? LastWallRunSide : 0.f);
    const float TargetRoll = -Side * WallRunCameraTilt;
    CameraRoll = FMath::FInterpTo(CameraRoll, TargetRoll, DeltaTime, CameraTiltSpeed);

    // Ceiling flip: one smooth roll over a fixed time, never re-triggered mid-way.
    if (!FMath::IsNearlyZero(CameraFlipStart))
    {
        CameraFlipElapsed += DeltaTime;
        const float T = FMath::Clamp(CameraFlipElapsed / FMath::Max(CeilingFlipDuration, 0.01f), 0.f, 1.f);
        CameraFlipRoll = FMath::Lerp(CameraFlipStart, 0.f, FMath::SmoothStep(0.f, 1.f, T));
        if (T >= 1.f)
        {
            CameraFlipStart = 0.f;
            CameraFlipRoll = 0.f;
        }
    }

    // FOV: widen with ground speed so going fast feels fast.
    const float SpeedAlpha = GetSpeedFOVAlpha();
    const float TargetFOV = (bIsTemporalActive ? TemporalCameraFOV : BaseFOV) + SpeedFOVBoost * SpeedAlpha - FOVTension;
    FOVTension = 0.f;
    // The clap snaps the view wide; leaving it (and speed changes) ease more gently.
    const float FOVSpeed = (bIsTemporalActive && TargetFOV > CurrentFOV) ? TemporalFOVChangeSpeed : FOVChangeSpeed;
    CurrentFOV = FMath::FInterpTo(CurrentFOV, TargetFOV, DeltaTime, FOVSpeed);
    FOVKick = FMath::FInterpTo(FOVKick, 0.f, DeltaTime, 5.f);
    RecoilPitch = FMath::FInterpTo(RecoilPitch, 0.f, DeltaTime, 6.f);
    RecoilYaw = FMath::FInterpTo(RecoilYaw, 0.f, DeltaTime, 6.f);

    // Shake: smooth noise, strength = trauma squared (small hits barely move it, big ones jolt).
    CameraShakeTrauma = FMath::Max(CameraShakeTrauma - DeltaTime * 1.6f, 0.f);
    CameraShakeTime += DeltaTime;
    const float Shake = CameraShakeTrauma * CameraShakeTrauma;
    CameraShakeRot = FRotator(
        FMath::PerlinNoise1D(CameraShakeTime * 23.f) * 4.f * Shake,
        FMath::PerlinNoise1D(CameraShakeTime * 23.f + 37.f) * 4.f * Shake,
        FMath::PerlinNoise1D(CameraShakeTime * 19.f + 71.f) * 3.f * Shake);
    if (ViewCamera)
    {
        ViewCamera->SetFieldOfView(CurrentFOV + FOVKick);
    }
}

float ATMCharacter::GetDamageMultiplier() const
{
    float M = CurrentDamageMultiplier;
    if (BuffEffects && BuffEffects->HasBuff(EBuffType::Damage)) M *= BuffEffects->GetBuffMultiplier(EBuffType::Damage);
    return M;
}

void ATMCharacter::UpdateMomentumBraking()
{
    UCharacterMovementComponent* CMC = GetCharacterMovement();

    // Ice (slippery spell structures): no friction, no braking - you keep everything.
    const ASpellStructure* FloorStructure = CMC->IsMovingOnGround() ? Cast<ASpellStructure>(CMC->CurrentFloor.HitResult.GetActor()) : nullptr;
    if (FloorStructure && FloorStructure->IsSlippery())
    {
        if (!bOnSlipperyFloor) SavedGroundFriction = CMC->GroundFriction;
        bOnSlipperyFloor = true;
        CMC->GroundFriction = 0.f;
        CMC->bUseSeparateBrakingFriction = true;
        CMC->BrakingFriction = 0.f;
        CMC->BrakingDecelerationWalking = 0.f;
        bOverspeedBraking = true;   // so normal braking is restored once off the ice
        return;
    }
    if (bOnSlipperyFloor)
    {
        CMC->GroundFriction = SavedGroundFriction;
        bOnSlipperyFloor = false;
    }
    if (CurrentStateName == TEXT("Slide"))
    {
        // The slide sets its own friction; just hand back the friction mode.
        if (bOverspeedBraking)
        {
            CMC->bUseSeparateBrakingFriction = false;
            bOverspeedBraking = false;
        }
        return;
    }
    const float Speed = URingFrameUtils::StripVertical(GetVelocity(), CachedRingUp).Size();
    const bool bOver = CMC->IsMovingOnGround() && Speed > CMC->MaxWalkSpeed + 50.f;
    if (bOver)
    {
        // Coast: no friction, gentle deceleration. Normal braking resumes near walking speed.
        CMC->bUseSeparateBrakingFriction = true;
        CMC->BrakingFriction = 0.f;
        CMC->BrakingDecelerationWalking = OverspeedGroundDeceleration;
        bOverspeedBraking = true;
    }
    else if (bOverspeedBraking)
    {
        CMC->bUseSeparateBrakingFriction = false;
        CMC->BrakingDecelerationWalking = DefaultBrakingDecelerationWalking;
        bOverspeedBraking = false;
    }
}

void ATMCharacter::SteerViewToward(const FVector& WorldPoint, float DeltaTime, float Rate)
{
    const float A = 1.f - FMath::Exp(-Rate * DeltaTime);
    const FVector Up = GetSurfaceUp();
    const FVector Eye = ViewCamera ? ViewCamera->GetComponentLocation() : GetActorLocation();
    const FVector Dir = (WorldPoint - Eye).GetSafeNormal();
    const FVector Flat = FVector::VectorPlaneProject(Dir, Up).GetSafeNormal();
    if (!Flat.IsNearlyZero())
    {
        CachedFacingForward = (CachedFacingForward + (Flat - CachedFacingForward) * A).GetSafeNormal();
    }
    CameraYawOffset = FMath::Lerp(CameraYawOffset, 0.f, A);
    const float WantPitch = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Dir, Up), -1.f, 1.f)));
    CameraPitch = FMath::Clamp(FMath::Lerp(CameraPitch, WantPitch, A), MinCameraPitch, MaxCameraPitch);
}

float ATMCharacter::GetSpeedFOVAlpha() const
{
    const float Speed = URingFrameUtils::StripVertical(GetCharacterMovement()->Velocity, CachedRingUp).Size();
    return FMath::Clamp((Speed - SpeedFOVStartSpeed) / FMath::Max(SpeedFOVFullSpeed - SpeedFOVStartSpeed, 1.f), 0.f, 1.f);
}

bool ATMCharacter::CanStartCeilingRun() const
{
    return !IsCeilingRunning() && GetWorld()->GetTimeSeconds() >= CeilingRunReadyTime
        && GetSpeedFOVAlpha() >= CeilingRunSpeedAlpha;
}

void ATMCharacter::StartCeilingRunCooldown()
{
    CeilingRunReadyTime = GetWorld()->GetTimeSeconds() + CeilingRunCooldown;
}

void ATMCharacter::SetSurfaceUp(const FVector& NewUp, bool bOverride)
{
    // Where the camera looks right now, in world space.
    const FQuat CamWorld = GetActorQuat() * FRotator(CameraPitch, CameraYawOffset, CameraRoll + CameraFlipRoll).Quaternion();

    bSurfaceUpOverride = bOverride && !NewUp.IsNearlyZero();
    SurfaceUpOverride = NewUp.GetSafeNormal();
    const FVector Up = GetSurfaceUp();

    FVector Fwd = FVector::VectorPlaneProject(CachedFacingForward, Up).GetSafeNormal();
    if (Fwd.IsNearlyZero())
    {
        Fwd = FVector::VectorPlaneProject(GetActorUpVector(), Up).GetSafeNormal();
    }
    CachedFacingForward = Fwd;
    SetActorRotation(FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat());
    GetCharacterMovement()->SetGravityDirection(-Up);

    // Same view, expressed relative to the re-oriented body. Pitch/yaw carry over; the
    // roll (about 180 for a ceiling) is what we ease out, so the world turns over smoothly.
    const FRotator Rel = (GetActorQuat().Inverse() * CamWorld).Rotator();
    CameraPitch = FMath::Clamp(Rel.Pitch, MinCameraPitch, MaxCameraPitch);
    CameraYawOffset = Rel.Yaw;
    CameraFlipStart = FRotator::NormalizeAxis(Rel.Roll - CameraRoll);
    CameraFlipRoll = CameraFlipStart;
    CameraFlipElapsed = 0.f;
    if (CameraPitchTarget)
    {
        CameraPitchTarget->SetRelativeRotation(FRotator(CameraPitch, CameraYawOffset, CameraRoll + CameraFlipRoll));
    }
    UE_LOG(LogTemp, Warning, TEXT("[TM] Surface up %s, camera roll %.0f to ease out"),
        bSurfaceUpOverride ? TEXT("-> ceiling") : TEXT("-> ring"), CameraFlipStart);
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
    if (!IA_Spellbook)
    {
        IA_Spellbook = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/IA_Spellbook.IA_Spellbook"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    }
    if (IA_Spellbook)
    {
        EIC->BindAction(IA_Spellbook, ETriggerEvent::Started, this, &ATMCharacter::ToggleSpellbook);
    }

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
        EIC->BindAction(IA_Fire, ETriggerEvent::Completed, this, &ATMCharacter::OnFireReleased);
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
    if (bSpellbookOpen) return;   // the mouse is a cursor in the spellbook
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
    if (LaunchComponent && LaunchComponent->IsFlying()) return;
    if (IsCeilingRunning())
    {
        bCeilingJumpRequested = true;   // the ceiling run decides: hop, or drop if already hopping
        return;
    }
    if (GrappleComponent && GrappleComponent->IsPerched())
    {
        GrappleComponent->JumpOffPerch();
        return;
    }
    if (GrappleComponent && GrappleComponent->IsGluedToWall())
    {
        GrappleComponent->ReleaseGrapple();   // jumping lets go of the wall glue
    }
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
    const FVector SurfUp = GetSurfaceUp();
    const FVector Flat = FVector::VectorPlaneProject(Direction, SurfUp).GetSafeNormal();
    if (Flat.IsNearlyZero()) return;

    // Signed yaw change around local up. The camera is offset by the opposite
    // amount so its world direction doesn't move this frame, then eases in.
    const float Cos = FVector::DotProduct(CachedFacingForward, Flat);
    const float Sin = FVector::DotProduct(FVector::CrossProduct(CachedFacingForward, Flat), SurfUp);
    const float DeltaYaw = FMath::RadiansToDegrees(FMath::Atan2(Sin, Cos));
    CameraYawOffset = FRotator::NormalizeAxis(CameraYawOffset - DeltaYaw);

    CachedFacingForward = Flat;
    // Apply now (not next tick) so side traces like wall-run detection see the new facing.
    SetActorRotation(FRotationMatrix::MakeFromXZ(Flat, SurfUp).ToQuat());
}

// ── DAMAGE ──

float ATMCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    // Our own spells (FireBlast radial damage) shouldn't hurt us.
    if (DamageCauser == this || EventInstigator == GetController()) return 0.f;
    if (bIsDead || bGodModeForTesting) return 0.f;

    const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    // Defence buff (Rock Armour).
    float Taken = Applied;
    if (BuffEffects && BuffEffects->HasBuff(EBuffType::Defense)) Taken /= FMath::Max(BuffEffects->GetBuffMultiplier(EBuffType::Defense), 0.1f);
    Health = FMath::Max(0.f, Health - Taken);
    LastHurtTime = GetWorld()->GetRealTimeSeconds();
    AddCameraShake(0.35f);
    UE_LOG(LogTemp, Warning, TEXT("[TM] Player took %.0f damage from %s, health %.0f"),
        Applied, *GetNameSafe(DamageCauser), Health);
    if (Health <= 0.f) HandleDeath();
    return Applied;
}

void ATMCharacter::SettleOntoTerrain()
{
    // On the ring's big terrain mesh, the movement component sometimes keeps a character that
    // has come to rest on the ground in Falling (it never counts the landing, and slides down
    // gentle slopes). If we're on a terrain tile, not moving along up, with walkable floor right
    // under the feet for a few frames, we've landed. Only plain falling (not wall/ceiling runs).
    UCharacterMovementComponent* CMC = GetCharacterMovement();
    if (CMC->MovementMode != MOVE_Falling || bSurfaceUpOverride || IsWallRunning() || IsCeilingRunning())
    {
        TerrainSettleFrames = 0;
        return;
    }
    if (FMath::Abs(FVector::DotProduct(CMC->Velocity, GetRingUp())) > 60.f)
    {
        TerrainSettleFrames = 0;
        return;
    }
    FFindFloorResult Floor;
    CMC->FindFloor(CMC->UpdatedComponent->GetComponentLocation(), Floor, false);
    const bool bOnTerrain = Floor.HitResult.GetActor() && Floor.HitResult.GetActor()->IsA<ARingFloorTile>();
    if (!bOnTerrain || !Floor.IsWalkableFloor() || Floor.FloorDist > 10.f)
    {
        TerrainSettleFrames = 0;
        return;
    }
    if (++TerrainSettleFrames >= 3)
    {
        TerrainSettleFrames = 0;
        CMC->SetMovementMode(MOVE_Walking);
        UE_LOG(LogTemp, Log, TEXT("[TM] Settled onto ring terrain (was resting in Falling)"));
    }
}

void ATMCharacter::HandleDeath()
{
    if (bIsDead) return;
    bIsDead = true;
    DeathTime = GetWorld()->GetRealTimeSeconds();
    if (WeaponComponent) WeaponComponent->StopFiring();
    if (bSpellWheelOpen) CloseSpellWheel();
    if (bSpellbookOpen) ToggleSpellbook();
    EndTemporalClap();
    // No more control (the restart keys are read straight from the controller in Tick).
    if (APlayerController* PC = Cast<APlayerController>(GetController())) DisableInput(PC);
    // Slow motion while the death screen comes up; restarting loads a fresh world at normal speed.
    UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 0.3f);
    AddCameraShake(0.6f);
    UE_LOG(LogTemp, Warning, TEXT("[TM] Player died"));
}

void ATMCharacter::RestartRun()
{
    const FString Map = UGameplayStatics::GetCurrentLevelName(this, true);
    UE_LOG(LogTemp, Warning, TEXT("[TM] Restarting %s"), *Map);
    UGameplayStatics::OpenLevel(this, FName(*Map));
}

// ── COMBAT INPUT HANDLERS ──

void ATMCharacter::OnFirePressed()
{
    // Spellbook open: left click picks spells and slots.
    if (bSpellbookOpen)
    {
        if (APlayerController* PC = Cast<APlayerController>(GetController()))
        {
            if (ATMHUD* HUD = Cast<ATMHUD>(PC->GetHUD())) HUD->SpellbookClick();
        }
        return;
    }
    // On a launch pad, aiming at the ring: click launches you there instead of shooting.
    if (LaunchComponent && (LaunchComponent->IsFlying() || LaunchComponent->TryLaunch())) return;
    if (WeaponComponent) WeaponComponent->StartFiring();
}

void ATMCharacter::OnFireReleased()
{
    if (WeaponComponent) WeaponComponent->StopFiring();
}

// ── MAGIC INPUT HANDLERS ──

void ATMCharacter::OnDrawSigilPressed()
{
    TransfigurationCasting->BeginDrawMode(DrawSigilSpellID);
}

void ATMCharacter::OnPlaceSigilPressed()
{
    if (bSpellbookOpen) return;
    // Start the hold timer; if Q is still down when it fires, the wheel opens.
    GetWorldTimerManager().SetTimer(SpellWheelOpenTimer, this, &ATMCharacter::OpenSpellWheel,
        SpellWheelHoldTime, false);
}

void ATMCharacter::OnPlaceSigilReleased()
{
    if (bSpellbookOpen) return;
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

void ATMCharacter::ToggleSpellbook()
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC) return;
    if (bSpellWheelOpen) CloseSpellWheel();
    bSpellbookOpen = !bSpellbookOpen;
    if (bSpellbookOpen && WeaponComponent) WeaponComponent->StopFiring();

    if (bSpellbookOpen)
    {
        PC->SetShowMouseCursor(true);
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
        PC->SetInputMode(Mode);
        int32 W = 0, H = 0;
        PC->GetViewportSize(W, H);
        PC->SetMouseLocation(W / 2, H / 2);
        // The world crawls while you browse (unless the temporal clap already owns time).
        if (!bIsTemporalActive) UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 0.1f);
    }
    else
    {
        PC->SetShowMouseCursor(false);
        PC->SetInputMode(FInputModeGameOnly());
        if (!bIsTemporalActive) UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.f);
        // The selected spell has to still be on the wheel.
        if (TransfigurationCasting)
        {
            const TArray<FName> Wheel = TransfigurationCasting->GetAvailableSpellIDs();
            if (Wheel.Num() > 0 && !Wheel.Contains(DrawSigilSpellID)) DrawSigilSpellID = Wheel[0];
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("[TM] Spellbook %s"), bSpellbookOpen ? TEXT("opened") : TEXT("closed"));
}

// Where the smoke test started (movement spells move the player; combos reset to here).
static FVector GSmokeStartLocation = FVector::ZeroVector;
static FVector GSmokeStartForward = FVector::ForwardVector;

void ATMCharacter::StartSpellSmokeTest()
{
    UE_LOG(LogTemp, Warning, TEXT("[TM] SMOKE: spell test starting"));
    bGodModeForTesting = true;
    GSmokeStartLocation = GetActorLocation();
    GSmokeStartForward = GetActorForwardVector();
    bInfiniteManaForTesting = true;
    SmokeStep = 0;
    CameraPitch = -22.f;   // look down at where the sigils go
    GetWorldTimerManager().SetTimer(SmokeTestTimer, this, &ATMCharacter::SpellSmokeTestStep, 1.6f, true, 2.f);
}

void ATMCharacter::SpellSmokeTestStep()
{
    UWorld* World = GetWorld();
    if (!World || !TransfigurationCasting) return;
    const TArray<FName> Spells = TransfigurationCasting->GetKnownSpellIDs();
    if (SmokeStep >= Spells.Num())
    {
        // Combos: back to the start spot, looking down at where the sigils go.
        SetActorLocation(GSmokeStartLocation, false, nullptr, ETeleportType::TeleportPhysics);
        GetCharacterMovement()->Velocity = FVector::ZeroVector;
        CachedFacingForward = GSmokeStartForward;
        SetActorRotation(FRotationMatrix::MakeFromXZ(GSmokeStartForward, GetRingUp()).ToQuat());
        CameraPitch = -22.f;
    }
    const FVector Up = GetRingUp();
    const FVector Fwd = GetActorForwardVector();
    CurrentMana = MaxMana;
    Health = MaxHealth;
    TransfigurationCasting->ClearCooldowns();

    // Find the ground a few metres ahead.
    auto Ground = [&](const FVector& At) -> FVector
    {
        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(SmokeGround), false, this);
        if (World->LineTraceSingleByChannel(Hit, At + Up * 500.f, At - Up * 2000.f, ECC_WorldStatic, Params)) return Hit.ImpactPoint;
        return At - Up * 90.f;
    };

    if (SmokeStep < Spells.Num())
    {
        const FName Id = Spells[SmokeStep];
        // A few zombies to hit (every other spell, so corpses don't pile up).
        for (int32 i = 0; i < ((SmokeStep % 2) ? 0 : 3); ++i)
        {
            const FVector P = Ground(GetActorLocation() + Fwd * (900.f + i * 150.f) + GetActorRightVector() * (i - 1) * 250.f) + Up * 110.f;
            FActorSpawnParameters SP;
            SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
            if (AProceduralZombie* Z = World->SpawnActor<AProceduralZombie>(AProceduralZombie::StaticClass(), P, GetActorRotation() + FRotator(0.f, 180.f, 0.f), SP))
            {
                Z->SetLifeSpan(12.f);
            }
        }
        const FVector Spot = Ground(GetActorLocation() + Fwd * 600.f);
        const bool bPlaced = TransfigurationCasting->CastAtLocation(Id, Spot, Up);
        UE_LOG(LogTemp, Warning, TEXT("[TM] SMOKE %d/%d: %s placed=%d"), SmokeStep + 1, Spells.Num(), *Id.ToString(), bPlaced ? 1 : 0);
        // Fire manual sigils a moment later (proximity / timed ones go off by themselves).
        FTimerHandle Fire;
        GetWorldTimerManager().SetTimer(Fire, FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            if (TransfigurationCasting) TransfigurationCasting->TriggerAllSigils();
        }), 0.4f, false);
        // Infused shots at the zombies (the spell just infused the gun).
        FTimerHandle Shoot;
        GetWorldTimerManager().SetTimer(Shoot, FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            if (WeaponComponent) for (int32 i = 0; i < 3; ++i) WeaponComponent->FireWeapon();
        }), 0.9f, false);
        // Screenshots (with HUD) while the spell plays out: Saved/Screenshots.
        FTimerHandle Shot;
        const FString ShotName = FString::Printf(TEXT("Smoke_%02d_%s"), SmokeStep + 1, *Id.ToString());
        GetWorldTimerManager().SetTimer(Shot, FTimerDelegate::CreateWeakLambda(this, [ShotName]()
        {
            FScreenshotRequest::RequestScreenshot(ShotName, true, false);
        }), 0.55f, false);
        // Mix in the UI now and then (drawn only when rendering).
        if (SmokeStep == 3) OpenSpellWheel();
        if (SmokeStep == 4) CloseSpellWheel();
        if (SmokeStep == 6 || SmokeStep == 7) ToggleSpellbook();
        ++SmokeStep;
        return;
    }

    // Combos: two sigils 2 m apart, then set them off.
    const TArray<UTransfigurationDefinition*> Combos = TransfigurationCasting->GetComboSpells();
    const int32 ComboStep = SmokeStep - Spells.Num();
    if (ComboStep < Combos.Num() + 2)
    {
        auto PickSpell = [&](ETransfigurationElement E) -> FName
        {
            for (const FName& Id : Spells)
            {
                const UTransfigurationDefinition* D = TransfigurationCasting->GetSpellDefinition(Id);
                if (D && D->Element == E && D->TriggerType == ESigilTriggerType::Manual && D->SelfAction == ESpellSelfAction::None
                    && !D->bIsPersistentSigil && !D->bFollowCaster && D->Delay <= 0.f) return Id;
            }
            return NAME_None;
        };
        ETransfigurationElement A, B;
        FString Label;
        if (ComboStep < Combos.Num())
        {
            A = Combos[ComboStep]->ComboElementA;
            B = Combos[ComboStep]->ComboElementB;
            Label = Combos[ComboStep]->SpellID.ToString();
        }
        else if (ComboStep == Combos.Num())
        {
            A = B = ETransfigurationElement::Fire;
            Label = TEXT("Overcharge");
        }
        else
        {
            A = ETransfigurationElement::Arcane;
            B = ETransfigurationElement::Ice;
            Label = TEXT("Amplify");
        }
        const FName SA = PickSpell(A), SB = PickSpell(B);
        for (int32 i = 0; i < 3; ++i)
        {
            const FVector P = Ground(GetActorLocation() + Fwd * (900.f + i * 150.f) + GetActorRightVector() * (i - 1) * 250.f) + Up * 110.f;
            FActorSpawnParameters SP;
            SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
            if (AProceduralZombie* Z = World->SpawnActor<AProceduralZombie>(AProceduralZombie::StaticClass(), P, GetActorRotation() + FRotator(0.f, 180.f, 0.f), SP))
            {
                Z->SetLifeSpan(10.f);
            }
        }
        const FVector Spot = Ground(GetActorLocation() + Fwd * 700.f);
        const bool bA = !SA.IsNone() && TransfigurationCasting->CastAtLocation(SA, Spot - GetActorRightVector() * 100.f, Up);
        TransfigurationCasting->ClearCooldowns();
        const bool bB = !SB.IsNone() && TransfigurationCasting->CastAtLocation(SB, Spot + GetActorRightVector() * 100.f, Up);
        UE_LOG(LogTemp, Warning, TEXT("[TM] SMOKE combo %s: %s + %s placed=%d%d"), *Label, *SA.ToString(), *SB.ToString(), bA ? 1 : 0, bB ? 1 : 0);
        FTimerHandle Fire;
        GetWorldTimerManager().SetTimer(Fire, FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            if (TransfigurationCasting) TransfigurationCasting->TriggerAllSigils();
        }), 0.4f, false);
        FTimerHandle Shot;
        const FString ShotName = FString::Printf(TEXT("Smoke_C%02d_%s"), ComboStep + 1, *Label);
        GetWorldTimerManager().SetTimer(Shot, FTimerDelegate::CreateWeakLambda(this, [ShotName]()
        {
            FScreenshotRequest::RequestScreenshot(ShotName, true, false);
        }), 0.7f, false);
        if (ComboStep == Combos.Num() + 1)
        {
            // Last one: open the spellbook to see the discovered combos.
            FTimerHandle Book;
            GetWorldTimerManager().SetTimer(Book, FTimerDelegate::CreateWeakLambda(this, [this]() { ToggleSpellbook(); }), 1.2f, false);
            FTimerHandle BookShot;
            GetWorldTimerManager().SetTimer(BookShot, FTimerDelegate::CreateWeakLambda(this, []()
            {
                FScreenshotRequest::RequestScreenshot(TEXT("Smoke_Spellbook_Combos"), true, false);
            }), 1.25f, false);
        }
        ++SmokeStep;
        return;
    }

    if (ComboStep == Combos.Num() + 2 && WeaponComponent)
    {
        // Gunplay: hold the trigger on a row of zombies for a second.
        if (bSpellbookOpen) ToggleSpellbook();
        for (TActorIterator<ASpellStructure> It(World); It; ++It) It->Destroy();   // the combos' ice spire is in the way
        TWeakObjectPtr<AProceduralZombie> Target;
        for (int32 i = 0; i < 4; ++i)
        {
            // The first one straight ahead (the burst aims at its head), the rest behind it.
            const float Side = i == 0 ? 0.f : (i - 2) * 80.f;
            const FVector P = Ground(GetActorLocation() + Fwd * (800.f + i * 120.f) + GetActorRightVector() * Side) + Up * 110.f;
            FActorSpawnParameters SP;
            SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
            if (AProceduralZombie* Z = World->SpawnActor<AProceduralZombie>(AProceduralZombie::StaticClass(), P, GetActorRotation() + FRotator(0.f, 180.f, 0.f), SP))
            {
                Z->SetLifeSpan(8.f);
                if (i == 0) Target = Z;
            }
        }
        FTimerHandle Aim;
        GetWorldTimerManager().SetTimer(Aim, FTimerDelegate::CreateWeakLambda(this, [this, Target]()
        {
            // Look at the first zombie's head (the recoil climbs from there).
            if (!Target.IsValid() || !ViewCamera) return;
            FVector Head = Target->GetMesh() ? Target->GetMesh()->GetBoneLocation(TEXT("head")) : FVector::ZeroVector;
            if (Head.IsNearlyZero()) Head = Target->GetActorLocation() + GetRingUp() * 65.f;
            const FVector D = Head - ViewCamera->GetComponentLocation();
            const float Rise = FVector::DotProduct(D, GetRingUp());
            CameraPitch = FMath::RadiansToDegrees(FMath::Atan2(Rise, URingFrameUtils::StripVertical(D, GetRingUp()).Size()));
        }), 0.2f, false);
        const int32 ShotsBefore = WeaponComponent->GetShotsFired();
        FTimerHandle Start, Stop, Shot;
        GetWorldTimerManager().SetTimer(Start, FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            if (WeaponComponent) WeaponComponent->StartFiring();
            FScreenshotRequest::RequestScreenshot(TEXT("Smoke_Gun_Tracer"), true, false);   // the frame of the first shot
        }), 0.3f, false);
        GetWorldTimerManager().SetTimer(Shot, FTimerDelegate::CreateWeakLambda(this, []()
        {
            FScreenshotRequest::RequestScreenshot(TEXT("Smoke_Gun_Burst"), true, false);
        }), 0.75f, false);
        GetWorldTimerManager().SetTimer(Stop, FTimerDelegate::CreateWeakLambda(this, [this, ShotsBefore]()
        {
            if (!WeaponComponent) return;
            WeaponComponent->StopFiring();
            UE_LOG(LogTemp, Warning, TEXT("[TM] SMOKE gun: %d shots in 1.2 s held, recoil now %.2f deg"),
                WeaponComponent->GetShotsFired() - ShotsBefore, RecoilPitch);
        }), 1.5f, false);
        ++SmokeStep;
        return;
    }

    GetWorldTimerManager().ClearTimer(SmokeTestTimer);
    UE_LOG(LogTemp, Warning, TEXT("[TM] SMOKE: spell test done (%d spells, %d combos) - quitting"), Spells.Num(), Combos.Num());
    FTimerHandle Quit;
    GetWorldTimerManager().SetTimer(Quit, FTimerDelegate::CreateWeakLambda(this, [this]()
    {
        if (APlayerController* PC = Cast<APlayerController>(GetController())) PC->ConsoleCommand(TEXT("quit"));
    }), 4.f, false);
}

// Arena test runs (statics survive the map reload, so the second load knows it's the restart).
static int32 GArenaTestLoads = 0;

void ATMCharacter::StartArenaTest()
{
    ++GArenaTestLoads;
    UE_LOG(LogTemp, Warning, TEXT("[TM] ARENA test: load %d"), GArenaTestLoads);
    if (GArenaTestLoads >= 2)
    {
        // Back after the restart: see the fresh countdown, then quit.
        UE_LOG(LogTemp, Warning, TEXT("[TM] ARENA test: restart worked (fresh map, health %.0f)"), Health);
        FTimerHandle Shot, Quit;
        GetWorldTimerManager().SetTimer(Shot, FTimerDelegate::CreateWeakLambda(this, []()
        {
            FScreenshotRequest::RequestScreenshot(TEXT("Arena_03_Restarted"), true, false);
        }), 2.f, false);
        GetWorldTimerManager().SetTimer(Quit, FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            UE_LOG(LogTemp, Warning, TEXT("[TM] ARENA test done - quitting"));
            if (APlayerController* PC = Cast<APlayerController>(GetController())) PC->ConsoleCommand(TEXT("quit"));
        }), 4.f, false);
        return;
    }
    bGodModeForTesting = true;
    ArenaTestStart = GetWorld()->GetTimeSeconds();
    GetWorldTimerManager().SetTimer(ArenaTestTimer, this, &ATMCharacter::ArenaTestTick, 0.05f, true, 0.5f);
}

void ATMCharacter::ArenaTestTick()
{
    const float T = static_cast<float>(GetWorld()->GetTimeSeconds() - ArenaTestStart);
    if (bIsDead)
    {
        // Show the death screen, then press restart.
        static double ShotAt = 0.0;
        const double Real = GetWorld()->GetRealTimeSeconds();
        if (ShotAt <= 0.0)
        {
            ShotAt = Real;
            UE_LOG(LogTemp, Warning, TEXT("[TM] ARENA test: died after %.0f s"), T);
        }
        if (Real - ShotAt > 1.6 && Real - ShotAt < 1.7) FScreenshotRequest::RequestScreenshot(TEXT("Arena_02_Death"), true, false);
        if (Real - ShotAt > 2.5)
        {
            GetWorldTimerManager().ClearTimer(ArenaTestTimer);
            RestartRun();
        }
        return;
    }

    if (T < 30.f)
    {
        // Fight: turn to the nearest living enemy and hold the trigger.
        ABaseEnemy* Best = nullptr;
        float BestDist = 6000.f;
        for (TActorIterator<ABaseEnemy> It(GetWorld()); It; ++It)
        {
            if (!It->IsAlive()) continue;
            const float D = FVector::Dist(It->GetActorLocation(), GetActorLocation());
            if (D < BestDist) { BestDist = D; Best = *It; }
        }
        const FVector Up = GetRingUp();
        if (Best && ViewCamera)
        {
            const FVector To = Best->GetActorLocation() + Up * 30.f - ViewCamera->GetComponentLocation();
            const FVector Flat = URingFrameUtils::StripVertical(To, Up).GetSafeNormal();
            if (!Flat.IsNearlyZero()) CachedFacingForward = Flat;
            CameraPitch = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(To, Up), URingFrameUtils::StripVertical(To, Up).Size()));
            if (WeaponComponent && !WeaponComponent->IsFiring()) WeaponComponent->StartFiring();
        }
        else if (WeaponComponent && WeaponComponent->IsFiring())
        {
            WeaponComponent->StopFiring();
        }
        static bool bShot = false;
        if (!bShot && T > 14.f)
        {
            bShot = true;
            FScreenshotRequest::RequestScreenshot(TEXT("Arena_01_Fight"), true, false);
        }
        return;
    }

    if (T < 52.f)
    {
        // Up on a pillar, out of reach (Pillar_0 in make_combat_arena.py): enemies should
        // gather at its base instead of giving up on pathing.
        static bool bUp = false;
        if (!bUp)
        {
            bUp = true;
            if (WeaponComponent) WeaponComponent->StopFiring();
            SetActorLocation(FVector(1478.f, 612.f, 1700.f), false, nullptr, ETeleportType::TeleportPhysics);
            GetCharacterMovement()->Velocity = FVector::ZeroVector;
            UE_LOG(LogTemp, Warning, TEXT("[TM] ARENA test: up on a pillar at %.0f s"), T);
        }
        CameraPitch = -55.f;
        static bool bShotUp = false;
        if (!bShotUp && T > 50.f)
        {
            bShotUp = true;
            int32 Near = 0, Total = 0;
            for (TActorIterator<ABaseEnemy> It(GetWorld()); It; ++It)
            {
                if (!It->IsAlive()) continue;
                ++Total;
                if (FVector::Dist2D(It->GetActorLocation(), FVector(1478.f, 612.f, 0.f)) < 700.f) ++Near;
            }
            UE_LOG(LogTemp, Warning, TEXT("[TM] ARENA test: %d of %d enemies waiting at the pillar base"), Near, Total);
            FScreenshotRequest::RequestScreenshot(TEXT("Arena_01b_OnPillar"), true, false);
        }
        return;
    }

    // Back down; stop defending and let them win.
    if (bGodModeForTesting)
    {
        bGodModeForTesting = false;
        if (WeaponComponent) WeaponComponent->StopFiring();
        SetActorLocation(FVector(0.f, -2500.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
        CameraPitch = 0.f;
        UE_LOG(LogTemp, Warning, TEXT("[TM] ARENA test: stopped defending at %.0f s"), T);
    }
    if (T > 100.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] ARENA test: nobody reached the player in 45 s - forcing death"));
        for (TActorIterator<ABaseEnemy> It(GetWorld()); It; ++It)
        {
            if (It->IsAlive())
            {
                UE_LOG(LogTemp, Warning, TEXT("[TM] ARENA test:   %s at %s, speed %.0f, state %d"), *It->GetName(),
                    *It->GetActorLocation().ToString(), It->GetVelocity().Size(), static_cast<int32>(It->CurrentState));
            }
        }
        Health = 0.f;
        HandleDeath();
    }
}

void ATMCharacter::StartRingTest()
{
    UE_LOG(LogTemp, Warning, TEXT("[TM] RING test: starting"));
    bGodModeForTesting = true;
    bInfiniteManaForTesting = true;
    RingTestStepNum = 0;
    GetWorldTimerManager().SetTimer(RingTestTimer, this, &ATMCharacter::RingTestStep, 4.f, true, 3.f);
}

void ATMCharacter::RingTestStep()
{
    const ARingContentStreamer* Streamer = ARingContentStreamer::Get(GetWorld());
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    if (!Streamer || Spec.bFlatWorldForTesting)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] RING test: not a ring map - quitting"));
        GetWorldTimerManager().ClearTimer(RingTestTimer);
        if (APlayerController* PC = Cast<APlayerController>(GetController())) PC->ConsoleCommand(TEXT("quit"));
        return;
    }
    const FRingRegions& Map = FRingRegions::Get(Streamer->WorldSeed);
    const int32 Step = RingTestStepNum++;

    // One region of each biome (the first along the ring).
    TArray<const FRingRegion*> Targets;
    for (int32 B = 0; B < static_cast<int32>(ERingBiome::Count); ++B)
    {
        for (const FRingRegion& R : Map.GetRegions())
        {
            if (static_cast<int32>(R.Biome) == B) { Targets.Add(&R); break; }
        }
    }

    auto Report = [this, &Map, &Spec, Streamer](const FString& Shot)
    {
        const FRingCoords C = URingWorldGeometry::WorldToRing(Spec, GetActorLocation());
        const double Ground = Map.Height(C.Arc, C.Lateral);
        const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const FRingRegion* Here = Streamer->GetCurrentRegion();
        UE_LOG(LogTemp, Warning, TEXT("[TM] RING test %s: in %s, grounded %d, feet %.0f cm above the terrain (terrain %.1f m), %d pieces, %d pending"),
            *Shot, Here ? *Here->Name : TEXT("?"), GetCharacterMovement()->IsMovingOnGround() ? 1 : 0,
            C.Altitude - Half - Ground, Ground / 100.0, Streamer->GetSpawnedCount(), Streamer->GetPendingCount());
        // Floor diagnostics: what the trace and the movement component see under the feet.
        const FVector Up = URingWorldGeometry::GetSurfaceFrame(Spec, C).GetUnitAxis(EAxis::Z);
        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(RingTestFloor), true, this);
        const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(), GetActorLocation() - Up * 500.f, ECC_Visibility, Params);
        const double DH = Map.Height(C.Arc + 100.0, C.Lateral) - Map.Height(C.Arc - 100.0, C.Lateral);
        const double DL = Map.Height(C.Arc, C.Lateral + 100.0) - Map.Height(C.Arc, C.Lateral - 100.0);
        FFindFloorResult Floor;
        GetCharacterMovement()->FindFloor(GetCharacterMovement()->UpdatedComponent->GetComponentLocation(), Floor, false);
        UE_LOG(LogTemp, Warning, TEXT("[TM] RING test   floor: trace %d on %s, impact.up %.3f normal.up %.3f | slope %.1f deg | CMC floor hit %d walkable %d dist %.1f, mode %d, gravity.up %.3f"),
            bHit ? 1 : 0, bHit ? *GetNameSafe(Hit.GetComponent()) : TEXT("-"), bHit ? FVector::DotProduct(Hit.ImpactNormal, Up) : 0.0,
            bHit ? FVector::DotProduct(Hit.Normal, Up) : 0.0, FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(DH * DH + DL * DL) / 200.0)),
            Floor.bBlockingHit ? 1 : 0, Floor.bWalkableFloor ? 1 : 0, Floor.FloorDist, static_cast<int32>(GetCharacterMovement()->MovementMode),
            FVector::DotProduct(-GetCharacterMovement()->GetGravityDirection(), Up));
        FScreenshotRequest::RequestScreenshot(Shot, true, false);
    };

    if (Step == 0)
    {
        // At the arena: look up at the ring.
        CameraPitch = 62.f;
        FTimerHandle T;
        GetWorldTimerManager().SetTimer(T, FTimerDelegate::CreateWeakLambda(this, [Report]() { Report(TEXT("Ring_00_ArenaLookUp")); }), 1.f, false);
        return;
    }
    const int32 Visit = Step - 1;
    if (Visit < Targets.Num())
    {
        const FRingRegion& R = *Targets[Visit];
        const double HalfW = Spec.Width * 0.5;
        const double Lo = R.LatLo < -1.0e11 ? -HalfW : R.LatLo, Hi = R.LatHi > 1.0e11 ? HalfW : R.LatHi;
        const double Lat = FMath::Clamp((Lo + Hi) * 0.5, -0.35 * Spec.Width, 0.35 * Spec.Width);
        const double Arc = (R.ArcStart + R.ArcEnd) * 0.5;
        const double Ground = Map.Height(Arc, Lat);
        const FTransform Frame = URingWorldGeometry::GetSurfaceFrame(Spec, FRingCoords(Arc, Lat, Ground + 250.0));
        SetActorLocation(Frame.GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
        GetCharacterMovement()->Velocity = FVector::ZeroVector;
        CachedFacingForward = Frame.GetUnitAxis(EAxis::X);
        CameraPitch = -8.f;
        UE_LOG(LogTemp, Warning, TEXT("[TM] RING test: to %s (%s) at arc %.0f km / lateral %.0f km, terrain %.1f m"), *R.Name,
            FRingRegions::Biome(R.Biome).Name, Arc / 1e5, Lat / 1e5, Ground / 100.0);
        const FString Shot = FString::Printf(TEXT("Ring_%02d_%s"), Step, *FString(FRingRegions::Biome(R.Biome).Name).Replace(TEXT(" "), TEXT("")));
        FTimerHandle T;
        GetWorldTimerManager().SetTimer(T, FTimerDelegate::CreateWeakLambda(this, [Report, Shot]() { Report(Shot); }), 3.f, false);
        return;
    }
    if (Visit == Targets.Num())
    {
        CameraPitch = 65.f;
        FTimerHandle T;
        GetWorldTimerManager().SetTimer(T, FTimerDelegate::CreateWeakLambda(this, [Report]() { Report(TEXT("Ring_99_LookUp")); }), 1.f, false);
        return;
    }
    GetWorldTimerManager().ClearTimer(RingTestTimer);
    UE_LOG(LogTemp, Warning, TEXT("[TM] RING test done (%d biomes visited) - quitting"), Targets.Num());
    if (APlayerController* PC = Cast<APlayerController>(GetController())) PC->ConsoleCommand(TEXT("quit"));
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
    // Tap = boost, hold = anchor, double tap = pull. The grapple sorts out which.
    if (!GrappleComponent) return;
    if (LaunchComponent && LaunchComponent->IsFlying()) return;
    FHitResult Hit;
    const bool bHit = TraceFromView(Hit, GrappleComponent->MaxGrappleRange);
    UE_LOG(LogTemp, Warning, TEXT("[TM] Tab pressed: %s"), bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("nothing in range"));
    GrappleComponent->OnTabPressed(Hit, bHit);
}

void ATMCharacter::OnGrappleReleased()
{
    if (GrappleComponent) GrappleComponent->OnTabReleased();
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
    if (IsCeilingRunning())
    {
        TransitionTo<TMState_Fall>();
    }
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