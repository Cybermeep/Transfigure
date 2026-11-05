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
#include "States/TMState_Jump.h"
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
    UpdateTabTiming();
    UpdateGroundAnchor();

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
    if ((bIsSwinging || bWallGlue || bPerched || bGroundAnchor) && OwnerCharacter)
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

// ── TAB INPUT ──────────────────────────────────────────────────────────────
//   tap                  -> boost toward where you aimed (fires after the double-tap window)
//   hold                 -> anchor: glue to the wall you run on / swing round a corner / pendulum
//   double tap           -> pull to the point (wall: wall run, ceiling at full speed: ceiling run)
//   double tap, keep held-> pull, then stay stuck to that spot until you let go
// Timing uses real time so the temporal clap (slowed world) doesn't stretch the windows.

static bool IsWallNormalFor(const FVector& N, const FVector& Up, float MaxTiltDegrees)
{
    return !N.IsNearlyZero() && FMath::Abs(FVector::DotProduct(N, Up)) <= FMath::Sin(FMath::DegreesToRadians(MaxTiltDegrees));
}

FVector UGrappleComponent::ClampMomentum(const FVector& V) const
{
    // Cap the speed along the ground; falls keep their own (sanely bounded) speed.
    const FVector Up = OwnerCharacter ? URingFrameUtils::GetUpFor(OwnerCharacter) : FVector::UpVector;
    const float Vert = FVector::DotProduct(V, Up);
    return (V - Up * Vert).GetClampedToMaxSize(MaxMomentumSpeed) + Up * FMath::Clamp(Vert, -MaxMomentumSpeed, MaxMomentumSpeed);
}

void UGrappleComponent::OnTabPressed(const FHitResult& Hit, bool bHit)
{
    if (!OwnerCharacter) return;
    const double Now = GetWorld()->GetRealTimeSeconds();

    // Second press inside the window: pull. Keep holding it to stay stuck where you land.
    if (TabPhase == ETabPhase::WaitSecond && Now - FirstReleaseTime <= DoubleTapWindow)
    {
        if (bHit)
        {
            PendingPoint = Hit.ImpactPoint;
            PendingNormal = Hit.ImpactNormal;
        }
        bPullHeld = true;
        TabPhase = ETabPhase::Pull;
        StartPull(false);   // the first tap paid
        return;
    }
    if (TabPhase != ETabPhase::Idle || bIsSwinging || bPerched || bWallGlue) return;
    if (!bHit) return;

    // A living enemy in range: combat grapple straight away.
    ABaseEnemy* Enemy = Cast<ABaseEnemy>(Hit.GetActor());
    if (Enemy && Enemy->IsAlive() && IsEnemyGrappleable(Enemy))
    {
        FireGrapple(Hit.ImpactPoint, Enemy, Hit.ImpactNormal);
        return;
    }

    PendingPoint = Hit.ImpactPoint;
    PendingNormal = Hit.ImpactNormal;
    FirstPressTime = Now;
    // Instant tug: a hold or double tap builds on it.
    bPressPaid = StartBoost();
    TabPhase = bPressPaid ? ETabPhase::FirstHeld : ETabPhase::Idle;
}

void UGrappleComponent::OnTabReleased()
{
    switch (TabPhase)
    {
    case ETabPhase::FirstHeld:
        // Quick tap: wait a moment in case it's a double tap.
        FirstReleaseTime = GetWorld()->GetRealTimeSeconds();
        TabPhase = ETabPhase::WaitSecond;
        break;
    case ETabPhase::Anchor:
        if (bCharging) Launch();   // let go while charging: slingshot
        else ReleaseGrapple();
        break;
    case ETabPhase::Pull:
        bPullHeld = false;
        if (bPerched) ReleaseGrapple();   // was stuck to the spot: let go
        break;
    default:
        break;
    }
}

void UGrappleComponent::UpdateTabTiming()
{
    const double Now = GetWorld()->GetRealTimeSeconds();
    if (TabPhase == ETabPhase::FirstHeld && Now - FirstPressTime >= HoldThreshold)
    {
        TabPhase = ETabPhase::Anchor;
        StartAnchor();
    }
    else if (TabPhase == ETabPhase::WaitSecond && Now - FirstReleaseTime > DoubleTapWindow)
    {
        TabPhase = ETabPhase::Idle;   // it was a single tap (its boost already happened)
    }

    // The glued wall run ended some other way (wall jump, landed...): nothing left to hold.
    if (bWallGlue && OwnerCharacter && !OwnerCharacter->IsWallRunning())
    {
        bWallGlue = false;
        if (TabPhase == ETabPhase::Anchor && !bIsSwinging) TabPhase = ETabPhase::Idle;
    }
}

// Tap: a quick connected tug that adds momentum toward the aim point.
bool UGrappleComponent::StartBoost()
{
    if (!OwnerCharacter) return false;
    if (!OwnerCharacter->ConsumeMana(SwingManaCost))
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Not enough mana for grapple"));
        return false;
    }
    auto* CMC = OwnerCharacter->GetCharacterMovement();
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    const FVector Pos = OwnerCharacter->GetActorLocation();
    FVector Add = (PendingPoint - Pos).GetSafeNormal() * TapBoostSpeed;

    const TCHAR* Where = TEXT("air");
    if (OwnerCharacter->IsWallRunning())
    {
        // Along the wall only, so you stay on it (and keep the tilt).
        Add = FVector::VectorPlaneProject(URingFrameUtils::StripVertical(Add, Up), OwnerCharacter->WallRunNormal);
        Where = TEXT("wall run");
    }
    else if (OwnerCharacter->IsCeilingRunning())
    {
        OwnerCharacter->CeilingRunBoost += TapBoostSpeed;   // the ceiling run adds it to its momentum
        Add = FVector::ZeroVector;
        Where = TEXT("ceiling run");
    }
    else if (CMC->IsMovingOnGround())
    {
        Add = URingFrameUtils::StripVertical(Add, Up);
        Where = TEXT("ground");
    }
    CMC->Velocity = ClampMomentum(CMC->Velocity + Add);
    OwnerCharacter->AddFOVKick(TapBoostFOVKick);

