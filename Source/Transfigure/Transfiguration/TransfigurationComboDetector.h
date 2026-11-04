// Source/TMovement/Public/Transfiguration/TransfigurationComboDetector.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TransfigurationTypes.h"
#include "TransfigurationComboDetector.generated.h"

class ASigilActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnComboDetected, FName, ComboSpellID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChainProgress, int32, ChainLength);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TRANSFIGURE_API UTransfigurationComboDetector : public UActorComponent
{
    GENERATED_BODY()

public:
    UTransfigurationComboDetector();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combo")
        TArray<FSigilComboRecipe> ComboRecipes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combo")
        float ComboProximityRadius = 500.f;

    // Combo chain detection
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combo|Chains")
        TArray<FComboChainRecipe> ComboChainRecipes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combo|Chains")
        float MaxChainBreakTime = 5.0f;

    UFUNCTION(BlueprintCallable, Category = "Combo|Chains")
        void StartComboChain(FName ChainName);

    UFUNCTION(BlueprintCallable, Category = "Combo|Chains")
        void AddToComboChain(ETransfigurationElement Element);

    UFUNCTION(BlueprintCallable, Category = "Combo|Chains")
        void ResetComboChain();

    UPROPERTY(BlueprintAssignable)
        FOnComboDetected OnComboDetected;

    UPROPERTY(BlueprintAssignable)
        FOnChainProgress OnChainProgress;

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    struct FActiveComboChain
    {
        TArray<ETransfigurationElement> CurrentChain;
        float LastElementTime;
        FName ExpectedResultID;
    } CurrentChain;

    void CheckForCombos(const TArray<TObjectPtr<ASigilActor>>& Sigils);
    bool MatchesRecipe(ETransfigurationElement A, ETransfigurationElement B, FName& OutResultID) const;
    TSet<TTuple<ASigilActor*, ASigilActor*>> FiredCombosThisFrame;
};