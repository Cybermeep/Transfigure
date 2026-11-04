// Source/Transfigure/Limb/LimbTypes.h
//
// Data for procedurally generated body parts. One FLimbPartSpec describes a
// part (which bones it spans, its radius profile, health). Mutations are
// rolled per zombie from a seed into an FLimbShape, which the generator turns
// into a mesh. The same numbers give the hit-zone capsule and joint points.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "LimbTypes.generated.h"

UENUM(BlueprintType)
enum class ELimbPart : uint8
{
    Head,
    Torso,
    UpperArmL,
    LowerArmL,
    UpperArmR,
    LowerArmR,
    ThighL,
    CalfL,
    ThighR,
    CalfR,
    None UMETA(Hidden)
};

/** Designer-facing description of one body part. */
USTRUCT(BlueprintType)
struct FLimbPartSpec
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb")
    ELimbPart Part = ELimbPart::Torso;

    /** The part attaches to (and moves with) this bone. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb")
    FName StartBone;

    /** The part ends at this bone. None = continue along the parent->StartBone direction for ExtraLength. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb")
    FName EndBone;

    /** Extra length past EndBone (hands, feet, top of the head). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb")
    float ExtraLength = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb|Shape")
    float StartRadius = 6.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb|Shape")
    float EndRadius = 5.f;

    /** Extra radius in the middle of the part (muscle / belly). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb|Shape")
    float Bulge = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb|Damage")
    float MaxHealth = 40.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb|Damage")
    bool bSeverable = true;

    /** Damage dealt to the body when this part is hit is multiplied by this (head = 2). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb|Damage")
    float BodyDamageMultiplier = 1.f;

    /** Severing the parent also takes this part with it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limb")
    ELimbPart ParentPart = ELimbPart::None;
};

/** Bounded random mutations applied on top of the spec. */
USTRUCT(BlueprintType)
struct FLimbMutationRanges
{
    GENERATED_BODY()

    /** Whole-body thickness multiplier range. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mutation")
    FVector2D BodyFatness = FVector2D(0.9f, 1.15f);

    /** Per-part extra thickness multiplier range (asymmetry). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mutation")
    FVector2D PartThickness = FVector2D(0.85f, 1.25f);

    /** Added to each part's bulge (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mutation")
    FVector2D BulgeOffset = FVector2D(-1.f, 3.f);

    /** Sideways bend of the part as a fraction of its length. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mutation")
    FVector2D Bend = FVector2D(0.f, 0.08f);

    /** Surface lumpiness (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mutation")
    FVector2D Lumpiness = FVector2D(0.4f, 1.8f);
};

/** Fully resolved shape of one part, in the StartBone's local space. */
struct FLimbShape
{
    FVector Start = FVector::ZeroVector;
    FVector End = FVector::ForwardVector;
    FVector BendOffset = FVector::ZeroVector;   // control-point offset for the curve
    float StartRadius = 5.f;
    float EndRadius = 5.f;
    float Bulge = 0.f;
    float Lumpiness = 0.f;
    int32 NoiseSeed = 0;
    int32 Rings = 12;
    int32 Sides = 12;

    float MaxRadius() const { return FMath::Max(StartRadius, EndRadius) + FMath::Max(Bulge, 0.f) + Lumpiness; }
};

/** Generated triangle mesh ready for UProceduralMeshComponent. */
struct FLimbMesh
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FProcMeshTangent> Tangents;
};
