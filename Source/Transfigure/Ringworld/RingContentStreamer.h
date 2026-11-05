// Source/Transfigure/Ringworld/RingContentStreamer.h
//
// Procedural content for the open ring, outside hand-built levels.
//
// The ring is cut into square cells (200 m by default) on its surface. Each cell's
// layout comes only from (WorldSeed, cell indices), so the same place always looks
// the same. The region map (FRingRegions) decides each cell's biome: its colours,
// structure kit (pillar forests, wall mazes, canopies for ceiling runs, mesas and
// ramps, spikes, crystal clusters, arches), zombie mix and danger. Everything sits
// on the terrain height there.
//
// It also tracks which region the player is in (the HUD banner reads it) and blends
// the map's fog and sun toward that region's look.
//
// Cells near the "focus" are planned and spawned a few pieces per tick, far cells
// are removed. While the player flies from a launch pad the focus is the landing
// spot, so the destination builds in the background during the flight.
//
// ARingAuthoredZone marks hand-built areas where nothing is generated; spots added
// with AddClearPoint (landing spots, pads) are kept free of pieces.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RingContentStreamer.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USphereComponent;
class UExponentialHeightFogComponent;
class ULightComponent;
struct FRingRegion;

/** One generated static piece (pillar, wall, slab...). */
UCLASS(NotPlaceable)
class TRANSFIGURE_API ARingPiece : public AActor
{
    GENERATED_BODY()

public:
    ARingPiece();

    UPROPERTY(VisibleAnywhere, Category = "Ring World")
    TObjectPtr<UStaticMeshComponent> Mesh;

    /** Half its width across the ground (cm), for keeping landing spots clear. */
    float FootprintRadius = 0.f;
};

/** Marks a hand-built area: the streamer generates nothing within Radius of it. */
UCLASS()
class TRANSFIGURE_API ARingAuthoredZone : public AActor
{
    GENERATED_BODY()

public:
    ARingAuthoredZone();

    UPROPERTY(EditAnywhere, Category = "Ring World")
    float Radius = 300000.f;

    UPROPERTY(VisibleAnywhere, Category = "Ring World")
    TObjectPtr<USphereComponent> Area;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
    virtual void OnConstruction(const FTransform& Transform) override;
};

UCLASS()
class TRANSFIGURE_API ARingContentStreamer : public AActor
{
    GENERATED_BODY()

public:
    ARingContentStreamer();

    static ARingContentStreamer* Get(const UWorld* World);

    /** Edge of one generated cell (cm). */
    UPROPERTY(EditAnywhere, Category = "Ring World|Content")
    double CellSize = 20000.0;

    /** Cells this many steps from the focus cell are kept (2 = a 5x5 block, about 1 km). */
    UPROPERTY(EditAnywhere, Category = "Ring World|Content")
    int32 LoadRadiusCells = 2;

    UPROPERTY(EditAnywhere, Category = "Ring World|Content")
    int32 WorldSeed = 1337;

    /** Pieces spawned per tick, so building never causes a hitch. */
    UPROPERTY(EditAnywhere, Category = "Ring World|Content")
    int32 SpawnBudgetPerTick = 10;

    /** No piece within this distance of a landing spot or pad (cm). */
    UPROPERTY(EditAnywhere, Category = "Ring World|Content")
    double LandingClearRadius = 2500.0;

    /** No zombie spawns within this distance of a landing spot or pad (cm). */
    UPROPERTY(EditAnywhere, Category = "Ring World|Content")
    double EnemyClearRadius = 5000.0;

    UPROPERTY(EditAnywhere, Category = "Ring World|Content")
    int32 MaxZombies = 14;

    /** Keep this spot free of pieces (removes any already there). */
    void AddClearPoint(const FVector& WorldPos);

    /** Build around this spot instead of the player (used while flying to it). */
    void SetFocusOverride(bool bEnable, const FVector& WorldPos = FVector::ZeroVector);

    /** Pieces still waiting to be spawned. */
    int32 GetPendingCount() const;
    int32 GetSpawnedCount() const { return SpawnedTotal; }

    /** The region the player is in (null until known), and the real time they entered it. */
    const FRingRegion* GetCurrentRegion() const { return CurrentRegion; }
    double GetRegionEnteredTime() const { return RegionEnteredAt; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

private:
    struct FPlanned
    {
        bool bZombie = false;
        bool bCylinder = false;
        uint8 Biome = 0;
        FTransform Xf;
        float Footprint = 0.f;
        FLinearColor Color = FLinearColor::Gray;
    };
    struct FCell
    {
        int64 ArcIdx = 0;
        int64 LatIdx = 0;
        TArray<FPlanned> Pending;
        TArray<TWeakObjectPtr<AActor>> Actors;
    };

    static int64 CellKey(int64 ArcIdx, int64 LatIdx) { return ArcIdx * 4096 + (LatIdx + 2048); }
    void UpdateCells(const FVector& Focus);
    void UpdateRegion(const FVector& PlayerPos, float DeltaSeconds);
    void PlanCell(FCell& Cell);
    void SpawnSome();
    void UnloadCell(FCell& Cell);
    bool IsClear(const FVector& WorldPos, double Radius) const;
    UMaterialInstanceDynamic* GetColorMaterial(const FLinearColor& Color);

    TMap<int64, FCell> Cells;
    TArray<FVector> ClearPoints;
    TArray<TPair<FVector, double>> AuthoredZones;
    bool bFocusOverride = false;
    FVector FocusOverride = FVector::ZeroVector;
    int32 SpawnedTotal = 0;
    int32 SpawnedAtLastReport = 0;
    bool bWasBuilding = false;
    TArray<TWeakObjectPtr<AActor>> Zombies;

    // Region tracking and atmosphere.
    const FRingRegion* CurrentRegion = nullptr;
    double RegionEnteredAt = -100.0;
    float RegionCheckIn = 0.f;
    TWeakObjectPtr<UExponentialHeightFogComponent> Fog;
    TWeakObjectPtr<ULightComponent> Sun;
    FLinearColor OrigFogColor = FLinearColor::White, OrigSunColor = FLinearColor::White;
    float OrigFogDensity = 0.02f;
    FLinearColor FogTarget = FLinearColor::White, SunTarget = FLinearColor::White;
    float FogDensityTarget = 1.f;
    FLinearColor FogNow = FLinearColor::White, SunNow = FLinearColor::White;
    float FogDensityNow = 1.f;
    bool bAtmosphereStarted = false;

    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeMesh;

    UPROPERTY()
    TObjectPtr<UStaticMesh> CylinderMesh;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> BaseMaterial;

    UPROPERTY()
    TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> ColorMaterials;

    static TWeakObjectPtr<ARingContentStreamer> Instance;
};
