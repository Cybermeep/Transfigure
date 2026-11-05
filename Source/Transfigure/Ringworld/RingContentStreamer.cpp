// Source/Transfigure/Ringworld/RingContentStreamer.cpp

#include "RingContentStreamer.h"
#include "RingRegions.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"
#include "RingLaunchComponent.h"
#include "RingLaunchPad.h"
#include "Limb/ProceduralZombie.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

TWeakObjectPtr<ARingContentStreamer> ARingContentStreamer::Instance;

namespace
{
    int32 CellSeed(int32 World, int64 A, int64 B, uint64 Salt)
    {
        return static_cast<int32>(RingNoise::Mix(RingNoise::Mix(RingNoise::Mix(static_cast<uint64>(World) ^ Salt) ^ static_cast<uint64>(A)) ^ static_cast<uint64>(B)) & 0x7FFFFFFF);
    }
}

// ── pieces / zones ──────────────────────────────────────────────────────────

ARingPiece::ARingPiece()
{
    PrimaryActorTick.bCanEverTick = false;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    // Movable: the mesh is chosen after spawning, and a Static component refuses that
    // (it silently stayed empty - invisible, no collision). Pieces never actually move.
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    Mesh->SetCanEverAffectNavigation(false);
}

ARingAuthoredZone::ARingAuthoredZone()
{
    PrimaryActorTick.bCanEverTick = false;
    Area = CreateDefaultSubobject<USphereComponent>(TEXT("Area"));
    SetRootComponent(Area);
    Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Area->SetSphereRadius(Radius);
    Area->ShapeColor = FColor(255, 170, 40);
    Area->SetHiddenInGame(true);
}

void ARingAuthoredZone::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if (Area) Area->SetSphereRadius(Radius);
}

#if WITH_EDITOR
void ARingAuthoredZone::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    Super::PostEditChangeProperty(Event);
    if (Area) Area->SetSphereRadius(Radius);
}
#endif

// ── streamer ────────────────────────────────────────────────────────────────

ARingContentStreamer::ARingContentStreamer()
{
    PrimaryActorTick.bCanEverTick = true;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    CubeMesh = Cube.Object;
    CylinderMesh = Cylinder.Object;
    BaseMaterial = Mat.Object;
}

ARingContentStreamer* ARingContentStreamer::Get(const UWorld* World)
{
    ARingContentStreamer* S = Instance.Get();
    return (S && S->GetWorld() == World) ? S : nullptr;
}

void ARingContentStreamer::BeginPlay()
{
    Super::BeginPlay();
    Instance = this;
    for (TActorIterator<ARingAuthoredZone> It(GetWorld()); It; ++It)
    {
        AuthoredZones.Add(TPair<FVector, double>(It->GetActorLocation(), It->Radius));
    }
    for (TActorIterator<ARingLaunchPad> It(GetWorld()); It; ++It)
    {
        ClearPoints.Add(It->GetActorLocation());
    }
    // The map's fog and sun get tinted toward each region's look (relative to how the map set them).
    for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
    {
        if (UExponentialHeightFogComponent* C = It->GetComponent())
        {
            // Height fog measures height along world Z, but on the ring "up" is only world Z near
            // where the map was built: elsewhere it went solid or vanished. So on the ring it becomes
            // an even haze that follows the player, capped so the arch overhead always shows through.
            Fog = C;
            OrigFogColor = C->FogInscatteringLuminance;
            OrigFogDensity = 0.0004f;
            C->SetFogHeightFalloff(0.001f);
            C->SetFogMaxOpacity(0.4f);
            C->SetStartDistance(0.f);
            C->SetFogDensity(OrigFogDensity);
            break;
        }
    }
    for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
    {
        if (ULightComponent* C = It->GetLightComponent())
        {
            Sun = C;
            OrigSunColor = C->GetLightColor();
            break;
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("[TM] Ring content: seed %d, %.0f m cells, %d authored zone(s), fog %s, sun %s"), WorldSeed, CellSize / 100.0,
        AuthoredZones.Num(), Fog.IsValid() ? TEXT("yes") : TEXT("no"), Sun.IsValid() ? TEXT("yes") : TEXT("no"));
}

void ARingContentStreamer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (Instance.Get() == this) Instance = nullptr;
    Super::EndPlay(EndPlayReason);
}

