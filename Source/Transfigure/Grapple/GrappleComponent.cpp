// Source/TMovement/Private/Grapple/GrappleComponent.cpp
// FULL REPLACEMENT — fills in the swing physics stub with Verlet integration.

#include "Grapple/GrappleComponent.h"
#include "Transfigure.h"
#include "Grapple/GrappleLine.h"
#include "Grapple/CombatGrappleMove.h"
#include "Enemy/BaseEnemy.h"
#include "DrawDebugHelpers.h"
#include "States/TMState_WallRun.h"
#include "States/TMState_CeilingRun.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
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
    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple range %.0f (combat %.0f)"), MaxGrappleRange, CombatGrappleRange);
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

    if (bIsSlamming)
    {
        UpdateSlam(DeltaTime);
    }

#if !UE_BUILD_SHIPPING
    // Nothing renders grapple lines yet, so draw them as debug lines.
    if (bIsSwinging && OwnerCharacter)
    {
        DrawDebugLine(GetWorld(), OwnerCharacter->GetActorLocation(), SwingAnchorPoint, FColor::Cyan, false, -1.f, 0, 2.f);
    }
    for (const FGrappleLineData& Line : ActiveLines)
    {
        if (CurrentState == EGrappleState::CombatGrapple)
        {
            DrawDebugLine(GetWorld(), Line.StartPoint, Line.EndPoint, FColor::Orange, false, -1.f, 0, 2.f);
        }
    }