#if !UE_BUILD_SHIPPING
    DrawDebugLine(GetWorld(), Pos, PendingPoint, FColor::Cyan, false, 0.15f, 0, 2.f);
#endif
    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: BOOST (%s) +%.0f -> %.0f cm/s"), Where, Add.Size(), CMC->Velocity.Size());
    return true;
}

// Double tap: the snappy yank to the point.
void UGrappleComponent::StartPull(bool bChargeMana)
{
    if (!OwnerCharacter) return;
    if (bChargeMana && !OwnerCharacter->ConsumeMana(SwingManaCost))
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Not enough mana for grapple pull"));
        TabPhase = ETabPhase::Idle;
        return;
    }
    if (OwnerCharacter->IsCeilingRunning()) OwnerCharacter->TransitionTo<TMState_Fall>();
    bWallGlue = false;

    auto* CMC = OwnerCharacter->GetCharacterMovement();
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    const FVector Pos = OwnerCharacter->GetActorLocation();
    const FVector PreVelocity = CMC->Velocity;
    const FVector Vh = URingFrameUtils::StripVertical(PreVelocity, Up);
    AnchorNormal = PendingNormal.GetSafeNormal();

    bOrbit = false;
    bCeilingRunEligible = false;
    if (IsWallNormalFor(AnchorNormal, Up, WallMaxTiltDegrees))
    {
        SwingMode = ESwingMode::WallPull;
    }
    else if (!AnchorNormal.IsNearlyZero() && FVector::DotProduct(AnchorNormal, -Up) >= CeilingMinDownDot)
    {
        SwingMode = ESwingMode::CeilingPull;
        bCeilingRunEligible = OwnerCharacter->CanStartCeilingRun();
        CeilingEntrySpeed = Vh.Size();
    }
    else
    {
        SwingMode = ESwingMode::SurfacePull;
    }
    bWallAnchor = (SwingMode == ESwingMode::WallPull);

    // Snappy yank, but chained pulls can't stack speed forever: never faster than
    // your current speed or the pull speed, whichever is higher (and the cap).
    FVector V = PreVelocity * MomentumConservation + (PendingPoint - Pos).GetSafeNormal() * WallPullSpeed;
    V = ClampMomentum(V.GetClampedToMaxSize(FMath::Max(WallPullSpeed, PreVelocity.Size())));
    AttachRope(PendingPoint, V, MaxSwingTime);
}

// Hold: the point becomes an anchor.
void UGrappleComponent::StartAnchor()
{
    if (!OwnerCharacter) return;
    if (OwnerCharacter->IsCeilingRunning()) OwnerCharacter->TransitionTo<TMState_Fall>();
    bCharging = false;

    auto* CMC = OwnerCharacter->GetCharacterMovement();
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    const FVector Pos = OwnerCharacter->GetActorLocation();
    const FVector PreVelocity = CMC->Velocity;
    const FVector Vh = URingFrameUtils::StripVertical(PreVelocity, Up);
    AnchorNormal = PendingNormal.GetSafeNormal();
    const bool bWall = IsWallNormalFor(AnchorNormal, Up, WallMaxTiltDegrees);
    const bool bFromWallRun = OwnerCharacter->IsWallRunning();
    bWallAnchor = bWall;
    bOrbit = false;

    // a) Anchored on the wall you're running on: glued to it. Momentum kept, no falling off;
    //    a wall ahead or the end of the wall is handled by the wall run (OnGluedWallAhead / OnGluedWallEnded).
    if (bWall && bFromWallRun && FVector::DotProduct(OwnerCharacter->WallRunNormal, AnchorNormal) > GlueMatchDot)
    {
        SwingAnchorPoint = PendingPoint;
        bWallGlue = true;
        UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: ANCHOR - glued to the wall run at %.0f cm/s"), Vh.Size());
        return;
    }

    // b) Running into the wall you anchored on: snap round onto it, keeping the speed.
    if (bWall && bEnableCornerSwing)
    {
        const float DistToWall = FVector::DotProduct(Pos - PendingPoint, AnchorNormal);
        const float Approach = Vh.IsNearlyZero() ? 0.f : FVector::DotProduct(Vh.GetSafeNormal(), -AnchorNormal);
        if (DistToWall <= CornerSwingMaxDistance && DistToWall > WallArrivalDistance
            && Vh.Size() >= CornerSwingMinSpeed && Approach >= CornerSwingMinApproach)
        {
            // Turn toward the side you aimed at (else the way you're drifting, else your right).
            const FVector Along = FVector::CrossProduct(Up, AnchorNormal).GetSafeNormal();
            const float Lateral = FVector::DotProduct(PendingPoint - Pos, Along);
            const float Drift = FVector::DotProduct(Vh, Along);
            const float Side = FMath::Abs(Lateral) > 30.f ? Lateral
                : (FMath::Abs(Drift) > 50.f ? Drift : FVector::DotProduct(OwnerCharacter->GetActorRightVector(), Along));
            BeginCornerSnap(PendingPoint, AnchorNormal, Vh.GetSafeNormal(), Along * (Side >= 0.f ? 1.f : -1.f),
                FMath::Max(Vh.Size(), OwnerCharacter->WallRunSpeed) * CornerSwingSpeedBoost);
            return;
        }
    }

    // On the ground: the anchor's placed but you keep walking. No keys reels you in,
    // S charges a slingshot (UpdateGroundAnchor).
    if (CMC->IsMovingOnGround())
    {
        SwingAnchorPoint = PendingPoint;
        bGroundAnchor = true;
        LastMoveInputTime = GetWorld()->GetRealTimeSeconds();
        UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: ANCHOR placed (on the ground)"));
        return;
    }

    // c) Everything else: a pendulum. Started from a wall run it stays level (an orbit),
    //    which is what carries you round an outside corner.
    SwingMode = ESwingMode::Swing;
    bOrbit = bFromWallRun || OwnerCharacter->GetTimeSinceWallRun() < 0.6f;
    const FVector V = PreVelocity * MomentumConservation + (PendingPoint - Pos).GetSafeNormal() * (bOrbit ? 0.f : SwingAttachTug);
    AttachRope(PendingPoint, ClampMomentum(V), AnchorMaxTime);
}

