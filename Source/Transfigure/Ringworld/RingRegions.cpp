// Source/Transfigure/Ringworld/RingRegions.cpp

#include "RingRegions.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"

// ── noise ───────────────────────────────────────────────────────────────────

uint64 RingNoise::Mix(uint64 X)
{
    // splitmix64: stable across runs and platforms (never GetTypeHash for seeds).
    X += 0x9E3779B97F4A7C15ull;
    X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
    X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
    return X ^ (X >> 31);
}

namespace
{
    // 16 unit gradients.
    const double RnGradX[16] = { 1.0, 0.9239, 0.7071, 0.3827, 0.0, -0.3827, -0.7071, -0.9239, -1.0, -0.9239, -0.7071, -0.3827, 0.0, 0.3827, 0.7071, 0.9239 };
    const double RnGradY[16] = { 0.0, 0.3827, 0.7071, 0.9239, 1.0, 0.9239, 0.7071, 0.3827, 0.0, -0.3827, -0.7071, -0.9239, -1.0, -0.9239, -0.7071, -0.3827 };

    FORCEINLINE double RnFade(double T) { return T * T * T * (T * (T * 6.0 - 15.0) + 10.0); }
    FORCEINLINE double RnSmooth(double X) { X = FMath::Clamp(X, 0.0, 1.0); return X * X * (3.0 - 2.0 * X); }
    FORCEINLINE double RnUnit(double N) { return FMath::Clamp(N * 0.5 + 0.5, 0.0, 1.0); }

    FORCEINLINE uint64 RnHash2(int64 X, int64 Y, uint64 Seed)
    {
        return RingNoise::Mix(RingNoise::Mix(Seed ^ (static_cast<uint64>(X) * 0x9E3779B97F4A7C15ull)) ^ (static_cast<uint64>(Y) * 0xC2B2AE3D27D4EB4Full));
    }

    FLinearColor RnSRGB(uint8 R, uint8 G, uint8 B) { return FLinearColor::FromSRGBColor(FColor(R, G, B)); }

    struct FRnFlatSpot { double Arc, Lat, Radius, Falloff; };
    TArray<FRnFlatSpot> GRnFlatSpots;
}

double RingNoise::Perlin(double X, double Y, uint64 Seed)
{
    const double FX = FMath::FloorToDouble(X), FY = FMath::FloorToDouble(Y);
    const int64 IX = static_cast<int64>(FX), IY = static_cast<int64>(FY);
    const double DX = X - FX, DY = Y - FY;
    auto G = [Seed](int64 CX, int64 CY, double OX, double OY)
    {
        const int32 I = static_cast<int32>(RnHash2(CX, CY, Seed) & 15);
        return RnGradX[I] * OX + RnGradY[I] * OY;
    };
    const double N00 = G(IX, IY, DX, DY);
    const double N10 = G(IX + 1, IY, DX - 1.0, DY);
    const double N01 = G(IX, IY + 1, DX, DY - 1.0);
    const double N11 = G(IX + 1, IY + 1, DX - 1.0, DY - 1.0);
    const double U = RnFade(DX), V = RnFade(DY);
    return FMath::Lerp(FMath::Lerp(N00, N10, U), FMath::Lerp(N01, N11, U), V) * 1.414;
}

double RingNoise::Fbm(double X, double Y, uint64 Seed, int32 Octaves)
{
    double Sum = 0.0, Amp = 1.0, Total = 0.0, F = 1.0;
    for (int32 O = 0; O < Octaves; ++O)
    {
        Sum += Amp * Perlin(X * F, Y * F, Seed + static_cast<uint64>(O) * 101ull);
        Total += Amp;
        Amp *= 0.5;
        F *= 2.03;
    }
    return Sum / Total;
}

double RingNoise::Ridged(double X, double Y, uint64 Seed, int32 Octaves)
{
    double Sum = 0.0, Amp = 1.0, Total = 0.0, F = 1.0;
    for (int32 O = 0; O < Octaves; ++O)
    {
        double R = 1.0 - FMath::Abs(Perlin(X * F, Y * F, Seed + static_cast<uint64>(O) * 131ull));
        Sum += Amp * R * R;
        Total += Amp;
        Amp *= 0.5;
        F *= 2.07;
    }
    return FMath::Clamp(Sum / Total, 0.0, 1.0);
}

// ── biomes ──────────────────────────────────────────────────────────────────