#endif
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
    if (SwingMode == ESwingMode::CornerSwing)
    {
        UpdateCornerSwing(DeltaTime);
        return;
    }

    auto* CMC = OwnerCharacter->GetCharacterMovement();

    // Step 1 — CharacterMovement is paused (MOVE_None) for the whole swing, so
    // nothing else moves the character or applies gravity. We own it.

    // Speed before this frame's forces: the rope may redirect momentum but never brake below it.
    const float PreSpeed = SwingVelocity.Size();

    // Step 2 — Apply gravity manually to our swing velocity.
    // Was GetWorld()->GetGravityZ() added to SwingVelocity.Z, i.e. world -Z.
    // On the ring, gravity points away from the spin axis from wherever the
    // player currently is, and its magnitude varies with altitude.
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    const double Altitude = URingWorldGeometry::WorldToRing(Spec, OwnerCharacter->GetActorLocation()).Altitude;
    const double GravityMag = URingWorldGeometry::GetGravityAtAltitude(Spec, Altitude);
    const FVector GravityDir = URingFrameUtils::GetDownFor(OwnerCharacter);
    SwingVelocity += GravityDir * static_cast<float>(GravityMag) * DeltaTime;

    // Step 2b — WASD steers/pumps the swing (movement input, flattened to the ground plane).
    const FVector Up = -GravityDir;
    const FVector Steer = FVector::VectorPlaneProject(OwnerCharacter->GetLastMovementInputVector(), Up).GetClampedToMaxSize(1.f);
    SwingVelocity += Steer * SwingSteerAcceleration * DeltaTime;

    // Step 2c — Reel the rope in so swings gain height instead of dragging along the floor.
    const float Reel = (SwingMode == ESwingMode::Swing) ? SwingReelInSpeed : WallReelInSpeed;
    SwingRopeLength = FMath::Max(MinRopeLength, SwingRopeLength - Reel * DeltaTime);

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

        // The rope can't stretch: remove the outward part of the velocity...
        FVector RopeDir = (SwingAnchorPoint - NewPos).GetSafeNormal();
        const float Speed = SwingVelocity.Size();
        float OutwardSpeed = FVector::DotProduct(SwingVelocity, -RopeDir);
        if (OutwardSpeed > 0.f)
        {
            // ...unless we're heading mostly away from the anchor (it's behind us): the rope
            // would just stop us dead, so let go and keep the speed.
            if (SwingMode == ESwingMode::Swing && Speed > 1.f && OutwardSpeed / Speed > SwingReleaseAwayDot)
            {
                UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: moving away from the anchor, released at %.0f"), Speed);
                ReleaseGrapple();
                return;
            }
            const FVector Tangent = SwingVelocity + RopeDir * OutwardSpeed;
            // ...but redirect momentum rather than lose it: never slower than before this frame.
            // (Only this frame's gravity may be taken away, or swings would gain energy.)
            SwingVelocity = Tangent.IsNearlyZero() ? Tangent
                : Tangent.GetSafeNormal() * FMath::Max(Tangent.Size(), FMath::Min(PreSpeed, Speed));
        }
    }

    // Step 5 — Apply damping so the swing doesn't gain energy over time
    SwingVelocity *= FMath::Pow(MomentumConservation, DeltaTime);

    // Step 6 — Move character to new position (bypasses CMC so we own movement).
    // If we hit something, slide along it instead of keeping speed into it.
    FHitResult SweepHit;
    OwnerCharacter->SetActorLocation(NewPos, true, &SweepHit);
    if (SweepHit.bStartPenetrating)
    {
        // Started slightly inside something (usually the floor): a sweep from
        // inside can't move at all, which felt like sticking. Push out, retry.
        OwnerCharacter->AddActorWorldOffset(SweepHit.Normal * (SweepHit.PenetrationDepth + 2.f));
        OwnerCharacter->SetActorLocation(NewPos, true, &SweepHit);
    }
    if (SweepHit.bBlockingHit)
    {
        SwingVelocity = FVector::VectorPlaneProject(SwingVelocity, SweepHit.ImpactNormal);
    }
    CMC->Velocity = SwingVelocity;

    // Ceiling at full speed: once we're up against it, start the ceiling run.
    if (SwingMode == ESwingMode::CeilingPull)
    {
        const float Half = OwnerCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const float DistToCeiling = FVector::DotProduct(OwnerCharacter->GetActorLocation() - SwingAnchorPoint, AnchorNormal);
        const bool bTouchedCeiling = SweepHit.bBlockingHit && FVector::DotProduct(SweepHit.ImpactNormal, AnchorNormal) > 0.8f;
        if (bTouchedCeiling || DistToCeiling < Half + CeilingArrivalSlack)
        {
            ArriveAtCeiling();
            return;
        }
    }

    // Wall anchor reached (close to it, or our capsule touched that wall):
    // let go and go straight into a wall run along it.
    // Measured to the wall SURFACE (not the anchor point, which may be far above/below),
    // so arrival happens exactly when a wall run can see the wall and no snap is needed.
    const bool bTouchedAnchorWall = SweepHit.bBlockingHit &&
        FVector::DotProduct(SweepHit.ImpactNormal, AnchorNormal) > 0.8f;
    const float DistToWall = FVector::DotProduct(OwnerCharacter->GetActorLocation() - SwingAnchorPoint, AnchorNormal);
    if (SwingMode == ESwingMode::WallPull && (bTouchedAnchorWall || DistToWall < WallArrivalDistance))
    {
        ArriveAtWall(Up);
        return;
    }

    // Swing: a pendulum for as long as you hold the button. If the swing carries
    // you into the anchored surface, let go keeping momentum (minus the part
    // pushing into that surface).
    if (SwingMode == ESwingMode::Swing)
    {
        const bool bTouchedAnchorSurface = SweepHit.bBlockingHit && !AnchorNormal.IsNearlyZero() &&
            FVector::DotProduct(SweepHit.ImpactNormal, AnchorNormal) > 0.8f;
        if (bTouchedAnchorSurface)
        {
            if (!AnchorNormal.IsNearlyZero() && FVector::DotProduct(SwingVelocity, AnchorNormal) < 0.f)
            {
                SwingVelocity = FVector::VectorPlaneProject(SwingVelocity, AnchorNormal);
            }
            const FVector Keep = SwingVelocity;
            ReleaseGrapple();
            // Under a ceiling the release hop would push back into it, so skip it there.
            if (FVector::DotProduct(AnchorNormal, Up) < -0.5f)
            {
                CMC->Velocity = Keep;
            }
            UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: swing touched its anchor surface, released at %.0f"), Keep.Size());
            return;
        }
    }

    // Step 7 — Update the visual line endpoint to track current player position
    if (ActiveLines.Num() > 0)
    {
        ActiveLines.Last().StartPoint = NewPos;
    }
}

