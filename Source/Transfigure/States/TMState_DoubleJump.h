// Source/TMovement/Public/States/TMState_DoubleJump.h

#pragma once

#include "StateMachine/TMStateBase.h"

class TMState_DoubleJump : public TMStateBase
{
public:
    explicit TMState_DoubleJump(ATMCharacter* InOwner) : TMStateBase(InOwner) {}
    virtual void Enter() override;
    virtual void Tick(float DeltaTime) override;
    virtual void Exit() override;
};