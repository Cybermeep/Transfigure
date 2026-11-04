//// Source/TMovement/Private/States/TMState_WallJump.cpp
// FULL REPLACEMENT AirTime guard prevents the flicker loop.

#include "States/TMState_WallJump.h"
#include "Character/TMCharacter.h"
#include "States/TMState_Jump.h"
#include "States/TMState_Walk.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RingFrameUtils.h"
#include "States/TMState_DoubleJump.h"
#include "States/TMState_WallRun.h"

void TMState_WallJump::Enter()
{
    Owner->SetStateName("WallJump");
    AirTime = 0.f;

    // Launch away from the wall with lateral boost and full jump height.
    // We use -ForwardVector as a rough "away from wall" direction.
    // A more precise version would use the stored WallNormal from WallRun,
    // but that requires passing it through this is sufficient for feel.
    // GetActorForwardVector() is already ring-correct (actor rotation is
    // aligned to local up every tick in TMCharacter::UpdateRingGravity), so
    // the lateral push needs no change. Only the vertical component did --
    // was LaunchVelocity.Z = JumpZVelocity, which meant world Z.
    const FVector Up = Owner->GetRingUp();
    FVector WallNormal;
    bool bWallOnLeft = false;

    const FVector Push = TMState_WallRun::DetectWall(Owner, WallNormal, bWallOnLeft)
        ? WallNormal * Owner->WallJumpLateralBoost
        : -Owner->GetActorForwardVector() * Owner->WallJumpLateralBoost;

    const FVector Planar = URingFrameUtils::StripVertical(Owner->GetVelocity(), Up);
    const FVector LaunchVelocity = URingFrameUtils::SetVertical(Planar + Push, Up, Owner->JumpZVelocity);

    Owner->LaunchCharacter(LaunchVelocity, true, true);
    Owner->JumpsRemaining = 1;
}

void TMState_WallJump::Tick(float DeltaTime)
{
    AirTime += DeltaTime;

    // Don't allow exit transitions until we've been airborne long enough.
    // This prevents a one frame flicker where the downward arc of the wall
    // jump immediately triggers the Z < 0 ground check.
    if (AirTime < MinAirTime) return;
    if (Owner->JumpsRemaining < 1)
    {
        Owner->TransitionTo<TMState_DoubleJump>();
        return;
    }

    if (Owner->GetCharacterMovement()->IsMovingOnGround())
    {
        Owner->JumpsRemaining = 2;
        Owner->TransitionTo<TMState_Walk>();
    }
}

void TMState_WallJump::Exit()
{
    AirTime = 0.f;
}