void UGrappleComponent::ArriveAtWall(const FVector& Up)
{
    // Direction along the wall: keep whichever way we were already drifting sideways.
    FVector AlongWall = FVector::VectorPlaneProject(FVector::VectorPlaneProject(SwingVelocity, AnchorNormal), Up);
    if (AlongWall.Size() < 50.f)
    {
        // Came in head-on: pick the side the camera/actor is turned toward.
        AlongWall = FVector::VectorPlaneProject(FVector::VectorPlaneProject(
            OwnerCharacter->GetActorForwardVector() + OwnerCharacter->GetActorRightVector() * 0.01f, AnchorNormal), Up);
    }
    AlongWall = AlongWall.GetSafeNormal();
    const float Speed = FMath::Max(SwingVelocity.Size(), OwnerCharacter->WallRunSpeed);

    if (bFaceAlongWallOnArrival)
    {
        OwnerCharacter->SetFacingDirection(AlongWall);
    }

    SwingVelocity = AlongWall * Speed;
    ReleaseGrapple();
    // No release hop here: the wall run takes over.
    OwnerCharacter->GetCharacterMovement()->Velocity = SwingVelocity;

    if (TMState_WallRun::CanEnter(OwnerCharacter))
    {
        OwnerCharacter->TransitionTo<TMState_WallRun>();
        UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: reached wall -> wall run at %.0f"), Speed);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: reached wall, no wall run possible here (on ground or no wall beside us)"));
    }
}

// ── CORNER SWING ───────────────────────────────────────────────────────────
// Grappled the wall we're running into: instead of a yank, curve round so we end up
// running along that wall at the same speed. The turn radius is recomputed every tick
// from the distance left to the wall, so the turn finishes right as we reach it.
void UGrappleComponent::UpdateCornerSwing(float DeltaTime)
{
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    const FVector Pos = OwnerCharacter->GetActorLocation();
    const float DistToWall = FVector::DotProduct(Pos - SwingAnchorPoint, AnchorNormal);

    const FVector Vh = URingFrameUtils::StripVertical(SwingVelocity, Up);
    FVector Vv = SwingVelocity - Vh;
    FVector Dir = Vh.IsNearlyZero() ? CornerExitDir : Vh.GetSafeNormal();
    const float Phi = FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, CornerExitDir), -1.f, 1.f));

    if (Phi < FMath::DegreesToRadians(3.f) || DistToWall <= WallArrivalDistance)
    {
        SwingVelocity = Dir * CornerSpeed;
        ArriveAtWall(Up);
        return;
    }

    // Heading at angle Phi to the wall direction covers R*(1-cos Phi) toward the wall while turning.
    const float Remaining = FMath::Max(DistToWall - WallArrivalDistance * 0.8f, 1.f);
    const float Radius = FMath::Max(Remaining / FMath::Max(1.f - FMath::Cos(Phi), 0.02f), CornerSwingMinRadius);
    const float Step = FMath::Min(CornerSpeed / Radius * DeltaTime, Phi);
    FVector Axis = FVector::CrossProduct(Dir, CornerExitDir);
    Axis = Axis.IsNearlyZero() ? Up : Axis.GetSafeNormal();
    Dir = URingFrameUtils::StripVertical(FQuat(Axis, Step).RotateVector(Dir), Up).GetSafeNormal();

    // Light gravity, like a wall run.
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    const double Altitude = URingWorldGeometry::WorldToRing(Spec, Pos).Altitude;
    const float G = static_cast<float>(URingWorldGeometry::GetGravityAtAltitude(Spec, Altitude));
    Vv += -Up * G * CornerSwingGravityScale * DeltaTime;
    SwingVelocity = Dir * CornerSpeed + Vv;

    FHitResult Hit;
    OwnerCharacter->SetActorLocation(Pos + SwingVelocity * DeltaTime, true, &Hit);
    if (Hit.bStartPenetrating)
    {
        OwnerCharacter->AddActorWorldOffset(Hit.Normal * (Hit.PenetrationDepth + 2.f));
        OwnerCharacter->SetActorLocation(Pos + SwingVelocity * DeltaTime, true, &Hit);
    }
    OwnerCharacter->GetCharacterMovement()->Velocity = SwingVelocity;
    // Turn the body (and, eased, the camera) with the swing.
    OwnerCharacter->SetFacingDirection(Dir);
    if (ActiveLines.Num() > 0)
    {
        ActiveLines.Last().StartPoint = OwnerCharacter->GetActorLocation();
    }

    if (Hit.bBlockingHit)
    {
        if (FVector::DotProduct(Hit.ImpactNormal, AnchorNormal) > 0.8f)
        {
            SwingVelocity = Dir * CornerSpeed;   // reached the wall a bit early: run along it
            ArriveAtWall(Up);
        }
        else if (FVector::DotProduct(Hit.ImpactNormal, Up) > 0.7f)
        {
            SwingVelocity = Dir * CornerSpeed;   // skimming the floor: drop the downward part
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: corner swing blocked by %s"), *GetNameSafe(Hit.GetActor()));
            SwingVelocity = FVector::VectorPlaneProject(SwingVelocity, Hit.ImpactNormal);
            ReleaseGrapple();
        }
    }
}

