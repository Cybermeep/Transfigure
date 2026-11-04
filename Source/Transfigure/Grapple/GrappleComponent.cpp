// Source/TMovement/Private/Grapple/GrappleComponent.cpp
// FULL REPLACEMENT — fills in the swing physics stub with Verlet integration.

#include "Grapple/GrappleComponent.h"
#include "Grapple/GrappleLine.h"
#include "Character/TMCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "RingFrameUtils.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"

UGrappleComponent::UGrappleComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UGrappleComponent::BeginPlay()
{
    Super::BeginPlay();
    OwnerCharacter = Cast<ATMCharacter>(GetOwner());
}

void UGrappleComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    UpdateLineLifecycles(DeltaTime);

    if (bIsSwinging)
    {
        UpdateSwingPhysics(DeltaTime);
    }
}

// ── SWING PHYSICS (Verlet constraint) ──────────────────────────────────────
//
// How it works:
//   1. Apply gravity to SwingVelocity each frame.
//   2. Move the character by SwingVelocity * DeltaTime.
//   3. Measure how far the new position is from the anchor point.
//   4. If distance > rope length, project the character back onto the sphere
//      surface and strip the outward component from velocity (constraint).
//
// This gives natural pendulum arcs without needing a full physics joint.
// MomentumConservation (0–1) controls how much velocity survives each frame.
// ───────────────────────────────────────────────────────────────────────────
void UGrappleComponent::UpdateSwingPhysics(float DeltaTime)
{
    if (!OwnerCharacter) return;

    auto* CMC = OwnerCharacter->GetCharacterMovement();

    // Step 1 — Disable built-in gravity while we're handling it manually
    CMC->GravityScale = 0.f;

    // Step 2 — Apply gravity manually to our swing velocity.
    // Was GetWorld()->GetGravityZ() added to SwingVelocity.Z, i.e. world -Z.
    // On the ring, gravity points away from the spin axis from wherever the
    // player currently is, and its magnitude varies with altitude.
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    const double Altitude = URingWorldGeometry::WorldToRing(Spec, OwnerCharacter->GetActorLocation()).Altitude;
    const double GravityMag = URingWorldGeometry::GetGravityAtAltitude(Spec, Altitude);
    const FVector GravityDir = URingFrameUtils::GetDownFor(OwnerCharacter);
    SwingVelocity += GravityDir * static_cast<float>(GravityMag) * DeltaTime;

    // Step 3 — Integrate position
    FVector CurrentPos = OwnerCharacter->GetActorLocation();
    FVector NewPos = CurrentPos + SwingVelocity * DeltaTime;

    // Step 4 — Enforce rope length constraint (project back onto sphere)
    FVector ToAnchor = SwingAnchorPoint - NewPos;
    float CurrentDist = ToAnchor.Size();

    if (CurrentDist > SwingRopeLength && CurrentDist > KINDA_SMALL_NUMBER)
    {
        // Push the position back so it's exactly RopeLength from the anchor
        FVector Correction = ToAnchor.GetSafeNormal() * (CurrentDist - SwingRopeLength);
        NewPos += Correction;

        // Strip outward velocity component (the rope can't stretch)
        FVector RopeDir = (SwingAnchorPoint - NewPos).GetSafeNormal();
        float OutwardSpeed = FVector::DotProduct(SwingVelocity, -RopeDir);
        if (OutwardSpeed > 0.f)
        {
            SwingVelocity += RopeDir * OutwardSpeed;
        }
    }

    // Step 5 — Apply damping so the swing doesn't gain energy over time
    SwingVelocity *= FMath::Pow(MomentumConservation, DeltaTime);

    // Step 6 — Move character to new position (bypasses CMC so we own movement)
    OwnerCharacter->SetActorLocation(NewPos, true);

    // Step 7 — Update the visual line endpoint to track current player position
    if (ActiveLines.Num() > 0)
    {
        ActiveLines.Last().StartPoint = NewPos;
    }
}

