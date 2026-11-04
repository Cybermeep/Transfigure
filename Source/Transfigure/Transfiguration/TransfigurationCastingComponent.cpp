// Source/TMovement/Private/Transfiguration/TransfigurationCastingComponent.cpp

#include "Transfiguration/TransfigurationCastingComponent.h"
#include "Transfiguration/TransfigurationRegistry.h"
#include "Transfiguration/TransfigurationGameInstance.h"
#include "Transfiguration/PersistentSigilActor.h"
#include "Character/TMCharacter.h"
#include "Destruction/DestructibleSurface.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "TimerManager.h"

UTransfigurationCastingComponent::UTransfigurationCastingComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UTransfigurationCastingComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* TickFunc)
{
    Super::TickComponent(DeltaTime, TickType, TickFunc);

    if (CurrentState == ECastingState::Drawing)
    {
        PerformAimTrace(CurrentAimHit);
        OnAimUpdated.Broadcast(CurrentState);
    }

    CleanupDestroyedSigils();
}

bool UTransfigurationCastingComponent::PerformAimTrace(FHitResult& OutHit) const
{
    ACharacter* Char = Cast<ACharacter>(GetOwner());
    if (!Char) return false;

    FVector CamLoc;
    FRotator CamRot;
    Char->GetController()->GetPlayerViewPoint(CamLoc, CamRot);

    FVector TraceEnd = CamLoc + CamRot.Vector() * MaxPlacementDistance;

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(GetOwner());

    return GetWorld()->LineTraceSingleByChannel(
        OutHit, CamLoc, TraceEnd, ECC_WorldStatic, Params);
}

bool UTransfigurationCastingComponent::CanAffordSpell(FName SpellID) const
{
    ATMCharacter* TMChar = Cast<ATMCharacter>(GetOwner());
    if (!TMChar) return false;

    float ManaCost = GetSpellManaCost(SpellID);
    return TMChar->HasEnoughMana(ManaCost);
}

float UTransfigurationCastingComponent::GetSpellManaCost(FName SpellID) const
{
    UTransfigurationRegistry* Reg = GetSpellRegistry();
    if (!Reg) return 0.0f;

    UTransfigurationDefinition* SpellDef = Reg->GetSpell(SpellID);
    if (!SpellDef) return 0.0f;

    float BaseCost = SpellDef->ManaCost;

    if (SpellDef->bCostScalesWithSpeed)
    {
        ATMCharacter* TMChar = Cast<ATMCharacter>(GetOwner());
        if (TMChar)
        {
            float SpeedFactor = TMChar->GetSpeedScalingFactor();
            float SpeedMultiplier = FMath::GetMappedRangeValueClamped(
                FVector2D(1.0f, TMChar->MaxDamageScale),
                FVector2D(1.0f, SpellDef->MaxSpeedManaMultiplier),
                SpeedFactor);
            BaseCost *= SpeedMultiplier;
        }
    }

    return BaseCost;
}

void UTransfigurationCastingComponent::BeginDrawMode(FName SpellID)
{
    if (CurrentState != ECastingState::Idle) return;

    if (!CanAffordSpell(SpellID))
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot afford spell: %s"), *SpellID.ToString());
        return;
    }

    UTransfigurationRegistry* Reg = GetSpellRegistry();
    if (!Reg || !Reg->GetSpell(SpellID))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("TransfigurationCasting: SpellID %s not in registry"),
            *SpellID.ToString());
        return;
    }

    LastSpellManaCost = GetSpellManaCost(SpellID);
    ActiveSpellID = SpellID;
    TransitionToState(ECastingState::Drawing);
    OnDrawModeEntered(SpellID);
}

void UTransfigurationCastingComponent::ConfirmPlacement()
{
    if (CurrentState != ECastingState::Drawing) return;
    if (!CurrentAimHit.bBlockingHit) return;

    // Check if surface is destructible
    ADestructibleSurface* Surface = Cast<ADestructibleSurface>(CurrentAimHit.GetActor());
    if (Surface && !Surface->CanPlaceSigilAtLocation(CurrentAimHit.ImpactPoint))
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot place sigil: Surface too damaged"));
        return;
    }

    if (ActiveSigils.Num() >= MaxActiveSigils)
    {
        if (ActiveSigils[0].Get())
            ActiveSigils[0]->Destroy();
        ActiveSigils.RemoveAt(0);
    }

    UTransfigurationDefinition* SpellDef =
        GetSpellRegistry()->GetSpell(ActiveSpellID);
    if (!SpellDef) return;

    ATMCharacter* TMChar = Cast<ATMCharacter>(GetOwner());
    if (TMChar && LastSpellManaCost > 0.0f)
    {
        TMChar->ConsumeMana(LastSpellManaCost);
    }

    FVector HitNormal = CurrentAimHit.ImpactNormal;
    FVector HitPoint = CurrentAimHit.ImpactPoint;
    FRotator SigilRot = HitNormal.Rotation();
    FVector SpawnLoc = HitPoint + HitNormal * 2.f;

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = GetOwner();
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    ASigilActor* Sigil = GetWorld()->SpawnActor<ASigilActor>(
        ASigilActor::StaticClass(),
        SpawnLoc, SigilRot, SpawnParams);

    if (Sigil)
    {
        Sigil->Initialize(SpellDef, GetOwner());
        ActiveSigils.Add(Sigil);
        OnPlacementConfirmed(Sigil);
    }

    TransitionToState(ECastingState::Idle);
}