void UGrappleComponent::ArriveAtCeiling()
{
    const FVector Normal = AnchorNormal;
    ReleaseGrapple();
    OwnerCharacter->CeilingRunNormal = Normal;
    OwnerCharacter->CeilingRunEntrySpeed = CeilingEntrySpeed;   // the speed you built up, not the yank
    OwnerCharacter->TransitionTo<TMState_CeilingRun>();
}

// ── SLAM ───────────────────────────────────────────────────────────────────
// Movement tech: dive at whatever you aim at, area damage where you land.

void UGrappleComponent::StartSlam(FVector Target, FVector TargetNormal, AActor* TargetActor, bool bHasTarget)
{
    if (!OwnerCharacter || bIsSlamming) return;

    auto* CMC = OwnerCharacter->GetCharacterMovement();
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);

    if (!bHasTarget)
    {
        // Aimed at nothing: ground-pound straight down, but only from the air.
        if (CMC->IsMovingOnGround())
        {
            UE_LOG(LogTemp, Warning, TEXT("[TM] Slam: nothing aimed at and already on the ground"));
            return;
        }
        Target = OwnerCharacter->GetActorLocation() - Up * 5000.f;
        TargetNormal = Up;
        TargetActor = nullptr;
    }

    if (!OwnerCharacter->ConsumeMana(SlamManaCost))
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Slam refused: not enough mana"));
        return;
    }

    // Reward speed: how fast we're going when V is pressed scales the impact.
    const float EntrySpeed = bIsSwinging ? SwingVelocity.Size() : CMC->Velocity.Size();
    const float Alpha = FMath::GetMappedRangeValueClamped(
        FVector2D(SlamMomentumMinSpeed, SlamMomentumMaxSpeed), FVector2D(0.f, 1.f), EntrySpeed);
    SlamMomentumScale = FMath::Lerp(1.f, SlamMaxMomentumScale, Alpha);

    // The dive itself gets faster with momentum too, and never slows you down.
    SlamDiveSpeed = FMath::Max(SlamSpeed * FMath::Lerp(1.f, SlamMaxDiveSpeedScale, Alpha), EntrySpeed * 1.15f);

    if (bIsSwinging) ReleaseGrapple();

    SlamTarget = Target;
    SlamTargetNormal = TargetNormal.GetSafeNormal();
    SlamTargetActor = TargetActor;
    SlamDir = (Target - OwnerCharacter->GetActorLocation()).GetSafeNormal();
    SlamElapsed = 0.f;
    bIsSlamming = true;

    CMC->SetMovementMode(MOVE_Falling);
    CMC->Velocity = SlamDir * SlamDiveSpeed;

    UE_LOG(LogTemp, Warning, TEXT("[TM] Slam: diving at %s (%s), entry speed %.0f -> momentum x%.2f, dive speed %.0f"),
        *GetNameSafe(TargetActor), bHasTarget ? TEXT("aimed") : TEXT("ground-pound"), EntrySpeed, SlamMomentumScale, SlamDiveSpeed);
}

