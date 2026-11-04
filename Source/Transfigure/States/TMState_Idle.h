// Source/TMovement/Public/States/TMState_Idle.h

#pragma once

#include "StateMachine/TMStateBase.h"

class TMState_Idle : public TMStateBase
{
public:
    explicit TMState_Idle(ATMCharacter* InOwner) : TMStateBase(InOwner) {}
    virtual void Enter() override;
    virtual void Tick(float DeltaTime) override;
    virtual void Exit() override;
};