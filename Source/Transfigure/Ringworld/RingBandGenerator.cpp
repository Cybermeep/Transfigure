// RingBandGenerator.cpp

#include "RingBandGenerator.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"
#include "RingNearField.h"
#include "RingContentStreamer.h"
#include "RingRegions.h"
#include "RingLaunchPad.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"

#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

using namespace UE::Geometry;

ARingBandGenerator::ARingBandGenerator()
{
    PrimaryActorTick.bCanEverTick = false;

    DynamicMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("DynamicMeshComponent"));
    RootComponent = DynamicMeshComponent;
}

void ARingBandGenerator::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    GenerateRingBand();
}

void ARingBandGenerator::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    // Runs before any actor's BeginPlay, so everything that reads the spec this
    // session sees ring gravity. Game worlds only (not the editor's own world).
    UWorld* World = GetWorld();
    if (bActivateRingWorld && World && World->IsGameWorld())
    {
        URingWorldSettings* Settings = GetMutableDefault<URingWorldSettings>();
        bSavedFlatFlag = Settings->Spec.bFlatWorldForTesting;
        Settings->Spec.bFlatWorldForTesting = false;
        bChangedFlatFlag = true;
        UE_LOG(LogTemp, Warning, TEXT("[TM] Ring world ON for this map: curved gravity, radius %.0f km"), Settings->Spec.Radius / 1e5);
    }
}

void ARingBandGenerator::BeginPlay()
{
    Super::BeginPlay();
    if (bChangedFlatFlag)
    {
        // Hand-built areas and pads stay flat (the terrain reads these; reset every play).
        const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
        FRingRegions::ResetFlatten();
        for (TActorIterator<ARingAuthoredZone> It(GetWorld()); It; ++It)
        {
            const FRingCoords C = URingWorldGeometry::WorldToRing(Spec, It->GetActorLocation());
            FRingRegions::AddFlatten(C.Arc, C.Lateral, It->Radius, 150000.0);
        }
        for (TActorIterator<ARingLaunchPad> It(GetWorld()); It; ++It)
        {
            const FRingCoords C = URingWorldGeometry::WorldToRing(Spec, It->GetActorLocation());
            FRingRegions::AddFlatten(C.Arc, C.Lateral, 3000.0, 8000.0);
        }
        if (ARingNearField* NearField = GetWorld()->SpawnActor<ARingNearField>(ARingNearField::StaticClass(), FTransform::Identity))
        {
            NearField->WorldSeed = WorldSeed;
        }
        UE_LOG(LogTemp, Warning, TEXT("[TM] Ring regions: %d regions from seed %d"), FRingRegions::Get(WorldSeed).GetRegions().Num(), WorldSeed);
        if (bGenerateContent)
        {
            FActorSpawnParameters Params;
            Params.bDeferConstruction = true;
            if (ARingContentStreamer* Streamer = GetWorld()->SpawnActor<ARingContentStreamer>(ARingContentStreamer::StaticClass(), FTransform::Identity, Params))
            {
                Streamer->WorldSeed = WorldSeed;
                Streamer->FinishSpawning(FTransform::Identity);
            }
        }
    }
}

void ARingBandGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (bChangedFlatFlag)
    {
        // Put the project setting back so flat test maps stay flat in the same editor session.
        GetMutableDefault<URingWorldSettings>()->Spec.bFlatWorldForTesting = bSavedFlatFlag;
        bChangedFlatFlag = false;
    }
    Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void ARingBandGenerator::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (bRegenerateOnPropertyChange)
    {
        GenerateRingBand();
    }
}
#endif

