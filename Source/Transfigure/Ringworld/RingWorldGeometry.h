// RingWorldGeometry.h
//
// Coordinate conventions. All distances are Unreal centimetres.
//
//   Spin axis      -> world +Y
//   Ring circle    -> world XZ plane
//   theta = 0      -> world (0, Lateral, -Radius), the "bottom" of the ring
//
// At theta = 0 the surface frame is exactly the world identity basis:
//   Forward = +X  (direction of increasing Arc, "spinward")
//   Right   = +Y  (direction of increasing Lateral)
//   Up      = +Z  (toward the spin axis)
//
// This is deliberate: it means your unrolled simulation space and the curved
// ring space coincide at the origin, so you can author and test everything
// flat and only apply curvature in the renderer.
//
// Replace RINGWORLD_API with your module's API macro.

// NOTE: Build.cs for this module needs "GeometryCore" and "GeometryFramework"
// added to PublicDependencyModuleNames for RingBandGenerator (Step 2) to compile.
// See RingBandGenerator.h for details.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RingWorldGeometry.generated.h"

/** A position expressed in ring-native coordinates. */
USTRUCT(BlueprintType)
struct FRingCoords
{
	GENERATED_BODY()

	/** Distance along the circumference. Wrapped to [0, Circumference). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RingWorld")
	double Arc = 0.0;

	/** Offset across the ring's width. Zero at the centreline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RingWorld")
	double Lateral = 0.0;

	/** Height above the inner surface, measured toward the spin axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RingWorld")
	double Altitude = 0.0;

	FRingCoords() = default;
	FRingCoords(double InArc, double InLateral, double InAltitude)
		: Arc(InArc), Lateral(InLateral), Altitude(InAltitude) {
	}
};

/**
 * The single source of truth for ring dimensions.
 *
 * Put one of these on a UDeveloperSettings subclass or a GameInstance subsystem
 * and have the terrain generator, bend material, skybox, and nav map all read
 * it. Do not duplicate these numbers anywhere.
 */
USTRUCT(BlueprintType)
struct FRingWorldSpec
{
	GENERATED_BODY()

	/** Treat the level as flat: down is -Z and altitude is world Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RingWorld")
	bool bFlatWorldForTesting = false;

	/** Spin axis to inner habitable surface. Default 1,600 km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RingWorld", meta = (ClampMin = "100000.0"))
	double Radius = 1.6e8;

	/** Full width of the habitable band. Default 200 km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RingWorld", meta = (ClampMin = "100000.0"))
	double Width = 2.0e7;

	/** Target surface gravity in cm/s^2. 981 = 1 g. Determines spin rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RingWorld", meta = (ClampMin = "1.0"))
	double TargetGravity = 981.0;

	/** Rim wall height, used for atmosphere containment and far-field silhouette. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RingWorld")
	double RimWallHeight = 1.0e7;

	/** Altitude at which the local (unrolled) sim hands off to scaled orbital space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RingWorld")
	double OrbitalHandoffAltitude = 4.0e6;
};

UCLASS()
class TRANSFIGURE_API URingWorldGeometry : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// ---- Derived scalars -------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "RingWorld|Metrics")
	static double GetCircumference(const FRingWorldSpec& Spec);

	/** Radians per second. Derived from TargetGravity and Radius. */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Metrics")
	static double GetAngularVelocity(const FRingWorldSpec& Spec);

	/** Surface speed in cm/s. Useful for launch/reentry velocity budgeting. */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Metrics")
	static double GetTangentialSpeed(const FRingWorldSpec& Spec);

	/** Seconds for one rotation. This is your day length if the star is fixed. */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Metrics")
	static double GetSpinPeriod(const FRingWorldSpec& Spec);

	/** Spin gravity falls off linearly toward the axis: g = omega^2 * (R - Alt). */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Metrics")
	static double GetGravityAtAltitude(const FRingWorldSpec& Spec, double Altitude);

	// ---- Arc wrapping ----------------------------------------------------

	/** Wrap an arc value into [0, Circumference). Handles negatives. */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Arc")
	static double WrapArc(const FRingWorldSpec& Spec, double Arc);

	/**
	 * Signed shortest distance from one arc position to another, in
	 * (-Circumference/2, Circumference/2]. This is what makes "400 km spinward"
	 * resolve correctly across the seam. Use it for every distance comparison,
	 * streaming source test, and nav query along the arc axis.
	 */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Arc")
	static double ShortestArcDelta(const FRingWorldSpec& Spec, double From, double To);

	/** Arc expressed as 0-360 degrees, for map UI and save data readability. */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Arc")
	static double ArcToLongitudeDegrees(const FRingWorldSpec& Spec, double Arc);

	// ---- Conversions -----------------------------------------------------

	/** Ring coordinates to true curved world position. */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Transform")
	static FVector RingToWorld(const FRingWorldSpec& Spec, const FRingCoords& Coords);

	/** True curved world position to ring coordinates. */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Transform")
	static FRingCoords WorldToRing(const FRingWorldSpec& Spec, const FVector& WorldPos);

	/** Unit vector pointing "down" (away from the spin axis) at a world position. */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Transform")
	static FVector GetGravityDirection(const FRingWorldSpec& Spec, const FVector& WorldPos);

	/** Full orthonormal surface frame. Use to orient pawns, props, and spawns. */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Transform")
	static FTransform GetSurfaceFrame(const FRingWorldSpec& Spec, const FRingCoords& Coords);

	// ---- Rendering support ----------------------------------------------

	/**
	 * Exact vertical rise of the ring surface at a given along-ring distance
	 * from the viewer: R - R*cos(d/R).
	 *
	 * Feed this to your World Position Offset bend. The shader can use the
	 * cheap approximation d^2 / (2R) below roughly 0.1 * R without visible
	 * error; use this exact form for anything that must match CPU-side logic
	 * such as impostor placement or skybox seams.
	 */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Render")
	static double GetCurvatureRise(const FRingWorldSpec& Spec, double AlongRingDistance);

	/** The cheap shader-side approximation, exposed so both sides agree. */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Render")
	static double GetCurvatureRiseApprox(const FRingWorldSpec& Spec, double AlongRingDistance);

	// ---- Rotating-frame physics -----------------------------------------

	/**
	 * Coriolis acceleration for an object moving in the rotating frame:
	 * a = -2 * omega x v.
	 *
	 * Negligible for walking. Very noticeable for anything thrown or flown:
	 * a projectile fired spinward drifts toward the surface, antispinward
	 * drifts up. Worth enabling on ballistics for flavour, worth disabling
	 * on the player character so movement stays predictable.
	 */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Physics")
	static FVector GetCoriolisAcceleration(const FRingWorldSpec& Spec, const FVector& RelativeVelocity);

	/** True if this altitude should hand off to scaled orbital space. */
	UFUNCTION(BlueprintPure, Category = "RingWorld|Physics")
	static bool IsAboveOrbitalHandoff(const FRingWorldSpec& Spec, double Altitude);
};