// Source/TMovement/Private/States/TMState_WallRun.cpp
// FULL REPLACEMENT — DetectWall is now static, CanEnter() calls it directly.

#include "States/TMState_WallRun.h"
#include "Character/TMCharacter.h"
#include "States/TMState_WallJump.h"
#include "States/TMState_Jump.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Destruction/DestructionManager.h"
#include "Grapple/GrappleComponent.h"
#include "RingFrameUtils.h"

// ── BUG 6 FIX: Static DetectWall — no instance required ──
// Takes the character explicitly so CanEnter() can call it without
// constructing a throwaway TMState_WallRun object.
bool TMState_WallRun::DetectWall(ATMCharacter* Char, FVector& OutNormal, bool& OutOnLeft, FVector* OutPoint)
{
    UWorld* World = Char->GetWorld();
    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Char);

    FVector Start = Char->GetActorLocation();
    const float TraceLen = 80.f;

    // Check right side first
    FVector RightEnd = Start + Char->GetActorRightVector() * TraceLen;
    if (World->LineTraceSingleByChannel(Hit, Start, RightEnd, ECC_WorldStatic, Params))
    {
        OutNormal = Hit.ImpactNormal;
        OutOnLeft = false;
        if (OutPoint) *OutPoint = Hit.ImpactPoint;
        return true;
    }

    // Check left side
    FVector LeftEnd = Start - Char->GetActorRightVector() * TraceLen;
    if (World->LineTraceSingleByChannel(Hit, Start, LeftEnd, ECC_WorldStatic, Params))
    {
        OutNormal = Hit.ImpactNormal;
        OutOnLeft = true;
        if (OutPoint) *OutPoint = Hit.ImpactPoint;
        return true;
    }

    return false;
}

bool TMState_WallRun::CanEnter(ATMCharacter* Char)
{
    if (Char->IsGrappling()) return false;
    auto* CMC = Char->GetCharacterMovement();
    if (CMC->IsMovingOnGround()) return false;
    // Was GetVelocity().Size2D() -- see RingFrameUtils.h
    if (URingFrameUtils::GetPlanarSpeed(Char) < 400.f) return false;

    // ── BUG 6 FIX: Call static DetectWall directly — no temp instance ──
    FVector Dummy;
    bool bDummy;
    if (!DetectWall(Char, Dummy, bDummy)) return false;

    if (!Char->CanWallRunAtLocation(Char->GetActorLocation())) return false;

    return true;
}

void TMState_WallRun::Enter()
{
    Owner->SetStateName("WallRun");

    // Use static DetectWall with Owner
    DetectWall(Owner, WallNormal, bWallOnLeft);
    Owner->WallRunSide = bWallOnLeft ? -1.f : 1.f;   // camera tilt
    Owner->WallRunNormal = WallNormal;
    MissTime = 0.f;
    JumpsAtEntry = Owner->JumpsRemaining;

    auto* CMC = Owner->GetCharacterMovement();

    // Zero vertical velocity so we run horizontally along the wall.
    // Was V.Z = 0.f -- see RingFrameUtils.h
    const FVector V = URingFrameUtils::StripVertical(Owner->GetVelocity(), Owner->GetRingUp());
    CMC->Velocity = V;

    // Keep whatever speed we arrived with (never slower than the base wall-run speed).
    RunSpeed = FMath::Max(V.Size(), Owner->WallRunSpeed);

    CMC->GravityScale = Owner->WallRunGravityScale;
    Owner->JumpsRemaining = 1; // Allow one wall jump
}

