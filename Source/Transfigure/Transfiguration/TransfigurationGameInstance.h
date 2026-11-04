// Source/Transfigure/Public/Transfiguration/TransfigurationGameInstance.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Transfiguration/TransfigurationRegistry.h"
#include "TransfigurationGameInstance.generated.h"

UCLASS()
class TRANSFIGURE_API UTransfigurationGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    virtual void Init() override;

    UFUNCTION(BlueprintCallable, Category = "Transfiguration")
    UTransfigurationRegistry* GetSpellRegistry() const { return SpellRegistry; }

private:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Transfiguration",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UTransfigurationRegistry> SpellRegistry;
};