// Source/Transfigure/Ringworld/RingContentStreamer.cpp

#include "RingContentStreamer.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"
#include "RingLaunchComponent.h"
#include "RingLaunchPad.h"
#include "Limb/ProceduralZombie.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

TWeakObjectPtr<ARingContentStreamer> ARingContentStreamer::Instance;

namespace
{
    // Stable 64-bit mix (splitmix64). Never use GetTypeHash/std::hash for seeds: they can change.
    uint64 Mix(uint64 X)
    {
        X += 0x9E3779B97F4A7C15ull;
        X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
        X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
        return X ^ (X >> 31);
    }
    int32 Seed3(int32 World, int64 A, int64 B, uint64 Salt)
    {
        return static_cast<int32>(Mix(Mix(Mix(static_cast<uint64>(World) ^ Salt) ^ static_cast<uint64>(A)) ^ static_cast<uint64>(B)) & 0x7FFFFFFF);
    }

    /** Look and mix of one 50 km region. */
    struct FRegionStyle
    {
        float Density = 0.6f;
        float Height = 1.f;
        float Danger = 0.3f;
        float WPillars = 1.f, WWalls = 1.f, WCanopy = 1.f, WMesa = 1.f;
        FLinearColor Colors[3];
    };

    FRegionStyle MakeRegionStyle(int32 WorldSeed, int64 RegionArc, int64 RegionLat)
    {
        FRandomStream R(Seed3(WorldSeed, RegionArc, RegionLat, 0x5EED0001ull));
        FRegionStyle S;
        S.Density = R.FRandRange(0.35f, 0.95f);
        S.Height = R.FRandRange(0.7f, 1.6f);
        S.Danger = R.FRand();
        S.WPillars = R.FRandRange(0.1f, 1.f);
        S.WWalls = R.FRandRange(0.1f, 1.f);
        S.WCanopy = R.FRandRange(0.1f, 1.f);
        S.WMesa = R.FRandRange(0.1f, 1.f);
        const float Hue = R.FRandRange(0.f, 360.f);
        for (int32 i = 0; i < 3; ++i)
        {
            const FLinearColor HSV(FMath::Fmod(Hue + i * R.FRandRange(15.f, 40.f), 360.f), R.FRandRange(0.2f, 0.45f), R.FRandRange(0.3f, 0.7f));
            S.Colors[i] = HSV.HSVToLinearRGB();
        }
        return S;
    }
}

// ── pieces / zones ──────────────────────────────────────────────────────────

