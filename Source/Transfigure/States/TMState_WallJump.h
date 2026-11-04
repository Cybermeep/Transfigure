// Source/TMovement/Public/States/TMState_WallJump.h
// FULL REPLACEMENT — adds AirTime guard to prevent flicker loop where
// WallJump immediately transitions back to Jump because Z velocity < 0.

#pragma once

#include "StateMachine/TMStateBase.h"

class TMState_WallJump : public TMStateBase
{
public:
    explicit TMState_WallJump(ATMCharacter* InOwner) : TMStateBase(InOwner) {}
    virtual void Enter() override;
    virtual void Tick(float DeltaTime) override;
    virtual void Exit() override;

private:
    // Minimum time in the air before we allow transition back to ground state.
    // Prevents a one-frame flicker where descending Z velocity triggers an
    // immediate exit back to Jump/Walk right after the wall jump launches.
    float AirTime = 0.f;
    static constexpr float MinAirTime = 0.25f;
};