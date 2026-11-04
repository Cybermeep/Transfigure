// Source/TMovement/Public/States/TMState_Jump.h

#pragma once

#include "StateMachine/TMStateBase.h"

class TMState_Jump : public TMStateBase
{
public:
    explicit TMState_Jump(ATMCharacter* InOwner) : TMStateBase(InOwner) {}
    virtual void Enter() override;
    virtual void Tick(float DeltaTime) override;
    virtual void Exit() override;
};