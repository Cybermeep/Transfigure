// Source/Transfigure/Public/Transfiguration/TransfigurationRegistry.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Transfiguration/TransfigurationDefinition.h"
#include "Transfiguration/TransfigurationTypes.h"
#include "TransfigurationRegistry.generated.h"

UCLASS(BlueprintType)
class TRANSFIGURE_API UTransfigurationRegistry : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spells")
    TMap<FName, TObjectPtr<UTransfigurationDefinition>> SpellTable;

    UFUNCTION(BlueprintCallable, Category = "Spells")
    UTransfigurationDefinition* GetSpell(FName SpellID) const;

    UFUNCTION(BlueprintCallable, Category = "Spells")
    TArray<UTransfigurationDefinition*> GetSpellsByElement(ETransfigurationElement Element) const;

    UFUNCTION(BlueprintCallable, Category = "Spells")
    bool HasSpell(FName SpellID) const;

    UFUNCTION(BlueprintCallable, Category = "Spells")
    TArray<FName> GetAllSpellIDs() const;
};