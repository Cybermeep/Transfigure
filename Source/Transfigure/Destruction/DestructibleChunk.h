// Source/TMovement/Public/Destruction/DestructibleChunk.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DestructibleChunk.generated.h"

class UProceduralMeshComponent;

USTRUCT(BlueprintType)
struct FChunkCollisionData
{
    GENERATED_BODY()

        UPROPERTY()
        TArray<FVector> Vertices;

    UPROPERTY()
        TArray<int32> Triangles;

    UPROPERTY()
        TArray<FVector> Normals;

    UPROPERTY()
        TArray<FVector2D> UVs;
};

UCLASS()
class TRANSFIGURE_API ADestructibleChunk : public AActor
{
    GENERATED_BODY()

public:
    ADestructibleChunk();

    UFUNCTION(BlueprintCallable, Category = "Destruction")
        void InitializeFromMesh(UProceduralMeshComponent* SourceMesh, int32 SectionIndex);

    UFUNCTION(BlueprintCallable, Category = "Destruction")
        bool ApplyDamage(FVector HitLocation, float DamageRadius, float DamageForce);

    UFUNCTION(BlueprintPure, Category = "Destruction")
        bool IsValid() const { return bIsValid; }

    UFUNCTION(BlueprintPure, Category = "Destruction")
        FChunkCollisionData GetCollisionData() const { return CollisionData; }

protected:
    UPROPERTY(VisibleAnywhere)
        TObjectPtr<UProceduralMeshComponent> ProceduralMesh;

    UPROPERTY()
        FChunkCollisionData CollisionData;

    UPROPERTY()
        bool bIsValid = true;

    UPROPERTY()
        float Mass = 10.0f;

    UPROPERTY()
        FVector InitialPosition;

    UPROPERTY()
        FRotator InitialRotation;

    UFUNCTION()
        void ApplyPhysics(FVector Force, FVector Location);

    UFUNCTION()
        void OnPhysicsSleep();
};