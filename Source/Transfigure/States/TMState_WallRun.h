// Source/TMovement/Public/States/TMState_WallRun.h
// FULL REPLACEMENT — DetectWall is now static so CanEnter() doesn't need
// to construct a temporary TMState_WallRun instance to call it.

#pragma once

#include "StateMachine/TMStateBase.h"

class TMState_WallRun : public TMStateBase
{
public:
    explicit TMState_WallRun(ATMCharacter* InOwner) : TMStateBase(InOwner) {}
    virtual void Enter() override;
    virtual void Tick(float DeltaTime) override;
    virtual void Exit() override;

    static bool CanEnter(ATMCharacter* Char);

    // ── BUG 6 FIX: DetectWall is now static — takes explicit character pointer.
    // CanEnter() can call it directly without constructing a temporary instance.
    // Enter() and Tick() call it as DetectWall(Owner, ...).
    static bool DetectWall(ATMCharacter* Char, FVector& OutNormal, bool& OutOnLeft, FVector* OutPoint = nullptr);

private:
    FVector WallNormal;
    bool bWallOnLeft = false;
    int32 JumpsAtEntry = 0;

    /** Current run speed: starts at the arrival speed (at least WallRunSpeed) and accelerates. */
    float RunSpeed = 0.f;

    /** Glued (holding a grapple anchor): how long the wall has been missing, for a short grace. */
    float MissTime = 0.f;
    /** Last point on the wall and run direction, for whipping round its end when glued. */
    FVector LastWallPoint = FVector::ZeroVector;
    FVector LastRunDir = FVector::ZeroVector;
};