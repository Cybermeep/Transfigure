// Source/TMovement/Public/States/TMState_Sprint.h

#pragma once

#include "StateMachine/TMStateBase.h"

class TMState_Sprint : public TMStateBase
{
public:
    explicit TMState_Sprint(ATMCharacter* InOwner) : TMStateBase(InOwner) {}
    virtual void Enter() override;
    virtual void Tick(float DeltaTime) override;
    virtual void Exit() override;
};