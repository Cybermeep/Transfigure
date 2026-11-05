// Source/Transfigure/States/TMState_CeilingRun.h
//
// Ceiling run with mag boots: entered from a grapple to a ceiling at top speed (or
// by letting go after sticking to a ceiling). The character's "up" flips to the
// ceiling normal (the camera rolls over smoothly, see ATMCharacter::SetSurfaceUp).
// You keep the run's momentum (bleeding off slowly), Tab taps and jump-hops add to
// it, and WASD walks you around once it's gone. You stay up there until YOU drop:
// jump once to hop along the ceiling, jump again mid-hop (double jump) to drop and
// roll the view back. Running off the edge also drops you. Leaving the state always
// flips back exactly once (Exit), and a cooldown stops an instant re-entry.

#pragma once

#include "StateMachine/TMStateBase.h"
#include "CoreMinimal.h"

class TMState_CeilingRun : public TMStateBase
{
public:
    explicit TMState_CeilingRun(ATMCharacter* InOwner) : TMStateBase(InOwner) {}
    virtual void Enter() override;
    virtual void Tick(float DeltaTime) override;
    virtual void Exit() override;

private:
    FVector AlongCeiling() const;

    /** Ceiling normal (points away from the ceiling, i.e. toward the real ground). */
    FVector CeilingUp = FVector::DownVector;
    /** Run speed along where you look; decays, topped up by taps and hops. */
    float Momentum = 0.f;
    float Elapsed = 0.f;
    float LastContactTime = 0.f;

    /** Hop: moving away from the ceiling, the boots pull you back. A second jump now drops you. */
    bool bHopping = false;
    float HopSpeed = 0.f;
};