void ARingContentStreamer::SetFocusOverride(bool bEnable, const FVector& WorldPos)
{
    bFocusOverride = bEnable;
    FocusOverride = WorldPos;
}

int32 ARingContentStreamer::GetPendingCount() const
{
    int32 N = 0;
    for (const auto& Pair : Cells) N += Pair.Value.Pending.Num();
    return N;
}

bool ARingContentStreamer::IsClear(const FVector& WorldPos, double Radius) const
{
    for (const FVector& P : ClearPoints)
    {
        if (FVector::DistSquared(P, WorldPos) < Radius * Radius) return false;
    }
    return true;
}

void ARingContentStreamer::AddClearPoint(const FVector& WorldPos)
{
    ClearPoints.Add(WorldPos);
    // Anything already standing there goes, and anything planned there is dropped.
    for (auto& Pair : Cells)
    {
        FCell& Cell = Pair.Value;
        Cell.Pending.RemoveAll([&](const FPlanned& P)
        {
            return FVector::Dist(P.Xf.GetLocation(), WorldPos) < LandingClearRadius + P.Footprint;
        });
        for (TWeakObjectPtr<AActor>& A : Cell.Actors)
        {
            const ARingPiece* Piece = Cast<ARingPiece>(A.Get());
            if (Piece && FVector::Dist(Piece->GetActorLocation(), WorldPos) < LandingClearRadius + Piece->FootprintRadius)
            {
                A->Destroy();
            }
        }
    }
}

void ARingContentStreamer::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (URingWorldSettings::GetSpec().bFlatWorldForTesting) return;

    const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    FVector Focus = FocusOverride;
    if (!bFocusOverride)
    {
        if (!Pawn) return;
        Focus = Pawn->GetActorLocation();
    }
    UpdateCells(Focus);
    SpawnSome();
    if (Pawn) UpdateRegion(Pawn->GetActorLocation(), DeltaSeconds);

    // Log each finished burst of building, so a play-test log shows what was generated where.
    const int32 Pending = GetPendingCount();
    if (Pending > 0)
    {
        bWasBuilding = true;
    }
    else if (bWasBuilding)
    {
        bWasBuilding = false;
        const FRingCoords F = URingWorldGeometry::WorldToRing(URingWorldSettings::GetSpec(), Focus);
        UE_LOG(LogTemp, Warning, TEXT("[TM] Ring content ready near arc %.1f km / lateral %.1f km: +%d pieces (%d cells loaded, %d zombies alive)"),
            F.Arc / 1e5, F.Lateral / 1e5, SpawnedTotal - SpawnedAtLastReport, Cells.Num(), Zombies.Num());
        SpawnedAtLastReport = SpawnedTotal;
    }
}

