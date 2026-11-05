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
    int32 Rings = 16;
    int32 Sides = 14;

    /** Cross-section scale along WideAxis (Width) and across it (Depth): torsos are wider than deep. */
    float Width = 1.f;
    float Depth = 1.f;
    /** Part-local direction the Width runs along. Zero = no preference. */
    FVector WideAxis = FVector::ZeroVector;

    /** Where along the part (0..1) the muscle/belly bulge peaks. */
    float BulgeCenter = 0.5f;
    /** How much the part narrows just inside each joint (0..0.4). */
    float Pinch = 0.f;
    /** End-cap roundness: ~0.35 rounded, ~0.05 flat. */
    float CapExponent = 0.35f;

    /** Hard-edged machine part: few sides, ribbed segments, no lumps. */
    bool bMechanical = false;

    /** Which vertex-colour pattern to paint: flesh (veins/bruises), growth (pulse phase), metal (rust). */
    enum class EStyle : uint8 { Flesh, Growth, Metal };
    EStyle Style = EStyle::Flesh;

    /** Strength of the fine wrinkle noise, as a fraction of Lumpiness. */
    float Wrinkle = 0.35f;
    int32 Segments = 3;

    float MaxRadius() const
    {
        return (FMath::Max(StartRadius, EndRadius) + FMath::Max(Bulge, 0.f) + Lumpiness) * FMath::Max(Width, Depth)
            * (bMechanical ? 1.15f : 1.f);
    }
};

/** A body form. Each spawn picks one (by Weight) and then mutates it. */
USTRUCT(BlueprintType)
struct FZombieArchetype
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype")
    FName Name = TEXT("Husk");

    /** Relative chance of being picked. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype")
    float Weight = 1.f;

    /** Uniform scale of the whole body (skeleton, parts and capsule). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Proportions")
    float BodyScale = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Proportions")
    float TorsoThickness = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Proportions")
    float ArmThickness = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Proportions")
    float LegThickness = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Proportions")
    float HeadScale = 1.f;

    /** Extra length on the forearms (cm): long blade-like arms. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Proportions")
    float ForearmExtraLength = 0.f;

    /** Forearm tip radius multiplier (below 1 tapers to a point). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Proportions")
    float ForearmTipScale = 1.f;

    /** Extra torso bulge (cm) for a swollen gut. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Proportions")
    float Belly = 0.f;

    /** Chance one arm is massively swollen, and how much. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Proportions")
    float BigArmChance = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Proportions")
    float BigArmScale = 1.8f;

    /** Number of fleshy sacs bulging from the body (min, max). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Growths")
    FIntPoint GrowthCount = FIntPoint(1, 3);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Growths")
    FVector2D GrowthRadius = FVector2D(3.f, 7.f);

    /** Number of dangling tendrils from the shoulders/back (min, max). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Growths")
    FIntPoint TendrilCount = FIntPoint(0, 2);

    /** Grow a palm and five fingers on each hand (off for blade-armed forms). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Proportions")
    bool bHasFingers = true;

    /** Claw length past each fingertip (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Proportions")
    float ClawLength = 1.5f;

    /** Chance this corpse was "stuffed" by a robot: 1-2 machine limbs plus torso plates. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Machine")
    float RobotChance = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Stats")
    float WalkSpeed = 250.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Stats")
    float HealthMultiplier = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Stats")
    float PartHealthMultiplier = 1.f;

    /** Skin colour ranges, HSV (hue in degrees). Purple flesh by default. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Colour")
    FVector2D SkinHue = FVector2D(265.f, 290.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Colour")
    FVector2D SkinSaturation = FVector2D(0.25f, 0.45f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Colour")
    FVector2D SkinValue = FVector2D(0.28f, 0.42f);
};

/** Generated triangle mesh ready for UProceduralMeshComponent. */
struct FLimbMesh
{
    /** Per-vertex detail for the zombie materials: R = veins, G = mottling / rust / pulse phase, B = crease darkening. */
    TArray<FColor> Colors;
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FProcMeshTangent> Tangents;
};