namespace
{
    const FRingBiomeInfo* BuildRingBiomes()
    {
        static FRingBiomeInfo B[static_cast<int32>(ERingBiome::Count)];
        auto Set = [](FRingBiomeInfo& I, const TCHAR* Name, ETransfigurationElement El,
            FLinearColor Low, FLinearColor High, FLinearColor P0, FLinearColor P1, FLinearColor P2,
            FLinearColor Fog, float FogDensity, FLinearColor Sun, float Amp, float Density, float PieceH, float Danger)
        {
            I.Name = Name; I.Element = El; I.GroundLow = Low; I.GroundHigh = High;
            I.Piece[0] = P0; I.Piece[1] = P1; I.Piece[2] = P2;
            I.Fog = Fog; I.FogDensity = FogDensity; I.Sun = Sun;
            I.TerrainAmp = Amp; I.Density = Density; I.PieceHeight = PieceH; I.DangerBase = Danger;
        };
        auto Mixes = [](FRingBiomeInfo& I, float Pillars, float Walls, float Canopy, float Mesa, float Spikes, float Crystals, float Arches,
            float Husk, float Bloat, float Gaunt)
        {
            I.WPillars = Pillars; I.WWalls = Walls; I.WCanopy = Canopy; I.WMesa = Mesa;
            I.WSpikes = Spikes; I.WCrystals = Crystals; I.WArches = Arches;
            I.WHusk = Husk; I.WBloat = Bloat; I.WGaunt = Gaunt;
        };
        using E = ETransfigurationElement;
        FRingBiomeInfo& Ember = B[static_cast<int32>(ERingBiome::Ember)];
        Set(Ember, TEXT("Ember Wastes"), E::Fire, RnSRGB(92, 48, 32), RnSRGB(170, 96, 52), RnSRGB(60, 30, 24), RnSRGB(190, 90, 40), RnSRGB(42, 36, 34),
            RnSRGB(255, 150, 90), 1.4f, RnSRGB(255, 205, 165), 30.f, 0.7f, 1.1f, 3.5f);
        Mixes(Ember, 0.2f, 0.6f, 0.1f, 1.0f, 0.8f, 0.0f, 0.3f, 0.6f, 1.6f, 0.4f);

        FRingBiomeInfo& Frost = B[static_cast<int32>(ERingBiome::Frost)];
        Set(Frost, TEXT("Frost Shelf"), E::Ice, RnSRGB(170, 190, 212), RnSRGB(232, 242, 255), RnSRGB(150, 200, 235), RnSRGB(220, 240, 255), RnSRGB(90, 130, 170),
            RnSRGB(190, 215, 240), 1.2f, RnSRGB(220, 235, 255), 12.f, 0.5f, 1.0f, 2.5f);
        Mixes(Frost, 0.2f, 0.2f, 0.0f, 0.3f, 1.0f, 1.0f, 0.2f, 0.6f, 0.3f, 1.6f);

        FRingBiomeInfo& Storm = B[static_cast<int32>(ERingBiome::Storm)];
        Set(Storm, TEXT("Storm Highlands"), E::Lightning, RnSRGB(55, 62, 78), RnSRGB(100, 112, 135), RnSRGB(70, 80, 100), RnSRGB(150, 160, 190), RnSRGB(230, 210, 120),
            RnSRGB(120, 130, 160), 1.3f, RnSRGB(200, 210, 235), 28.f, 0.7f, 1.5f, 3.0f);
        Mixes(Storm, 1.2f, 0.3f, 0.2f, 0.3f, 0.3f, 0.2f, 0.8f, 1.0f, 1.0f, 1.0f);

        FRingBiomeInfo& Verdant = B[static_cast<int32>(ERingBiome::Verdant)];
        Set(Verdant, TEXT("Verdant Canopy"), E::Earth, RnSRGB(52, 82, 40), RnSRGB(112, 140, 62), RnSRGB(84, 62, 42), RnSRGB(62, 122, 52), RnSRGB(150, 170, 80),
            RnSRGB(170, 200, 150), 1.0f, RnSRGB(255, 245, 210), 14.f, 0.8f, 1.2f, 1.8f);
        Mixes(Verdant, 1.0f, 0.1f, 1.2f, 0.3f, 0.0f, 0.1f, 0.3f, 1.5f, 0.6f, 0.6f);

        FRingBiomeInfo& Prism = B[static_cast<int32>(ERingBiome::Prism)];
        Set(Prism, TEXT("Prism Fields"), E::Arcane, RnSRGB(120, 100, 160), RnSRGB(90, 170, 175), RnSRGB(180, 120, 230), RnSRGB(90, 220, 210), RnSRGB(240, 200, 255),
            RnSRGB(200, 170, 230), 0.9f, RnSRGB(245, 225, 255), 10.f, 0.6f, 1.0f, 1.8f);
        Mixes(Prism, 0.3f, 0.1f, 0.2f, 0.3f, 0.2f, 1.4f, 0.9f, 1.0f, 1.0f, 1.0f);

        FRingBiomeInfo& Void = B[static_cast<int32>(ERingBiome::Void)];
        Set(Void, TEXT("Void Scar"), E::Void, RnSRGB(30, 20, 38), RnSRGB(72, 42, 92), RnSRGB(22, 14, 30), RnSRGB(110, 50, 160), RnSRGB(60, 30, 80),
            RnSRGB(80, 50, 110), 1.6f, RnSRGB(200, 170, 230), 10.f, 0.6f, 1.2f, 4.5f);
        Mixes(Void, 0.3f, 0.6f, 0.1f, 0.2f, 1.2f, 0.4f, 0.2f, 1.0f, 1.0f, 1.2f);

        FRingBiomeInfo& Foundry = B[static_cast<int32>(ERingBiome::Foundry)];
        Set(Foundry, TEXT("Foundry Ruins"), E::None, RnSRGB(80, 80, 84), RnSRGB(120, 110, 100), RnSRGB(100, 100, 105), RnSRGB(150, 80, 50), RnSRGB(60, 62, 66),
            RnSRGB(150, 150, 155), 1.1f, RnSRGB(240, 235, 225), 4.f, 0.9f, 1.0f, 3.0f);
        Mixes(Foundry, 0.5f, 1.4f, 0.8f, 0.4f, 0.1f, 0.0f, 0.3f, 1.0f, 0.8f, 1.0f);

        FRingBiomeInfo& Dunes = B[static_cast<int32>(ERingBiome::Dunes)];
        Set(Dunes, TEXT("Dune Sea"), E::None, RnSRGB(190, 158, 108), RnSRGB(232, 202, 142), RnSRGB(200, 170, 120), RnSRGB(160, 120, 80), RnSRGB(230, 215, 180),
            RnSRGB(240, 220, 180), 0.8f, RnSRGB(255, 240, 210), 18.f, 0.25f, 1.0f, 1.5f);
        Mixes(Dunes, 0.1f, 0.1f, 0.0f, 0.6f, 0.1f, 0.0f, 0.7f, 1.0f, 1.0f, 1.0f);
        return B;
    }

