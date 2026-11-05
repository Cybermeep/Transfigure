// Source/Transfigure/States/TMState_CeilingRun.cpp

#include "States/TMState_CeilingRun.h"
#include "Character/TMCharacter.h"
#include "States/TMState_Jump.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

void TMState_CeilingRun::Enter()
{
    Owner->SetStateName("CeilingRun");

    CeilingUp = Owner->CeilingRunNormal.GetSafeNormal();
    RunSpeed = FMath::Max(Owner->CeilingRunEntrySpeed, Owner->CeilingRunMinSpeed + 1.f);
    // Time on the ceiling is proportional to the speed you built up.
    Duration = FMath::Clamp(RunSpeed / 1000.f * Owner->CeilingRunTimePer1000Speed,
        Owner->CeilingRunMinTime, Owner->CeilingRunMaxTime);
    Elapsed = 0.f;
    LastContactTime = 0.f;

    // Flip "up" to the ceiling normal. The camera keeps its view this frame and rolls over smoothly.
    Owner->SetSurfaceUp(CeilingUp, true);

    // Flying: no gravity and no floor logic fighting us; we steer it every tick.
    auto* CMC = Owner->GetCharacterMovement();
    CMC->SetMovementMode(MOVE_Flying);
    CMC->Velocity = AlongCeiling() * RunSpeed;
    Owner->JumpsRemaining = 1;

    UE_LOG(LogTemp, Warning, TEXT("[TM] Ceiling run: start at %.0f cm/s for up to %.1fs"), RunSpeed, Duration);
}

FVector TMState_CeilingRun::AlongCeiling() const
{
    return FVector::VectorPlaneProject(Owner->GetActorForwardVector(), CeilingUp).GetSafeNormal();
}

void TMState_CeilingRun::Tick(float DeltaTime)
{
    Elapsed += DeltaTime;
    // You lose momentum up there.
    RunSpeed = FMath::Max(0.f, RunSpeed - Owner->CeilingRunDeceleration * DeltaTime);

    // Still under the ceiling? Short trace from the capsule centre toward it.
    const float Half = Owner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Pos = Owner->GetActorLocation();
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(TMCeilingRun), false, Owner);
    if (Owner->GetWorld()->LineTraceSingleByChannel(Hit, Pos, Pos - CeilingUp * (Half + 40.f), ECC_WorldStatic, Params))
    {
        LastContactTime = Elapsed;
    }

    const TCHAR* Why = nullptr;
    if (Elapsed >= Duration)                    Why = TEXT("time up");
    else if (RunSpeed < Owner->CeilingRunMinSpeed) Why = TEXT("too slow");
    else if (Elapsed - LastContactTime > 0.2f)  Why = TEXT("ran off the ceiling");   // grace for seams
    if (Why)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Ceiling run: drop (%s) after %.1fs at %.0f cm/s"), Why, Elapsed, RunSpeed);
        Owner->TransitionTo<TMState_Fall>();
        return;   // this state object is gone now
    }

    // Run where you look, pressed lightly into the ceiling so you stay on it.
    Owner->GetCharacterMovement()->Velocity = AlongCeiling() * RunSpeed - CeilingUp * Owner->CeilingStickSpeed;
}

void TMState_CeilingRun::Exit()
{
    auto* CMC = Owner->GetCharacterMovement();
    const FVector Keep = FVector::VectorPlaneProject(CMC->Velocity, CeilingUp);

    // Flip back to ring gravity (camera rolls back smoothly), push off the ceiling.
    Owner->SetSurfaceUp(FVector::ZeroVector, false);
    CMC->SetMovementMode(MOVE_Falling);
    CMC->Velocity = Keep + CeilingUp * Owner->CeilingDropSpeed;
    Owner->JumpsRemaining = FMath::Max(Owner->JumpsRemaining, 1);
    Owner->StartCeilingRunCooldown();
}
