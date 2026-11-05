// Source/Transfigure/Ringworld/RingRegions.h
//
// The ring's region map (WORLDGEN_PLAN R1-R3): which biome, name and danger every
// spot on the ring has, and the terrain height and ground colour there.
//
// The ring is split along its length into 70-160 km stretches, each split across
// into 1-3 regions. Borders are warped with noise and blended over ~1.5 km, so the
// terrain and colours never jump. Everything comes from the world seed, so the same
// spot always looks the same; the only runtime input is the list of flat spots
// (authored zones, pads, landing spots), reset at the start of each play.
//
// Plain C++ (no UObjects): pure functions of (arc, lateral) in cm, so the near
// field, the content streamer, the band and the HUD all agree without traces.

#pragma once

#include "CoreMinimal.h"
#include "Transfiguration/TransfigurationTypes.h"

enum class ERingBiome : uint8
{
    Ember,      // Fire: terraced plateaus
    Frost,      // Ice: flat sheets, pressure ridges
    Storm,      // Lightning: high rolling hills
    Verdant,    // Earth: gentle hills, canopies
    Prism,      // Arcane: mounds and basins, crystals
    Void,       // Void: craters
    Foundry,    // machine ruins: low steps, wall mazes
    Dunes,      // sand sea
    Count
};

struct FRingBiomeInfo
{
    const TCHAR* Name = TEXT("");
    ETransfigurationElement Element = ETransfigurationElement::None;
    FLinearColor GroundLow, GroundHigh;     // linear
    FLinearColor Piece[3];
    FLinearColor Fog;
    float FogDensity = 0.02f;
    FLinearColor Sun;
    /** Typical terrain height (m), for colouring by height. */
    float TerrainAmp = 10.f;
    /** Chance a cell gets a structure. */
    float Density = 0.6f;
    float PieceHeight = 1.f;
    float DangerBase = 2.f;
    // Structure mix.
    float WPillars = 1.f, WWalls = 1.f, WCanopy = 1.f, WMesa = 1.f, WSpikes = 0.f, WCrystals = 0.f, WArches = 0.f;
    // Zombie body mix.
    float WHusk = 1.f, WBloat = 1.f, WGaunt = 1.f;
};

struct FRingRegion
{
    int32 Id = 0;
    ERingBiome Biome = ERingBiome::Dunes;
    FString Name;
    int32 Danger = 1;           // 1..5
    float Variation = 1.f;      // terrain height multiplier
    uint32 Seed = 0;
    int32 Segment = 0;
    double ArcStart = 0.0, ArcEnd = 0.0;    // warped arc bounds of its stretch
    double LatLo = 0.0, LatHi = 0.0;        // warped lateral bounds (outer parts are open-ended)
};

/** What's at a point: the main region plus up to two neighbours it blends into (weights sum to 1). */
struct FRingRegionSample
{
    int32 Num = 0;
    const FRingRegion* Regions[3] = { nullptr, nullptr, nullptr };
    float Weights[3] = { 0.f, 0.f, 0.f };
    const FRingRegion* Main() const { return Regions[0]; }
};

class TRANSFIGURE_API FRingRegions
{
public:
    /** The map for this seed (built on first use, then cached). */
    static const FRingRegions& Get(int32 WorldSeed);
    static const FRingBiomeInfo& Biome(ERingBiome B);

    /** Region weights at a point (arc and lateral in cm). */
    FRingRegionSample Sample(double Arc, double Lateral) const;
    const FRingRegion& RegionAt(double Arc, double Lateral) const { return *Sample(Arc, Lateral).Main(); }

    /** Terrain height above the base surface (cm, >= 0), with flat spots applied. */
    double Height(double Arc, double Lateral) const;
    /** Height and ground colour (linear) together. */
    void HeightAndColor(double Arc, double Lateral, double& OutHeight, FLinearColor& OutColor) const;
    /** Only the colour, ignoring flat spots and height shading (the far band). */
    FLinearColor MapColor(double Arc, double Lateral) const;

    /** Blended look of the biomes at a point (fog, sun) for the atmosphere. */
    void Atmosphere(double Arc, double Lateral, FLinearColor& OutFog, float& OutFogDensity, FLinearColor& OutSun) const;

    const TArray<FRingRegion>& GetRegions() const { return Regions; }
    int32 GetSeed() const { return Seed; }

    // ── flat spots (runtime: authored zones, pads, landing spots) ──
    /** Flat within Radius, blending back to full terrain over Falloff (cm). */
    static void AddFlatten(double Arc, double Lateral, double Radius, double Falloff);
    static void ResetFlatten();
    /** 0 = fully flat here, 1 = full terrain. */
    static double FlattenFactor(double Arc, double Lateral);

private:
    explicit FRingRegions(int32 InSeed);
    void Build();
    int32 FindSegment(double WarpedArc) const;
    int32 FindPart(int32 SegmentIdx, double WarpedLat) const;
    void Warp(double Arc, double Lateral, double& OutArc, double& OutLat) const;
    /** One biome's raw terrain (cm, >= 0) at a point. */
    static double BiomeHeight(ERingBiome B, float Variation, double Arc, double Lateral);

    struct FSegment
    {
        double ArcStart = 0.0, ArcEnd = 0.0;
        TArray<int32> Parts;    // region indices, ordered by lateral
    };

    int32 Seed = 0;
    double Circumference = 1.0;
    double Width = 1.0;
    TArray<FSegment> Segments;
    TArray<FRingRegion> Regions;
};

/** Deterministic double-precision noise (no repeats, no float jitter far from the origin). */
namespace RingNoise
{
    uint64 Mix(uint64 X);
    /** Gradient noise, about [-1, 1]. */
    double Perlin(double X, double Y, uint64 Seed);
    /** Fractal sum of Perlin, about [-1, 1]. */
    double Fbm(double X, double Y, uint64 Seed, int32 Octaves);
    /** Ridged fractal, [0, 1] (1 on the ridges). */
    double Ridged(double X, double Y, uint64 Seed, int32 Octaves);
}