    // Name parts per biome.
    const TCHAR* const RnEmberAdj[] = { TEXT("Cinder"), TEXT("Ashen"), TEXT("Smoldering"), TEXT("Scorched"), TEXT("Blackglass"), TEXT("Burning"), TEXT("Red") };
    const TCHAR* const RnEmberNoun[] = { TEXT("Barrens"), TEXT("Wastes"), TEXT("Reach"), TEXT("Expanse"), TEXT("Terraces"), TEXT("Crags") };
    const TCHAR* const RnFrostAdj[] = { TEXT("Pale"), TEXT("Rime"), TEXT("Frozen"), TEXT("Glacial"), TEXT("Silent"), TEXT("Hoarfrost"), TEXT("White") };
    const TCHAR* const RnFrostNoun[] = { TEXT("Shelf"), TEXT("Fields"), TEXT("Reach"), TEXT("Sheets"), TEXT("Expanse"), TEXT("Tundra") };
    const TCHAR* const RnStormAdj[] = { TEXT("Thundering"), TEXT("Iron"), TEXT("Howling"), TEXT("Stormwrought"), TEXT("Grey"), TEXT("Crackling") };
    const TCHAR* const RnStormNoun[] = { TEXT("Highlands"), TEXT("Heights"), TEXT("Downs"), TEXT("Spires"), TEXT("Ridge"), TEXT("Moor") };
    const TCHAR* const RnVerdantAdj[] = { TEXT("Green"), TEXT("Whispering"), TEXT("Mossy"), TEXT("Verdant"), TEXT("Deep"), TEXT("Wild"), TEXT("Old") };
    const TCHAR* const RnVerdantNoun[] = { TEXT("Canopy"), TEXT("Wilds"), TEXT("Weald"), TEXT("Groves"), TEXT("Thicket"), TEXT("Vale") };
    const TCHAR* const RnPrismAdj[] = { TEXT("Prism"), TEXT("Shimmering"), TEXT("Glass"), TEXT("Dreaming"), TEXT("Starlit"), TEXT("Hollow") };
    const TCHAR* const RnPrismNoun[] = { TEXT("Fields"), TEXT("Gardens"), TEXT("Meadows"), TEXT("Lattice"), TEXT("Basin"), TEXT("Choir") };
    const TCHAR* const RnVoidAdj[] = { TEXT("Hollow"), TEXT("Broken"), TEXT("Starless"), TEXT("Torn"), TEXT("Shattered"), TEXT("Silent") };
    const TCHAR* const RnVoidNoun[] = { TEXT("Scar"), TEXT("Rift"), TEXT("Wound"), TEXT("Craters"), TEXT("Maw"), TEXT("Gulf") };
    const TCHAR* const RnFoundryAdj[] = { TEXT("Rusted"), TEXT("Iron"), TEXT("Fallen"), TEXT("Forgotten"), TEXT("Old"), TEXT("Silent") };
    const TCHAR* const RnFoundryNoun[] = { TEXT("Foundry"), TEXT("Works"), TEXT("Yards"), TEXT("Halls"), TEXT("Ruins"), TEXT("Engines") };
    const TCHAR* const RnDunesAdj[] = { TEXT("Shifting"), TEXT("Golden"), TEXT("Endless"), TEXT("Sunlit"), TEXT("Singing"), TEXT("Amber") };
    const TCHAR* const RnDunesNoun[] = { TEXT("Dunes"), TEXT("Sea"), TEXT("Sands"), TEXT("Drifts"), TEXT("Waves"), TEXT("Erg") };

