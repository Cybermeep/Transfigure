// Source/Transfigure/Ringworld/RingNearField.cpp

#include "RingNearField.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"
#include "RingLaunchComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ARingFloorTile::ARingFloorTile()
{
    PrimaryActorTick.bCanEverTick = false;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded())
    {
        Mesh->SetStaticMesh(Cube.Object);
    }
    Mesh->SetMobility(EComponentMobility::Static);
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    Mesh->SetCanEverAffectNavigation(false);
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
    const bool bFar = !Tile
        || FMath::Abs(URingWorldGeometry::ShortestArcDelta(Spec, TileArc, Coords.Arc)) > RecenterDistance
        || FMath::Abs(Coords.Lateral - TileLateral) > RecenterDistance;
    if (bFar)
    {
        Recenter(Coords.Arc, Coords.Lateral);
    }
}

void ARingNearField::Recenter(double Arc, double Lateral)
{
    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    // Stay on the band (the rim walls are at +-Width/2).
    const double MaxLateral = FMath::Max(Spec.Width * 0.5 - TileSize * 0.5, 0.0);
    Lateral = FMath::Clamp(Lateral, -MaxLateral, MaxLateral);

    FTransform Xf = URingWorldGeometry::GetSurfaceFrame(Spec, FRingCoords(Arc, Lateral, -(SurfaceSink + TileThickness * 0.5)));
    Xf.SetScale3D(FVector(TileSize / 100.0, TileSize / 100.0, TileThickness / 100.0));   // engine cube is 100 cm

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ARingFloorTile* NewTile = GetWorld()->SpawnActor<ARingFloorTile>(ARingFloorTile::StaticClass(), Xf, Params);
    if (Tile)
    {
        Tile->Destroy();
    }
    Tile = NewTile;
    TileArc = Arc;
    TileLateral = Lateral;
    UE_LOG(LogTemp, Warning, TEXT("[TM] Ring floor tile at arc %.1f km, lateral %.1f km"), Arc / 1e5, Lateral / 1e5);
}
