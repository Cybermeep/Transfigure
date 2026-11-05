// Source/Transfigure/Ringworld/RingLaunchPad.h
//
// A launch point. Stand on it, look up at the ring arching overhead, and the HUD
// marks where your crosshair meets the ring surface. Click (Fire) to be launched
// across the ring's interior and land there (URingLaunchComponent on the player
// does the flight). Only works in ring-world maps.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RingLaunchPad.generated.h"

class UStaticMeshComponent;
class USphereComponent;

UCLASS()
class TRANSFIGURE_API ARingLaunchPad : public AActor
{
    GENERATED_BODY()

public:
    ARingLaunchPad();

    /** Landing spots closer than this (cm) don't count: you're looking at the ground near you. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring World")
    double MinLaunchDistance = 300000.0;

    UPROPERTY(VisibleAnywhere, Category = "Ring World")
    TObjectPtr<UStaticMeshComponent> Pad;

    /** Standing inside this makes the pad active. */
    UPROPERTY(VisibleAnywhere, Category = "Ring World")
    TObjectPtr<USphereComponent> Zone;

protected:
    UFUNCTION()
    void OnZoneBegin(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);

    UFUNCTION()
    void OnZoneEnd(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex);
};