    FString MakeRingRegionName(ERingBiome B, FRandomStream& R, TSet<FString>& Used)
    {
        const TCHAR* const* Adj = RnDunesAdj; int32 NA = UE_ARRAY_COUNT(RnDunesAdj);
        const TCHAR* const* Noun = RnDunesNoun; int32 NN = UE_ARRAY_COUNT(RnDunesNoun);
        switch (B)
        {
        case ERingBiome::Ember:   Adj = RnEmberAdj;   NA = UE_ARRAY_COUNT(RnEmberAdj);   Noun = RnEmberNoun;   NN = UE_ARRAY_COUNT(RnEmberNoun); break;
        case ERingBiome::Frost:   Adj = RnFrostAdj;   NA = UE_ARRAY_COUNT(RnFrostAdj);   Noun = RnFrostNoun;   NN = UE_ARRAY_COUNT(RnFrostNoun); break;
        case ERingBiome::Storm:   Adj = RnStormAdj;   NA = UE_ARRAY_COUNT(RnStormAdj);   Noun = RnStormNoun;   NN = UE_ARRAY_COUNT(RnStormNoun); break;
        case ERingBiome::Verdant: Adj = RnVerdantAdj; NA = UE_ARRAY_COUNT(RnVerdantAdj); Noun = RnVerdantNoun; NN = UE_ARRAY_COUNT(RnVerdantNoun); break;
        case ERingBiome::Prism:   Adj = RnPrismAdj;   NA = UE_ARRAY_COUNT(RnPrismAdj);   Noun = RnPrismNoun;   NN = UE_ARRAY_COUNT(RnPrismNoun); break;
        case ERingBiome::Void:    Adj = RnVoidAdj;    NA = UE_ARRAY_COUNT(RnVoidAdj);    Noun = RnVoidNoun;    NN = UE_ARRAY_COUNT(RnVoidNoun); break;
        case ERingBiome::Foundry: Adj = RnFoundryAdj; NA = UE_ARRAY_COUNT(RnFoundryAdj); Noun = RnFoundryNoun; NN = UE_ARRAY_COUNT(RnFoundryNoun); break;
        default: break;
        }
        FString Name;
        for (int32 Try = 0; Try < 16; ++Try)
        {
            Name = FString::Printf(TEXT("The %s %s"), Adj[R.RandRange(0, NA - 1)], Noun[R.RandRange(0, NN - 1)]);
            if (!Used.Contains(Name)) break;
        }
        if (Used.Contains(Name))
        {
            static const TCHAR* const Marks[] = { TEXT("Upper"), TEXT("Lower"), TEXT("Far"), TEXT("Near"), TEXT("Outer"), TEXT("Inner") };
            Name = FString::Printf(TEXT("%s, %s"), *Name, Marks[R.RandRange(0, UE_ARRAY_COUNT(Marks) - 1)]);
        }
        Used.Add(Name);
        return Name;
    }
}

