// Source/TMovement/Private/Grapple/ElementalGrappleEffect.cpp

#include "Grapple/ElementalGrappleEffect.h"
#include "NiagaraComponent.h"
#include "Components/BoxComponent.h"
#include "Enemy/BaseEnemy.h"
#include "TimerManager.h"

AElementalGrappleEffect::AElementalGrappleEffect()
{
    PrimaryActorTick.bCanEverTick = true;

    EffectParticle = CreateDefaultSubobject<UNiagaraComponent>(TEXT("EffectParticle"));
    SetRootComponent(EffectParticle);

    DamageVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("DamageVolume"));
    DamageVolume->SetupAttachment(RootComponent);
    DamageVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    DamageVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    DamageVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    DamageVolume->OnComponentBeginOverlap.AddDynamic(this, &AElementalGrappleEffect::OnDamageVolumeOverlap);
}

void AElementalGrappleEffect::BeginPlay()
{
    Super::BeginPlay();
}

void AElementalGrappleEffect::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

void AElementalGrappleEffect::InitializeEffect(ETransfigurationElement Element, FVector StartLocation,
    FVector EndLocation, float Duration)
{
    CurrentElement = Element;
    EffectDuration = Duration;

    SetActorLocation((StartLocation + EndLocation) / 2.0f);

    // Orient the effect along the line
    FVector Direction = (EndLocation - StartLocation).GetSafeNormal();
    SetActorRotation(Direction.Rotation());

    // Scale the damage volume to match the line
    float LineLength = FVector::Dist(StartLocation, EndLocation);
    DamageVolume->SetBoxExtent(FVector(LineLength / 2.0f, 50.0f, 50.0f));

    // Start damage timer
    GetWorldTimerManager().SetTimer(
        DamageTimerHandle,
        this, &AElementalGrappleEffect::ApplyDamageTick,
        DamageInterval,
        true);

    // Set lifespan
    SetLifeSpan(Duration);
}

void AElementalGrappleEffect::ApplyDamageTick()
{
    // Damage tick handled by overlap events
}

void AElementalGrappleEffect::OnDamageVolumeOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    ABaseEnemy* Enemy = Cast<ABaseEnemy>(OtherActor);
    if (!Enemy) return;

    ApplyEffectToEnemy(Enemy);
}

void AElementalGrappleEffect::ApplyEffectToEnemy(AActor* Enemy)
{
    ABaseEnemy* BaseEnemy = Cast<ABaseEnemy>(Enemy);
    if (!BaseEnemy) return;

    // Apply damage and effect based on element
    float DamageAmount = 10.0f;

    switch (CurrentElement)
    {
    case ETransfigurationElement::Fire:
        DamageAmount = 15.0f;
        // Apply burning DOT
        break;
    case ETransfigurationElement::Ice:
        DamageAmount = 5.0f;
        // Apply slow
        break;
    case ETransfigurationElement::Lightning:
        DamageAmount = 20.0f;
        // Apply stun
        break;
    case ETransfigurationElement::Void:
        DamageAmount = 25.0f;
        // Apply blind
        break;
    case ETransfigurationElement::Earth:
        DamageAmount = 30.0f;
        // Apply knockback
        break;
    default:
        break;
    }

    BaseEnemy->TakePoolDamage(DamageAmount, GetActorLocation());
}