// Source/Transfigure/Limb/ZombieBodyVariant.h
//
// One pre-generated zombie body (made by Tools/blender/zombie_body_gen.py and imported by
// Tools/import_zombie_bodies.py): a skinned mesh on the UE5 Mannequin skeleton, plus the
// data the zombie needs to use it (form, templates, measured limb thickness).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ZombieBodyVariant.generated.h"

class USkeletalMesh;

UCLASS(BlueprintType)
class TRANSFIGURE_API UZombieBodyVariant : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** The body mesh. Material slots are body regions ("UpperArmL", ...) plus "<Region>_Metal". */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zombie")
    TSoftObjectPtr<USkeletalMesh> Mesh;

    /** Body form this was generated for (Husk / Bloat / Gaunt). Searchable without loading. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Zombie")
    FName Archetype;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zombie")
    int32 Seed = 0;

    /** Template per region, e.g. arm_L = claws, torso = hump. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zombie")
    TMap<FName, FName> Templates;

    /** Measured limb thickness (cm) per part name ("UpperArmL", "Torso", ...), for hit zones. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zombie")
    TMap<FName, float> PartRadius;

    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(TEXT("ZombieBodyVariant"), GetFName());
    }
};
