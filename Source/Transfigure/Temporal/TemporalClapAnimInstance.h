// Source/TMovement/Public/Temporal/TemporalClapAnimInstance.h

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "TemporalClapAnimInstance.generated.h"

class ATMCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTemporalClapAnimEvent);

UCLASS()
class TRANSFIGURE_API UTemporalClapAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Temporal|Animation")
    void PlayClapAnimation();

    UFUNCTION(BlueprintCallable, Category = "Temporal|Animation")
    void StopClapAnimation();

    UFUNCTION(BlueprintImplementableEvent, Category = "Temporal|Animation")
    void OnClapStart();

    UFUNCTION(BlueprintImplementableEvent, Category = "Temporal|Animation")
    void OnClapPeak();

    UFUNCTION(BlueprintImplementableEvent, Category = "Temporal|Animation")
    void OnClapEnd();

    UPROPERTY(BlueprintAssignable)
    FTemporalClapAnimEvent OnClapPeakEvent;

    UPROPERTY(BlueprintAssignable)
    FTemporalClapAnimEvent OnClapEndEvent;

    // --- Clap state ---
    UPROPERTY(BlueprintReadOnly, Category = "Temporal|Animation")
    bool bIsClapping = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Animation")
    UAnimMontage* ClapMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Animation")
    FName ClapPeakNotifyName = "ClapPeak";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Animation")
    FName ClapEndNotifyName = "ClapEnd";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Animation")
    float ClapBlendInTime = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Animation")
    float ClapBlendOutTime = 0.3f;

    // --- Movement state (driven each frame, read by Anim BP) ---
    UPROPERTY(BlueprintReadOnly, Category = "Temporal|Animation")
    float CharacterSpeed = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Temporal|Animation")
    bool bIsWallRunning = false;

    UPROPERTY(BlueprintReadOnly, Category = "Temporal|Animation")
    bool bIsSliding = false;

    UPROPERTY(BlueprintReadOnly, Category = "Temporal|Animation")
    bool bIsSprinting = false;

    UPROPERTY(BlueprintReadOnly, Category = "Temporal|Animation")
    bool bIsInAir = false;

    UPROPERTY(BlueprintReadOnly, Category = "Temporal|Animation")
    bool bIsTemporalActive = false;

    // --- Blend values ---
    UPROPERTY(BlueprintReadOnly, Category = "Temporal|Animation")
    float TemporalChargeAlpha = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Temporal|Animation")
    float WallRunLean = 0.f;

    // --- Notify override ---
    virtual bool HandleNotify(const FAnimNotifyEvent& AnimNotifyEvent) override;

protected:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
    UPROPERTY()
    TObjectPtr<ATMCharacter> OwnerCharacter;

    float ClapPlayRate = 1.0f;
};