void UGrappleComponent::BeginCornerSnap(const FVector& WallPoint, const FVector& WallNormal, const FVector& Heading,
    const FVector& ExitDir, float Speed)
{
    AnchorNormal = WallNormal;
    bWallAnchor = true;
    bOrbit = false;
    SwingMode = ESwingMode::CornerSwing;
    CornerExitDir = ExitDir;
    CornerSpeed = FMath::Min(Speed, MaxMomentumSpeed);
    AttachRope(WallPoint, Heading * CornerSpeed, AnchorMaxTime);

    // Path, from where we are now (AttachRope may have lifted us off the floor):
    // dash along our heading to the wall-run line, snapping round the last bit of it.
    const float Approach = FMath::Max(FVector::DotProduct(Heading, -WallNormal), 0.1f);
    CornerP0 = OwnerCharacter->GetActorLocation();
    const float LineDist = WallArrivalDistance * 0.8f;
    const float ToLine = FMath::Max(FVector::DotProduct(CornerP0 - WallPoint, WallNormal) - LineDist, 1.f) / Approach;
    const float R = FMath::Min(ToLine * 0.5f, CornerTurnRadius);
    CornerHeading = Heading;
    CornerC = CornerP0 + Heading * ToLine;
    CornerP1 = CornerC - Heading * R;
    CornerP2 = CornerC + CornerExitDir * R;
    CornerStraight = ToLine - R;
    CornerCurveLen = 0.f;
    FVector Prev = CornerP1;
    for (int32 i = 1; i <= 8; ++i)
    {
        const float U = i / 8.f;
        const FVector P = (1.f - U) * (1.f - U) * CornerP1 + 2.f * (1.f - U) * U * CornerC + U * U * CornerP2;
        CornerCurveLen += FVector::Dist(P, Prev);
        Prev = P;
    }
    CornerTravel = 0.f;
    CornerLift = 0.f;
    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: corner snap - %.0f cm dash, %.0f cm turn at %.0f cm/s"),
        CornerStraight, CornerCurveLen, CornerSpeed);
}

void UGrappleComponent::OnGluedWallAhead(const FVector& Point, const FVector& Normal, const FVector& RunDir, float Speed,
    const FVector& OldWallNormal)
{
    if (!bWallGlue || !OwnerCharacter) return;
    bWallGlue = false;
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    // Turn away from the wall we were on, along the one ahead.
    FVector Exit = URingFrameUtils::StripVertical(FVector::VectorPlaneProject(OldWallNormal, Normal), Up).GetSafeNormal();
    if (Exit.IsNearlyZero()) Exit = FVector::CrossProduct(Up, Normal).GetSafeNormal();
    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: glued run - wall ahead, snapping round onto it"));
    BeginCornerSnap(Point, Normal, RunDir, Exit, Speed);
}

void UGrappleComponent::OnGluedWallEnded(const FVector& LastWallPoint, const FVector& WallNormal, const FVector& RunDir, float Speed)
{
    if (!bWallGlue || !OwnerCharacter) return;
    bWallGlue = false;
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    const FVector Pos = OwnerCharacter->GetActorLocation();
    const FVector N = WallNormal.GetSafeNormal();

    // A thin wall has a back face close behind: pivot on its middle and wrap 180 onto the back.
    // Otherwise (a building corner) pivot on the edge and wrap 90 onto the end face.
    const FVector Probe = LastWallPoint - RunDir * 15.f;
    FHitResult Back;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(TMEdgeWrap), false, OwnerCharacter);
    float Half = 0.f;
    if (GetWorld()->LineTraceSingleByChannel(Back, Probe - N * 600.f, Probe - N * 2.f, ECC_WorldStatic, Params)
        && FVector::DotProduct(Back.ImpactNormal, -N) > 0.8f)
    {
        Half = FVector::DotProduct(Probe - Back.ImpactPoint, N) * 0.5f;
    }
    EdgePivot = LastWallPoint + RunDir * 5.f - N * Half;
    const FVector ToPlayer = Pos - EdgePivot;
    EdgeVertical = Up * FVector::DotProduct(ToPlayer, Up);
    EdgeOffset = ToPlayer - EdgeVertical;
    EdgeRadius = FMath::Max(EdgeOffset.Size(), 60.f);
    EdgeOffset = EdgeOffset.GetSafeNormal() * EdgeRadius;
    // Orbit the way that starts us off moving forward (and so curls round toward the wall).
    EdgeSign = FVector::DotProduct(FVector::CrossProduct(Up, EdgeOffset), RunDir) >= 0.f ? 1.f : -1.f;
    EdgeAngle = 0.f;
    EdgeSpeed = FMath::Min(FMath::Max(Speed, OwnerCharacter->WallRunSpeed), MaxMomentumSpeed);
    EdgeStartNormal = N;
    AnchorNormal = N;

    SwingMode = ESwingMode::EdgeWrap;
    bOrbit = false;
    AttachRope(EdgePivot, RunDir * EdgeSpeed, AnchorMaxTime, /*bLift=*/false);
    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: glued wall ended - whipping round its end (%s, radius %.0f) at %.0f"),
        Half > 0.f ? TEXT("thin wall, onto the back") : TEXT("corner, onto the end face"), EdgeRadius, EdgeSpeed);
}

