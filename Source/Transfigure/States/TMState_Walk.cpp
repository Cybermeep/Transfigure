// Source/TMovement/Private/States/TMState_Walk.cpp

#include "States/TMState_Walk.h"
#include "Character/TMCharacter.h"
#include "States/TMState_Idle.h"
#include "States/TMState_Sprint.h"
#include "States/TMState_Jump.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RingFrameUtils.h"

void TMState_Walk::Enter()
{
    Owner->SetStateName("Walk");
    Owner->SetBaseWalkSpeed(Owner->WalkSpeed);  // keeps any speed buff applied
}

void TMState_Walk::Tick(float DeltaTime)
{
    auto* CMC = Owner->GetCharacterMovement();

    // Was GetVelocity().Size2D() -- see RingFrameUtils.h
    if (URingFrameUtils::GetPlanarSpeed(Owner) < 10.f)
    {
        Owner->TransitionTo<TMState_Idle>();
        return;
    }

    if (Owner->bSprintHeld)
    {
        Owner->TransitionTo<TMState_Sprint>();
        return;
    }

    if (!CMC->IsMovingOnGround())
    {
        Owner->JumpsRemaining = 2;
        Owner->TransitionTo<TMState_Fall>();
    }
}

void TMState_Walk::Exit() {}