void UGrappleComponent::UpdateSlam(float DeltaTime)
{
    auto* CMC = OwnerCharacter->GetCharacterMovement();
    SlamElapsed += DeltaTime;

    const FVector Pos = OwnerCharacter->GetActorLocation();
    const float Radius = OwnerCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();

#if !UE_BUILD_SHIPPING
    DrawDebugLine(GetWorld(), Pos, SlamTarget, FColor::Orange, false, -1.f, 0, 2.f);
#endif

    // Impact when: landed, reached the point, got blocked (e.g. hit a wall), or timed out.
    const bool bLanded = SlamElapsed > 0.05f && CMC->IsMovingOnGround();
    const bool bReached = FVector::Dist(Pos, SlamTarget) < Radius + 60.f;
    const bool bBlocked = SlamElapsed > 0.08f && FVector::DotProduct(CMC->Velocity, SlamDir) < SlamDiveSpeed * 0.3f;
    if (bLanded || bReached || bBlocked || SlamElapsed > SlamMaxTime)
    {
        const FVector Normal = bLanded ? URingFrameUtils::GetUpFor(OwnerCharacter) : SlamTargetNormal;
        SlamImpact(bReached ? SlamTarget : Pos, Normal, SlamTargetActor.Get());
        return;
    }

    // Hold the dive speed so gravity/air friction don't soften it.
    CMC->Velocity = SlamDir * SlamDiveSpeed;
}

void UGrappleComponent::SlamImpact(const FVector& Location, const FVector& Normal, AActor* Surface)
{
    bIsSlamming = false;
    auto* CMC = OwnerCharacter->GetCharacterMovement();
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);

    // Stop the dive; a small bounce keeps it feeling springy rather than dead.
    CMC->Velocity = Up * 250.f;

    // Area damage + knockback on every enemy in range, all scaled by momentum.
    const float Radius = SlamRadius * SlamMomentumScale;
    const float Damage = SlamDamage * SlamMomentumScale;
    const float Knockback = SlamKnockback * SlamMomentumScale;

    // Weapon channel: finds enemy capsules and zombie hit zones, including on corpses.
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleSlam), false, OwnerCharacter);
    GetWorld()->OverlapMultiByChannel(Overlaps, Location, FQuat::Identity, ECC_Weapon,
        FCollisionShape::MakeSphere(Radius), Params);

    TSet<ABaseEnemy*> Hit;
    for (const FOverlapResult& Overlap : Overlaps)
    {
        ABaseEnemy* Enemy = Cast<ABaseEnemy>(Overlap.GetActor());
        if (!Enemy || Hit.Contains(Enemy)) continue;
        Hit.Add(Enemy);   // corpses included: they can still lose limbs

        // Full at the centre, half at the edge; zombies spread it over every part in range.
        Enemy->TakeAreaDamage(Damage * OwnerCharacter->GetDamageMultiplier(), Location, 0.f, Radius);

        if (Enemy->IsAlive())
        {
            const FVector Away = FVector::VectorPlaneProject(Enemy->GetActorLocation() - Location, Up).GetSafeNormal();
            Enemy->LaunchCharacter((Away + Up * 0.6f).GetSafeNormal() * Knockback, true, true);
        }
    }

#if !UE_BUILD_SHIPPING
    DrawDebugSphere(GetWorld(), Location, Radius, 16, FColor::Orange, false, 0.6f, 0, 2.f);
