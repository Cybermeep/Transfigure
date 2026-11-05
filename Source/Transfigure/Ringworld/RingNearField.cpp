// Source/Transfigure/Ringworld/RingNearField.cpp

#include "RingNearField.h"
#include "RingRegions.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"
#include "RingLaunchComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "HAL/PlatformTime.h"

ARingFloorTile::ARingFloorTile()
{
    PrimaryActorTick.bCanEverTick = false;
    Terrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Terrain"));
    SetRootComponent(Terrain);
    Terrain->SetCollisionProfileName(TEXT("BlockAll"));
    Terrain->bUseComplexAsSimpleCollision = true;
    Terrain->SetCanEverAffectNavigation(false);
    Terrain->SetCastShadow(true);
}

void ARingFloorTile::Build(int32 WorldSeed, double CArc, double CLat, double Size, double Step, double EdgeFade,
    double Sink, bool bAsyncCollision, UMaterialInterface* Material)
{
    const double Start = FPlatformTime::Seconds();
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    const FRingRegions& Map = FRingRegions::Get(WorldSeed);
    const int32 N = FMath::Max(2, FMath::RoundToInt(Size / Step));   // quads per side
    const int32 V = N + 1;
    const double Half = Size * 0.5;
    const FTransform Xf = GetActorTransform();

    TArray<FVector> Pos;
    TArray<FColor> Colors;
    TArray<FVector> Normals;
    TArray<int32> Tris;
    Pos.SetNumUninitialized(V * V);
    Colors.SetNumUninitialized(V * V);
    Normals.SetNumUninitialized(V * V);
    Tris.Reserve(N * N * 6);

    double MaxH = 0.0;
    for (int32 j = 0; j < V; ++j)
    {
        const double DL = -Half + j * Step;
        for (int32 i = 0; i < V; ++i)
        {
            const double DA = -Half + i * Step;
            const double Arc = URingWorldGeometry::WrapArc(Spec, CArc + DA);
            const double Lat = CLat + DL;
            double H;
            FLinearColor C;
            Map.HeightAndColor(Arc, Lat, H, C);
            // Fade to flat at the edge, where the tile meets the band.
            const double Edge = FMath::Min(Half - FMath::Abs(DA), Half - FMath::Abs(DL));
            const double T = FMath::Clamp(Edge / EdgeFade, 0.0, 1.0);
            H *= T * T * (3.0 - 2.0 * T);
            MaxH = FMath::Max(MaxH, H);
            Pos[j * V + i] = Xf.InverseTransformPosition(URingWorldGeometry::RingToWorld(Spec, FRingCoords(Arc, Lat, H - Sink)));
            // Linear values straight into the vertex colour (the material reads them as they are).
            Colors[j * V + i] = FColor(
                static_cast<uint8>(FMath::Clamp(C.R, 0.f, 1.f) * 255.f),
                static_cast<uint8>(FMath::Clamp(C.G, 0.f, 1.f) * 255.f),
                static_cast<uint8>(FMath::Clamp(C.B, 0.f, 1.f) * 255.f), 255);
        }
    }

    for (int32 j = 0; j < V; ++j)
    {
        for (int32 i = 0; i < V; ++i)
        {
            const FVector DX = Pos[j * V + FMath::Min(i + 1, N)] - Pos[j * V + FMath::Max(i - 1, 0)];
            const FVector DY = Pos[FMath::Min(j + 1, N) * V + i] - Pos[FMath::Max(j - 1, 0) * V + i];
            FVector Nrm = FVector::CrossProduct(DX, DY).GetSafeNormal();
            if (Nrm.Z < 0.f) Nrm = -Nrm;   // local +Z is ring-up
            Normals[j * V + i] = Nrm;
        }
    }

    // Local X runs along the ring (i), Y across it (j); both triangles' (B-A)x(C-A) point up.
    for (int32 j = 0; j < N; ++j)
    {
        for (int32 i = 0; i < N; ++i)
        {
            const int32 A = j * V + i, B = A + 1, C = A + V, D = A + V + 1;
            Tris.Append({ A, B, D, A, D, C });
        }
    }

    Terrain->bUseAsyncCooking = bAsyncCollision;
    Terrain->CreateMeshSection(0, Pos, Tris, Normals, TArray<FVector2D>(), Colors, TArray<FProcMeshTangent>(), true);
    if (Material) Terrain->SetMaterial(0, Material);

    UE_LOG(LogTemp, Warning, TEXT("[TM] Ring terrain tile: %s, %dx%d, highest %.0f m, built in %.0f ms%s"),
        *Map.RegionAt(CArc, CLat).Name, V, V, MaxH / 100.0, (FPlatformTime::Seconds() - Start) * 1000.0,
        bAsyncCollision ? TEXT(" (collision in background)") : TEXT(""));
}

ARingNearField::ARingNearField()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ARingNearField::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Pawn) return;
    // Mid-launch the player crosses thousands of km of empty interior: wait for the landing.
    const URingLaunchComponent* Launch = Pawn->FindComponentByClass<URingLaunchComponent>();
    if (Launch && Launch->IsFlying()) return;

    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    if (Spec.bFlatWorldForTesting) return;

    const FRingCoords Coords = URingWorldGeometry::WorldToRing(Spec, Pawn->GetActorLocation());
    if (!Tile)
    {
        Recenter(Coords.Arc, Coords.Lateral, false);
        return;
    }
    const double DA = FMath::Abs(URingWorldGeometry::ShortestArcDelta(Spec, TileArc, Coords.Arc));
    const double DL = FMath::Abs(Coords.Lateral - TileLateral);
    if (DA > RecenterDistance || DL > RecenterDistance)
    {
        // Still on the old tile (walking on): cook the new one's collision in the background and keep
        // the old one a moment. Off it (teleport, landing): cook now so there's ground to land on.
        const double Inner = TileSize * 0.5 - EdgeFade;
        Recenter(Coords.Arc, Coords.Lateral, DA < Inner && DL < Inner);
    }
}

void ARingNearField::Recenter(double Arc, double Lateral, bool bAsyncCollision)
{
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    // Stay on the band (the rim walls are at +-Width/2).
    const double MaxLateral = FMath::Max(Spec.Width * 0.5 - TileSize * 0.5, 0.0);
    Lateral = FMath::Clamp(Lateral, -MaxLateral, MaxLateral);

    if (!GroundMaterial)
    {
        GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RingWorld/M_RingGround.M_RingGround"), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!GroundMaterial) GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    }

    const FTransform Xf = URingWorldGeometry::GetSurfaceFrame(Spec, FRingCoords(Arc, Lateral, 0.0));
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ARingFloorTile* NewTile = GetWorld()->SpawnActor<ARingFloorTile>(ARingFloorTile::StaticClass(), Xf, Params);
    if (!NewTile) return;
    NewTile->Build(WorldSeed, Arc, Lateral, TileSize, GridStep, EdgeFade, SurfaceSink, bAsyncCollision, GroundMaterial);

    if (ARingFloorTile* Old = Tile.Get())
    {
        if (bAsyncCollision)
        {
            // Keep the old ground (collision only) until the new collision has cooked.
            Old->Terrain->SetVisibility(false);
            Old->SetLifeSpan(1.5f);
        }
        else
        {
            Old->Destroy();
        }
    }
    Tile = NewTile;
    TileArc = Arc;
    TileLateral = Lateral;
}