void UGrappleComponent::UpdateEdgeWrap(float DeltaTime)
{
    auto* CMC = OwnerCharacter->GetCharacterMovement();
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    EdgeAngle += EdgeSign * EdgeSpeed / EdgeRadius * DeltaTime;

    const FVector Offset = FQuat(Up, EdgeAngle).RotateVector(EdgeOffset);
    const FVector Tangent = (EdgeSign * FVector::CrossProduct(Up, Offset)).GetSafeNormal();
    FHitResult Hit;
    OwnerCharacter->SetActorLocation(EdgePivot + EdgeVertical + Offset, true, &Hit);
    SwingVelocity = Tangent * EdgeSpeed;
    CMC->Velocity = SwingVelocity;
    OwnerCharacter->SetFacingDirection(Tangent);
    if (ActiveLines.Num() > 0)
    {
        ActiveLines.Last().StartPoint = OwnerCharacter->GetActorLocation();
    }

    const FVector Up2 = Up;
    if (Hit.bBlockingHit)
    {
        if (IsWallNormalFor(Hit.ImpactNormal, Up2, WallMaxTiltDegrees))
        {
            ArriveAtWall(Up2, Hit.ImpactNormal);   // ran into the next face: run along it
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: edge whip blocked by %s"), *GetNameSafe(Hit.GetActor()));
            ReleaseGrapple();
        }
        return;
    }

    // Once we're round a bit, look toward the pivot for the next face to run along.
    if (FMath::Abs(EdgeAngle) > FMath::DegreesToRadians(30.f))
    {
        const FVector Pos = OwnerCharacter->GetActorLocation();
        FHitResult Side;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(TMEdgeWrap), false, OwnerCharacter);
        if (GetWorld()->LineTraceSingleByChannel(Side, Pos, Pos - Offset.GetSafeNormal() * 120.f, ECC_WorldStatic, Params)
            && IsWallNormalFor(Side.ImpactNormal, Up2, WallMaxTiltDegrees)
            && FVector::DotProduct(Side.ImpactNormal, EdgeStartNormal) < 0.7f)
        {
            UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: edge whip done after %.0f deg"), FMath::RadiansToDegrees(FMath::Abs(EdgeAngle)));
            ArriveAtWall(Up2, Side.ImpactNormal);
            return;
        }
    }
    if (FMath::Abs(EdgeAngle) > FMath::DegreesToRadians(EdgeWrapMaxAngle))
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: edge whip found no wall, letting go"));
        ReleaseGrapple();
    }
}

void UGrappleComponent::StartSurfaceCharge()
{
    if (!OwnerCharacter) return;
    auto* CMC = OwnerCharacter->GetCharacterMovement();
    bWallGlue = false;
    bGroundAnchor = false;
    SwingMode = ESwingMode::Swing;
    bOrbit = false;
    bCharging = true;
    ChargeTime = 0.f;
    SlingInputGone = 0.f;
    AttachRope(SwingAnchorPoint, CMC->Velocity, AnchorMaxTime, /*bLift=*/false);
    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: slingshot charging"));
}

void UGrappleComponent::UpdateGroundAnchor()
{
    if (!bGroundAnchor || !OwnerCharacter) return;
    if (TabPhase != ETabPhase::Anchor)
    {
        bGroundAnchor = false;
        return;
    }
    auto* CMC = OwnerCharacter->GetCharacterMovement();
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    const FVector Input = FVector::VectorPlaneProject(OwnerCharacter->GetLastMovementInputVector(), Up).GetClampedToMaxSize(1.f);
    const FVector Facing = URingFrameUtils::StripVertical(OwnerCharacter->GetActorForwardVector(), Up).GetSafeNormal();
    const double Now = GetWorld()->GetRealTimeSeconds();
    if (!Input.IsNearlyZero()) LastMoveInputTime = Now;

    if (FVector::DotProduct(Input, Facing) < -0.5f)
    {
        StartSurfaceCharge();   // S on the ground: dig in and charge
        return;
    }
    if (!CMC->IsMovingOnGround())
    {
        // Jumped or walked off: now it's an anchor in the air.
        bGroundAnchor = false;
        SwingMode = ESwingMode::Swing;
        bOrbit = false;
        AttachRope(SwingAnchorPoint, CMC->Velocity, AnchorMaxTime, /*bLift=*/false);
        return;
    }
    if (Now - LastMoveInputTime > 0.06)
    {
        // No keys: let it reel you in.
        bGroundAnchor = false;
        SwingMode = ESwingMode::Swing;
        bOrbit = false;
        AttachRope(SwingAnchorPoint, CMC->Velocity, AnchorMaxTime, /*bLift=*/true);
    }
    // W/A/D: keep walking; the rope just trails.
}

