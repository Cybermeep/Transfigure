// Source/TMovement/Private/States/TMState_Jump.cpp
// Source/TMovement/Private/States/TMState_Jump.cpp

#include "States/TMState_Jump.h"
#include "Character/TMCharacter.h"
#include "States/TMState_Walk.h"
#include "States/TMState_DoubleJump.h"
#include "States/TMState_WallRun.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RingFrameUtils.h"

void TMState_Jump::Enter()
{
    Owner->SetStateName("Jump");

    // Was LaunchCharacter(FVector(0,0,JumpZVelocity), false, true) -- that
    // sets vertical speed along world Z, which is wrong everywhere on the
    // ring except exactly arc 0. SetVertical replaces the along-up component
    // of the current velocity with JumpZVelocity, leaving the planar part
    // untouched; passing both override flags true to LaunchCharacter makes
    // it a straight assignment regardless of world-axis alignment.
    const FVector Up = Owner->GetRingUp();
    const FVector NewVelocity = URingFrameUtils::SetVertical(Owner->GetVelocity(), Up, Owner->JumpZVelocity);
    Owner->LaunchCharacter(NewVelocity, true, true);
}

void TMState_Jump::Tick(float DeltaTime)
{
    auto* CMC = Owner->GetCharacterMovement();

    if (CMC->IsMovingOnGround())
    {
        Owner->JumpsRemaining = 2;
        Owner->TransitionTo<TMState_Walk>();
        return;
    }

    if (Owner->JumpsRemaining < 1)
    {
        Owner->TransitionTo<TMState_DoubleJump>();
        return;
    }

    if (TMState_WallRun::CanEnter(Owner))
    {
        Owner->TransitionTo<TMState_WallRun>();
    }
}

void TMState_Jump::Exit() {}