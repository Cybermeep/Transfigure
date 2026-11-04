// Source/TMovement/Public/States/TMState_Slide.h

#pragma once

#include "StateMachine/TMStateBase.h"

class TMState_Slide : public TMStateBase
{
public:
    explicit TMState_Slide(ATMCharacter* InOwner) : TMStateBase(InOwner) {}
    virtual void Enter() override;
    virtual void Tick(float DeltaTime) override;
    virtual void Exit() override;

private:
    float SlideTimer = 0.f;
    const float MaxSlideDuration = 1.5f;
};