void UGrappleComponent::AttachRope(const FVector& Point, const FVector& InitialVelocity, float MaxTime, bool bLift)
{
    auto* CMC = OwnerCharacter->GetCharacterMovement();
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);

    SwingAnchorPoint = Point;
    SwingRopeLength = FMath::Max(FVector::Dist(OwnerCharacter->GetActorLocation(), Point), bOrbit ? 100.f : MinRopeLength);
    SwingVelocity = InitialVelocity;

    // Starting from the ground: lift off a little so the first frames don't scrape the floor.
    if (bLift && CMC->IsMovingOnGround())
    {
        OwnerCharacter->AddActorWorldOffset(Up * 10.f, true);
        SwingVelocity += Up * (SwingMode == ESwingMode::CornerSwing ? 150.f : GroundLiftSpeed);
    }

    FGrappleLineData NewLine;
    NewLine.StartPoint = OwnerCharacter->GetActorLocation();
    NewLine.EndPoint = Point;
    NewLine.Element = CurrentElement;
    NewLine.Lifetime = MaxTime;
    NewLine.bIsActive = true;
    ActiveLines.Add(NewLine);
    OnGrappleLineCreated.Broadcast(NewLine);

    // Pause CharacterMovement so it doesn't fight the rope physics.
    CMC->SetMovementMode(MOVE_None);
    bIsSwinging = true;
    CurrentState = EGrappleState::Swinging;
    OnGrappleStateChanged.Broadcast(CurrentState);

    GetWorld()->GetTimerManager().SetTimer(SwingTimerHandle, this, &UGrappleComponent::ReleaseGrapple, MaxTime, false);
    ApplyElementalEffect(ActiveLines.Last());

    static const TCHAR* ModeNames[] = { TEXT("ANCHOR swing"), TEXT("PULL to wall"), TEXT("ANCHOR corner swing"),
        TEXT("PULL to ceiling"), TEXT("PULL to surface"), TEXT("EDGE whip") };
    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: %s%s, rope %.0f, speed %.0f (zoom %.2f)%s"),
        ModeNames[static_cast<int32>(SwingMode)], bOrbit ? TEXT(" (level orbit)") : TEXT(""),
        SwingRopeLength, SwingVelocity.Size(), OwnerCharacter->GetSpeedFOVAlpha(),
        bCeilingRunEligible && SwingMode == ESwingMode::CeilingPull ? TEXT(" -> ceiling run") : TEXT(""));
}

