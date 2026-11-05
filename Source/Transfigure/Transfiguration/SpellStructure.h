// Source/Transfigure/Transfiguration/SpellStructure.h
//
// Solid things spells raise: ice walls, stone pillars, ice ramps, earth spikes.
// They block, so you can wall run on them, grapple them and whip round them.
// Rise out of the ground, stay for a while, sink back. Slippery ones (ice) take
// away friction and braking while you stand on them (see ATMCharacter).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Transfiguration/SpellTypes.h"
#include "SpellStructure.generated.h"

class UStaticMeshComponent;

UCLASS(NotPlaceable)
class TRANSFIGURE_API ASpellStructure : public AActor
{
    GENERATED_BODY()

public:
    ASpellStructure();

    /** Actor sits on the surface (root at the base, Z = surface normal). Size in cm. */
    void Init(ESpellStructureShape InShape, const FVector& SizeCm, const FLinearColor& Color, float Lifetime, bool bInSlippery);

    UPROPERTY(VisibleAnywhere, Category = "Spell")
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere, Category = "Spell")
    TObjectPtr<UStaticMeshComponent> Mesh;

    bool IsSlippery() const { return bSlippery; }

    /** At most this many spell structures exist at once (oldest sink first). */
    static constexpr int32 MaxStructures = 12;

protected:
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    ESpellStructureShape Shape = ESpellStructureShape::Wall;
    bool bSlippery = false;
    float Height = 100.f;
    float RiseTime = 0.25f;
    float Life = 12.f;
    float Age = 0.f;
    float SinkTime = 0.4f;
    FVector MeshBase = FVector::ZeroVector;   // relative location when fully risen

    static TArray<TWeakObjectPtr<ASpellStructure>> All;
};
