// Source/TMovement/Private/States/// Source/TMovement/Private/States/TMState_Idle.cpp

#include "States/TMState_Idle.h"
#include "Character/TMCharacter.h"
#include "States/TMState_Walk.h"
#include "States/TMState_Jump.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RingFrameUtils.h"

void TMState_Idle::Enter()
{
    Owner->SetStateName("Idle");
    Owner->GetCharacterMovement()->MaxWalkSpeed = Owner->WalkSpeed;
}

void TMState_Idle::Tick(float DeltaTime)
{
    auto* CMC = Owner->GetCharacterMovement();

    // If the player has started moving, go to Walk
    // Was GetVelocity().Size2D() -- see RingFrameUtils.h
    if (URingFrameUtils::GetPlanarSpeed(Owner) > 10.f)
    {
        Owner->TransitionTo<TMState_Walk>();
        return;
    }

    // If the player walked off a ledge without jumping, enter air state
    if (!CMC->IsMovingOnGround())
    {
        Owner->JumpsRemaining = 1; // Walked off edge = no double jump
        Owner->TransitionTo<TMState_Fall>();
        return;
    }
}

void TMState_Idle::Exit()
{
    // Nothing to clean up walk speed is reset by the entering state
}