const FRingBiomeInfo& FRingRegions::Biome(ERingBiome B)
{
    static const FRingBiomeInfo* Table = BuildRingBiomes();
    return Table[FMath::Clamp(static_cast<int32>(B), 0, static_cast<int32>(ERingBiome::Count) - 1)];
}

// ── region map ──────────────────────────────────────────────────────────────

const FRingRegions& FRingRegions::Get(int32 WorldSeed)
{
    static TMap<int32, TUniquePtr<FRingRegions>> Cache;
    if (const TUniquePtr<FRingRegions>* Found = Cache.Find(WorldSeed)) return **Found;
    return *Cache.Add(WorldSeed, TUniquePtr<FRingRegions>(new FRingRegions(WorldSeed)));
}

FRingRegions::FRingRegions(int32 InSeed)
    : Seed(InSeed)
{
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    Circumference = URingWorldGeometry::GetCircumference(Spec);
    Width = Spec.Width;
    Build();
}

void FRingRegions::Build()
{
    FRandomStream R(static_cast<int32>(RingNoise::Mix(static_cast<uint64>(Seed) ^ 0x5E6100ull) & 0x7FFFFFFF));

    // Stretches along the ring.
    double Arc = 0.0;
    while (Arc < Circumference - 1.0)
    {
        double Len = R.FRandRange(70.0e5, 160.0e5);
        if (Circumference - (Arc + Len) < 50.0e5) Len = Circumference - Arc;
        FSegment& S = Segments.AddDefaulted_GetRef();
        S.ArcStart = Arc;
        S.ArcEnd = Arc + Len;
        Arc += Len;
    }

    // How common each biome is.
    static const float Common[static_cast<int32>(ERingBiome::Count)] = { 1.f, 1.f, 1.f, 1.2f, 0.8f, 0.6f, 0.9f, 1.f };
    constexpr int32 NB = static_cast<int32>(ERingBiome::Count);
    TSet<FString> Used;
    ERingBiome PrevFirst = ERingBiome::Count;
    for (int32 SI = 0; SI < Segments.Num(); ++SI)
    {
        FSegment& S = Segments[SI];
        const float Roll = R.FRand();
        const int32 NumParts = Roll < 0.3f ? 1 : (Roll < 0.75f ? 2 : 3);
        TArray<double> Cuts;
        if (NumParts == 2) Cuts.Add(R.FRandRange(-0.25, 0.25) * Width);
        if (NumParts == 3)
        {
            Cuts.Add(R.FRandRange(-0.35, -0.12) * Width);
            Cuts.Add(R.FRandRange(0.12, 0.35) * Width);
        }
        ERingBiome Prev = PrevFirst;
        for (int32 P = 0; P < NumParts; ++P)
        {
            FRingRegion Reg;
            Reg.Id = Regions.Num();
            Reg.Segment = SI;
            Reg.ArcStart = S.ArcStart;
            Reg.ArcEnd = S.ArcEnd;
            Reg.LatLo = P == 0 ? -1.0e12 : Cuts[P - 1];
            Reg.LatHi = P == NumParts - 1 ? 1.0e12 : Cuts[P];
            Reg.Seed = static_cast<uint32>(RingNoise::Mix(static_cast<uint64>(Seed) * 31ull + static_cast<uint64>(Reg.Id)) & 0xFFFFFFFFull);

            // Weighted pick, never the same biome as the neighbour before it.
            float Total = 0.f;
            for (int32 B = 0; B < NB; ++B) if (static_cast<ERingBiome>(B) != Prev) Total += Common[B];
            float Pick = R.FRand() * Total;
            ERingBiome Chosen = Prev == ERingBiome::Dunes ? ERingBiome::Verdant : ERingBiome::Dunes;
            for (int32 B = 0; B < NB; ++B)
            {
                if (static_cast<ERingBiome>(B) == Prev) continue;
                Pick -= Common[B];
                if (Pick <= 0.f) { Chosen = static_cast<ERingBiome>(B); break; }
            }
            Reg.Biome = Chosen;
            Reg.Danger = FMath::Clamp(FMath::RoundToInt(Biome(Chosen).DangerBase + R.FRandRange(-1.f, 1.f)), 1, 5);
            Reg.Variation = R.FRandRange(0.6f, 1.4f);
            Reg.Name = MakeRingRegionName(Chosen, R, Used);
            Prev = Chosen;
            S.Parts.Add(Reg.Id);
            Regions.Add(Reg);
        }
        PrevFirst = Regions[S.Parts[0]].Biome;
    }
}

