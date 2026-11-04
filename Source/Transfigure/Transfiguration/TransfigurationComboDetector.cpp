// Source/TMovement/Private/Transfiguration/TransfigurationComboDetector.cpp

#include "Transfiguration/TransfigurationComboDetector.h"
#include "Transfiguration/SigilActor.h"
#include "Transfiguration/TransfigurationCastingComponent.h"
#include "Transfiguration/TransfigurationDefinition.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"

UTransfigurationComboDetector::UTransfigurationComboDetector()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UTransfigurationComboDetector::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    FiredCombosThisFrame.Empty();

    auto* Owner = GetOwner();
    if (!Owner) return;

    auto* CastingComp = Owner->FindComponentByClass<UTransfigurationCastingComponent>();
    if (!CastingComp) return;

    CheckForCombos(CastingComp->ActiveSigils);
}

void UTransfigurationComboDetector::CheckForCombos(
    const TArray<TObjectPtr<ASigilActor>>& Sigils)
{
    for (int32 i = 0; i < Sigils.Num(); i++)
    {
        for (int32 j = i + 1; j < Sigils.Num(); j++)
        {
            ASigilActor* A = Sigils[i].Get();
            ASigilActor* B = Sigils[j].Get();

            if (!A || !B || !A->IsArmed() || !B->IsArmed()) continue;

            auto* DefA = A->GetSpellDefinition();
            auto* DefB = B->GetSpellDefinition();
            if (!DefA || !DefB) continue;

            if (FVector::Dist(A->GetActorLocation(), B->GetActorLocation()) > ComboProximityRadius)
                continue;

            auto Pair = TTuple<ASigilActor*, ASigilActor*>(A, B);
            if (FiredCombosThisFrame.Contains(Pair)) continue;

            FName ComboID;
            if (MatchesRecipe(DefA->Element, DefB->Element, ComboID) ||
                MatchesRecipe(DefB->Element, DefA->Element, ComboID))
            {
                FiredCombosThisFrame.Add(Pair);
                OnComboDetected.Broadcast(ComboID);
            }
        }
    }

    // Update chain detection
    for (const auto& Sigil : Sigils)
    {
        if (Sigil.Get() && Sigil->IsArmed())
        {
            auto* Def = Sigil->GetSpellDefinition();
            if (Def && !CurrentChain.ExpectedResultID.IsNone())
            {
                AddToComboChain(Def->Element);
            }
        }
    }
}

bool UTransfigurationComboDetector::MatchesRecipe(
    ETransfigurationElement A,
    ETransfigurationElement B,
    FName& OutResultID) const
{
    for (const auto& Recipe : ComboRecipes)
    {
        if (Recipe.ElementA == A && Recipe.ElementB == B)
        {
            OutResultID = Recipe.ResultSpellID;
            return true;
        }
    }
    return false;
}

void UTransfigurationComboDetector::StartComboChain(FName ChainName)
{
    for (const FComboChainRecipe& Recipe : ComboChainRecipes)
    {
        if (Recipe.ResultSpellID == ChainName)
        {
            CurrentChain.CurrentChain.Empty();
            CurrentChain.ExpectedResultID = ChainName;
            CurrentChain.LastElementTime = GetWorld()->GetTimeSeconds();
            break;
        }
    }
}

void UTransfigurationComboDetector::AddToComboChain(ETransfigurationElement Element)
{
    if (CurrentChain.CurrentChain.Num() == 0 && CurrentChain.ExpectedResultID.IsNone())
        return;

    float CurrentTime = GetWorld()->GetTimeSeconds();
    if (CurrentTime - CurrentChain.LastElementTime > MaxChainBreakTime)
    {
        ResetComboChain();
        return;
    }

    for (const FComboChainRecipe& Recipe : ComboChainRecipes)
    {
        if (Recipe.ResultSpellID == CurrentChain.ExpectedResultID)
        {
            int32 NextIndex = CurrentChain.CurrentChain.Num();
            if (NextIndex < Recipe.ElementChain.Num() &&
                Recipe.ElementChain[NextIndex] == Element)
            {
                CurrentChain.CurrentChain.Add(Element);
                CurrentChain.LastElementTime = CurrentTime;
                OnChainProgress.Broadcast(CurrentChain.CurrentChain.Num());

                if (CurrentChain.CurrentChain.Num() == Recipe.ElementChain.Num())
                {
                    OnComboDetected.Broadcast(Recipe.ResultSpellID);

                    if (Recipe.bConsumesSigils)
                    {
                        UWorld* World = GetWorld();
                        if (!World) return;

                        AActor* Owner = GetOwner();
                        if (!Owner) return;

                        // Collect all element types used in this chain
                        TSet<ETransfigurationElement> ChainElements(Recipe.ElementChain);


                        // Find all SigilActors in the level and destroy matching ones
                        for (TActorIterator<ASigilActor> It(World); It; ++It)
                        {
                            ASigilActor* Sigil = *It;
                            if (!Sigil || !Sigil->IsArmed()) continue;

                            UTransfigurationDefinition* Def = Sigil->GetSpellDefinition();
                            if (!Def) continue;

                            // Only consume sigils whose element is part of this chain
                            if (!ChainElements.Contains(Def->Element)) continue;

                            // Only consume sigils within the combo proximity radius of the owner
                            float Distance = FVector::Dist(
                                Sigil->GetActorLocation(),
                                Owner->GetActorLocation());

                            if (Distance <= ComboProximityRadius)
                            {
                                Sigil->Destroy();
                            }
                        }
                    }

                    ResetComboChain();
                }
            }
            else
            {
                ResetComboChain();
            }
            break;
        }
    }
}

void UTransfigurationComboDetector::ResetComboChain()
{
    CurrentChain.CurrentChain.Empty();
    CurrentChain.ExpectedResultID = NAME_None;
    CurrentChain.LastElementTime = 0.f;
}