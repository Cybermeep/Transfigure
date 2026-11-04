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
    static bool DetectWall(ATMCharacter* Char, FVector& OutNormal, bool& OutOnLeft);

private:
    FVector WallNormal;
    bool bWallOnLeft = false;
    int32 JumpsAtEntry = 0;
};