int32 FRingRegions::FindSegment(double WarpedArc) const
{
    int32 Lo = 0, Hi = Segments.Num() - 1;
    while (Lo < Hi)
    {
        const int32 Mid = (Lo + Hi + 1) / 2;
        if (Segments[Mid].ArcStart <= WarpedArc) Lo = Mid; else Hi = Mid - 1;
    }
    return Lo;
}

int32 FRingRegions::FindPart(int32 SegmentIdx, double WarpedLat) const
{
    const FSegment& S = Segments[SegmentIdx];
    for (int32 P = 0; P < S.Parts.Num(); ++P)
    {
        if (WarpedLat < Regions[S.Parts[P]].LatHi) return P;
    }
    return S.Parts.Num() - 1;
}

void FRingRegions::Warp(double Arc, double Lateral, double& OutArc, double& OutLat) const
{
    // Wavy borders: shift the lookup by slow noise (12 km along, 8 km across).
    const uint64 S = static_cast<uint64>(Seed);
    double A = Arc + RingNoise::Fbm(Lateral / 4.0e6, Arc / 4.0e6, S ^ 0xA1ull, 2) * 12.0e5;
    A = FMath::Fmod(A, Circumference);
    if (A < 0.0) A += Circumference;
    OutArc = A;
    OutLat = Lateral + RingNoise::Fbm(Arc / 3.0e6, Lateral / 3.0e6 + 17.0, S ^ 0xB2ull, 2) * 8.0e5;
}

FRingRegionSample FRingRegions::Sample(double Arc, double Lateral) const
{
    FRingRegionSample Out;
    double A, L;
    Warp(Arc, Lateral, A, L);
    const int32 SI = FindSegment(A);
    const FSegment& S = Segments[SI];
    const int32 PartIdx = FindPart(SI, L);
    const FRingRegion* Own = &Regions[S.Parts[PartIdx]];
    constexpr double B = 1.5e5;   // blend half-width: 1.5 km either side of a border

    // Across this stretch's start or end.
    double WA = 0.0;
    const FRingRegion* ArcN = nullptr;
    const double DLo = A - S.ArcStart, DHi = S.ArcEnd - A;
    if (DLo < B)
    {
        WA = 0.5 * RnSmooth(1.0 - DLo / B);
        const int32 NS = (SI - 1 + Segments.Num()) % Segments.Num();
        ArcN = &Regions[Segments[NS].Parts[FindPart(NS, L)]];
    }
    else if (DHi < B)
    {
        WA = 0.5 * RnSmooth(1.0 - DHi / B);
        const int32 NS = (SI + 1) % Segments.Num();
        ArcN = &Regions[Segments[NS].Parts[FindPart(NS, L)]];
    }
    // Across a split inside the stretch.
    double WL = 0.0;
    const FRingRegion* LatN = nullptr;
    if (PartIdx > 0 && L - Own->LatLo < B)
    {
        WL = 0.5 * RnSmooth(1.0 - (L - Own->LatLo) / B);
        LatN = &Regions[S.Parts[PartIdx - 1]];
    }
    else if (PartIdx < S.Parts.Num() - 1 && Own->LatHi - L < B)
    {
        WL = 0.5 * RnSmooth(1.0 - (Own->LatHi - L) / B);
        LatN = &Regions[S.Parts[PartIdx + 1]];
    }

    Out.Regions[0] = Own;
    Out.Weights[0] = static_cast<float>((1.0 - WA) * (1.0 - WL));
    Out.Num = 1;
    if (ArcN && WA > 0.0)
    {
        Out.Regions[Out.Num] = ArcN;
        Out.Weights[Out.Num++] = static_cast<float>(WA);
    }
    if (LatN && WL > 0.0)
    {
        Out.Regions[Out.Num] = LatN;
        Out.Weights[Out.Num++] = static_cast<float>((1.0 - WA) * WL);
    }
    return Out;
}

