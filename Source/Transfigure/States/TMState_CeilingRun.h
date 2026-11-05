// Source/Transfigure/States/TMState_CeilingRun.h
//
// Ceiling run: entered only from a grapple to a ceiling at (near) full speed.
// The character's "up" flips to the ceiling normal for the run (the camera rolls
// over smoothly, see ATMCharacter::SetSurfaceUp), it runs where you look and
// bleeds speed, and drops off when time or speed runs out, the ceiling ends, or
// you press jump. Leaving the state ALWAYS flips back exactly once (Exit), and a
// cooldown stops another ceiling run straight away, so the view can't flip-flop.

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
    float RunSpeed = 0.f;
    float Duration = 0.f;
    float Elapsed = 0.f;
    float LastContactTime = 0.f;
};