// ── ROPE PHYSICS (Verlet constraint) ───────────────────────────────────────
// Gravity + steering move the character; if it ends up further than the rope
// length from the anchor it's put back on the sphere, and the velocity is
// turned along the rope's tangent (momentum is redirected, never braked).
void UGrappleComponent::UpdateSwingPhysics(float DeltaTime)
{
    if (!OwnerCharacter) return;
    if (SwingMode == ESwingMode::CornerSwing)
    {
        UpdateCornerSwing(DeltaTime);
        return;
    }
    if (SwingMode == ESwingMode::EdgeWrap)
    {
        UpdateEdgeWrap(DeltaTime);
        return;
    }

    auto* CMC = OwnerCharacter->GetCharacterMovement();
    const float PreSpeed = SwingVelocity.Size();

    // Ring gravity (not world -Z). An orbit round a corner keeps the run level.
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    const double Altitude = URingWorldGeometry::WorldToRing(Spec, OwnerCharacter->GetActorLocation()).Altitude;
    const double GravityMag = URingWorldGeometry::GetGravityAtAltitude(Spec, Altitude);
    const FVector GravityDir = URingFrameUtils::GetDownFor(OwnerCharacter);
    const FVector Up = -GravityDir;
    SwingVelocity += GravityDir * static_cast<float>(GravityMag) * (bOrbit ? OrbitGravityScale : 1.f) * DeltaTime;

    // Holding an anchor:
    //   no keys - reeled in toward it (keeps your momentum going)
    //   W/A/D   - steer / pump the swing
    //   S       - bleed your speed fast and hang there charging a slingshot; let go of S or Tab to launch
    if (SwingMode == ESwingMode::Swing)
    {
        const FVector Pos = OwnerCharacter->GetActorLocation();
        const FVector Input = FVector::VectorPlaneProject(OwnerCharacter->GetLastMovementInputVector(), Up).GetClampedToMaxSize(1.f);
        const FVector Facing = URingFrameUtils::StripVertical(OwnerCharacter->GetActorForwardVector(), Up).GetSafeNormal();
        const bool bHolding = (TabPhase == ETabPhase::Anchor);
        const double Now = GetWorld()->GetRealTimeSeconds();
        if (!Input.IsNearlyZero()) LastMoveInputTime = Now;

        if (bCharging && bHolding && FVector::DotProduct(Input, Facing) < -0.5f)
        {
            SlingInputGone = 0.f;
        }
        else if (bCharging)
        {
            SlingInputGone += DeltaTime;
            if (SlingInputGone > 0.08f)
            {
                Launch();   // let go of S: fly
                return;
            }
        }

        if (bCharging)
        {
            ChargeTime += DeltaTime;
            // Hang there: undo this frame's gravity and bleed the speed off quickly (not instantly).
            SwingVelocity -= GravityDir * static_cast<float>(GravityMag) * (bOrbit ? OrbitGravityScale : 1.f) * DeltaTime;
            SwingVelocity *= FMath::Exp(-SlingBrakeRate * DeltaTime);
            OwnerCharacter->SetFOVTension(FMath::Clamp(ChargeTime / FMath::Max(SlingChargeTime, 0.01f), 0.f, 1.f) * SlingFOVPull);
        }
        else if (bHolding && Now - LastMoveInputTime > 0.06)
        {
            // No keys: reel in toward the anchor, rope kept tight so you don't bounce back out.
            SwingVelocity += (SwingAnchorPoint - Pos).GetSafeNormal() * AnchorYankAcceleration * DeltaTime;
            SwingRopeLength = FMath::Max(100.f, FMath::Min(SwingRopeLength, FVector::Dist(Pos, SwingAnchorPoint)));
        }
        else
        {
            SwingVelocity += Input * SwingSteerAcceleration * DeltaTime;
        }
    }

    // Pulls reel in; a swing keeps its length.
    const bool bPull = SwingMode == ESwingMode::WallPull || SwingMode == ESwingMode::CeilingPull || SwingMode == ESwingMode::SurfacePull;
    SwingRopeLength = FMath::Max(bOrbit ? 100.f : MinRopeLength, SwingRopeLength - (bPull ? WallReelInSpeed : SwingReelInSpeed) * DeltaTime);
    if (SwingMode == ESwingMode::Swing && TabPhase == ETabPhase::Anchor)
    {
        // Held anchor = a pivot: the rope takes up slack as you get closer, so the moment you
        // pass the anchor you start swinging round it (e.g. round a corner) instead of flying on.
        SwingRopeLength = FMath::Max(100.f, FMath::Min(SwingRopeLength, FVector::Dist(OwnerCharacter->GetActorLocation(), SwingAnchorPoint)));
    }

    FVector NewPos = OwnerCharacter->GetActorLocation() + SwingVelocity * DeltaTime;
    const FVector ToAnchor = SwingAnchorPoint - NewPos;
    const float CurrentDist = ToAnchor.Size();
    if (CurrentDist > SwingRopeLength && CurrentDist > KINDA_SMALL_NUMBER)
    {
        NewPos += ToAnchor.GetSafeNormal() * (CurrentDist - SwingRopeLength);
        const FVector RopeDir = (SwingAnchorPoint - NewPos).GetSafeNormal();
        const float Speed = SwingVelocity.Size();
        const float OutwardSpeed = FVector::DotProduct(SwingVelocity, -RopeDir);
        if (OutwardSpeed > 0.f)
        {
            // Heading mostly away from the anchor (it's behind you): the rope would just
            // stop you dead, so let go and keep the speed.
            if (SwingMode == ESwingMode::Swing && !bCharging && Speed > 1.f && OutwardSpeed / Speed > SwingReleaseAwayDot)
            {
                UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: moving away from the anchor, released at %.0f"), Speed);
                ReleaseGrapple();
                return;
            }
            // Redirect, don't brake: never slower than before this frame (only this frame's
            // gravity may be taken away, or swings would gain energy).
            const FVector Tangent = SwingVelocity + RopeDir * OutwardSpeed;
            SwingVelocity = Tangent.IsNearlyZero() ? Tangent
                : Tangent.GetSafeNormal() * FMath::Max(Tangent.Size(), FMath::Min(PreSpeed, Speed));
        }
    }

    SwingVelocity = ClampMomentum(SwingVelocity * FMath::Pow(MomentumConservation, DeltaTime));

    // Move (bypasses CMC). If we hit something, slide along it.
    FHitResult SweepHit;
    OwnerCharacter->SetActorLocation(NewPos, true, &SweepHit);
    if (SweepHit.bStartPenetrating)
    {
        // Started slightly inside something (usually the floor): push out and retry.
        OwnerCharacter->AddActorWorldOffset(SweepHit.Normal * (SweepHit.PenetrationDepth + 2.f));
        OwnerCharacter->SetActorLocation(NewPos, true, &SweepHit);
    }
    if (SweepHit.bBlockingHit)
    {
        SwingVelocity = FVector::VectorPlaneProject(SwingVelocity, SweepHit.ImpactNormal);
    }
    CMC->Velocity = SwingVelocity;
    if (ActiveLines.Num() > 0)
    {
        ActiveLines.Last().StartPoint = OwnerCharacter->GetActorLocation();
    }

    const bool bTouchedAnchorSurface = SweepHit.bBlockingHit && !AnchorNormal.IsNearlyZero() &&
        FVector::DotProduct(SweepHit.ImpactNormal, AnchorNormal) > 0.8f;

    // Holding a swing and it carries you onto a wall: stick to it (glued wall run).
    if (SwingMode == ESwingMode::Swing && TabPhase == ETabPhase::Anchor && SweepHit.bBlockingHit
        && IsWallNormalFor(SweepHit.ImpactNormal, Up, WallMaxTiltDegrees)
        && FVector::VectorPlaneProject(URingFrameUtils::StripVertical(SwingVelocity, Up), SweepHit.ImpactNormal).Size() >= 400.f)
    {
        ArriveAtWall(Up, SweepHit.ImpactNormal);
        return;
    }

    switch (SwingMode)
    {
    case ESwingMode::WallPull:
    {
        // Measured to the wall SURFACE (not the anchor point, which may be far above/below),
        // so arrival happens exactly when a wall run can see the wall.
        const float DistToWall = FVector::DotProduct(OwnerCharacter->GetActorLocation() - SwingAnchorPoint, AnchorNormal);
        if (bTouchedAnchorSurface || DistToWall < WallArrivalDistance)
        {
            ArriveAtWall(Up, AnchorNormal);
            return;
        }
        break;
    }
    case ESwingMode::CeilingPull:
    {
        const float Half = OwnerCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const float DistToCeiling = FVector::DotProduct(OwnerCharacter->GetActorLocation() - SwingAnchorPoint, AnchorNormal);
        if (bTouchedAnchorSurface || DistToCeiling < Half + CeilingArrivalSlack)
        {
            ArriveAtCeiling();
            return;
        }
        break;
    }
    case ESwingMode::SurfacePull:
    case ESwingMode::Swing:
    {
        const bool bAtPoint = SwingMode == ESwingMode::SurfacePull &&
            FVector::Dist(OwnerCharacter->GetActorLocation(), SwingAnchorPoint) < 150.f;
        if (bTouchedAnchorSurface || bAtPoint)
        {
            // Let go keeping momentum, minus the part pushing into that surface.
            if (!AnchorNormal.IsNearlyZero() && FVector::DotProduct(SwingVelocity, AnchorNormal) < 0.f)
            {
                SwingVelocity = FVector::VectorPlaneProject(SwingVelocity, AnchorNormal);
            }
            UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: reached the anchor surface, released at %.0f"), SwingVelocity.Size());
            // Under a ceiling the release hop would push back into it.
            StopRope(FVector::DotProduct(AnchorNormal, Up) >= -0.5f);
            TabPhase = ETabPhase::Idle;
            return;
        }
        break;
    }
    default:
        break;
    }
}