double FRingRegions::BiomeHeight(ERingBiome B, float Var, double Arc, double Lateral)
{
    // Metres; noise seeded per biome (not per region) so neighbouring regions of a kind join up.
    const double X = Arc / 100.0, Y = Lateral / 100.0;
    const uint64 BS = 0xB10E0000ull + static_cast<uint64>(B) * 977ull;
    double H = 0.0;
    switch (B)
    {
    case ERingBiome::Ember:
    {
        // Terraced plateaus: 4 flat steps with short rises.
        const double S = RnUnit(RingNoise::Fbm(X / 700.0, Y / 700.0, BS, 4)) * 4.0;
        const double Fl = FMath::FloorToDouble(S);
        H = 30.0 * Var * (Fl + RnSmooth((S - Fl - 0.7) / 0.3)) / 4.0 + 0.8 * RnUnit(RingNoise::Fbm(X / 60.0, Y / 60.0, BS + 7, 2));
        break;
    }
    case ERingBiome::Frost:
    {
        // Flat ice with pressure ridges.
        const double Ridge = FMath::Max(0.0, (RingNoise::Ridged(X / 450.0, Y / 450.0, BS, 3) - 0.55) / 0.45);
        H = 12.0 * Var * Ridge * Ridge + 0.6 * RnUnit(RingNoise::Fbm(X / 90.0, Y / 90.0, BS + 7, 2));
        break;
    }
    case ERingBiome::Storm:
        H = 28.0 * Var * FMath::Pow(RnUnit(RingNoise::Fbm(X / 600.0, Y / 600.0, BS, 4)), 1.5) + 2.0 * RnUnit(RingNoise::Fbm(X / 120.0, Y / 120.0, BS + 7, 2));
        break;
    case ERingBiome::Verdant:
        H = 14.0 * Var * RnUnit(RingNoise::Fbm(X / 400.0, Y / 400.0, BS, 4)) + 1.5 * RnUnit(RingNoise::Fbm(X / 90.0, Y / 90.0, BS + 7, 2));
        break;
    case ERingBiome::Prism:
        H = 10.0 * Var * RnSmooth((RnUnit(RingNoise::Fbm(X / 300.0, Y / 300.0, BS, 3)) - 0.35) / 0.35) + 0.5 * RnUnit(RingNoise::Fbm(X / 70.0, Y / 70.0, BS + 7, 2));
        break;
    case ERingBiome::Void:
    {
        // Raised broken ground pitted with craters.
        H = 5.0 + 3.0 * RnUnit(RingNoise::Fbm(X / 500.0, Y / 500.0, BS, 3));
        constexpr double Cell = 350.0;
        const int64 CX = static_cast<int64>(FMath::FloorToDouble(X / Cell)), CY = static_cast<int64>(FMath::FloorToDouble(Y / Cell));
        for (int64 DX = -1; DX <= 1; ++DX)
        {
            for (int64 DY = -1; DY <= 1; ++DY)
            {
                const uint64 Hh = RnHash2(CX + DX, CY + DY, BS + 3);
                if ((Hh & 0xFF) > 140) continue;   // about half the cells have one
                const double JX = ((Hh >> 8) & 0xFFFF) / 65535.0, JY = ((Hh >> 24) & 0xFFFF) / 65535.0;
                const double Rad = 50.0 + ((Hh >> 40) & 0xFF) / 255.0 * 100.0;
                const double PX = (static_cast<double>(CX + DX) + 0.15 + 0.7 * JX) * Cell;
                const double PY = (static_cast<double>(CY + DY) + 0.15 + 0.7 * JY) * Cell;
                const double D = FMath::Sqrt(FMath::Square(X - PX) + FMath::Square(Y - PY)) / Rad;
                const double Depth = FMath::Min(Rad * 0.12, 10.0);
                if (D < 1.0) H -= Depth * (1.0 - D * D);
                H += Depth * 0.35 * FMath::Exp(-FMath::Square((D - 1.0) / 0.22));
            }
        }
        H = FMath::Max(H, 0.0) * Var;
        break;
    }
    case ERingBiome::Foundry:
    {
        // Low flat steps (old foundations).
        const double S = RnUnit(RingNoise::Fbm(X / 250.0, Y / 250.0, BS, 3)) * 3.0;
        const double Fl = FMath::FloorToDouble(S);
        H = 4.0 * Var * (Fl + RnSmooth((S - Fl - 0.8) / 0.2)) / 3.0;
        break;
    }
    case ERingBiome::Dunes:
    default:
    {
        // Long dunes across a wind direction, on slow swells.
        const double U = X * 0.8253 + Y * 0.5646, V = -X * 0.5646 + Y * 0.8253;
        const double Swell = RnUnit(RingNoise::Fbm(X / 1500.0, Y / 1500.0, BS + 3, 2));
        H = 18.0 * Var * FMath::Pow(RingNoise::Ridged(U / 220.0, V / 600.0, BS, 2), 1.6) * (0.5 + 0.5 * Swell) + 3.0 * Swell;
        break;
    }
    }
    return FMath::Max(H, 0.0) * 100.0;
}

