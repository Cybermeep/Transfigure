// Source/Transfigure/Limb/SeveredBodyPart.h
//
// A limb cut off a baked (skinned-mesh) zombie: a second copy of the same body mesh that
// shows only the severed region's material sections, positioned so the cut bone sits
// exactly where it was, with that bone's physics joint broken (Unreal's gore path) so the
// limb ragdolls. Gravity is ring "down", not world -Z. Persistent, capped like ASeveredLimb.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Limb/LimbTypes.h"
#include "SeveredBodyPart.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UMaterialInterface;

UCLASS()
class TRANSFIGURE_API ASeveredBodyPart : public AActor
{
    GENERATED_BODY()

public:
    ASeveredBodyPart();

    /**
     * @param SlotMaterials  material per slot of Mesh (same as the zombie's)
     * @param VisibleSlots   slot indices to show (everything else is hidden)
     * @param CutBone        root bone of the severed subtree
     * @param CutBoneWorld   the zombie's current world transform of CutBone
     */
    void Init(USkeletalMesh* InMesh, const TArray<UMaterialInterface*>& SlotMaterials, const TArray<int32>& VisibleSlots,
        FName CutBone, const FTransform& CutBoneWorld, const FVector& LaunchVelocity);

    /** Wound cap / bone stub, in CutBone's local space. */
    void AddWound(const FLimbMesh& WoundMesh, UMaterialInterface* Material);

    static constexpr int32 MaxParts = 60;

protected:
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(VisibleAnywhere, Category = "Limb")
    TObjectPtr<USkeletalMeshComponent> Mesh;

    FName RootBone;

    static TArray<TWeakObjectPtr<ASeveredBodyPart>> AllParts;
};
