// Source/TMovement/Public/Grapple/GrappleComponent.h
// FULL REPLACEMENT — adds SetEvadeDirection() and EvadeDirection member

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GrappleTypes.h"
#include "GrappleComponent.generated.h"

class AGrappleLine;
class ATMCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGrappleStateChanged, EGrappleState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGrappleLineCreated, const FGrappleLineData&, LineData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCombatGrappleExecuted, ECombatGrappleMove, MoveType);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TRANSFIGURE_API UGrappleComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGrappleComponent();

    // Configuration
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Config")
    float MaxGrappleRange = 3000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Config")
    float SwingStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Config")
    float MomentumConservation = 0.95f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Config")
    float MaxSwingTime = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Config")
    int32 MaxSimultaneousLines = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Tier")
    EGrappleTier CurrentTier = EGrappleTier::Strand;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Mana")
    float SwingManaCost = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Mana")
    float CombatGrappleManaCost = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Mana")
    float WebLineManaCost = 10.0f;

    // State
    UPROPERTY(BlueprintReadOnly, Category = "Grapple|State")
    EGrappleState CurrentState = EGrappleState::Inactive;

    UPROPERTY(BlueprintReadOnly, Category = "Grapple|State")
    ETransfigurationElement CurrentElement = ETransfigurationElement::Fire;

    UPROPERTY(BlueprintReadOnly, Category = "Grapple|State")
    TArray<FGrappleLineData> ActiveLines;

    UPROPERTY(BlueprintReadOnly, Category = "Grapple|State")
    int32 CurrentLineCount = 0;

    // Delegates
    UPROPERTY(BlueprintAssignable)
    FOnGrappleStateChanged OnGrappleStateChanged;

    UPROPERTY(BlueprintAssignable)
    FOnGrappleLineCreated OnGrappleLineCreated;

    UPROPERTY(BlueprintAssignable)
    FOnCombatGrappleExecuted OnCombatGrappleExecuted;

    // Public methods
    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void StartAiming(ETransfigurationElement Element);

    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void FireGrapple(FVector AimLocation, AActor* TargetEnemy = nullptr);

    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void ExecuteCombatMove(ECombatGrappleMove MoveType, AActor* TargetEnemy);

    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void ReleaseGrapple();

    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void CancelAiming();

    UFUNCTION(BlueprintPure, Category = "Grapple")
    EGrappleState GetCurrentState() const { return CurrentState; }

    // ── BUG 3 FIX: SetEvadeDirection so TMCharacter can pass input direction ──
    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void SetEvadeDirection(FVector Direction) { EvadeDirection = Direction.GetSafeNormal(); }

    UFUNCTION(BlueprintCallable, Category = "Grapple|Web")
    void CreateWebLine(FVector StartPoint, FVector EndPoint);

    UFUNCTION(BlueprintCallable, Category = "Grapple|Web")
    bool CanCreateWebLine() const;

    UFUNCTION(BlueprintCallable, Category = "Grapple|Web")
    TArray<FVector> FindWebConnectionPoints() const;

    UFUNCTION(BlueprintCallable, Category = "Grapple|Combat")
    bool IsEnemyGrappleable(AActor* Enemy) const;

    UFUNCTION(BlueprintCallable, Category = "Grapple|Query")
    float GetCurrentTierLines() const;

    UFUNCTION(BlueprintCallable, Category = "Grapple|Query")
    float GetMomentumPreservation() const;

    UFUNCTION(BlueprintPure, Category = "Grapple")
    EGrappleTier GetCurrentTier() const { return CurrentTier; }

    UFUNCTION(BlueprintCallable, Category = "Grapple")
    void SetCurrentTier(EGrappleTier NewTier) { CurrentTier = NewTier; }

    UFUNCTION(BlueprintPure, Category = "Grapple")
    TArray<FGrappleLineData>& GetActiveLines() { return ActiveLines; }

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    UPROPERTY()
    ATMCharacter* OwnerCharacter;

    UPROPERTY()
    TArray<AGrappleLine*> SpawnedLines;

    UPROPERTY()
    FGrappleLineData CurrentSwingData;

    UPROPERTY()
    FTimerHandle SwingTimerHandle;

    // Swing physics state (Verlet integration)
    UPROPERTY()
    FVector SwingAnchorPoint = FVector::ZeroVector;

    UPROPERTY()
    FVector SwingVelocity = FVector::ZeroVector;

    UPROPERTY()
    float SwingRopeLength = 0.f;

    UPROPERTY()
    bool bIsSwinging = false;

    // ── BUG 3 FIX: Stores evade direction set from input handler ──
    FVector EvadeDirection = FVector::ForwardVector;

    void PerformGrappleSwing(FVector AnchorPoint);
    void PerformCombatGrapple(AActor* TargetEnemy, ECombatGrappleMove MoveType);
    void UpdateSwingPhysics(float DeltaTime);
    void ApplyElementalEffect(FGrappleLineData& LineData);
    void UpdateLineLifecycles(float DeltaTime);
    void CleanupExpiredLines();
    bool HasLineTierCapability(FString Capability) const;
    void PreserveMomentum(FVector PreGrappleVelocity);
};