void UGrappleComponent::ArriveAtWall(const FVector& Up, const FVector& WallNormal)
{
    // Direction along the wall: keep whichever way we were already drifting sideways.
    FVector AlongWall = FVector::VectorPlaneProject(FVector::VectorPlaneProject(SwingVelocity, WallNormal), Up);
    if (AlongWall.Size() < 50.f)
    {
        // Came in head-on: pick the side the camera/actor is turned toward.
        AlongWall = FVector::VectorPlaneProject(FVector::VectorPlaneProject(
            OwnerCharacter->GetActorForwardVector() + OwnerCharacter->GetActorRightVector() * 0.01f, WallNormal), Up);
    }
    AlongWall = AlongWall.GetSafeNormal();

    // Double tap still held: stay stuck to the wall right here.
    if (TabPhase == ETabPhase::Pull && bPullHeld)
    {
        BeginPerch(WallNormal, false);
        return;
    }

    const float Speed = FMath::Min(FMath::Max(SwingVelocity.Size(), OwnerCharacter->WallRunSpeed), MaxMomentumSpeed);
    if (bFaceAlongWallOnArrival)
    {
        OwnerCharacter->SetFacingDirection(AlongWall);
    }
    SwingVelocity = AlongWall * Speed;
    const bool bHolding = (TabPhase == ETabPhase::Anchor);
    StopRope(false);   // no hop: the wall run takes over
    OwnerCharacter->GetCharacterMovement()->Velocity = SwingVelocity;

    if (TMState_WallRun::CanEnter(OwnerCharacter))
    {
        OwnerCharacter->TransitionTo<TMState_WallRun>();
        bWallGlue = bHolding;
        UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: reached wall -> wall run at %.0f%s"), Speed, bWallGlue ? TEXT(" (glued, holding)") : TEXT(""));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: reached wall, no wall run possible here (on ground or no wall beside us)"));
    }
    if (!bWallGlue) TabPhase = ETabPhase::Idle;
}

// ── CORNER SWING ───────────────────────────────────────────────────────────
// Held an anchor on the wall we're running into: curve round so we end up
// running along that wall at the same speed. The turn radius is recomputed every
// tick from the distance left to the wall, so the turn finishes as we reach it.
void UGrappleComponent::UpdateCornerSwing(float DeltaTime)
{
    // Follows the path set up in StartAnchor: dash, then snap round onto the wall.
    auto* CMC = OwnerCharacter->GetCharacterMovement();
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    CornerTravel += CornerSpeed * DeltaTime;

    FVector Target;
    FVector Dir;
    bool bDone = false;
    if (CornerTravel < CornerStraight)
    {
        Target = CornerP0 + CornerHeading * CornerTravel;
        Dir = CornerHeading;
    }
    else
    {
        const float U = CornerCurveLen > 1.f ? FMath::Clamp((CornerTravel - CornerStraight) / CornerCurveLen, 0.f, 1.f) : 1.f;
        Target = (1.f - U) * (1.f - U) * CornerP1 + 2.f * (1.f - U) * U * CornerC + U * U * CornerP2;
        Dir = (2.f * (1.f - U) * (CornerC - CornerP1) + 2.f * U * (CornerP2 - CornerC)).GetSafeNormal();
        bDone = U >= 1.f;
    }
    Target += Up * CornerLift;

    FHitResult Hit;
    OwnerCharacter->SetActorLocation(Target, true, &Hit);
    SwingVelocity = Dir * CornerSpeed;
    CMC->Velocity = SwingVelocity;
    OwnerCharacter->SetFacingDirection(Dir);   // body (and, eased, the camera) turn with it
    if (ActiveLines.Num() > 0)
    {
        ActiveLines.Last().StartPoint = OwnerCharacter->GetActorLocation();
    }

    if (Hit.bBlockingHit && !bDone)
    {
        if (FVector::DotProduct(Hit.ImpactNormal, AnchorNormal) > 0.8f)
        {
            bDone = true;   // touched the wall a bit early: start running along it
        }
        else if (FVector::DotProduct(Hit.ImpactNormal, Up) > 0.7f)
        {
            CornerLift += 5.f;   // grazing the floor: ride a little higher
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: corner snap blocked by %s"), *GetNameSafe(Hit.GetActor()));
            ReleaseGrapple();
            return;
        }
    }
    if (bDone)
    {
        SwingVelocity = CornerExitDir * CornerSpeed;
        ArriveAtWall(Up, AnchorNormal);
    }
}

void UGrappleComponent::ArriveAtCeiling()
{
    // Double tap still held: stay stuck to the ceiling right here.
    if (TabPhase == ETabPhase::Pull && bPullHeld)
    {
        BeginPerch(AnchorNormal, true);
        return;
    }
    const FVector Normal = AnchorNormal;
    TabPhase = ETabPhase::Idle;
    if (bCeilingRunEligible)
    {
        StopRope(false);
        OwnerCharacter->CeilingRunNormal = Normal;
        OwnerCharacter->CeilingRunEntrySpeed = FMath::Min(CeilingEntrySpeed, MaxMomentumSpeed);   // the speed you built up
        OwnerCharacter->TransitionTo<TMState_CeilingRun>();
    }
    else
    {
        // Not fast enough to run on it: let go along the ceiling, no hop back into it.
        SwingVelocity = FVector::VectorPlaneProject(SwingVelocity, Normal);
        StopRope(false);
    }
}