#endif

    UE_LOG(LogTemp, Warning, TEXT("[TM] Slam: impact on %s after %.2fs, x%.2f (radius %.0f, damage %.0f), hit %d enemies"),
        *GetNameSafe(Surface), SlamElapsed, SlamMomentumScale, Radius, Damage, Hit.Num());

    OnSlamImpact.Broadcast(Location, Normal, Surface);
    OnCombatGrappleExecuted.Broadcast(ECombatGrappleMove::Slam);
}

void UGrappleComponent::StartAiming(ETransfigurationElement Element)
{
    if (CurrentState != EGrappleState::Inactive) return;

    CurrentElement = Element;
    CurrentState = EGrappleState::Aiming;
    OnGrappleStateChanged.Broadcast(CurrentState);
}

void UGrappleComponent::FireGrapple(FVector AimLocation, AActor* TargetEnemy, FVector AimNormal)
{
    AnchorNormal = AimNormal.GetSafeNormal();
    if (CurrentState != EGrappleState::Aiming && CurrentState != EGrappleState::Inactive) return;
    if (!OwnerCharacter) return;

    // Only real, living enemies count as combat targets (was: any actor, e.g. a wall).
    const ABaseEnemy* Enemy = Cast<ABaseEnemy>(TargetEnemy);
    const bool bCombat = Enemy && Enemy->IsAlive() && IsEnemyGrappleable(TargetEnemy);

    if (!OwnerCharacter->ConsumeMana(bCombat ? CombatGrappleManaCost : SwingManaCost))
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Not enough mana for grapple"));
        return;
    }

    if (bCombat)
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
    SwingRopeLength = FMath::Max(SwingRopeLength, MinRopeLength);

    // Wall = surface normal close to horizontal (perpendicular to local up).
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    const float MaxUpDot = FMath::Sin(FMath::DegreesToRadians(WallMaxTiltDegrees));
    bWallAnchor = !AnchorNormal.IsNearlyZero() && FMath::Abs(FVector::DotProduct(AnchorNormal, Up)) <= MaxUpDot;
    const bool bCeiling = !AnchorNormal.IsNearlyZero() && FVector::DotProduct(AnchorNormal, -Up) >= CeilingMinDownDot;

    const FVector Pos = OwnerCharacter->GetActorLocation();
    const FVector Vh = URingFrameUtils::StripVertical(PreVelocity, Up);
    SwingMode = ESwingMode::Swing;
    if (bWallAnchor)
    {
        SwingMode = ESwingMode::WallPull;
        // Close to the wall and running into it: swing round onto it instead of yanking.
        const float DistToWall = FVector::DotProduct(Pos - AnchorPoint, AnchorNormal);
        const float Approach = Vh.IsNearlyZero() ? 0.f : FVector::DotProduct(Vh.GetSafeNormal(), -AnchorNormal);
        if (bEnableCornerSwing && DistToWall <= CornerSwingMaxDistance && DistToWall > WallArrivalDistance
            && Vh.Size() >= CornerSwingMinSpeed && Approach >= CornerSwingMinApproach)
        {
            SwingMode = ESwingMode::CornerSwing;
            // Turn toward the side you aimed at (else the way you're drifting, else your right).
            const FVector Along = FVector::CrossProduct(Up, AnchorNormal).GetSafeNormal();
            const float Lateral = FVector::DotProduct(AnchorPoint - Pos, Along);
            const float Drift = FVector::DotProduct(Vh, Along);
            const float Side = FMath::Abs(Lateral) > 30.f ? Lateral
                : (FMath::Abs(Drift) > 50.f ? Drift : FVector::DotProduct(OwnerCharacter->GetActorRightVector(), Along));
            CornerExitDir = Along * (Side >= 0.f ? 1.f : -1.f);
            CornerSpeed = FMath::Max(Vh.Size(), OwnerCharacter->WallRunSpeed) * CornerSwingSpeedBoost;
        }
    }
    else if (bCeiling && OwnerCharacter->CanStartCeilingRun())
    {
        SwingMode = ESwingMode::CeilingPull;
        CeilingEntrySpeed = Vh.Size();
    }

    // Yank toward the anchor so the move starts immediately, even from standing.
    const FVector ToAnchor = (AnchorPoint - Pos).GetSafeNormal();
    switch (SwingMode)
    {
    case ESwingMode::WallPull:
    case ESwingMode::CeilingPull:
        SwingVelocity += ToAnchor * WallPullSpeed;
        break;
    case ESwingMode::Swing:
        SwingVelocity += ToAnchor * SwingAttachTug;
        break;
    case ESwingMode::CornerSwing:
        // Keep the run, level out: the turn does the work.
        SwingVelocity = Vh + Up * FMath::Clamp(FVector::DotProduct(PreVelocity, Up), -200.f, 200.f);
        break;
    }

    // Create visual line data
    FGrappleLineData NewLine;
    NewLine.StartPoint = OwnerCharacter->GetActorLocation();
    NewLine.EndPoint = AnchorPoint;
    NewLine.Element = CurrentElement;
    NewLine.Lifetime = MaxSwingTime;
    NewLine.bIsActive = true;

    ActiveLines.Add(NewLine);
    OnGrappleLineCreated.Broadcast(NewLine);

    // Starting from the ground: lift off a little so the first frames don't scrape the floor.
    if (OwnerCharacter->GetCharacterMovement()->IsMovingOnGround())
    {
        OwnerCharacter->AddActorWorldOffset(Up * 10.f, true);
        SwingVelocity += Up * (SwingMode == ESwingMode::CornerSwing ? 150.f : GroundLiftSpeed);
    }

    // Pause CharacterMovement so it doesn't fight the swing physics.
    OwnerCharacter->GetCharacterMovement()->SetMovementMode(MOVE_None);

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

    static const TCHAR* ModeNames[] = { TEXT("SWING"), TEXT("WALL pull"), TEXT("CORNER swing"), TEXT("CEILING pull (ceiling run)") };
    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: %s to %s, rope %.0f, speed %.0f (zoom %.2f)%s"),
        ModeNames[static_cast<int32>(SwingMode)], *AnchorPoint.ToString(), SwingRopeLength, Vh.Size(),
        OwnerCharacter->GetSpeedFOVAlpha(),
        SwingMode == ESwingMode::CornerSwing ? *FString::Printf(TEXT(", turning %s"), *CornerExitDir.ToCompactString()) : TEXT(""));
}