void TMState_WallRun::Tick(float DeltaTime)
{
    FVector NewNormal;
    bool bLeft;

    UGrappleComponent* Grapple = Owner->GrappleComponent;
    const bool bGlued = Grapple && Grapple->IsGluedToWall();

    // If no wall is detected this frame, fall into air state
    FVector HitPoint;
    if (!DetectWall(Owner, NewNormal, bLeft, &HitPoint))
    {
        // Glued: ride out a seam for a moment; if the wall really ended, whip round its end.
        MissTime += DeltaTime;
        if (bGlued && MissTime < 0.03f) return;
        if (bGlued && !LastRunDir.IsNearlyZero()) Grapple->OnGluedWallEnded(LastWallPoint, WallNormal, LastRunDir, RunSpeed);
        Owner->TransitionTo<TMState_Fall>();
        return;
    }
    MissTime = 0.f;
    LastWallPoint = HitPoint;

    WallNormal = NewNormal;
    bWallOnLeft = bLeft;
    Owner->WallRunSide = bWallOnLeft ? -1.f : 1.f;
    Owner->WallRunNormal = WallNormal;
    // Glued to the wall by a held grapple: no sinking at all.
    Owner->GetCharacterMovement()->GravityScale = bGlued ? 0.f : Owner->WallRunGravityScale;

    // Player consumed their wall jump — transition to wall jump state
    if (Owner->JumpsRemaining < 1)
    {
        Owner->TransitionTo<TMState_WallJump>();
        return;
    }

    // Project movement along the wall surface.
    // Was FVector::UpVector/DownVector -- on a ring, "up" depends on where
    // on the ring you're standing, so this has to come from ring gravity.
    // Run along the wall in the direction the player is already moving
    // (was a cross product that could point backward).
    auto* CMC = Owner->GetCharacterMovement();
    const FVector Up = Owner->GetRingUp();

    FVector WallForward = URingFrameUtils::StripVertical(
        FVector::VectorPlaneProject(CMC->Velocity, WallNormal), Up).GetSafeNormal();

    if (WallForward.IsNearlyZero())
    {
        WallForward = URingFrameUtils::StripVertical(
            FVector::VectorPlaneProject(Owner->GetActorForwardVector(), WallNormal), Up).GetSafeNormal();
    }

    // Speed added from outside (grapple boosts) is kept.
    RunSpeed = FMath::Max(RunSpeed, FVector::VectorPlaneProject(URingFrameUtils::StripVertical(CMC->Velocity, Up), WallNormal).Size());
    // Build speed the longer the run lasts. Arrival speed above the cap is kept, not cut.
    RunSpeed = FMath::Max(RunSpeed, FMath::Min(RunSpeed + Owner->WallRunAcceleration * DeltaTime, Owner->WallRunMaxSpeed));
    LastRunDir = WallForward;

    if (bGlued)
    {
        // Anchor still ahead on the wall: it pulls you along toward it.
        const FVector ToAnchor = Grapple->GetAnchorPoint() - Owner->GetActorLocation();
        if (FVector::DotProduct(ToAnchor, WallForward) > 50.f)
        {
            RunSpeed = FMath::Min(RunSpeed + Grapple->GluedPullAcceleration * DeltaTime, FMath::Max(RunSpeed, Grapple->MaxMomentumSpeed));
        }

        // Holding Tab + S: charge a slingshot - only if the anchor is on a floor or ceiling
        // (a wall anchor never charges, so S can't bleed your speed mid-run).
        const FVector Input = FVector::VectorPlaneProject(Owner->GetLastMovementInputVector(), Up);
        const FVector Facing = URingFrameUtils::StripVertical(Owner->GetActorForwardVector(), Up).GetSafeNormal();
        if (Grapple->CanSlingshot() && FVector::DotProduct(Input.GetClampedToMaxSize(1.f), Facing) < -0.5f)
        {
            Grapple->StartSurfaceCharge();
            Owner->TransitionTo<TMState_Fall>();
            return;
        }
        // A wall straight ahead (inside corner): snap round onto it instead of hitting it.
        const FVector Pos = Owner->GetActorLocation();
        const float Look = FMath::Max(150.f, RunSpeed * Grapple->GluedLookAheadTime);
        FHitResult Ahead;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(TMWallRunAhead), false, Owner);
        if (Owner->GetWorld()->LineTraceSingleByChannel(Ahead, Pos, Pos + WallForward * Look, ECC_WorldStatic, Params)
            && FVector::DotProduct(Ahead.ImpactNormal, WallForward) < -0.7f)
        {
            Grapple->OnGluedWallAhead(Ahead.ImpactPoint, Ahead.ImpactNormal, WallForward, RunSpeed, WallNormal);
            Owner->TransitionTo<TMState_Fall>();
            return;
        }
    }
    CMC->Velocity = WallForward * RunSpeed;
}

void TMState_WallRun::Exit()
{
    Owner->GetCharacterMovement()->GravityScale = 1.f;
    Owner->WallRunSide = 0.f;
}