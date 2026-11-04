// Source/Transfigure/Limb/SeveredLimb.h
//
// A body part that has been cut off: a physics body built from the same
// generated mesh (convex collision per part), pulled along ring "down"
// instead of world -Z. Severed limbs persist; the oldest are removed once
// MaxSeveredLimbs is exceeded.
//
// Later tiers (crawling limbs, conglomerates, the limb wave) build on this.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Limb/LimbTypes.h"
#include "SeveredLimb.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

UCLASS()
class TRANSFIGURE_API ASeveredLimb : public AActor
{
    GENERATED_BODY()

public:
    ASeveredLimb();

    /** Add one part's mesh. Vertices must already be in this actor's local space. */
    void AddPartMesh(const FLimbMesh& Mesh, UMaterialInterface* Material);

    /** Call after all parts are added: turns on physics and applies the launch impulse. */
    void FinishAndLaunch(const FVector& Impulse, ELimbPart RootPart);

    UFUNCTION(BlueprintPure, Category = "Limb")
    ELimbPart GetRootPart() const { return RootPart; }

    /** Global cap on persistent severed limbs. */
    static constexpr int32 MaxSeveredLimbs = 80;

protected:
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(VisibleAnywhere, Category = "Limb")
    TObjectPtr<UProceduralMeshComponent> Mesh;

    ELimbPart RootPart = ELimbPart::None;
    int32 NextSection = 0;

    static TArray<TWeakObjectPtr<ASeveredLimb>> AllLimbs;
};
