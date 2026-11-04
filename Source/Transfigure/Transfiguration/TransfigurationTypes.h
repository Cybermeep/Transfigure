// Source/TMovement/Public/Transfiguration/TransfigurationTypes.h

#pragma once

#include "CoreMinimal.h"
#include "TransfigurationTypes.generated.h"

UENUM(BlueprintType)
enum class ETransfigurationElement : uint8
{
    None = 0 UMETA(DisplayName = "None"),
    Fire UMETA(DisplayName = "Fire"),
    Ice UMETA(DisplayName = "Ice"),
    Lightning UMETA(DisplayName = "Lightning"),
    Arcane UMETA(DisplayName = "Arcane"),
    Earth UMETA(DisplayName = "Earth"),
    Void UMETA(DisplayName = "Void")
};

UENUM(BlueprintType)
enum class ESigilTriggerType : uint8
{
    Manual UMETA(DisplayName = "Manual"),
    ProximityEnemy UMETA(DisplayName = "ProximityEnemy"),
    ProximityAny UMETA(DisplayName = "ProximityAny"),
    TimedAuto UMETA(DisplayName = "TimedAuto"),
    Stepped UMETA(DisplayName = "Stepped")
};

UENUM(BlueprintType)
enum class ECastingState : uint8
{
    Idle UMETA(DisplayName = "Idle"),
    Drawing UMETA(DisplayName = "Drawing"),
    Placing UMETA(DisplayName = "Placing"),
    Triggering UMETA(DisplayName = "Triggering")
};

UENUM(BlueprintType)
enum class EBuffType : uint8
{
    Speed UMETA(DisplayName = "Speed"),
    Damage UMETA(DisplayName = "Damage"),
    Defense UMETA(DisplayName = "Defense"),
    Jump UMETA(DisplayName = "Jump Height")
};

UENUM(BlueprintType)
enum class EPortalType : uint8
{
    Static UMETA(DisplayName = "Static"),
    Mobile UMETA(DisplayName = "Mobile"),
    OneWay UMETA(DisplayName = "OneWay"),
    TwoWay UMETA(DisplayName = "TwoWay"),
    Networked UMETA(DisplayName = "Networked")
};

UENUM(BlueprintType)
enum class EPortalActivation : uint8
{
    Immediate UMETA(DisplayName = "Immediate"),
    Delayed UMETA(DisplayName = "Delayed"),
    Conditional UMETA(DisplayName = "Conditional"),
    Charged UMETA(DisplayName = "Charged")
};

UENUM(BlueprintType)
enum class EPodState : uint8
{
    Inactive UMETA(DisplayName = "Inactive"),
    Spawning UMETA(DisplayName = "Spawning"),
    Active UMETA(DisplayName = "Active"),
    Defeated UMETA(DisplayName = "Defeated"),
    Reinforcing UMETA(DisplayName = "Reinforcing")
};

UENUM(BlueprintType)
enum class EGrappleType : uint8
{
    Swing UMETA(DisplayName = "Swing"),
    Combat UMETA(DisplayName = "Combat"),
    WebLine UMETA(DisplayName = "Web Line"),
    ElementalTrail UMETA(DisplayName = "Elemental Trail")
};

UENUM(BlueprintType)
enum class EGrappleState : uint8
{
    Inactive UMETA(DisplayName = "Inactive"),
    Aiming UMETA(DisplayName = "Aiming"),
    Swinging UMETA(DisplayName = "Swinging"),
    CombatGrapple UMETA(DisplayName = "Combat Grapple"),
    WebBuilding UMETA(DisplayName = "Web Building")
};

UENUM(BlueprintType)
enum class EGrappleTier : uint8
{
    None = 0 UMETA(Hidden),
    Strand = 1 UMETA(DisplayName = "Strand (Tier 1)"),
    Thread = 2 UMETA(DisplayName = "Thread (Tier 2)"),
    Cord = 3 UMETA(DisplayName = "Cord (Tier 3)"),
    Rope = 4 UMETA(DisplayName = "Rope (Tier 4)"),
    Web = 5 UMETA(DisplayName = "Web (Tier 5)"),
    Nexus = 6 UMETA(DisplayName = "Nexus (Tier 6)")
};

