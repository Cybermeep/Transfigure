// RingBandGenerator.cpp

#include "RingBandGenerator.h"
#include "RingWorldGeometry.h"
#include "RingWorldSettings.h"
#include "RingNearField.h"
#include "Engine/World.h"

#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"

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
        GetWorld()->SpawnActor<ARingNearField>(ARingNearField::StaticClass(), FTransform::Identity);
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

    const double ProfileR[4] = { WallTopR, R, R, WallTopR };
    const double ProfileY[4] = { -HalfWidth, -HalfWidth, HalfWidth, HalfWidth };
    constexpr int32 ProfileCount = 4;

    DynamicMeshComponent->GetDynamicMesh()->EditMesh([&](FDynamicMesh3& Mesh)
    {
        Mesh.Clear();
        Mesh.EnableAttributes();

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
            }
        }

        // Connect each angular step to the next, wrapping the last back to
        // the first to close the loop with no seam.
        for (int32 Step = 0; Step < N; ++Step)
        {
            const int32 NextStep = (Step + 1) % N;

            for (int32 P = 0; P < ProfileCount - 1; ++P)
            {
                const int32 A = Step * ProfileCount + P;
                const int32 B = Step * ProfileCount + P + 1;
                const int32 C2 = NextStep * ProfileCount + P + 1;
                const int32 D = NextStep * ProfileCount + P;

                // Two triangles per quad, wound so normals face toward the
                // axis (inward, toward the habitable side you stand on).
                Mesh.AppendTriangle(A, B, C2);
                Mesh.AppendTriangle(A, C2, D);
            }
        }

        FMeshNormals::QuickComputeVertexNormals(Mesh);
    });

    UE_LOG(LogTemp, Display, TEXT("RingBandGenerator: built %d triangles"), N * (ProfileCount - 1) * 2);
}