void ARingContentStreamer::UpdateRegion(const FVector& PlayerPos, float DeltaSeconds)
{
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    const URingLaunchComponent* Launch = Pawn ? Pawn->FindComponentByClass<URingLaunchComponent>() : nullptr;
    const bool bFlying = Launch && Launch->IsFlying();

    RegionCheckIn -= DeltaSeconds;
    if (RegionCheckIn <= 0.f && !bFlying)
    {
        RegionCheckIn = 0.25f;
        const FRingCoords C = URingWorldGeometry::WorldToRing(Spec, PlayerPos);
        const FRingRegions& Map = FRingRegions::Get(WorldSeed);
        const FRingRegion& Here = Map.RegionAt(C.Arc, C.Lateral);
        if (&Here != CurrentRegion)
        {
            CurrentRegion = &Here;
            RegionEnteredAt = GetWorld()->GetRealTimeSeconds();
            UE_LOG(LogTemp, Warning, TEXT("[TM] Region: %s (%s, danger %d) at arc %.1f km / lateral %.1f km"), *Here.Name,
                FRingRegions::Biome(Here.Biome).Name, Here.Danger, C.Arc / 1e5, C.Lateral / 1e5);
        }
        float Density;
        Map.Atmosphere(C.Arc, C.Lateral, FogTarget, Density, SunTarget);
        FogDensityTarget = Density;
        if (!bAtmosphereStarted)
        {
            bAtmosphereStarted = true;
            FogNow = FogTarget;
            SunNow = SunTarget;
            FogDensityNow = FogDensityTarget;
        }
    }
    if (!bAtmosphereStarted) return;

    // Ease toward the region's look over a few seconds.
    const float A = 1.f - FMath::Exp(-DeltaSeconds * 0.6f);
    FogNow = FMath::Lerp(FogNow, FogTarget, A);
    SunNow = FMath::Lerp(SunNow, SunTarget, A);
    FogDensityNow = FMath::Lerp(FogDensityNow, FogDensityTarget, A);
    if (UExponentialHeightFogComponent* F = Fog.Get())
    {
        if (AActor* FogActor = F->GetOwner()) FogActor->SetActorLocation(PlayerPos);
        F->SetFogInscatteringColor(FMath::Lerp(OrigFogColor, FogNow * FMath::Max(OrigFogColor.GetLuminance(), 0.05f) / FMath::Max(FogNow.GetLuminance(), 0.05f), 0.65f));
        F->SetFogDensity(OrigFogDensity * FMath::Clamp(FogDensityNow, 0.6f, 1.6f));
    }
    if (ULightComponent* L = Sun.Get())
    {
        L->SetLightColor(OrigSunColor * FMath::Lerp(FLinearColor::White, SunNow, 0.8f));
    }
}

void ARingContentStreamer::UpdateCells(const FVector& Focus)
{
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    const FRingCoords F = URingWorldGeometry::WorldToRing(Spec, Focus);
    const int64 NumArcCells = FMath::Max<int64>(1, FMath::FloorToInt64(URingWorldGeometry::GetCircumference(Spec) / CellSize));
    const int64 FArc = FMath::FloorToInt64(F.Arc / CellSize);
    const int64 FLat = FMath::FloorToInt64(F.Lateral / CellSize);
    const double HalfWidth = Spec.Width * 0.5 - CellSize;

    // Drop cells that are now too far (beyond the load block plus a margin, so edges don't flicker).
    const double Keep = (LoadRadiusCells + 1.5) * CellSize;
    for (auto It = Cells.CreateIterator(); It; ++It)
    {
        const FCell& C = It.Value();
        const double CArc = (C.ArcIdx + 0.5) * CellSize;
        const double CLat = (C.LatIdx + 0.5) * CellSize;
        if (FMath::Abs(URingWorldGeometry::ShortestArcDelta(Spec, F.Arc, CArc)) > Keep || FMath::Abs(CLat - F.Lateral) > Keep)
        {
            UnloadCell(It.Value());
            It.RemoveCurrent();
        }
    }

    // Add the cells around the focus.
    for (int32 DA = -LoadRadiusCells; DA <= LoadRadiusCells; ++DA)
    {
        for (int32 DL = -LoadRadiusCells; DL <= LoadRadiusCells; ++DL)
        {
            const int64 ArcIdx = ((FArc + DA) % NumArcCells + NumArcCells) % NumArcCells;
            const int64 LatIdx = FLat + DL;
            if (FMath::Abs((LatIdx + 0.5) * CellSize) > HalfWidth) continue;   // stay off the rim walls
            const int64 Key = CellKey(ArcIdx, LatIdx);
            if (Cells.Contains(Key)) continue;
            FCell& Cell = Cells.Add(Key);
            Cell.ArcIdx = ArcIdx;
            Cell.LatIdx = LatIdx;
            PlanCell(Cell);
        }
    }
}

