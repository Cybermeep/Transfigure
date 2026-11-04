// Source/TMovement/Public/Grapple/GrappleLine.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Grapple/GrappleTypes.h"
#include "GrappleLine.generated.h"

class UNiagaraComponent;
class USplineComponent;

UCLASS()
class TRANSFIGURE_API AGrappleLine : public AActor
{
    GENERATED_BODY()

public:
    AGrappleLine();

    UFUNCTION(BlueprintCallable, Category = "GrappleLine")
        void InitializeLine(const FGrappleLineData& InLineData);

    UFUNCTION(BlueprintCallable, Category = "GrappleLine")
        void StartLifespan();

    UFUNCTION(BlueprintCallable, Category = "GrappleLine")
        void ExpireLine();

    UFUNCTION(BlueprintCallable, Category = "GrappleLine")
        bool IsLineValid() const { return bIsValid; }

    UFUNCTION(BlueprintCallable, Category = "GrappleLine")
        FGrappleLineData GetLineData() const { return LineData; }

    UFUNCTION(BlueprintCallable, Category = "GrappleLine|Elemental")
        void ApplyElementalTrail();

    UFUNCTION(BlueprintCallable, Category = "GrappleLine|Elemental")
        void ApplyElementalEffectAtLocation(FVector Location);

    UFUNCTION(BlueprintCallable, Category = "GrappleLine|Web")
        bool CanConnectTo(AGrappleLine* OtherLine) const;

    UFUNCTION(BlueprintCallable, Category = "GrappleLine|Web")
        void CreateWebIntersection(AGrappleLine* OtherLine);

    // Components
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
        TObjectPtr<USplineComponent> SplineComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
        TObjectPtr<UNiagaraComponent> NiagaraEffect;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
        TObjectPtr<UDecalComponent> TrailDecal;

    // Blueprint events
    UFUNCTION(BlueprintImplementableEvent, Category = "GrappleLine|VFX")
        void OnLineCreated(const FGrappleLineData& Data);

    UFUNCTION(BlueprintImplementableEvent, Category = "GrappleLine|VFX")
        void OnLineExpired();

    UFUNCTION(BlueprintImplementableEvent, Category = "GrappleLine|Elemental")
        void OnElementalEffect(FVector Location, ETransfigurationElement Element);

    UFUNCTION(BlueprintCallable, Category = "GrappleLine|Web")
        virtual void OnWebIntersectionCreated(FVector IntersectionPoint, const TArray<AGrappleLine*>& ConnectedLines);

protected:
    virtual void Tick(float DeltaTime) override;

    UPROPERTY()
        FGrappleLineData LineData;

    UPROPERTY()
        bool bIsValid = true;

    UPROPERTY()
        float TimeRemaining = 0.0f;

    UPROPERTY()
        FTimerHandle ExpirationTimer;

    UPROPERTY()
        TArray<FVector> TrailPoints;

    UFUNCTION()
        void ApplyFireEffect(FVector Location);

    UFUNCTION()
        void ApplyIceEffect(FVector Location);

    UFUNCTION()
        void ApplyLightningEffect(FVector Location);

    UFUNCTION()
        void ApplyVoidEffect(FVector Location);

    UFUNCTION()
        void ApplyEarthEffect(FVector Location);
};