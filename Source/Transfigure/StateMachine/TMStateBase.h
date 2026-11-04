// Source/TMovement/Public/StateMachine/TMStateBase.h

#pragma once

#include "CoreMinimal.h"

class ATMCharacter;

class TMStateBase
{
public:
    explicit TMStateBase(ATMCharacter* InOwner)
        : Owner(InOwner) {}

    virtual ~TMStateBase() = default;

    virtual void Enter() = 0;
    virtual void Tick(float DeltaTime) = 0;
    virtual void Exit() = 0;

protected:
    ATMCharacter* Owner;
};