// Source/TMovement// Source/TMovement/Private/States/TMState_DoubleJump.cpp

#include "States/TMState_DoubleJump.h"
#include "Character/TMCharacter.h"
#include "States/TMState_Jump.h"
#include "States/TMState_WallRun.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RingFrameUtils.h"
#include "States/TMState_Walk.h"

void TMState_DoubleJump::Enter()
{
    Owner->SetStateName("DoubleJump");

    // Was: zero V.Z, then LaunchCharacter(FVector(0,0,JumpZVelocity), false, true).
    // Both assumed world Z is "up". Strip the existing vertical component and
    // relaunch along the actual local up instead.
    const FVector Up = Owner->GetRingUp();
    const FVector Stripped = URingFrameUtils::StripVertical(Owner->GetVelocity(), Up);
    Owner->GetCharacterMovement()->Velocity = Stripped;

    const FVector NewVelocity = URingFrameUtils::SetVertical(Stripped, Up, Owner->JumpZVelocity);
    Owner->LaunchCharacter(NewVelocity, true, true);
}

void TMState_DoubleJump::Tick(float DeltaTime)
{
    if (Owner->GetCharacterMovement()->IsMovingOnGround())
    {
        Owner->JumpsRemaining = 2;
        Owner->TransitionTo<TMState_Walk>();
        return;
    }

    if (TMState_WallRun::CanEnter(Owner))
    {
        Owner->TransitionTo<TMState_WallRun>();
    }
}

void TMState_DoubleJump::Exit() {}