void UGrappleComponent::PerformCombatGrapple(AActor* TargetEnemy, ECombatGrappleMove MoveType)
{
    ABaseEnemy* Enemy = Cast<ABaseEnemy>(TargetEnemy);
    if (!OwnerCharacter || !Enemy) return;

    // Pull toward the enemy (was Distance * 10, i.e. up to 30,000 cm/s).
    // Launch (not impulse) so ground friction can't eat the yank; slight lift keeps it snappy.
    const FVector ToEnemy = Enemy->GetActorLocation() - OwnerCharacter->GetActorLocation();
    const float PullSpeed = FMath::Min(ToEnemy.Size() * 3.f, MaxCombatPullSpeed);
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    OwnerCharacter->LaunchCharacter(ToEnemy.GetSafeNormal() * PullSpeed + Up * 250.f, true, true);

    // The move itself deals the damage (was never called).
    TSubclassOf<UCombatGrappleMove> MoveClass = UCombatGrappleMove::StaticClass();
    if (const TSubclassOf<UCombatGrappleMove>* Found = CombatMoveClasses.Find(MoveType))
    {
        if (*Found) MoveClass = *Found;
    }

    UCombatGrappleMove* Move = NewObject<UCombatGrappleMove>(this, MoveClass);
    Move->MoveType = MoveType;
    Move->ExecuteMove(OwnerCharacter, Enemy, CurrentElement);

    CurrentState = EGrappleState::CombatGrapple;
    OnGrappleStateChanged.Broadcast(CurrentState);
    OnCombatGrappleExecuted.Broadcast(MoveType);

    FGrappleLineData CombatLine;
    CombatLine.StartPoint = OwnerCharacter->GetActorLocation();
    CombatLine.EndPoint = Enemy->GetActorLocation();
    CombatLine.Element = CurrentElement;
    CombatLine.Lifetime = CombatMoveDuration;
    CombatLine.bIsActive = true;
    ActiveLines.Add(CombatLine);
    OnGrappleLineCreated.Broadcast(CombatLine);

    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: combat move %s on %s"),
        *UEnum::GetValueAsString(MoveType), *Enemy->GetName());

    // Was never cleared, which locked the grapple forever after one combat move.
    GetWorld()->GetTimerManager().SetTimer(
        CombatMoveTimerHandle, this, &UGrappleComponent::FinishCombatMove, CombatMoveDuration, false);
}