double FRingRegions::Height(double Arc, double Lateral) const
{
    const double Flat = FlattenFactor(Arc, Lateral);
    if (Flat <= 0.0) return 0.0;
    const FRingRegionSample S = Sample(Arc, Lateral);
    double H = 0.0;
    for (int32 i = 0; i < S.Num; ++i)
    {
        H += S.Weights[i] * BiomeHeight(S.Regions[i]->Biome, S.Regions[i]->Variation, Arc, Lateral);
    }
    return H * Flat;
}

void FRingRegions::HeightAndColor(double Arc, double Lateral, double& OutHeight, FLinearColor& OutColor) const
{
    const FRingRegionSample S = Sample(Arc, Lateral);
    const double Flat = FlattenFactor(Arc, Lateral);
    double H = 0.0;
    FLinearColor Col(0.f, 0.f, 0.f, 1.f);
    for (int32 i = 0; i < S.Num; ++i)
    {
        const FRingRegion& R = *S.Regions[i];
        const FRingBiomeInfo& Info = Biome(R.Biome);
        const double Hi = Flat > 0.0 ? BiomeHeight(R.Biome, R.Variation, Arc, Lateral) : 0.0;
        H += S.Weights[i] * Hi;
        // Low ground darker, high ground lighter.
        const float T = FMath::Clamp(static_cast<float>(Hi * Flat / 100.0 / FMath::Max(Info.TerrainAmp * R.Variation, 1.f)), 0.f, 1.f);
        Col += (Info.GroundLow + (Info.GroundHigh - Info.GroundLow) * T) * S.Weights[i];
    }
    OutHeight = H * Flat;
    // Mottling every ~35 m, so the ground reads at speed.
    Col *= static_cast<float>(0.9 + 0.12 * RingNoise::Fbm(Arc / 3500.0, Lateral / 3500.0, 0x6D07ull, 2));
    Col.A = 1.f;
    OutColor = Col;
}

FLinearColor FRingRegions::MapColor(double Arc, double Lateral) const
{
    const FRingRegionSample S = Sample(Arc, Lateral);
    FLinearColor Col(0.f, 0.f, 0.f, 1.f);
    for (int32 i = 0; i < S.Num; ++i)
    {
        const FRingBiomeInfo& Info = Biome(S.Regions[i]->Biome);
        Col += (Info.GroundLow + Info.GroundHigh) * 0.5f * S.Weights[i];
    }
    Col.A = 1.f;
    return Col;
}

void FRingRegions::Atmosphere(double Arc, double Lateral, FLinearColor& OutFog, float& OutFogDensity, FLinearColor& OutSun) const
{
    const FRingRegionSample S = Sample(Arc, Lateral);
    OutFog = FLinearColor(0.f, 0.f, 0.f, 1.f);
    OutSun = FLinearColor(0.f, 0.f, 0.f, 1.f);
    OutFogDensity = 0.f;
    for (int32 i = 0; i < S.Num; ++i)
    {
        const FRingBiomeInfo& Info = Biome(S.Regions[i]->Biome);
        OutFog += Info.Fog * S.Weights[i];
        OutSun += Info.Sun * S.Weights[i];
        OutFogDensity += Info.FogDensity * S.Weights[i];
    }
    OutFog.A = OutSun.A = 1.f;
}

// ── flat spots ──────────────────────────────────────────────────────────────

void FRingRegions::AddFlatten(double Arc, double Lateral, double Radius, double Falloff)
{
    GRnFlatSpots.Add({ Arc, Lateral, Radius, FMath::Max(Falloff, 1.0) });
}

void FRingRegions::ResetFlatten()
{
    GRnFlatSpots.Reset();
}

double FRingRegions::FlattenFactor(double Arc, double Lateral)
{
    if (GRnFlatSpots.Num() == 0) return 1.0;
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    double F = 1.0;
    for (const FRnFlatSpot& S : GRnFlatSpots)
    {
        const double DA = URingWorldGeometry::ShortestArcDelta(Spec, S.Arc, Arc);
        const double DL = Lateral - S.Lat;
        const double Outer = S.Radius + S.Falloff;
        const double D2 = DA * DA + DL * DL;
        if (D2 >= Outer * Outer) continue;
        F = FMath::Min(F, RnSmooth((FMath::Sqrt(D2) - S.Radius) / S.Falloff));
    }
    return F;
}
