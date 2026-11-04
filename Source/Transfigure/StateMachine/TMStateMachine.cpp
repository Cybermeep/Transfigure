// Source/TMovement/Private/StateMachine/TMStateMachine.cpp

#include "StateMachine/TMStateMachine.h"

void TMStateMachine::TransitionTo(std::unique_ptr<TMStateBase> NewState)
{
    if (CurrentState) CurrentState->Exit();
    CurrentState = std::move(NewState);
    if (CurrentState) CurrentState->Enter();
}

void TMStateMachine::Tick(float DeltaTime)
{
    if (CurrentState) CurrentState->Tick(DeltaTime);
}