void UTransfigurationCastingComponent::CancelDrawMode()
{
    if (CurrentState != ECastingState::Drawing) return;
    TransitionToState(ECastingState::Idle);
    OnDrawModeCancelled();
}

void UTransfigurationCastingComponent::TriggerNearestManualSigil()
{
    if (ActiveSigils.IsEmpty()) return;

    FVector PlayerLoc = GetOwner()->GetActorLocation();
    float BestDist = ManualTriggerScanRadius;
    ASigilActor* Nearest = nullptr;

    for (auto& SigilPtr : ActiveSigils)
    {
        ASigilActor* S = SigilPtr.Get();
        if (!S || !S->IsArmed()) continue;

        auto* Def = S->GetSpellDefinition();
        if (!Def || Def->TriggerType != ESigilTriggerType::Manual)
            continue;

        float Dist = FVector::Dist(PlayerLoc, S->GetActorLocation());
        if (Dist < BestDist)
        {
            BestDist = Dist;
            Nearest = S;
        }
    }

    if (Nearest) Nearest->TriggerManual();
}

void UTransfigurationCastingComponent::TriggerAllSigils()
{
    TArray<TObjectPtr<ASigilActor>> Snapshot = ActiveSigils;
    for (auto& S : Snapshot)
        if (S.Get() && S->IsArmed()) S->TriggerManual();
}

void UTransfigurationCastingComponent::PrepareSigilsForTemporalClap()
{
    PendingTemporalSigils.Empty();

    for (auto& SigilPtr : ActiveSigils)
    {
        ASigilActor* Sigil = SigilPtr.Get();
        if (!Sigil || !Sigil->IsArmed()) continue;

        UTransfigurationDefinition* Def = Sigil->GetSpellDefinition();
        if (Def && !Def->bIsPersistentSigil)
        {
            PendingTemporalSigils.Add(Sigil);
        }
    }
}

void UTransfigurationCastingComponent::TriggerSigilsSynchronized()
{
    if (PendingTemporalSigils.Num() == 0) return;

    float DelayBetween = TemporalSigilActivationDelay / FMath::Max(1, PendingTemporalSigils.Num());
    float CurrentDelay = 0.0f;

    for (ASigilActor* Sigil : PendingTemporalSigils)
    {
        if (!Sigil || !Sigil->IsArmed()) continue;

        FTimerHandle DelayHandle;
        FTimerDelegate Delegate = FTimerDelegate::CreateLambda([this, Sigil]()
            {
                if (Sigil && Sigil->IsArmed())
                {
                    OnSigilTemporalActivate.Broadcast(Sigil);
                    Sigil->TriggerManual();
                }
            });

        GetWorld()->GetTimerManager().SetTimer(DelayHandle, Delegate, CurrentDelay, false);
        CurrentDelay += DelayBetween;
    }

    PendingTemporalSigils.Empty();
}

void UTransfigurationCastingComponent::ConvertToPersistentSigil(ASigilActor* Sigil, float Duration)
{
    if (!Sigil) return;

    UTransfigurationDefinition* SpellDef = Sigil->GetSpellDefinition();
    if (!SpellDef || !SpellDef->bIsPersistentSigil) return;

    FVector Location = Sigil->GetActorLocation();
    FRotator Rotation = Sigil->GetActorRotation();

    Sigil->Destroy();

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = GetOwner();

    APersistentSigilActor* PersistentSigil = GetWorld()->SpawnActor<APersistentSigilActor>(
        APersistentSigilActor::StaticClass(), Location, Rotation, SpawnParams);

    if (PersistentSigil)
    {
        PersistentSigil->Initialize(SpellDef, GetOwner());
        PersistentSigils.Add(PersistentSigil);
    }
}

TArray<APersistentSigilActor*> UTransfigurationCastingComponent::GetActivePersistentSigils() const
{
    TArray<APersistentSigilActor*> Result;
    for (auto& Sigil : PersistentSigils)
    {
        if (Sigil.Get() && Sigil->IsArmed())
            Result.Add(Sigil.Get());
    }
    return Result;
}

void UTransfigurationCastingComponent::CleanupDestroyedSigils()
{
    ActiveSigils.RemoveAll([](const TObjectPtr<ASigilActor>& S)
        { return !S.Get() || !IsValid(S.Get()); });

    PersistentSigils.RemoveAll([](const TObjectPtr<APersistentSigilActor>& S)
        { return !S.Get() || !IsValid(S.Get()); });
}

void UTransfigurationCastingComponent::TransitionToState(ECastingState NewState)
{
    CurrentState = NewState;
    OnCastingStateChanged.Broadcast(NewState);
}

UTransfigurationRegistry* UTransfigurationCastingComponent::GetSpellRegistry() const
{
    auto* GI = Cast<UTransfigurationGameInstance>(
        GetWorld()->GetGameInstance());
    return GI ? GI->GetSpellRegistry() : nullptr;
}

void UTransfigurationCastingComponent::NotifySpellHitResult(bool bHitEnemy)
{
    if (!bHitEnemy && LastSpellManaCost > 0.0f)
    {
        ATMCharacter* TMChar = Cast<ATMCharacter>(GetOwner());
        UTransfigurationDefinition* SpellDef = GetSpellRegistry()->GetSpell(ActiveSpellID);

        if (TMChar && SpellDef && SpellDef->bRefundOnMiss)
        {
            float RefundAmount = LastSpellManaCost * SpellDef->RefundPercentage;
            TMChar->AddMana(RefundAmount);
        }
    }
}