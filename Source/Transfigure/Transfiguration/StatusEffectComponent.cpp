// Source/Transfigure/Transfiguration/StatusEffectComponent.cpp

#include "Transfiguration/StatusEffectComponent.h"
#include "Enemy/BaseEnemy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"

UStatusEffectComponent::UStatusEffectComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.05f;
}

void UStatusEffectComponent::ApplyStatus(ESpellStatus Status, float Stacks, float Duration, AActor* Instigator)
{
    ABaseEnemy* Enemy = Cast<ABaseEnemy>(GetOwner());
    if (!Enemy || !Enemy->IsAlive() || Status == ESpellStatus::None) return;

    FActiveStatus& S = Active.FindOrAdd(Status);
    S.Stacks += FMath::Max(Stacks, 0.f);
    S.TimeLeft = FMath::Max(S.TimeLeft, Duration);
    S.Instigator = Instigator;

    switch (Status)
    {
    case ESpellStatus::Burning:
        S.Stacks = FMath::Min(S.Stacks, 3.f);
        // Fire thaws ice.
        Active.Remove(ESpellStatus::Chilled);
        FrozenTime = 0.f;
        break;

    case ESpellStatus::Chilled:
        Active.Remove(ESpellStatus::Burning);
        if (S.Stacks >= FreezeAtStacks)
        {
            Active.Remove(ESpellStatus::Chilled);
            FrozenTime = FreezeDuration;
            Enemy->ApplyStun(FreezeDuration);
            UE_LOG(LogTemp, Warning, TEXT("[TM] %s FROZEN for %.1fs (limbs shatter when hit)"), *Enemy->GetName(), FreezeDuration);
        }
        else
        {
            Enemy->ApplySlow(FMath::Max(1.f - ChillSlowPerStack * S.Stacks, 0.2f), S.TimeLeft);
        }
        break;

    case ESpellStatus::Shocked:
        Enemy->ApplyStun(ShockStun);
        break;

    case ESpellStatus::Staggered:
        Enemy->ApplyStun(StaggerStun);
        break;

    case ESpellStatus::Voided:
        Enemy->ApplySlow(VoidSlow, S.TimeLeft);
        break;

    default:
        break;
    }
    UpdateTint();
}

bool UStatusEffectComponent::HasStatus(ESpellStatus Status) const
{
    const FActiveStatus* S = Active.Find(Status);
    return S && S->TimeLeft > 0.f;
}

float UStatusEffectComponent::GetStacks(ESpellStatus Status) const
{
    const FActiveStatus* S = Active.Find(Status);
    return S && S->TimeLeft > 0.f ? S->Stacks : 0.f;
}

float UStatusEffectComponent::GetDamageTakenMultiplier() const
{
    float M = 1.f;
    if (HasStatus(ESpellStatus::Marked)) M *= MarkMultiplier;
    if (HasStatus(ESpellStatus::Voided)) M *= VoidMultiplier;
    return M;
}

TArray<ESpellStatus> UStatusEffectComponent::GetActiveStatuses() const
{
    TArray<ESpellStatus> Out;
    for (const auto& Pair : Active)
    {
        if (Pair.Value.TimeLeft > 0.f) Out.Add(Pair.Key);
    }
    if (IsFrozen()) Out.AddUnique(ESpellStatus::Chilled);
    return Out;
}

void UStatusEffectComponent::ClearAll()
{
    Active.Reset();
    FrozenTime = 0.f;
    UpdateTint();
}

void UStatusEffectComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (Active.Num() == 0 && FrozenTime <= 0.f && !bTinted) return;

    ABaseEnemy* Enemy = Cast<ABaseEnemy>(GetOwner());
    if (!Enemy) return;
    if (!Enemy->IsAlive())
    {
        ClearAll();
        return;
    }

    bool bChanged = false;
    if (FrozenTime > 0.f)
    {
        FrozenTime -= DeltaTime;
        if (FrozenTime <= 0.f) bChanged = true;
    }

    // Burning: damage in half-second pulses.
    if (const FActiveStatus* Burn = Active.Find(ESpellStatus::Burning))
    {
        BurnTimer += DeltaTime;
        if (BurnTimer >= 0.5f && Burn->TimeLeft > 0.f)
        {
            BurnTimer -= 0.5f;
            Enemy->TakeStatusDamage(BurnDamagePerSecond * 0.5f * Burn->Stacks, ETransfigurationElement::Fire);
            if (!Enemy->IsAlive()) return;
        }
    }

    for (auto It = Active.CreateIterator(); It; ++It)
    {
        It.Value().TimeLeft -= DeltaTime;
        if (It.Value().TimeLeft <= 0.f)
        {
            It.RemoveCurrent();
            bChanged = true;
        }
    }
    if (bChanged) UpdateTint();
}

void UStatusEffectComponent::UpdateTint()
{
    ABaseEnemy* Enemy = Cast<ABaseEnemy>(GetOwner());
    USkeletalMeshComponent* Mesh = Enemy ? Enemy->GetMesh() : nullptr;
    if (!Mesh) return;

    // Strongest-reading status wins the colour.
    ESpellStatus Show = ESpellStatus::None;
    if (IsFrozen()) Show = ESpellStatus::Chilled;
    else
    {
        static const ESpellStatus Priority[] = { ESpellStatus::Voided, ESpellStatus::Shocked, ESpellStatus::Burning,
            ESpellStatus::Chilled, ESpellStatus::Marked, ESpellStatus::Staggered };
        for (ESpellStatus S : Priority)
        {
            if (HasStatus(S)) { Show = S; break; }
        }
    }
    if (Show == TintedAs) return;

    if (!bHaveOriginalGlow)
    {
        if (UMaterialInterface* Mat = Mesh->GetMaterial(0))
        {
            Mat->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("GlowColor")), OriginalGlow);
            Mat->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("GlowStrength")), OriginalGlowStrength);
        }
        bHaveOriginalGlow = true;
    }

    if (Show == ESpellStatus::None)
    {
        Mesh->SetVectorParameterValueOnMaterials(TEXT("GlowColor"), FVector(OriginalGlow));
        Mesh->SetScalarParameterValueOnMaterials(TEXT("GlowStrength"), OriginalGlowStrength);
        bTinted = false;
    }
    else
    {
        const FLinearColor C = USpellLibrary::GetStatusColor(Show);
        Mesh->SetVectorParameterValueOnMaterials(TEXT("GlowColor"), FVector(C.R, C.G, C.B));
        Mesh->SetScalarParameterValueOnMaterials(TEXT("GlowStrength"), FMath::Max(OriginalGlowStrength, 1.f) * 1.25f);
        bTinted = true;
    }
    TintedAs = Show;
}
