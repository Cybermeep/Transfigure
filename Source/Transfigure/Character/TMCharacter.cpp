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
#include "Transfiguration/TransfigurationCastingComponent.h"
#include "Transfiguration/TransfigurationComboDetector.h"
#include "Transfiguration/BuffEffectComponent.h"
#include "Grapple/GrappleComponent.h"
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
}

void ATMCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (auto* Sub = ULocalPlayer::GetSubsystem<
            UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            Sub->AddMappingContext(DefaultMappingContext, 0);
        }
    }

    TransitionTo<TMState_Idle>();

    CurrentMana = StartingMana;
    StartManaRegen();
}

void ATMCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    UpdateRingGravity(DeltaTime);
    StateMachine.Tick(DeltaTime);
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
    const FVector Right = FVector::CrossProduct(CachedFacingForward, CachedRingUp).GetSafeNormal();
    const FMatrix Basis(CachedFacingForward, Right, CachedRingUp, FVector::ZeroVector);
    SetActorRotation(Basis.ToQuat());

    // Pitch is camera-only, never touches the actor.
    if (!FMath::IsNearlyZero(PendingPitchInput))
    {
        CameraPitch = FMath::Clamp(CameraPitch - PendingPitchInput, MinCameraPitch, MaxCameraPitch);
        PendingPitchInput = 0.0f;
    }
    if (CameraPitchTarget)
    {
        CameraPitchTarget->SetRelativeRotation(FRotator(CameraPitch, 0.0f, 0.0f));
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
    EIC->BindAction(IA_PlaceSigil, ETriggerEvent::Started, this, &ATMCharacter::OnPlaceSigilPressed);
    EIC->BindAction(IA_TriggerSigil, ETriggerEvent::Started, this, &ATMCharacter::OnTriggerSigilPressed);
    EIC->BindAction(IA_TriggerAll, ETriggerEvent::Started, this, &ATMCharacter::OnTriggerAllPressed);

    // Temporal Input
    EIC->BindAction(IA_TemporalClap, ETriggerEvent::Started, this, &ATMCharacter::OnTemporalClapPressed);

    // Grapple Inputs
    EIC->BindAction(IA_Grapple, ETriggerEvent::Started, this, &ATMCharacter::OnGrapplePressed);
    EIC->BindAction(IA_Grapple, ETriggerEvent::Triggered, this, &ATMCharacter::OnGrapplePressed);
    EIC->BindAction(IA_Grapple, ETriggerEvent::Completed, this, &ATMCharacter::OnGrapplePressed);
    EIC->BindAction(IA_GrappleKick, ETriggerEvent::Started, this, &ATMCharacter::OnGrappleKickPressed);
    EIC->BindAction(IA_GrappleSlam, ETriggerEvent::Started, this, &ATMCharacter::OnGrappleSlamPressed);
    EIC->BindAction(IA_GrappleToss, ETriggerEvent::Started, this, &ATMCharacter::OnGrappleTossPressed);
    EIC->BindAction(IA_GrappleEvade, ETriggerEvent::Started, this, &ATMCharacter::OnGrappleEvadePressed);
    EIC->BindAction(IA_GrapplePull, ETriggerEvent::Started, this, &ATMCharacter::OnGrapplePullPressed);
    EIC->BindAction(IA_WebTrap, ETriggerEvent::Started, this, &ATMCharacter::OnWebTrapPressed);
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
    // Accumulated and consumed in UpdateRingGravity, not applied to
    // ControlRotation — see the comment on CameraPitchTarget in the header
    // for why (ControlRotation is a world-axis rotator, not gravity-relative).
    PendingYawInput += Vec.X * LookYawSensitivity;
    PendingPitchInput += Vec.Y * LookPitchSensitivity;
}

void ATMCharacter::OnJumpPressed()
{
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


// ── MAGIC INPUT HANDLERS ──

void ATMCharacter::OnDrawSigilPressed()
{
    TransfigurationCasting->BeginDrawMode(FName("FireBlast"));
}

void ATMCharacter::OnPlaceSigilPressed()
{
    TransfigurationCasting->ConfirmPlacement();
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

void ATMCharacter::OnGrapplePressed()
{
    if (!GrappleComponent) return;

    FHitResult Hit;
    FVector CamLoc;
    FRotator CamRot;
    GetController()->GetPlayerViewPoint(CamLoc, CamRot);
    FVector TraceEnd = CamLoc + CamRot.Vector() * 3000.0f;

    GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, TraceEnd, ECC_Visibility);

    if (Hit.bBlockingHit)
    {
        GrappleComponent->FireGrapple(Hit.ImpactPoint, Hit.GetActor());
    }
}

void ATMCharacter::OnGrappleKickPressed()
{
    if (!GrappleComponent) return;

    FHitResult Hit;
    FVector CamLoc;
    FRotator CamRot;
    GetController()->GetPlayerViewPoint(CamLoc, CamRot);
    FVector TraceEnd = CamLoc + CamRot.Vector() * 3000.0f;

    GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, TraceEnd, ECC_Pawn);

    if (Hit.bBlockingHit && Hit.GetActor())
    {
        GrappleComponent->ExecuteCombatMove(ECombatGrappleMove::Kick, Hit.GetActor());
    }
}