void UGrappleComponent::FinishCombatMove()
{
    CurrentState = EGrappleState::Inactive;
    OnGrappleStateChanged.Broadcast(CurrentState);
}

void UGrappleComponent::PerformEvade()
{
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now - LastEvadeTime < EvadeCooldown) return;

    if (!OwnerCharacter->ConsumeMana(EvadeManaCost))
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Evade refused: not enough mana"));
        return;
    }
    LastEvadeTime = Now;

    // Evading mid-swing lets go of the rope first.
    if (bIsSwinging) ReleaseGrapple();

    // Launch (not impulse): ground friction was cancelling the impulse almost instantly.
    // Flat dash along the input direction plus a small hop so friction doesn't apply.
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    const FVector Flat = FVector::VectorPlaneProject(EvadeDirection, Up).GetSafeNormal();
    OwnerCharacter->LaunchCharacter(Flat * EvadeSpeed + Up * 300.f, true, true);
    OnCombatGrappleExecuted.Broadcast(ECombatGrappleMove::Evade);
    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: evade toward %s"), *EvadeDirection.ToString());
}

void UGrappleComponent::ExecuteCombatMove(ECombatGrappleMove MoveType, AActor* TargetEnemy)
{
    if (!OwnerCharacter) return;

    // Evade works from any state (it cancels a swing).
    if (MoveType == ECombatGrappleMove::Evade)
    {
        PerformEvade();
        return;
    }

    // Combat moves can chain into each other; only an active swing blocks them.
    if (CurrentState == EGrappleState::Swinging || CurrentState == EGrappleState::WebBuilding)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Combat move %s refused: grapple busy (state %d)"),
            *UEnum::GetValueAsString(MoveType), (int32)CurrentState);
        return;
    }

    // WebTrap has no target-free behavior yet.
    if (MoveType == ECombatGrappleMove::WebTrap) return;

    ABaseEnemy* Enemy = Cast<ABaseEnemy>(TargetEnemy);
    if (!Enemy || !Enemy->IsAlive() || !IsEnemyGrappleable(Enemy))
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Combat move %s refused: aimed at %s, not a living enemy in range"),
            *UEnum::GetValueAsString(MoveType), *GetNameSafe(TargetEnemy));
        return;
    }
    if (!OwnerCharacter->ConsumeMana(CombatGrappleManaCost))
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Combat move %s refused: not enough mana"), *UEnum::GetValueAsString(MoveType));
        return;
    }

    PerformCombatGrapple(Enemy, MoveType);
}

void UGrappleComponent::ReleaseGrapple()
{
    if (!bIsSwinging) return;

    bIsSwinging = false;

    // Hand control back to CharacterMovement, carrying the swing's momentum.
    if (OwnerCharacter)
    {
        auto* CMC = OwnerCharacter->GetCharacterMovement();
        CMC->SetMovementMode(MOVE_Falling);
        CMC->Velocity = SwingVelocity + URingFrameUtils::GetUpFor(OwnerCharacter) * ReleaseHopSpeed;
    }

    CurrentState = EGrappleState::Inactive;
    OnGrappleStateChanged.Broadcast(CurrentState);

    GetWorld()->GetTimerManager().ClearTimer(SwingTimerHandle);

    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: released, exit velocity %s"),
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
    return Distance <= CombatGrappleRange;
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