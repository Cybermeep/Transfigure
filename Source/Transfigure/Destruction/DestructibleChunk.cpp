// Source/TMovement/Private/Destruction/DestructibleChunk.cpp

#include "Destruction/DestructibleChunk.h"
#include "ProceduralMeshComponent.h"
#include "PhysicsEngine/PhysicsSettings.h"

ADestructibleChunk::ADestructibleChunk()
{
    PrimaryActorTick.bCanEverTick = false;

    ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
    SetRootComponent(ProceduralMesh);

    ProceduralMesh->SetSimulatePhysics(true);
    ProceduralMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    ProceduralMesh->SetCollisionProfileName(TEXT("Destructible"));
    ProceduralMesh->bUseComplexAsSimpleCollision = false;
}

void ADestructibleChunk::InitializeFromMesh(UProceduralMeshComponent* SourceMesh, int32 SectionIndex)
{
    if (!SourceMesh) return;

    FProcMeshSection* Section = SourceMesh->GetProcMeshSection(SectionIndex);
    if (!Section) return;

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FProcMeshTangent> Tangents;

    for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
    {
        Vertices.Add(Vertex.Position);
        Normals.Add(Vertex.Normal);
        UVs.Add(Vertex.UV0);
        Tangents.Add(Vertex.Tangent);
    }

    Triangles.Reset(Section->ProcIndexBuffer.Num());
    for (uint32 Index : Section->ProcIndexBuffer)
    {
        Triangles.Add((int32)Index);
    }

    CollisionData.Vertices = Vertices;
    CollisionData.Triangles = Triangles;
    CollisionData.Normals = Normals;
    CollisionData.UVs = UVs;

    ProceduralMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, TArray<FColor>(), Tangents, true);

    FBoxSphereBounds Bounds = ProceduralMesh->Bounds;
    float Volume = Bounds.BoxExtent.X * Bounds.BoxExtent.Y * Bounds.BoxExtent.Z * 8.0f;
    Mass = Volume * 0.001f;

    ProceduralMesh->SetMassOverrideInKg(NAME_None, Mass);
    ProceduralMesh->SetLinearDamping(0.5f);
    ProceduralMesh->SetAngularDamping(0.5f);

    InitialPosition = GetActorLocation();
    InitialRotation = GetActorRotation();
}

bool ADestructibleChunk::ApplyDamage(FVector HitLocation, float DamageRadius, float DamageForce)
{
    if (!bIsValid || !ProceduralMesh) return false;

    float Distance = FVector::Dist(HitLocation, GetActorLocation());
    if (Distance > DamageRadius) return false;

    float ForceMultiplier = 1.0f - (Distance / DamageRadius);
    FVector Direction = (GetActorLocation() - HitLocation).GetSafeNormal();
    FVector Force = Direction * DamageForce * ForceMultiplier;

    ApplyPhysics(Force, HitLocation);

    if (ForceMultiplier > 0.5f)
    {
        bIsValid = false;
        SetLifeSpan(5.0f);
    }

    return true;
}

void ADestructibleChunk::ApplyPhysics(FVector Force, FVector Location)
{
    if (!ProceduralMesh) return;

    ProceduralMesh->AddImpulse(Force, NAME_None, true);
}

void ADestructibleChunk::OnPhysicsSleep()
{
    if (ProceduralMesh)
    {
        ProceduralMesh->SetSimulatePhysics(false);
    }
}