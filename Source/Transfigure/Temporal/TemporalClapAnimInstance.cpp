// Source/TMovement/Private/Temporal/TemporalClapAnimInstance.cpp

#include "Temporal/TemporalClapAnimInstance.h"
#include "Character/TMCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

void UTemporalClapAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();

    OwnerCharacter = Cast<ATMCharacter>(TryGetPawnOwner());
}

void UTemporalClapAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (!OwnerCharacter)
    {
        OwnerCharacter = Cast<ATMCharacter>(TryGetPawnOwner());
    }

    if (!OwnerCharacter) return;

    UCharacterMovementComponent* CMC = OwnerCharacter->GetCharacterMovement();

    // --- Movement state ---
    CharacterSpeed = OwnerCharacter->GetCurrentSpeed();
    bIsWallRunning = OwnerCharacter->IsWallRunning();
    bIsSliding = OwnerCharacter->IsSliding();
    bIsSprinting = OwnerCharacter->IsSprinting();
    bIsInAir = CMC ? CMC->IsFalling() : false;
    bIsTemporalActive = OwnerCharacter->bIsTemporalActive;

    // --- Temporal charge alpha ---
    // Lerps 0->1 while clapping, fades back to 0 when done.
    // Use this in the Anim BP to blend a charge additive pose.
    if (bIsClapping)
    {
        TemporalChargeAlpha = FMath::FInterpTo(
            TemporalChargeAlpha, 1.f, DeltaSeconds, 5.f);
    }
    else
    {
        TemporalChargeAlpha = FMath::FInterpTo(
            TemporalChargeAlpha, 0.f, DeltaSeconds, 8.f);
    }

    // --- Wall run lean ---
    // -1 = leaning left, +1 = leaning right.
    // Use this in the Anim BP to tilt the character into the wall.
    if (bIsWallRunning && CMC)
    {
        FVector RightVec = OwnerCharacter->GetActorRightVector();
        FVector Vel = CMC->Velocity.GetSafeNormal2D();
        WallRunLean = FVector::DotProduct(RightVec, Vel);
    }
    else
    {
        WallRunLean = FMath::FInterpTo(WallRunLean, 0.f, DeltaSeconds, 6.f);
    }
}

void UTemporalClapAnimInstance::PlayClapAnimation()
{
    if (!ClapMontage) return;

    bIsClapping = true;

    // Speed up the montage to compensate for temporal slow-mo so it
    // looks correct in world-time while everything else is slowed
    if (OwnerCharacter && OwnerCharacter->bIsTemporalActive)
    {
        ClapPlayRate = 1.0f / FMath::Max(OwnerCharacter->TemporalTimeDilation, 0.01f);
    }
    else
    {
        ClapPlayRate = 1.0f;
    }

    Montage_Play(ClapMontage, ClapPlayRate, EMontagePlayReturnType::MontageLength,
        0.f, true);

    OnClapStart();
}

void UTemporalClapAnimInstance::StopClapAnimation()
{
    if (!bIsClapping) return;

    bIsClapping = false;
    Montage_Stop(ClapBlendOutTime, ClapMontage);
}

bool UTemporalClapAnimInstance::HandleNotify(const FAnimNotifyEvent& AnimNotifyEvent)
{
    FName NotifyName = AnimNotifyEvent.NotifyName;

    if (NotifyName == ClapPeakNotifyName)
    {
        OnClapPeak();
        OnClapPeakEvent.Broadcast();
        return true;
    }
    else if (NotifyName == ClapEndNotifyName)
    {
        OnClapEnd();
        OnClapEndEvent.Broadcast();
        bIsClapping = false;
        return true;
    }

    return Super::HandleNotify(AnimNotifyEvent);
}