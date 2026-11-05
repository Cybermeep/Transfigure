// Source/Transfigure/Transfiguration/SpellFX.h
//
// Placeholder spell visuals built from engine shapes and one glowing material
// (/Game/Spells/Materials/M_SpellGlow, made by Tools/create_spells.py; falls back
// to the engine's basic shape material). Self-destroying.
//
//   Burst   expanding, fading sphere
//   Beam    stretched cylinder between two points (lightning, chains)
//   Column  rising cylinder (pillars of fire, launches)
//   Disc    flat ring on the ground (aura zones, sigil flashes)
//   Falling sphere dropping onto a point (meteor), then a burst

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpellFX.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;

UCLASS(NotPlaceable)
class TRANSFIGURE_API ASpellFX : public AActor
{
    GENERATED_BODY()

public:
    ASpellFX();

    static ASpellFX* SpawnBurst(UWorld* World, const FVector& Location, float Radius, const FLinearColor& Color, float Life = 0.35f);
    static ASpellFX* SpawnBeam(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color, float Width = 14.f, float Life = 0.25f);
    static ASpellFX* SpawnColumn(UWorld* World, const FVector& Base, const FVector& Up, float Radius, float Height, const FLinearColor& Color, float Life = 0.6f);
    static ASpellFX* SpawnDisc(UWorld* World, const FVector& Center, const FVector& Normal, float Radius, const FLinearColor& Color, float Life = 0.5f);
    static ASpellFX* SpawnFalling(UWorld* World, const FVector& From, const FVector& To, float Radius, const FLinearColor& Color, float Time);

    /** The glowing additive material (or a fallback), and engine shapes. */
    static UMaterialInterface* GetGlowMaterial();
    static UStaticMesh* GetShape(const TCHAR* Name);   // "Sphere", "Cylinder", "Cube"

    /** Keep showing (for zones that manage their own lifetime). */
    void SetPersistent(bool bInPersistent) { bPersistent = bInPersistent; }
    void SetIntensity(float Intensity);

protected:
    virtual void Tick(float DeltaSeconds) override;

private:
    enum class EKind : uint8 { Burst, Beam, Column, Disc, Falling };
    void Setup(EKind InKind, UStaticMesh* Shape, const FLinearColor& Color, float InLife);

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Mesh;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> MID;

    EKind Kind = EKind::Burst;
    float Life = 0.35f;
    float Age = 0.f;
    bool bPersistent = false;
    FVector BaseScale = FVector::OneVector;
    FVector FallFrom = FVector::ZeroVector;
    FVector FallTo = FVector::ZeroVector;
    float FallRadius = 100.f;
    FLinearColor FXColor = FLinearColor::White;
};
