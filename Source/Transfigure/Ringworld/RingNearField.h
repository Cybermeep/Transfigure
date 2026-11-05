// Source/Transfigure/Ringworld/RingNearField.h
//
// Collidable ground for the ring band near the player. The ring is 1,600 km in
// radius, far too big for one collision mesh, so this keeps a single flat floor
// tile (2 km square by default) tangent to the ring surface under the player and
// moves it on ahead when they get near its edge. Over 1 km the ring only rises
// about 30 cm above a flat tangent tile, so the tile is a good stand-in.
//
// Spawned by ARingBandGenerator in ring-world maps. The tile sits a few cm below
// the surface so authored level floors (at altitude 0) cover it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RingNearField.generated.h"

class UStaticMeshComponent;

/** One static floor tile. A new tile is spawned (not moved) when re-centring, so
 *  characters standing on it aren't dragged along as if it were a moving platform. */
UCLASS(NotPlaceable)
class TRANSFIGURE_API ARingFloorTile : public AActor
{
    GENERATED_BODY()

public:
    ARingFloorTile();

    UPROPERTY(VisibleAnywhere, Category = "Ring World")
    TObjectPtr<UStaticMeshComponent> Mesh;
};

UCLASS()
class TRANSFIGURE_API ARingNearField : public AActor
{
    GENERATED_BODY()

public:
    ARingNearField();

    /** Edge length of the floor tile (cm). */
    UPROPERTY(EditAnywhere, Category = "Ring World")
    double TileSize = 200000.0;

    UPROPERTY(EditAnywhere, Category = "Ring World")
    double TileThickness = 100.0;

    /** Move the tile on once the player is this far from its centre (cm). */
    UPROPERTY(EditAnywhere, Category = "Ring World")
    double RecenterDistance = 50000.0;

    /** Tile top sits this far below the ring surface, under authored floors (cm). */
    UPROPERTY(EditAnywhere, Category = "Ring World")
    double SurfaceSink = 5.0;

protected:
    virtual void Tick(float DeltaSeconds) override;

private:
    void Recenter(double Arc, double Lateral);

    UPROPERTY()
    TObjectPtr<ARingFloorTile> Tile;

    double TileArc = 0.0;
    double TileLateral = 0.0;
};
