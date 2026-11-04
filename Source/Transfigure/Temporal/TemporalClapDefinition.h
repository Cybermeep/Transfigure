// Source/TMovement/Public/Temporal/TemporalClapDefinition.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TemporalClapDefinition.generated.h"

class UNiagaraSystem;
class USoundBase;

UCLASS(BlueprintType)
class TRANSFIGURE_API UTemporalClapDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Timing")
        float ClapAnimationDuration = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Timing")
        float SlowMotionDuration = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Timing")
        float CooldownDuration = 15.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Speed")
        float TimeDilationFactor = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Camera")
        float CameraTiltAngle = 15.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Camera")
        float CameraFOV = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Camera")
        float CameraLerpSpeed = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Sigils")
        float SigilActivationStaggerDelay = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Audio")
        TObjectPtr<USoundBase> StartSound;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Audio")
        TObjectPtr<USoundBase> EndSound;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Audio")
        TObjectPtr<USoundBase> ClapSound;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|VFX")
        TObjectPtr<UNiagaraSystem> StartFX;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|VFX")
        TObjectPtr<UNiagaraSystem> EndFX;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|VFX")
        TObjectPtr<UNiagaraSystem> ClapShockwaveFX;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|VFX")
        TObjectPtr<UNiagaraSystem> TimeDistortionFX;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Mana")
        float ManaCost = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Mana")
        float ManaRefundOnNoSigils = 0.75f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Grapple")
        float GrappleLineExtensionMultiplier = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Destruction")
        bool bProcessDestructionDuringClap = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temporal|Destruction")
        float DestructionProcessSpeedMultiplier = 5.0f;
};