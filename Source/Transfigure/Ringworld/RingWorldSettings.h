// RingWorldSettings.h
//
// Header-only. Surfaces FRingWorldSpec in Project Settings under
// Project -> Ring World, and writes to DefaultGame.ini so the values are
// checked into source control rather than living in someone's local editor.
//
// Access from anywhere:
//     const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
//
// Replace RINGWORLD_API with your module's API macro.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Ring World"))
class TRANSFIGURE_API URingWorldSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	URingWorldSettings()
	{
		CategoryName = TEXT("Project");
		SectionName = TEXT("Ring World");
	}

	/** Physical dimensions of the ring. Everything else derives from these. */
	UPROPERTY(Config, EditAnywhere, Category = "Dimensions")
	FRingWorldSpec Spec;

	/**
	 * Size of one arc tile, in cm. Origin rebasing happens when the player
	 * crosses a tile boundary. Default 50 km.
	 *
	 * Rule of thumb: small enough that absolute coordinates stay well inside
	 * float range for anything handed to the GPU, large enough that rebasing
	 * is rare. 20-100 km is the useful band.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = "100000.0"))
	double ArcTileSize = 5.0e6;

	/** Radius around the player that streams real, collidable terrain. Default 15 km. */
	UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = "100000.0"))
	double NearFieldRadius = 1.5e6;

	/** Outer edge of the bent proxy band. Beyond this, the sky impostor takes over. Default 400 km. */
	UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = "1000000.0"))
	double MidFieldRadius = 4.0e7;

	/** Convenience accessor. Safe to call from anywhere after engine init. */
	static const FRingWorldSpec& GetSpec()
	{
		return GetDefault<URingWorldSettings>()->Spec;
	}

	static const URingWorldSettings* Get()
	{
		return GetDefault<URingWorldSettings>();
	}
};