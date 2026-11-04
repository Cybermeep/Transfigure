// Source/TMovement/Private/States/TMState_Sprint.cpp

#include "States/TMState_Sprint.h"
#include "Character/TMCharacter.h"
#include "States/TMState_Walk.h"
#include "States/TMState_Slide.h"
#include "States/TMState_Jump.h"
#include "GameFramework/CharacterMovementComponent.h"

void TMState_Sprint::Enter()
{
    Owner->SetStateName("Sprint");
    Owner->GetCharacterMovement()->MaxWalkSpeed = Owner->SprintSpeed;
}

void TMState_Sprint::Tick(float DeltaTime)
{
    auto* CMC = Owner->GetCharacterMovement();
    if (!Owner->bSprintHeld)
    {
        Owner->TransitionTo<TMState_Walk>();
        return;
    }
    if (Owner->bSlidePressed)
    {
        Owner->bSlidePressed = false;
        Owner->TransitionTo<TMState_Slide>();
        return;
    }
}

void TMState_Sprint::Exit() {}