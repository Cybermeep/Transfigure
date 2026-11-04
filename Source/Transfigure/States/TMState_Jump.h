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

protected:
    // Stops a queued launch from being read as "still grounded".
    float StateTime = 0.f;
};

class TMState_Fall : public TMState_Jump
{
public:
    using TMState_Jump::TMState_Jump;
    virtual void Enter() override;
};