void ARingContentStreamer::PlanCell(FCell& Cell)
{
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    const FRingRegions& Map = FRingRegions::Get(WorldSeed);
    const double CArc = (Cell.ArcIdx + 0.5) * CellSize;
    const double CLat = (Cell.LatIdx + 0.5) * CellSize;
    const FTransform Frame = URingWorldGeometry::GetSurfaceFrame(Spec, FRingCoords(CArc, CLat, 0.0));
    const FVector Up = Frame.GetUnitAxis(EAxis::Z);

    // Hand-built areas get nothing.
    for (const TPair<FVector, double>& Zone : AuthoredZones)
    {
        if (FVector::Dist(Zone.Key, Frame.GetLocation()) < Zone.Value + CellSize * 0.75) return;
    }

    // The cell's biome sets everything below.
    const FRingRegion& Region = Map.RegionAt(CArc, CLat);
    const FRingBiomeInfo& Style = FRingRegions::Biome(Region.Biome);
    FRandomStream Rng(CellSeed(WorldSeed, Cell.ArcIdx, Cell.LatIdx, 0xCE11ull));
    const float H = 100.f * Style.PieceHeight * FMath::Lerp(0.85f, 1.15f, Rng.FRand());   // 1 m, scaled per biome
    const float Half = static_cast<float>(CellSize) * 0.5f;

    // Terrain height under a local point (x along the ring, y across, cm).
    auto GroundAt = [&](const FVector& Local) -> double
    {
        const FRingCoords RC = URingWorldGeometry::WorldToRing(Spec, Frame.TransformPosition(FVector(Local.X, Local.Y, 0.0)));
        return Map.Height(RC.Arc, RC.Lateral);
    };
    // Lowest ground under a footprint (so nothing floats on a slope; the uphill side is buried).
    auto BaseAt = [&](const FVector& C, float Yaw, float HalfX, float HalfY) -> double
    {
        const FVector Along = FRotator(0.f, Yaw, 0.f).Vector();
        const FVector Across = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
        HalfX = FMath::Min(HalfX, 4000.f);
        HalfY = FMath::Min(HalfY, 4000.f);
        double G = GroundAt(C);
        G = FMath::Min(G, GroundAt(C + Along * HalfX));
        G = FMath::Min(G, GroundAt(C - Along * HalfX));
        G = FMath::Min(G, GroundAt(C + Across * HalfY));
        G = FMath::Min(G, GroundAt(C - Across * HalfY));
        return G - 30.0;
    };
    constexpr double NoBase = -1.0e30;

    // A box/cylinder of SizeCm on the ground (Base: shared ground height, or NoBase = its own).
    auto Place = [&](bool bCylinder, const FVector& LocalCenterCm, float Yaw, const FVector& SizeCm, int32 ColorIdx,
        float Pitch = 0.f, float Roll = 0.f, double Base = -1.0e30)
    {
        FPlanned P;
        P.bCylinder = bCylinder;
        const double G = Base > NoBase ? Base : BaseAt(LocalCenterCm, Yaw, SizeCm.X * 0.5f, SizeCm.Y * 0.5f);
        const FVector Local(LocalCenterCm.X, LocalCenterCm.Y, LocalCenterCm.Z + G + SizeCm.Z * 0.5f - 50.f);   // sink 50 cm
        const FTransform LocalXf(FRotator(Pitch, Yaw, Roll), Local, SizeCm / 100.f);
        P.Xf = LocalXf * Frame;
        P.Footprint = FMath::Max(SizeCm.X, SizeCm.Y) * 0.5f;
        P.Color = Style.Piece[ColorIdx % 3];
        if (!IsClear(P.Xf.GetLocation(), LandingClearRadius + P.Footprint)) return;
        Cell.Pending.Add(P);
    };
    auto RandIn = [&](float Spread) { return FVector(Rng.FRandRange(-Spread, Spread), Rng.FRandRange(-Spread, Spread), 0.f); };

    const int32 Features = Rng.FRand() < Style.Density ? (Rng.FRand() < Style.Density * 0.5f ? 2 : 1) : 0;
    for (int32 F = 0; F < Features; ++F)
    {
        const FVector C = RandIn(Half * 0.45f);
        const float Yaw = (Rng.FRand() < 0.5f) ? 0.f : 90.f;
        const float Total = Style.WPillars + Style.WWalls + Style.WCanopy + Style.WMesa + Style.WSpikes + Style.WCrystals + Style.WArches;
        float Pick = Rng.FRand() * Total;

        if ((Pick -= Style.WPillars) < 0.f)
        {
            // Pillar forest: swing and whip round them.
            const int32 N = Rng.RandRange(3, 7);
            for (int32 i = 0; i < N; ++i)
            {
                const float D = Rng.FRandRange(200.f, 500.f);
                Place(true, C + RandIn(4000.f), 0.f, FVector(D, D, Rng.FRandRange(12.f, 40.f) * H), i);
            }
        }
        else if ((Pick -= Style.WWalls) < 0.f)
        {
            // Wall maze: a corridor to wall run along, capped by a wall across the end (corners both ways).
            const float Len = Rng.FRandRange(2000.f, 6000.f);
            const float Gap = Rng.FRandRange(1000.f, 1600.f);
            const float WallH = Rng.FRandRange(6.f, 18.f) * H;
            const FVector Along = FRotator(0.f, Yaw, 0.f).Vector();
            const FVector Across = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
            Place(false, C + Across * Gap * 0.5f, Yaw, FVector(Len, 150.f, WallH), 0);
            Place(false, C - Across * Gap * 0.5f + Along * Rng.FRandRange(-800.f, 800.f), Yaw, FVector(Len * Rng.FRandRange(0.6f, 1.f), 150.f, WallH), 1);
            if ((Rng.FRand() < 0.5f))
            {
                Place(false, C + Along * (Len * 0.5f + 75.f), Yaw + 90.f, FVector(Gap + Rng.FRandRange(600.f, 2000.f), 150.f, WallH), 2);
            }
        }
        else if ((Pick -= Style.WCanopy) < 0.f)
        {
            // Canopy: a roof on four legs to ceiling-run under (all on the lowest leg's ground).
            const float S = Rng.FRandRange(3000.f, 6000.f);
            const float Roof = Rng.FRandRange(10.f, 16.f) * H;
            const float Inset = S * 0.5f - 300.f;
            FVector Legs[4];
            double Base = 1.0e30;
            for (int32 i = 0; i < 4; ++i)
            {
                const FVector Corner(i < 2 ? Inset : -Inset, (i % 2) ? Inset : -Inset, 0.f);
                Legs[i] = C + FRotator(0.f, Yaw, 0.f).RotateVector(Corner);
                Base = FMath::Min(Base, BaseAt(Legs[i], 0.f, 100.f, 100.f));
            }
            Place(false, C + FVector(0.f, 0.f, Roof), Yaw, FVector(S, S, 150.f), 0, 0.f, 0.f, Base);
            for (int32 i = 0; i < 4; ++i)
            {
                Place(true, Legs[i], 0.f, FVector(200.f, 200.f, Roof + 50.f), 1, 0.f, 0.f, Base);
            }
        }
        else if ((Pick -= Style.WMesa) < 0.f)
        {
            // Mesas: stepped platforms with a ramp up the first.
            const int32 N = Rng.RandRange(2, 4);
            float Height = Rng.FRandRange(3.f, 6.f) * H;
            FVector P = C;
            for (int32 i = 0; i < N; ++i)
            {
                const float W = Rng.FRandRange(1000.f, 3000.f);
                const double Base = BaseAt(P, Yaw, W * 0.5f, W * 0.5f);
                Place(false, P, Yaw, FVector(W, W, Height), i, 0.f, 0.f, Base);
                if (i == 0)
                {
                    // Ramp leading onto it (20 degrees), from the same ground.
                    const FVector Along = FRotator(0.f, Yaw, 0.f).Vector();
                    const float RampLen = Height / FMath::Sin(FMath::DegreesToRadians(20.f));
                    const FVector Mid = P - Along * (W * 0.5f + RampLen * 0.45f);
                    Place(false, Mid + FVector(0.f, 0.f, Height * 0.5f - 100.f), Yaw, FVector(RampLen, 600.f, 100.f), 2, 20.f, 0.f, Base);
                }
                P += FRotator(0.f, Yaw + Rng.FRandRange(-60.f, 60.f), 0.f).Vector() * W * 0.8f;
                Height += Rng.FRandRange(2.f, 6.f) * H;
            }
        }
        else if ((Pick -= Style.WSpikes) < 0.f)
        {
            // Spikes: leaning shards (grapple anchors, cover, something to weave through).
            const int32 N = Rng.RandRange(3, 8);
            for (int32 i = 0; i < N; ++i)
            {
                const float D = Rng.FRandRange(150.f, 400.f);
                const float Len = Rng.FRandRange(8.f, 30.f) * H;
                Place(Rng.FRand() < 0.5f, C + RandIn(3000.f), Rng.FRandRange(0.f, 360.f), FVector(D, D, Len), i,
                    Rng.FRandRange(-28.f, 28.f), Rng.FRandRange(-28.f, 28.f));
            }
        }
        else if ((Pick -= Style.WCrystals) < 0.f)
        {
            // Crystal cluster: tilted blocks growing out of one spot.
            const int32 N = Rng.RandRange(3, 6);
            const float S = Rng.FRandRange(250.f, 600.f);
            for (int32 i = 0; i < N; ++i)
            {
                const float Scale = Rng.FRandRange(0.6f, 1.4f);
                Place(false, C + RandIn(S * 1.5f), Rng.FRandRange(0.f, 360.f), FVector(S * Scale, S * Scale, S * Scale * Rng.FRandRange(2.f, 5.f)) * (H / 100.f),
                    i, Rng.FRandRange(-35.f, 35.f), Rng.FRandRange(-35.f, 35.f));
            }
        }
        else
        {
            // Arch: two legs and a span to swing under or run across.
            const float Span = Rng.FRandRange(1500.f, 4000.f);
            const float ArchH = Rng.FRandRange(10.f, 25.f) * H;
            const FVector Along = FRotator(0.f, Yaw, 0.f).Vector();
            const FVector L0 = C - Along * Span * 0.5f, L1 = C + Along * Span * 0.5f;
            const double Base = FMath::Min(BaseAt(L0, Yaw, 200.f, 200.f), BaseAt(L1, Yaw, 200.f, 200.f));
            Place(false, L0, Yaw, FVector(400.f, 400.f, ArchH), 0, 0.f, 0.f, Base);
            Place(false, L1, Yaw, FVector(400.f, 400.f, ArchH), 0, 0.f, 0.f, Base);
            Place(false, C + FVector(0.f, 0.f, ArchH), Yaw, FVector(Span + 400.f, 400.f, 300.f), 1, 0.f, 0.f, Base);
        }
    }

    // Cover blocks.
    const int32 Cover = Rng.RandRange(0, 4);
    for (int32 i = 0; i < Cover; ++i)
    {
        const float S = Rng.FRandRange(150.f, 300.f);
        Place(false, RandIn(Half * 0.8f), Rng.FRandRange(0.f, 90.f), FVector(S, S * Rng.FRandRange(0.5f, 2.f), S), i);
    }

    // Zombies, by the region's danger (1-5).
    if (Rng.FRand() < 0.06f * Region.Danger)
    {
        const int32 N = Rng.RandRange(1, 1 + Region.Danger / 2);
        for (int32 i = 0; i < N; ++i)
        {
            FPlanned Z;
            Z.bZombie = true;
            Z.Biome = static_cast<uint8>(Region.Biome);
            const FVector Local = RandIn(Half * 0.8f);
            Z.Xf = FTransform(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), Local + FVector(0.f, 0.f, GroundAt(Local) + 150.0)) * Frame;
            if (IsClear(Z.Xf.GetLocation(), EnemyClearRadius)) Cell.Pending.Add(Z);
        }
    }
    (void)Up;
}