void UGrappleComponent::StartAiming(ETransfigurationElement Element)
{
    if (CurrentState != EGrappleState::Inactive) return;

    CurrentElement = Element;
    CurrentState = EGrappleState::Aiming;
    OnGrappleStateChanged.Broadcast(CurrentState);
}

void UGrappleComponent::FireGrapple(FVector AimLocation, AActor* TargetEnemy)
{
    if (CurrentState != EGrappleState::Aiming && CurrentState != EGrappleState::Inactive) return;
    if (!OwnerCharacter) return;

    float ManaCost = (TargetEnemy ? CombatGrappleManaCost : SwingManaCost);
    if (!OwnerCharacter->ConsumeMana(ManaCost))
    {
        UE_LOG(LogTemp, Warning, TEXT("Not enough mana for grapple"));
        return;
    }

    if (TargetEnemy && IsEnemyGrappleable(TargetEnemy))
    {
        PerformCombatGrapple(TargetEnemy, ECombatGrappleMove::Kick);
    }
    else
    {
        PerformGrappleSwing(AimLocation);
    }
}

void UGrappleComponent::PerformGrappleSwing(FVector AnchorPoint)
{
    if (!OwnerCharacter) return;

    // Store pre-grapple velocity for initial swing velocity
    FVector PreVelocity = OwnerCharacter->GetCharacterMovement()->Velocity;
    PreserveMomentum(PreVelocity);

    // Record anchor and rope length for physics
    SwingAnchorPoint = AnchorPoint;
    SwingRopeLength = FVector::Dist(OwnerCharacter->GetActorLocation(), AnchorPoint);
    SwingRopeLength = FMath::Max(SwingRopeLength, 100.f); // Minimum rope length

    // Create visual line data
    FGrappleLineData NewLine;
    NewLine.StartPoint = OwnerCharacter->GetActorLocation();
    NewLine.EndPoint = AnchorPoint;
    NewLine.Element = CurrentElement;
    NewLine.Lifetime = MaxSwingTime;
    NewLine.bIsActive = true;

    ActiveLines.Add(NewLine);
    OnGrappleLineCreated.Broadcast(NewLine);

    bIsSwinging = true;
    CurrentState = EGrappleState::Swinging;
    OnGrappleStateChanged.Broadcast(CurrentState);

    // Auto-release after MaxSwingTime
    GetWorld()->GetTimerManager().SetTimer(
        SwingTimerHandle,
        this, &UGrappleComponent::ReleaseGrapple,
        MaxSwingTime,
        false);

    ApplyElementalEffect(ActiveLines.Last());

    UE_LOG(LogTemp, Verbose, TEXT("GrappleComponent: Started swing to %s, rope length %.1f"),
        *AnchorPoint.ToString(), SwingRopeLength);
}

void UGrappleComponent::PerformCombatGrapple(AActor* TargetEnemy, ECombatGrappleMove MoveType)
{
    if (!OwnerCharacter || !TargetEnemy) return;

    FVector Direction = (TargetEnemy->GetActorLocation() -
        OwnerCharacter->GetActorLocation()).GetSafeNormal();
    float Distance = FVector::Dist(OwnerCharacter->GetActorLocation(),
        TargetEnemy->GetActorLocation());

    OwnerCharacter->GetCharacterMovement()->AddImpulse(Direction * Distance * 10.0f, true);

    CurrentState = EGrappleState::CombatGrapple;
    OnGrappleStateChanged.Broadcast(CurrentState);
    OnCombatGrappleExecuted.Broadcast(MoveType);

    FGrappleLineData CombatLine;
    CombatLine.StartPoint = OwnerCharacter->GetActorLocation();
    CombatLine.EndPoint = TargetEnemy->GetActorLocation();
    CombatLine.Element = CurrentElement;
    CombatLine.Lifetime = 0.5f;
    CombatLine.bIsActive = true;

    ActiveLines.Add(CombatLine);
    OnGrappleLineCreated.Broadcast(CombatLine);
}

void UGrappleComponent::ExecuteCombatMove(ECombatGrappleMove MoveType, AActor* TargetEnemy)
{
    if (!OwnerCharacter) return;
    PerformCombatGrapple(TargetEnemy, MoveType);
}