ARingPiece::ARingPiece()
{
    PrimaryActorTick.bCanEverTick = false;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Static);
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
    UE_LOG(LogTemp, Warning, TEXT("[TM] Ring content: seed %d, %.0f m cells, %d authored zone(s)"), WorldSeed, CellSize / 100.0, AuthoredZones.Num());
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

    FVector Focus = FocusOverride;
    if (!bFocusOverride)
    {
        const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
        if (!Pawn) return;
        Focus = Pawn->GetActorLocation();
    }
    UpdateCells(Focus);
    SpawnSome();

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
    const double CArc = (Cell.ArcIdx + 0.5) * CellSize;
    const double CLat = (Cell.LatIdx + 0.5) * CellSize;
    const FTransform Frame = URingWorldGeometry::GetSurfaceFrame(Spec, FRingCoords(CArc, CLat, 0.0));

    // Hand-built areas get nothing.
    for (const TPair<FVector, double>& Zone : AuthoredZones)
    {
        if (FVector::Dist(Zone.Key, Frame.GetLocation()) < Zone.Value + CellSize * 0.75) return;
    }

    const int64 RegionCells = FMath::Max<int64>(1, FMath::RoundToInt64(5.0e6 / CellSize));   // 50 km regions
    const FRegionStyle Style = MakeRegionStyle(WorldSeed,
        FMath::FloorToInt64(static_cast<double>(Cell.ArcIdx) / RegionCells), FMath::FloorToInt64(static_cast<double>(Cell.LatIdx) / RegionCells));
    FRandomStream Rng(Seed3(WorldSeed, Cell.ArcIdx, Cell.LatIdx, 0xCE11ull));
    const float H = 100.f * Style.Height;   // 1 m, scaled by the region's height
    const float Half = static_cast<float>(CellSize) * 0.5f;

    // Local (x along the ring, y across, z up) box/cylinder of SizeM metres, sitting on the ground.
    auto Place = [&](bool bCylinder, const FVector& LocalCenterCm, float Yaw, const FVector& SizeCm, int32 ColorIdx, float Pitch = 0.f)
    {
        FPlanned P;
        P.bCylinder = bCylinder;
        const FVector Local(LocalCenterCm.X, LocalCenterCm.Y, LocalCenterCm.Z + SizeCm.Z * 0.5f - 50.f);   // sink 50 cm
        const FTransform LocalXf(FRotator(Pitch, Yaw, 0.f), Local, SizeCm / 100.f);
        P.Xf = LocalXf * Frame;
        P.Footprint = FMath::Max(SizeCm.X, SizeCm.Y) * 0.5f;
        P.Color = Style.Colors[ColorIdx % 3];
        if (!IsClear(P.Xf.GetLocation(), LandingClearRadius + P.Footprint)) return;
        Cell.Pending.Add(P);
    };
    auto RandIn = [&](float Spread) { return FVector(Rng.FRandRange(-Spread, Spread), Rng.FRandRange(-Spread, Spread), 0.f); };

    const int32 Features = Rng.FRand() < Style.Density ? (Rng.FRand() < Style.Density * 0.5f ? 2 : 1) : 0;
    for (int32 F = 0; F < Features; ++F)
    {
        const FVector C = RandIn(Half * 0.45f);
        const float Yaw = (Rng.FRand() < 0.5f) ? 0.f : 90.f;
        const float Total = Style.WPillars + Style.WWalls + Style.WCanopy + Style.WMesa;
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
            // Canopy: a roof on four legs to ceiling-run under.
            const float S = Rng.FRandRange(3000.f, 6000.f);
            const float Roof = Rng.FRandRange(10.f, 16.f) * H;
            Place(false, C + FVector(0.f, 0.f, Roof), Yaw, FVector(S, S, 150.f), 0);
            const float Inset = S * 0.5f - 300.f;
            for (int32 i = 0; i < 4; ++i)
            {
                const FVector Corner(i < 2 ? Inset : -Inset, (i % 2) ? Inset : -Inset, 0.f);
                Place(true, C + FRotator(0.f, Yaw, 0.f).RotateVector(Corner), 0.f, FVector(200.f, 200.f, Roof + 50.f), 1);
            }
        }
        else
        {
            // Mesas: stepped platforms with a ramp up the first.
            const int32 N = Rng.RandRange(2, 4);
            float Height = Rng.FRandRange(3.f, 6.f) * H;
            FVector P = C;
            for (int32 i = 0; i < N; ++i)
            {
                const float W = Rng.FRandRange(1000.f, 3000.f);
                Place(false, P, Yaw, FVector(W, W, Height), i);
                if (i == 0)
                {
                    // Ramp leading onto it (20 degrees).
                    const FVector Along = FRotator(0.f, Yaw, 0.f).Vector();
                    const float RampLen = Height / FMath::Sin(FMath::DegreesToRadians(20.f));
                    const FVector Mid = P - Along * (W * 0.5f + RampLen * 0.45f);
                    Place(false, Mid + FVector(0.f, 0.f, Height * 0.5f - 100.f), Yaw, FVector(RampLen, 600.f, 100.f), 2, 20.f);
                }
                P += FRotator(0.f, Yaw + Rng.FRandRange(-60.f, 60.f), 0.f).Vector() * W * 0.8f;
                Height += Rng.FRandRange(2.f, 6.f) * H;
            }
        }
    }

    // Cover blocks.
    const int32 Cover = Rng.RandRange(0, 4);
    for (int32 i = 0; i < Cover; ++i)
    {
        const float S = Rng.FRandRange(150.f, 300.f);
        Place(false, RandIn(Half * 0.8f), Rng.FRandRange(0.f, 90.f), FVector(S, S * Rng.FRandRange(0.5f, 2.f), S), i);
    }

    // Zombies, by the region's danger.
    if (Rng.FRand() < Style.Danger * 0.5f)
    {
        const int32 N = Rng.RandRange(1, 3);
        for (int32 i = 0; i < N; ++i)
        {
            FPlanned Z;
            Z.bZombie = true;
            Z.Xf = FTransform(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), RandIn(Half * 0.8f) + FVector(0.f, 0.f, 150.f)) * Frame;
            if (IsClear(Z.Xf.GetLocation(), EnemyClearRadius)) Cell.Pending.Add(Z);
        }
    }
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
                FActorSpawnParameters Params;
                Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
                if (AProceduralZombie* Z = GetWorld()->SpawnActor<AProceduralZombie>(AProceduralZombie::StaticClass(), P.Xf, Params))
                {
                    Cell.Actors.Add(Z);
                    Zombies.Add(Z);
                    Budget -= 3;   // a zombie builds a whole body: count it as several pieces
                }
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