void ARingBandGenerator::GenerateRingBand()
{
    if (!DynamicMeshComponent)
    {
        return;
    }

    // This actor must sit at the world origin with no rotation: the ring
    // coordinate convention (spin axis = world +Y) assumes it, and so does
    // every other system reading FRingWorldSpec.
    if (!GetActorLocation().IsNearlyZero() || !GetActorRotation().IsNearlyZero())
    {
        UE_LOG(LogTemp, Warning, TEXT("ARingBandGenerator: reset transform to origin/identity for correct alignment."));
    }

    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    const int32 N = FMath::Clamp(RadialSegments, 16, 8192);

    // Cross-section profile, in (radius-from-axis, lateral) pairs, ordered
    // left-wall-top -> left-edge -> right-edge -> right-wall-top. This is an
    // open polyline; revolving it 360 degrees produces the band plus rim
    // walls without end caps, which is what we want for a closed loop.
    // Drawn slightly below the true surface so near-field and level floors cover it.
    const double R = Spec.Radius + BandSink;
    const double HalfWidth = Spec.Width * 0.5;
    const double WallTopR = Spec.Radius - Spec.RimWallHeight;

    // Wall top, wall foot, LateralStrips+1 floor points, wall foot, wall top. The feet are
    // doubled with the floor edge so walls keep their own colour.
    const int32 Strips = FMath::Clamp(LateralStrips, 1, 64);
    TArray<double> ProfileR, ProfileY;
    TArray<bool> ProfileWall;
    ProfileR.Add(WallTopR); ProfileY.Add(-HalfWidth); ProfileWall.Add(true);
    ProfileR.Add(R); ProfileY.Add(-HalfWidth); ProfileWall.Add(true);
    for (int32 k = 0; k <= Strips; ++k)
    {
        ProfileR.Add(R);
        ProfileY.Add(-HalfWidth + Spec.Width * k / Strips);
        ProfileWall.Add(false);
    }
    ProfileR.Add(R); ProfileY.Add(HalfWidth); ProfileWall.Add(true);
    ProfileR.Add(WallTopR); ProfileY.Add(HalfWidth); ProfileWall.Add(true);
    const int32 ProfileCount = ProfileR.Num();
    const FRingRegions& Map = FRingRegions::Get(WorldSeed);
    const FVector4f WallColor(0.05f, 0.055f, 0.07f, 1.f);

    DynamicMeshComponent->GetDynamicMesh()->EditMesh([&](FDynamicMesh3& Mesh)
    {
        Mesh.Clear();
        Mesh.EnableAttributes();
        Mesh.Attributes()->EnablePrimaryColors();
        FDynamicMeshColorOverlay* Colors = Mesh.Attributes()->PrimaryColors();

        // One ring of ProfileCount vertices per angular step.
        for (int32 Step = 0; Step < N; ++Step)
        {
            const double Theta = (2.0 * UE_DOUBLE_PI * Step) / N;
            const double S = FMath::Sin(Theta);
            const double C = FMath::Cos(Theta);

            for (int32 P = 0; P < ProfileCount; ++P)
            {
                // Matches URingWorldGeometry::RingToWorld's convention exactly:
                // X = r*sin(theta), Y = lateral, Z = -r*cos(theta).
                const double X = ProfileR[P] * S;
                const double Y = ProfileY[P];
                const double Z = -ProfileR[P] * C;

                Mesh.AppendVertex(FVector3d(X, Y, Z));
                // One colour element per vertex, in the same order (element id = vertex id).
                if (ProfileWall[P])
                {
                    Colors->AppendElement(WallColor);
                }
                else
                {
                    const FLinearColor MapC = Map.MapColor(Theta * Spec.Radius, ProfileY[P]);
                    Colors->AppendElement(FVector4f(MapC.R, MapC.G, MapC.B, 1.f));
                }
            }
        }

        // Connect each angular step to the next, wrapping the last back to
        // the first to close the loop with no seam.
        for (int32 Step = 0; Step < N; ++Step)
        {
            const int32 NextStep = (Step + 1) % N;

            for (int32 P = 0; P < ProfileCount - 1; ++P)
            {
                // Wall foot -> floor edge is the same point twice: no quad there.
                if (P == 1 || P == ProfileCount - 3) continue;
                const int32 A = Step * ProfileCount + P;
                const int32 B = Step * ProfileCount + P + 1;
                const int32 C2 = NextStep * ProfileCount + P + 1;
                const int32 D = NextStep * ProfileCount + P;

                // Two triangles per quad, wound so normals face toward the
                // axis (inward, toward the habitable side you stand on).
                const int32 T0 = Mesh.AppendTriangle(A, B, C2);
                const int32 T1 = Mesh.AppendTriangle(A, C2, D);
                if (T0 >= 0) Colors->SetTriangle(T0, FIndex3i(A, B, C2));
                if (T1 >= 0) Colors->SetTriangle(T1, FIndex3i(A, C2, D));
            }
        }

        FMeshNormals::QuickComputeVertexNormals(Mesh);
    });

    if (UMaterialInterface* BandMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RingWorld/M_RingBand.M_RingBand"), nullptr, LOAD_NoWarn | LOAD_Quiet))
    {
        DynamicMeshComponent->SetMaterial(0, BandMat);
    }

    UE_LOG(LogTemp, Display, TEXT("RingBandGenerator: built %d triangles, %d regions"), N * (ProfileCount - 3) * 2, Map.GetRegions().Num());
}
