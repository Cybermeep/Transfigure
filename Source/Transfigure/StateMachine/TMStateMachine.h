// Source/TMovement/Public/StateMachine/TMStateMachine.h

#pragma once

#include "CoreMinimal.h"
#include "StateMachine/TMStateBase.h"
#include <memory>

class TMStateMachine
{
public:
    void TransitionTo(std::unique_ptr<TMStateBase> NewState);
    void Tick(float DeltaTime);
    TMStateBase* GetCurrentState() const { return CurrentState.get(); }

private:
    std::unique_ptr<TMStateBase> CurrentState;
};