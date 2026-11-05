// RingBandGenerator.h
//
// Generates the mid-field proxy: a full 360-degree revolved band with rim
// walls, built directly as an FDynamicMesh3 rather than through the
// GeometryScript Blueprint function library. This is the cheap, always-loaded
// stand-in for the whole ring; real terrain streams in over it near the
// player later.
//
// Requires "GeometryCore" and "GeometryFramework" in Build.cs. Replace
// TRANSFIGURE_API with your module's macro.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RingBandGenerator.generated.h"

class UDynamicMeshComponent;

UCLASS()
class TRANSFIGURE_API ARingBandGenerator : public AActor
{
    GENERATED_BODY()

public:

    ARingBandGenerator();

    /** Press this in the Details panel to (re)build the mesh. */
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Ring World")
    void GenerateRingBand();

    /**
     * Triangles around the full circumference. 512 gives a visibly smooth
     * curve at this scale; raise it only if you see faceting on the horizon.
     * Each segment is (Circumference / this number) wide, so at 10,700 km
     * circumference, 512 segments means ~20 km per facet -- plenty, since
     * you'll never be close enough to this proxy mesh to see the joints.
     */
    UPROPERTY(EditAnywhere, Category = "Ring World", meta = (ClampMin = "16", ClampMax = "8192"))
    int32 RadialSegments = 4096;

    /**
     * The band is drawn this far below the true surface (cm), so the collidable
     * near-field floor and level floors always cover it. With 4096 segments the
     * facets sag up to ~50 cm between vertices, so keep this above that.
     */
    UPROPERTY(EditAnywhere, Category = "Ring World", meta = (ClampMin = "0.0"))
    double BandSink = 300.0;

    /** Strips across the band's width, each coloured by the region under it (the map you see overhead). */
    UPROPERTY(EditAnywhere, Category = "Ring World", meta = (ClampMin = "1", ClampMax = "64"))
    int32 LateralStrips = 24;

    /**
     * Placing this actor in a map makes that map a ring world while it plays: curved
     * ring gravity (the Project Settings flag bFlatWorldForTesting is overridden off),
     * and a collidable floor follows the player. Maps without it stay flat.
     */
    UPROPERTY(EditAnywhere, Category = "Ring World")
    bool bActivateRingWorld = true;

    /** Generate procedural content on the open ring around the player (outside ARingAuthoredZones). */
    UPROPERTY(EditAnywhere, Category = "Ring World")
    bool bGenerateContent = true;

    /** Same seed = same ring. */
    UPROPERTY(EditAnywhere, Category = "Ring World")
    int32 WorldSeed = 1337;

    /** Rebuild automatically whenever a property above changes in the Details panel. */
    UPROPERTY(EditAnywhere, Category = "Ring World")
    bool bRegenerateOnPropertyChange = true;

protected:

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostInitializeComponents() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:

    UPROPERTY(VisibleAnywhere, Category = "Ring World")
    TObjectPtr<UDynamicMeshComponent> DynamicMeshComponent;

    bool bChangedFlatFlag = false;
    bool bSavedFlatFlag = true;
};