// ── STUCK TO A SPOT (double tap held) ──────────────────────────────────────
void UGrappleComponent::BeginPerch(const FVector& SurfaceNormal, bool bCeiling)
{
    auto* CMC = OwnerCharacter->GetCharacterMovement();
    bIsSwinging = false;
    bOrbit = false;
    GetWorld()->GetTimerManager().ClearTimer(SwingTimerHandle);
    SwingVelocity = FVector::ZeroVector;
    CMC->Velocity = FVector::ZeroVector;
    CMC->SetMovementMode(MOVE_None);
    bPerched = true;
    bPerchOnCeiling = bCeiling;
    PerchNormal = SurfaceNormal;
    if (bCeiling)
    {
        OwnerCharacter->SetSurfaceUp(SurfaceNormal, true);   // stand on it: the view rolls over
    }
    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: stuck to the %s until Tab is released"), bCeiling ? TEXT("ceiling") : TEXT("wall"));
}

void UGrappleComponent::EndPerch(bool bJump)
{
    if (!bPerched) return;
    bPerched = false;
    auto* CMC = OwnerCharacter->GetCharacterMovement();
    if (bPerchOnCeiling && !bJump)
    {
        // Mag boots: let go of the rope and you stay on the ceiling, walking. Double jump to drop.
        CurrentState = EGrappleState::Inactive;
        OnGrappleStateChanged.Broadcast(CurrentState);
        TabPhase = ETabPhase::Idle;
        OwnerCharacter->CeilingRunNormal = PerchNormal;
        OwnerCharacter->CeilingRunEntrySpeed = 0.f;
        OwnerCharacter->TransitionTo<TMState_CeilingRun>();
        UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: let go of the ceiling spot - mag boots hold you there"));
        return;
    }
    if (bPerchOnCeiling)
    {
        OwnerCharacter->SetSurfaceUp(FVector::ZeroVector, false);
        OwnerCharacter->StartCeilingRunCooldown();
    }
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    CMC->SetMovementMode(MOVE_Falling);
    CMC->Velocity = bJump ? PerchNormal * PerchJumpOffSpeed + Up * ReleaseHopSpeed : PerchNormal * 50.f;
    CurrentState = EGrappleState::Inactive;
    OnGrappleStateChanged.Broadcast(CurrentState);
    TabPhase = ETabPhase::Idle;
    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: let go of the spot%s"), bJump ? TEXT(" (jumped off)") : TEXT(""));
}

void UGrappleComponent::JumpOffPerch()
{
    EndPerch(true);
}

// Ends rope physics only (the Tab action may continue, e.g. into a glued wall run).
void UGrappleComponent::StopRope(bool bHop)
{
    if (!bIsSwinging) return;
    bIsSwinging = false;
    bOrbit = false;
    if (OwnerCharacter)
    {
        auto* CMC = OwnerCharacter->GetCharacterMovement();
        CMC->SetMovementMode(MOVE_Falling);
        CMC->Velocity = ClampMomentum(SwingVelocity) + (bHop ? URingFrameUtils::GetUpFor(OwnerCharacter) * ReleaseHopSpeed : FVector::ZeroVector);
    }
    CurrentState = EGrappleState::Inactive;
    OnGrappleStateChanged.Broadcast(CurrentState);
    GetWorld()->GetTimerManager().ClearTimer(SwingTimerHandle);
    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: rope off, exit speed %.0f"), SwingVelocity.Size());
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
    // FireGrapple / Blueprint path: behaves like a double-tap pull (mana already paid).
    PendingPoint = AnchorPoint;
    PendingNormal = AnchorNormal;
    bPullHeld = false;
    TabPhase = ETabPhase::Pull;
    StartPull(false);
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

void UGrappleComponent::Launch()
{
    const float Alpha = FMath::Clamp(ChargeTime / FMath::Max(SlingChargeTime, 0.01f), 0.f, 1.f);
    bCharging = false;
    const FVector Up = URingFrameUtils::GetUpFor(OwnerCharacter);
    const float Speed = FMath::Lerp(SlingMinSpeed, SlingMaxSpeed, Alpha);
    SwingVelocity = (SwingAnchorPoint - OwnerCharacter->GetActorLocation()).GetSafeNormal() * Speed + Up * SlingLift;
    OwnerCharacter->AddFOVKick(SlingLaunchFOVKick * (0.4f + 0.6f * Alpha));
    UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: SLINGSHOT after %.2fs charge (%.0f%%) -> %.0f cm/s"), ChargeTime, Alpha * 100.f, Speed);
    StopRope(false);
    TabPhase = ETabPhase::Idle;
}

void UGrappleComponent::ReleaseGrapple()
{
    // Let go of whatever Tab is doing: rope, wall glue, or a spot you're stuck to.
    bCharging = false;
    bGroundAnchor = false;
    if (bWallGlue)
    {
        bWallGlue = false;
        UE_LOG(LogTemp, Warning, TEXT("[TM] Grapple: unglued from the wall"));
    }
    if (bPerched)
    {
        EndPerch(false);
    }
    else if (bIsSwinging)
    {
        StopRope(true);
    }
    if (TabPhase == ETabPhase::Anchor || TabPhase == ETabPhase::Pull)
    {
        TabPhase = ETabPhase::Idle;
    }
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