void UGrappleComponent::ReleaseGrapple()
{
    if (!bIsSwinging) return;

    bIsSwinging = false;

    // Restore normal gravity
    if (OwnerCharacter)
    {
        OwnerCharacter->GetCharacterMovement()->GravityScale = 1.f;

        // Hand off swing velocity to the CharacterMovementComponent
        // so the player carries their momentum out of the swing
        OwnerCharacter->GetCharacterMovement()->Velocity = SwingVelocity;
    }

    CurrentState = EGrappleState::Inactive;
    OnGrappleStateChanged.Broadcast(CurrentState);

    GetWorld()->GetTimerManager().ClearTimer(SwingTimerHandle);

    UE_LOG(LogTemp, Verbose, TEXT("GrappleComponent: Released grapple, exit velocity %s"),
        *SwingVelocity.ToString());
}

void UGrappleComponent::CancelAiming()
{
    if (CurrentState != EGrappleState::Aiming) return;
    CurrentState = EGrappleState::Inactive;
    OnGrappleStateChanged.Broadcast(CurrentState);
}

void UGrappleComponent::CreateWebLine(FVector StartPoint, FVector EndPoint)
{
    if (!CanCreateWebLine()) return;
    if (!OwnerCharacter->ConsumeMana(WebLineManaCost)) return;

    FGrappleLineData WebLine;
    WebLine.StartPoint = StartPoint;
    WebLine.EndPoint = EndPoint;
    WebLine.Element = CurrentElement;
    WebLine.Lifetime = GetCurrentTierLines() * 2.0f;
    WebLine.bIsActive = true;

    ActiveLines.Add(WebLine);
    OnGrappleLineCreated.Broadcast(WebLine);
}

bool UGrappleComponent::CanCreateWebLine() const
{
    return CurrentTier >= EGrappleTier::Web && ActiveLines.Num() < MaxSimultaneousLines;
}

TArray<FVector> UGrappleComponent::FindWebConnectionPoints() const
{
    // Scan for nearby sigils and destroyed geometry to anchor web lines
    // Full implementation would use overlap queries against sigil actors
    return TArray<FVector>();
}

bool UGrappleComponent::IsEnemyGrappleable(AActor* Enemy) const
{
    if (!Enemy || !OwnerCharacter) return false;
    float Distance = FVector::Dist(OwnerCharacter->GetActorLocation(), Enemy->GetActorLocation());
    return Distance <= MaxGrappleRange;
}

float UGrappleComponent::GetCurrentTierLines() const
{
    switch (CurrentTier)
    {
    case EGrappleTier::Strand: return 1.0f;
    case EGrappleTier::Thread: return 2.0f;
    case EGrappleTier::Cord:   return 3.0f;
    case EGrappleTier::Rope:   return 4.0f;
    case EGrappleTier::Web:    return 6.0f;
    case EGrappleTier::Nexus:  return 8.0f;
    default:                   return 1.0f;
    }
}

float UGrappleComponent::GetMomentumPreservation() const
{
    return MomentumConservation;
}

void UGrappleComponent::ApplyElementalEffect(FGrappleLineData& LineData)
{
    // Elemental effects on grapple lines (visual/gameplay tinting)
    // Full VFX implementation driven from Blueprint via OnGrappleLineCreated delegate
}

void UGrappleComponent::UpdateLineLifecycles(float DeltaTime)
{
    for (int32 i = ActiveLines.Num() - 1; i >= 0; i--)
    {
        ActiveLines[i].Lifetime -= DeltaTime;
        if (ActiveLines[i].Lifetime <= 0.0f)
        {
            ActiveLines.RemoveAt(i);
        }
    }
    CurrentLineCount = ActiveLines.Num();
}

void UGrappleComponent::CleanupExpiredLines()
{
    // Handled in UpdateLineLifecycles
}

bool UGrappleComponent::HasLineTierCapability(FString Capability) const
{
    return true;
}

void UGrappleComponent::PreserveMomentum(FVector PreGrappleVelocity)
{
    SwingVelocity = PreGrappleVelocity * MomentumConservation;
}