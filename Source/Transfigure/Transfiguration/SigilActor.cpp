// Source/TMovement/Private/Transfiguration/SigilActor.cpp

#include "Transfiguration/SigilActor.h"
#include "Transfiguration/TransfigurationEffectBase.h"
#include "Components/SphereComponent.h"
#include "Components/DecalComponent.h"
#include "TimerManager.h"

ASigilActor::ASigilActor()
{
    PrimaryActorTick.bCanEverTick = true;

    TriggerSphere = CreateDefaultSubobject<USphereComponent>(TEXT("TriggerSphere"));
    SetRootComponent(TriggerSphere);
    TriggerSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    SigilDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("SigilDecal"));
    SigilDecal->SetupAttachment(RootComponent);
}

void ASigilActor::Initialize(UTransfigurationDefinition* InSpell, AActor* InCaster)
{
    SpellDef = InSpell;
    Caster = InCaster;
    bArmed = true;

    if (!SpellDef) return;

    if (SpellDef->SigilDecalMaterial)
        SigilDecal->SetDecalMaterial(SpellDef->SigilDecalMaterial);

    TriggerSphere->SetSphereRadius(SpellDef->TriggerRadius);

    if (SpellDef->TimeToLive > 0.f)
    {
        GetWorldTimerManager().SetTimer(
            TTLTimerHandle,
            this, &ASigilActor::OnTimedAutoFire,
            SpellDef->TimeToLive,
            /*bLooping=*/false);
    }

    SetupProximityTrigger();
    OnSigilPlaced();
}

void ASigilActor::BeginPlay()
{
    Super::BeginPlay();
}

void ASigilActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

void ASigilActor::SetupProximityTrigger()
{
    if (!SpellDef) return;

    bool bNeedsOverlap =
        SpellDef->TriggerType == ESigilTriggerType::ProximityEnemy ||
        SpellDef->TriggerType == ESigilTriggerType::ProximityAny ||
        SpellDef->TriggerType == ESigilTriggerType::Stepped;

    if (bNeedsOverlap)
    {
        TriggerSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        TriggerSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
        TriggerSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
        TriggerSphere->OnComponentBeginOverlap.AddDynamic(
            this, &ASigilActor::OnProximityOverlap);
    }
}

void ASigilActor::OnProximityOverlap(
    UPrimitiveComponent* OverlappedComp,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    if (!bArmed || !SpellDef) return;

    if (SpellDef->TriggerType == ESigilTriggerType::ProximityEnemy)
    {
        if (OtherActor == Caster.Get()) return;
        APawn* Pawn = Cast<APawn>(OtherActor);
        if (!Pawn) return;
    }

    ExecuteSpellEffect();
}

void ASigilActor::TriggerManual()
{
    if (!bArmed) return;
    ExecuteSpellEffect();
}

void ASigilActor::OnTimedAutoFire()
{
    if (!bArmed) return;

    if (SpellDef->TriggerType == ESigilTriggerType::TimedAuto)
        ExecuteSpellEffect();
    else
    {
        bArmed = false;
        OnSigilExpired();
        Destroy();
    }
}

void ASigilActor::ExecuteSpellEffect()
{
    if (!SpellDef || !SpellDef->EffectClass) return;

    bArmed = false;

    UTransfigurationEffectBase* Effect = NewObject<UTransfigurationEffectBase>(
        this, SpellDef->EffectClass);

    if (Effect)
    {
        Effect->Execute(
            GetActorLocation(),
            SpellDef,
            Caster.Get(),
            GetWorld());
    }

    OnTriggered.Broadcast(this);
    OnSigilTriggeredVFX();

    GetWorldTimerManager().ClearTimer(TTLTimerHandle);
    GetWorldTimerManager().ClearTimer(AutoFireTimerHandle);

    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
    SetLifeSpan(0.5f);
}