UMaterialInstanceDynamic* ARingContentStreamer::GetColorMaterial(const FLinearColor& Color)
{
    const FColor C = Color.ToFColor(false);
    const uint32 Key = (uint32(C.R) << 16) | (uint32(C.G) << 8) | uint32(C.B);
    if (TObjectPtr<UMaterialInstanceDynamic>* Found = ColorMaterials.Find(Key)) return *Found;
    if (!BaseMaterial) return nullptr;
    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
    MID->SetVectorParameterValue(TEXT("Color"), Color);
    ColorMaterials.Add(Key, MID);
    return MID;
}

void ARingContentStreamer::SpawnSome()
{
    int32 Budget = SpawnBudgetPerTick;
    Zombies.RemoveAll([](const TWeakObjectPtr<AActor>& Z) { return !Z.IsValid(); });
    for (auto& Pair : Cells)
    {
        FCell& Cell = Pair.Value;
        while (Budget > 0 && Cell.Pending.Num() > 0)
        {
            const FPlanned P = Cell.Pending.Pop(EAllowShrinking::No);
            --Budget;
            if (P.bZombie)
            {
                if (Zombies.Num() >= MaxZombies) continue;
                AProceduralZombie* Z = GetWorld()->SpawnActorDeferred<AProceduralZombie>(AProceduralZombie::StaticClass(), P.Xf, nullptr, nullptr,
                    ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
                if (!Z) continue;
                // The biome decides which bodies turn up (Bloats in the Ember Wastes, Gaunts on the ice...).
                const FRingBiomeInfo& B = FRingRegions::Biome(static_cast<ERingBiome>(P.Biome));
                for (FZombieArchetype& A : Z->Archetypes)
                {
                    if (A.Name == TEXT("Husk")) A.Weight *= B.WHusk;
                    else if (A.Name == TEXT("Bloat")) A.Weight *= B.WBloat;
                    else if (A.Name == TEXT("Gaunt")) A.Weight *= B.WGaunt;
                }
                UGameplayStatics::FinishSpawningActor(Z, P.Xf);
                Cell.Actors.Add(Z);
                Zombies.Add(Z);
                Budget -= 3;   // a zombie builds a whole body: count it as several pieces
                continue;
            }
            ARingPiece* Piece = GetWorld()->SpawnActorDeferred<ARingPiece>(ARingPiece::StaticClass(), P.Xf, nullptr, nullptr,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (!Piece) continue;
            Piece->Mesh->SetStaticMesh(P.bCylinder ? CylinderMesh : CubeMesh);
            if (UMaterialInstanceDynamic* MID = GetColorMaterial(P.Color)) Piece->Mesh->SetMaterial(0, MID);
            Piece->FootprintRadius = P.Footprint;
            UGameplayStatics::FinishSpawningActor(Piece, P.Xf);
            Cell.Actors.Add(Piece);
            ++SpawnedTotal;
        }
        if (Budget <= 0) break;
    }
}

void ARingContentStreamer::UnloadCell(FCell& Cell)
{
    const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    for (TWeakObjectPtr<AActor>& A : Cell.Actors)
    {
        AActor* Actor = A.Get();
        if (!Actor) continue;
        // A zombie still chasing you nearby stays.
        if (Pawn && Actor->IsA<AProceduralZombie>() && FVector::Dist(Actor->GetActorLocation(), Pawn->GetActorLocation()) < 30000.0) continue;
        Actor->Destroy();
    }
    Cell.Actors.Reset();
    Cell.Pending.Reset();
}
