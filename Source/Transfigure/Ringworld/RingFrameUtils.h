// RingFrameUtils.h
//
// Drop-in replacements for the world-Z idioms scattered through the TM state
// machine. Port target: every FVector::UpVector, .Z write, and Size2D() call
// in States/ and Grapple/ becomes one of these.
//
// Depends only on RingWorldGeometry + RingWorldSettings, both of which are
// self-contained. Copy those two pairs into Transfigure first.
//
// Replace TRANSFIGURE_API if your module macro differs.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"
#include "GameFramework/Actor.h"
#include "RingFrameUtils.generated.h"

UCLASS()
class TRANSFIGURE_API URingFrameUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Local up at a world position. Replaces FVector::UpVector. */
	UFUNCTION(BlueprintPure, Category = "RingFrame")
	static FVector GetUpAt(const FVector& WorldPos)
	{
		return -URingWorldGeometry::GetGravityDirection(URingWorldSettings::GetSpec(), WorldPos);
	}

	/** Local up for an actor. */
	UFUNCTION(BlueprintPure, Category = "RingFrame")
	static FVector GetUpFor(const AActor* Actor)
	{
		return Actor ? GetUpAt(Actor->GetActorLocation()) : FVector::UpVector;
	}

	/** Local down. Replaces FVector::DownVector. */
	UFUNCTION(BlueprintPure, Category = "RingFrame")
	static FVector GetDownFor(const AActor* Actor)
	{
		return -GetUpFor(Actor);
	}

	/**
	 * Replaces GetVelocity().Size2D().
	 *
	 * Size2D() means "speed ignoring the vertical component," which on a ring
	 * is the component perpendicular to local up, not the XY plane.
	 */
	UFUNCTION(BlueprintPure, Category = "RingFrame")
	static float GetPlanarSpeed(const AActor* Actor)
	{
		if (!Actor)
		{
			return 0.0f;
		}
		const FVector Up = GetUpFor(Actor);
		return static_cast<float>(FVector::VectorPlaneProject(Actor->GetVelocity(), Up).Size());
	}

	/** Signed speed along local up. Positive = rising. Replaces reading Velocity.Z. */
	UFUNCTION(BlueprintPure, Category = "RingFrame")
	static float GetVerticalSpeed(const AActor* Actor)
	{
		if (!Actor)
		{
			return 0.0f;
		}
		return static_cast<float>(FVector::DotProduct(Actor->GetVelocity(), GetUpFor(Actor)));
	}

	/** Replaces `V.Z = 0.f` — strips the component along local up. */
	UFUNCTION(BlueprintPure, Category = "RingFrame")
	static FVector StripVertical(const FVector& Velocity, const FVector& Up)
	{
		return FVector::VectorPlaneProject(Velocity, Up);
	}

	/** Replaces `V.Z = Amount` — sets the along-up component to an exact value. */
	UFUNCTION(BlueprintPure, Category = "RingFrame")
	static FVector SetVertical(const FVector& Velocity, const FVector& Up, float Amount)
	{
		return FVector::VectorPlaneProject(Velocity, Up) + Up * Amount;
	}

	/**
	 * Replaces `LaunchCharacter(FVector(0,0,JumpZVelocity), false, true)`.
	 *
	 * Note the bZOverride flag has no gravity-relative equivalent — it
	 * overrides world Z specifically. Call with bOverrideVertical = true to
	 * reproduce the intent: strip existing vertical, then add the launch.
	 */
	UFUNCTION(BlueprintPure, Category = "RingFrame")
	static FVector MakeLaunchVelocity(const FVector& CurrentVelocity, const FVector& Up,
		float VerticalSpeed, bool bOverrideVertical)
	{
		return bOverrideVertical
			? SetVertical(CurrentVelocity, Up, VerticalSpeed)
			: CurrentVelocity + Up * VerticalSpeed;
	}

	/**
	 * Replaces `Location.Z < -5000.f` fell-out-of-world checks.
	 *
	 * On a ring centred at the origin the surface sits at world Z near
	 * -Radius, so raw Z comparisons are meaningless. Altitude is measured
	 * toward the spin axis: negative means below the surface.
	 */
	UFUNCTION(BlueprintPure, Category = "RingFrame")
	static bool IsBelowSurface(const FVector& WorldPos, float MarginCm = 5000.0f)
	{
		const FRingCoords Coords = URingWorldGeometry::WorldToRing(URingWorldSettings::GetSpec(), WorldPos);
		return Coords.Altitude < -MarginCm;
	}

	/** Altitude above the ring surface. Use for spawn parking and cull checks. */
	UFUNCTION(BlueprintPure, Category = "RingFrame")
	static float GetAltitude(const FVector& WorldPos)
	{
		return static_cast<float>(
			URingWorldGeometry::WorldToRing(URingWorldSettings::GetSpec(), WorldPos).Altitude);
	}

	/**
	 * Replaces ground traces built as Location +/- FVector(0, 0, 100).
	 * Returns start and end points along the local vertical instead.
	 */
	UFUNCTION(BlueprintPure, Category = "RingFrame")
	static void MakeGroundTrace(const FVector& WorldPos, float HalfLength,
		FVector& OutStart, FVector& OutEnd)
	{
		const FVector Up = GetUpAt(WorldPos);
		OutStart = WorldPos + Up * HalfLength;
		OutEnd = WorldPos - Up * HalfLength;
	}
};