UENUM(BlueprintType)
enum class ECombatGrappleMove : uint8
{
    Kick UMETA(DisplayName = "Grapple Kick"),
    Slam UMETA(DisplayName = "Grapple Slam"),
    Toss UMETA(DisplayName = "Grapple Toss"),
    Evade UMETA(DisplayName = "Grapple Evade"),
    Pull UMETA(DisplayName = "Grapple Pull"),
    WebTrap UMETA(DisplayName = "Web Trap")
};

USTRUCT(BlueprintType)
struct FSigilComboRecipe
{
    GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite)
        ETransfigurationElement ElementA;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
        ETransfigurationElement ElementB;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
        FName ResultSpellID;
};

USTRUCT(BlueprintType)
struct FComboChainRecipe
{
    GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite)
        TArray<ETransfigurationElement> ElementChain;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
        FName ResultSpellID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
        float ChainTimeWindow = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
        bool bConsumesSigils = true;
};

USTRUCT()
struct FActiveComboChain
{
        GENERATED_BODY()

    TArray<ETransfigurationElement> CurrentChain;
    float LastElementTime;
    FName ExpectedResultID;
};

USTRUCT(BlueprintType)
struct FPortalPair
{
    GENERATED_BODY()

        UPROPERTY()
        TWeakObjectPtr<class APortalSigilActor> PortalA;

    UPROPERTY()
        TWeakObjectPtr<class APortalSigilActor> PortalB;

    UPROPERTY()
        bool bIsActive = true;

    UPROPERTY()
        float LastUsedTime = 0.f;
};

USTRUCT(BlueprintType)
struct FEnemySpawnPoint
{
    GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite)
        FVector Location;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
        FRotator Rotation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
        bool bIsPatrolPoint = false;
};

USTRUCT(BlueprintType)
struct FGrappleLineData
{
    GENERATED_BODY()

        UPROPERTY()
        FVector StartPoint;

    UPROPERTY()
        FVector EndPoint;

    UPROPERTY()
        ETransfigurationElement Element;

    UPROPERTY()
        float Lifetime = 3.0f;

    UPROPERTY()
        float LineWidth = 2.0f;

    UPROPERTY()
        bool bIsActive = true;
};

USTRUCT(BlueprintType)
struct FCombatGrappleData
{
    GENERATED_BODY()

        UPROPERTY()
        ECombatGrappleMove MoveType;

    UPROPERTY()
        AActor* TargetEnemy;

    UPROPERTY()
        float DamageMultiplier = 1.0f;

    UPROPERTY()
        float MomentumPreserved = 0.8f;
};

USTRUCT()
struct FEnemyPoolEntry
{
    GENERATED_BODY()

        UPROPERTY()
        class ABaseEnemy* Enemy = nullptr;

    UPROPERTY()
        bool bIsActive = false;

    UPROPERTY()
        float SpawnTime = 0.f;

    UPROPERTY()
        FVector SpawnLocation = FVector::ZeroVector;

    UPROPERTY()
        int32 PodID = -1;
};

USTRUCT()
struct FDestructionRequest
{
    GENERATED_BODY()

        UPROPERTY()
        FVector HitLocation;

    UPROPERTY()
        float DamageRadius;

    UPROPERTY()
        float DamageForce;

    UPROPERTY()
        TWeakObjectPtr<AActor> TargetActor;

    UPROPERTY()
        FDateTime RequestTime;

    FDestructionRequest()
        : HitLocation(FVector::ZeroVector)
        , DamageRadius(0.f)
        , DamageForce(0.f)
        , RequestTime(FDateTime::Now())
    {}
};

USTRUCT()
struct FChunkGenerationTask
{
    GENERATED_BODY()

        UPROPERTY()
        class UProceduralMeshComponent* SourceMesh;

    UPROPERTY()
        int32 SectionIndex;

    UPROPERTY()
        FVector HitLocation;

    UPROPERTY()
        float ChunkSize;

    UPROPERTY()
        TArray<FVector> GeneratedVertices;

    UPROPERTY()
        TArray<int32> GeneratedTriangles;

    bool bIsComplete = false;
};