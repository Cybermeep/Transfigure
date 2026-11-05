// Source/Transfigure/Ringworld/RingLaunchComponent.h
//
// On the player. While standing on an ARingLaunchPad it works out where the view
// ray meets the ring surface (the landing spot the HUD marks). TryLaunch (Fire)
// flies you there: a scripted flight straight across the ring's interior with an
// arc, your "up" turning smoothly from the start's ring-up to the destination's,
// the view drifting to keep the landing spot in sight, then a fast drop onto it.
// Thousands of km are covered in a few seconds.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RingWorldGeometry.h"
#include "RingLaunchComponent.generated.h"

class ARingLaunchPad;
class ATMCharacter;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TRANSFIGURE_API URingLaunchComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    URingLaunchComponent();

    /** Flight time for a short hop and for the far side of the ring (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring World|Launch")
    float FlightTimeMin = 4.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring World|Launch")
    float FlightTimeMax = 9.f;

    /** Extra height of the flight arc at its middle (cm), at least, and as a share of the distance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring World|Launch")
    float MinArcHeight = 40000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring World|Launch")
    float ArcHeightPerDistance = 0.12f;

    /** The flight ends this far above the landing spot (cm), then you drop at LandingSpeed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring World|Launch")
    float ArrivalHeight = 600.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring World|Launch")
    float LandingSpeed = 2500.f;

    /** How strongly the view is turned toward the landing spot during the flight (per second). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring World|Launch")
    float ViewSteerRate = 2.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring World|Launch")
    float LaunchFOVKick = 15.f;

    /** Aiming within this many degrees of another pad snaps the landing spot onto it (how you get home). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring World|Launch")
    float PadSnapDegrees = 3.f;

    /** Return pads left at landing spots; the oldest goes once there are more than this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring World|Launch")
    int32 MaxReturnPads = 8;

    void SetActivePad(ARingLaunchPad* Pad);
    ARingLaunchPad* GetActivePad() const { return ActivePad.Get(); }

    /** On a pad and the view ray meets the ring far enough away. */
    bool HasTarget() const { return ActivePad.IsValid() && bTargetValid && !bFlying; }
    FVector GetTarget() const { return Target; }
    /** The landing spot is another pad (snapped to it). */
    ARingLaunchPad* GetTargetPad() const { return TargetPad.Get(); }
    /** Every pad in the world (for HUD markers). */
    const TArray<TWeakObjectPtr<ARingLaunchPad>>& GetKnownPads() const { return KnownPads; }
    const FRingCoords& GetTargetCoords() const { return TargetCoords; }
    double GetTargetDistance() const { return TargetDistance; }

    /** Launch toward the current target. False if there's nothing to launch at. */
    bool TryLaunch();

    bool IsFlying() const { return bFlying; }
    float GetFlightTimeLeft() const { return bFlying ? FMath::Max(FlightTime - Elapsed, 0.f) : 0.f; }

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    void UpdateTarget();
    void UpdateFlight(float DeltaTime);
    ATMCharacter* GetCharacter() const;

    TWeakObjectPtr<ARingLaunchPad> ActivePad;
    TWeakObjectPtr<ARingLaunchPad> TargetPad;
    TArray<TWeakObjectPtr<ARingLaunchPad>> KnownPads;
    TArray<TWeakObjectPtr<ARingLaunchPad>> ReturnPads;
    int32 LandingCount = 0;
    void RefreshKnownPads();
    void SpawnReturnPad();
    bool bTargetValid = false;
    FVector Target = FVector::ZeroVector;
    FRingCoords TargetCoords;
    double TargetDistance = 0.0;

    bool bFlying = false;
    float FlightTime = 0.f;
    float Elapsed = 0.f;
    FVector StartPos = FVector::ZeroVector;
    FVector EndPos = FVector::ZeroVector;
    FVector StartUp = FVector::UpVector;
    FVector EndUp = FVector::UpVector;
    double TurnAngle = 0.0;     // rotation of "up" about the spin axis over the flight (radians)
    double ArcHeight = 0.0;
    FVector PrevPos = FVector::ZeroVector;
};