void ATMCharacter::OnGrappleSlamPressed()
{
    if (!GrappleComponent) return;

    FHitResult Hit;
    FVector CamLoc;
    FRotator CamRot;
    GetController()->GetPlayerViewPoint(CamLoc, CamRot);
    FVector TraceEnd = CamLoc + CamRot.Vector() * 3000.0f;

    GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, TraceEnd, ECC_Pawn);

    if (Hit.bBlockingHit && Hit.GetActor())
    {
        GrappleComponent->ExecuteCombatMove(ECombatGrappleMove::Slam, Hit.GetActor());
    }
}

void ATMCharacter::OnGrappleTossPressed()
{
    if (!GrappleComponent) return;

    FHitResult Hit;
    FVector CamLoc;
    FRotator CamRot;
    GetController()->GetPlayerViewPoint(CamLoc, CamRot);
    FVector TraceEnd = CamLoc + CamRot.Vector() * 3000.0f;

    GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, TraceEnd, ECC_Pawn);

    if (Hit.bBlockingHit && Hit.GetActor())
    {
        GrappleComponent->ExecuteCombatMove(ECombatGrappleMove::Toss, Hit.GetActor());
    }
}

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

void ATMCharacter::OnGrapplePullPressed()
{
    if (!GrappleComponent) return;

    FHitResult Hit;
    FVector CamLoc;
    FRotator CamRot;
    GetController()->GetPlayerViewPoint(CamLoc, CamRot);
    FVector TraceEnd = CamLoc + CamRot.Vector() * 3000.0f;

    GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, TraceEnd, ECC_Pawn);

    if (Hit.bBlockingHit && Hit.GetActor())
    {
        GrappleComponent->ExecuteCombatMove(ECombatGrappleMove::Pull, Hit.GetActor());
    }
}

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
    return GrappleComponent && GrappleComponent->GetCurrentState() != EGrappleState::Inactive;
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
    if (!IsLocationDestructible(Location)) return false;
    return true;
}

// ── MANA SYSTEM ──

bool ATMCharacter::ConsumeMana(float ManaCost)
{
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
        ClapAnimationDuration,
        false);

    GetWorldTimerManager().SetTimer(
        TemporalTimerHandle,
        this, &ATMCharacter::EndTemporalClap,
        TemporalClapDuration,
        false);
}

void ATMCharacter::StartTemporalEffects()
{
    UGameplayStatics::SetGlobalTimeDilation(GetWorld(), TemporalTimeDilation);
    CustomTimeDilation = 1.0f;

    APlayerController* PC = Cast<APlayerController>(GetController());
    if (PC && PC->PlayerCameraManager)
    {
        FRotator NewRotation = PC->GetControlRotation();
        NewRotation.Roll = TemporalCameraTiltAngle;
        PC->SetControlRotation(NewRotation);

        PC->PlayerCameraManager->SetFOV(TemporalCameraFOV);
    }
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

    EndTemporalEffects();
    OnTemporalClapEnded.Broadcast();
    BeginTemporalCooldown();
}

void ATMCharacter::EndTemporalEffects()
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (PC && PC->PlayerCameraManager)
    {
        FRotator NewRotation = PC->GetControlRotation();
        NewRotation.Roll = 0.0f;
        PC->SetControlRotation(NewRotation);

        PC->PlayerCameraManager->SetFOV(OriginalCameraFOV);
    }
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

    FTimerHandle UnusedHandle;
    GetWorldTimerManager().SetTimer(UnusedHandle, [this]()
        {
            CurrentDamageMultiplier = 1.0f;
        }, Duration, false);
}
void ATMCharacter::OnSprintStarted()
{
    TransitionTo<TMState_Sprint>();
}

void ATMCharacter::OnSprintEnded()
{
    // Only drop back to walk if we're actually sprinting
    // (button release during a jump etc. should be ignored)
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