// Source/TMovement/Public/States/TMState_Walk.h

#pragma once

#include "StateMachine/TMStateBase.h"

class TMState_Walk : public TMStateBase
{
public:
    explicit TMState_Walk(ATMCharacter* InOwner) : TMStateBase(InOwner) {}
    virtual void Enter() override;
    virtual void Tick(float DeltaTime) override;
    virtual void Exit() override;
};