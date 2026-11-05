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
    Momentum = FMath::Max(Owner->CeilingRunEntrySpeed, 0.f);
    Elapsed = 0.f;
    LastContactTime = 0.f;
    bHopping = false;
    HopSpeed = 0.f;
    Owner->CeilingRunBoost = 0.f;
    Owner->bCeilingJumpRequested = false;

    // Flip "up" to the ceiling normal. The camera keeps its view this frame and rolls over smoothly.
    Owner->SetSurfaceUp(CeilingUp, true);

    // Flying: no gravity and no floor logic fighting us; we steer it every tick.
    auto* CMC = Owner->GetCharacterMovement();
    CMC->SetMovementMode(MOVE_Flying);
    CMC->Velocity = AlongCeiling() * Momentum;
    Owner->JumpsRemaining = 1;

    UE_LOG(LogTemp, Warning, TEXT("[TM] Ceiling run: mag boots on at %.0f cm/s (jump = hop, jump again = drop)"), Momentum);
}

FVector TMState_CeilingRun::AlongCeiling() const
{
    return FVector::VectorPlaneProject(Owner->GetActorForwardVector(), CeilingUp).GetSafeNormal();
}

void TMState_CeilingRun::Tick(float DeltaTime)
{
    Elapsed += DeltaTime;

    // Tab taps on the ceiling add to the run.
    Momentum += Owner->CeilingRunBoost;
    Owner->CeilingRunBoost = 0.f;

    // Jump: hop along the ceiling (more speed). Jump again mid-hop: drop off (your choice).
    if (Owner->bCeilingJumpRequested)
    {
        Owner->bCeilingJumpRequested = false;
        if (bHopping)
        {
            UE_LOG(LogTemp, Warning, TEXT("[TM] Ceiling run: drop (double jump) after %.1fs"), Elapsed);
            Owner->TransitionTo<TMState_Fall>();
            return;   // this state object is gone now
        }
        bHopping = true;
        HopSpeed = Owner->CeilingHopSpeed;
        Momentum += Owner->CeilingHopBoost;
    }

    // Momentum bleeds off slowly; mag boots keep you up there regardless.
    Momentum = FMath::Clamp(Momentum - Owner->CeilingRunDeceleration * DeltaTime, 0.f, Owner->CeilingRunMaxMomentum);

    // Still under the ceiling? (Long enough to cover a hop.)
    const float Half = Owner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Pos = Owner->GetActorLocation();
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(TMCeilingRun), false, Owner);
    if (Owner->GetWorld()->LineTraceSingleByChannel(Hit, Pos, Pos - CeilingUp * (Half + 120.f), ECC_WorldStatic, Params))
    {
        LastContactTime = Elapsed;
    }
    if (Elapsed - LastContactTime > 0.25f)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TM] Ceiling run: drop (ran off the ceiling) after %.1fs"), Elapsed);
        Owner->TransitionTo<TMState_Fall>();
        return;
    }

    // Along the ceiling: the run's momentum where you look, plus walking with WASD.
    const FVector Input = FVector::VectorPlaneProject(Owner->GetLastMovementInputVector(), CeilingUp).GetClampedToMaxSize(1.f);
    FVector Along = AlongCeiling() * Momentum + Input * Owner->CeilingWalkSpeed;
    Along = Along.GetClampedToMaxSize(FMath::Max(Momentum, Owner->CeilingWalkSpeed));

    // Away from / into the ceiling: a hop goes out and the boots pull you back; otherwise pressed in.
    float Normal = -Owner->CeilingStickSpeed;
    if (bHopping)
    {
        HopSpeed -= Owner->CeilingMagPull * DeltaTime;
        Normal = FMath::Max(HopSpeed, -Owner->CeilingHopSpeed);
        if (HopSpeed <= -Owner->CeilingHopSpeed)
        {
            bHopping = false;   // back on the ceiling
        }
    }
    Owner->GetCharacterMovement()->Velocity = Along + CeilingUp * Normal;
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
    Owner->bCeilingJumpRequested = false;
    Owner->StartCeilingRunCooldown();
}
