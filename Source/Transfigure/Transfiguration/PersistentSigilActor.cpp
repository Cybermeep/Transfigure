// Source/TMovement/Private/Transfiguration/PersistentSigilActor.cpp

#include "Transfiguration/PersistentSigilActor.h"
#include "Transfiguration/SigilActor.h"
#include "Character/TMCharacter.h"
#include "Transfiguration/BuffEffectComponent.h"
#include "Components/SphereComponent.h"
#include "TimerManager.h"

APersistentSigilActor::APersistentSigilActor()
{
    PrimaryActorTick.bCanEverTick = true;

    BuffComponent = CreateDefaultSubobject<UBuffEffectComponent>(TEXT("BuffComponent"));

    if (TriggerSphere)
    {
        TriggerSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        TriggerSphere->SetGenerateOverlapEvents(true);
        TriggerSphere->OnComponentBeginOverlap.AddDynamic(this, &APersistentSigilActor::OnTriggerVolumeOverlap);
        TriggerSphere->OnComponentEndOverlap.AddDynamic(this, &APersistentSigilActor::OnTriggerVolumeEndOverlap);
    }
}

void APersistentSigilActor::Initialize(UTransfigurationDefinition* InSpell, AActor* InCaster)
{
    Super::Initialize(InSpell, InCaster);

    if (!InSpell) return;

    bIsHealingSigil = InSpell->bIsHealingSigil;
    bIsBuffSigil = InSpell->bIsBuffSigil;

    ATMCharacter* TMChar = Cast<ATMCharacter>(InCaster);
    if (TMChar)
    {
        HealRate = TMChar->HealingSigilRate;
        BuffMultiplier = TMChar->SpeedBuffMultiplier;
        BuffDuration = TMChar->SpeedBuffDuration;
    }

    if (bIsHealingSigil)
    {
        GetWorldTimerManager().SetTimer(
            HealingTimerHandle,
            this, &APersistentSigilActor::ApplyHealingToOverlappingActors,
            1.0f, true, 0.0f);
    }
}

void APersistentSigilActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (bIsBuffSigil && bArmed)
    {
        ApplyBuffToOverlappingActors();
    }
}

void APersistentSigilActor::ApplyHealingToOverlappingActors()
{
    if (!bIsHealingSigil || !bArmed) return;

    TArray<AActor*> OverlappingActors;
    TriggerSphere->GetOverlappingActors(OverlappingActors, ATMCharacter::StaticClass());

    for (AActor* Actor : OverlappingActors)
    {
        ATMCharacter* TMChar = Cast<ATMCharacter>(Actor);
        if (TMChar && TMChar != Caster.Get())
        {
            float NewHealth = TMChar->GetHealth() + HealRate;
            TMChar->SetHealth(FMath::Min(NewHealth, TMChar->GetMaxHealth()));
        }
    }
}

void APersistentSigilActor::ApplyBuffToOverlappingActors()
{
    if (!bIsBuffSigil || !bArmed) return;

    TArray<AActor*> OverlappingActors;
    TriggerSphere->GetOverlappingActors(OverlappingActors, ATMCharacter::StaticClass());

    for (AActor* Actor : OverlappingActors)
    {
        ATMCharacter* TMChar = Cast<ATMCharacter>(Actor);
        if (TMChar && !AffectedActors.Contains(Actor))
        {
            TMChar->ApplySpeedBuff(BuffMultiplier, BuffDuration);
            AffectedActors.Add(Actor);
        }
    }
}

void APersistentSigilActor::OnTriggerVolumeOverlap(UPrimitiveComponent* OverlappedComp,
    AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
    bool bFromSweep, const FHitResult& SweepResult)
{
    if (!bArmed) return;
}

void APersistentSigilActor::OnTriggerVolumeEndOverlap(UPrimitiveComponent* OverlappedComp,
    AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    AffectedActors.Remove(OtherActor);
}