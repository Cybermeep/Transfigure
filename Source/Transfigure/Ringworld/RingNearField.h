// Source/Transfigure/Ringworld/RingNearField.h
//
// Collidable ground for the ring band near the player (WORLDGEN_PLAN R3). The ring
// is 1,600 km in radius, far too big for one collision mesh, so this keeps one
// terrain tile (4 km square, 20 m grid) under the player and builds a new one
// ahead when they move 1 km from its centre. The terrain comes from FRingRegions:
// biome shapes blended across region borders, flat at authored zones, pads and
// landing spots, and faded to flat at the tile edge where it meets the band.
//
// Spawned by ARingBandGenerator in ring-world maps. The tile sits a few cm below
// the surface so authored level floors (at altitude 0) cover it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RingNearField.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/** One terrain tile. A new tile is spawned (not moved) when re-centring, so
 *  characters standing on it aren't dragged along as if it were a moving platform. */
UCLASS(NotPlaceable)
class TRANSFIGURE_API ARingFloorTile : public AActor
{
    GENERATED_BODY()

public:
    ARingFloorTile();

    UPROPERTY(VisibleAnywhere, Category = "Ring World")
    TObjectPtr<UProceduralMeshComponent> Terrain;

    /** Build the height field around (Arc, Lateral). bAsyncCollision: cook collision in the
     *  background (only when the player is already standing on another tile). */
    void Build(int32 WorldSeed, double Arc, double Lateral, double Size, double Step, double EdgeFade,
        double Sink, bool bAsyncCollision, UMaterialInterface* Material);
};

UCLASS()
class TRANSFIGURE_API ARingNearField : public AActor
{
    GENERATED_BODY()

public:
    ARingNearField();

    /** Edge length of the terrain tile (cm). */
    UPROPERTY(EditAnywhere, Category = "Ring World")
    double TileSize = 400000.0;

    /** Distance between terrain vertices (cm). */
    UPROPERTY(EditAnywhere, Category = "Ring World")
    double GridStep = 2000.0;

    /** The terrain flattens out over this much at the tile edge (cm). */
    UPROPERTY(EditAnywhere, Category = "Ring World")
    double EdgeFade = 60000.0;

    /** Build the next tile once the player is this far from the current one's centre (cm). */
    UPROPERTY(EditAnywhere, Category = "Ring World")
    double RecenterDistance = 100000.0;

    /** Tile top sits this far below the ring surface, under authored floors (cm). */
    UPROPERTY(EditAnywhere, Category = "Ring World")
    double SurfaceSink = 5.0;

    /** Seed for the region map (set by ARingBandGenerator). */
    UPROPERTY(EditAnywhere, Category = "Ring World")
    int32 WorldSeed = 1337;

protected:
    virtual void Tick(float DeltaSeconds) override;

private:
    void Recenter(double Arc, double Lateral, bool bAsyncCollision);

    UPROPERTY()
    TObjectPtr<ARingFloorTile> Tile;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> GroundMaterial;

    double TileArc = 0.0;
    double TileLateral = 0.0;
};
