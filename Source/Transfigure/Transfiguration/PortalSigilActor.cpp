// Source/TMovement/Private/Transfiguration/PortalSigilActor.cpp

#include "Transfiguration/PortalSigilActor.h"
#include "Transfiguration/SigilActor.h"
#include "Character/TMCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/BillboardComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"

APortalSigilActor::APortalSigilActor()
{
    PrimaryActorTick.bCanEverTick = true;

    PortalBillboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("PortalBillboard"));
    PortalBillboard->SetupAttachment(RootComponent);

    PortalEffect = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("PortalEffect"));
    PortalEffect->SetupAttachment(RootComponent);

    TeleportTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("TeleportTrigger"));
    TeleportTrigger->SetupAttachment(RootComponent);
    TeleportTrigger->SetSphereRadius(100.f);
    TeleportTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    TeleportTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    TeleportTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    TeleportTrigger->OnComponentBeginOverlap.AddDynamic(this, &APortalSigilActor::OnTeleportTriggerOverlap);
}

void APortalSigilActor::Initialize(UTransfigurationDefinition* InSpell, AActor* InCaster)
{
    Super::Initialize(InSpell, InCaster);

    if (!InSpell) return;

    if (InSpell->PortalActivation == EPortalActivation::Immediate)
    {
        ActivatePortal();
    }
    else if (InSpell->PortalActivation == EPortalActivation::Delayed)
    {
        GetWorldTimerManager().SetTimer(
            ActivationDelayHandle,
            this, &APortalSigilActor::ActivatePortal,
            InSpell->PortalActivationDelay,
            false);

        OnPortalChargeStart(InSpell->PortalActivationDelay);
    }

    SetActorScale3D(FVector(InSpell->PortalVisualScale));
}

void APortalSigilActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (bIsActive && bIsOnCooldown && PortalEffect)
    {
        float PulseAlpha = FMath::MakePulsatingValue(GetWorld()->GetTimeSeconds(), 1.0f);
        PortalEffect->SetColorParameter(FName("PulseColor"), FLinearColor(0.5f, 0.5f, 0.5f, PulseAlpha));
    }
}

bool APortalSigilActor::LinkToPortal(APortalSigilActor* OtherPortal)
{
    if (!OtherPortal || OtherPortal == this) return false;

    LinkedPortal = OtherPortal;
    OtherPortal->LinkedPortal = this;

    OnPortalLinkEstablished(OtherPortal);
    OtherPortal->OnPortalLinkEstablished(this);

    return true;
}

void APortalSigilActor::UnlinkPortal()
{
    if (LinkedPortal.IsValid())
    {
        LinkedPortal->LinkedPortal = nullptr;
        LinkedPortal->OnPortalLinkBroken();
    }

    LinkedPortal = nullptr;
    OnPortalLinkBroken();
}

void APortalSigilActor::ActivatePortal()
{
    bIsActive = true;
    OnPortalActivated.Broadcast(this);

    if (PortalEffect)
    {
        PortalEffect->Activate();
    }
}

void APortalSigilActor::DeactivatePortal()
{
    bIsActive = false;
    OnPortalDeactivated.Broadcast(this);

    if (PortalEffect)
    {
        PortalEffect->Deactivate();
    }
}

bool APortalSigilActor::CanTeleportActor(AActor* ActorToTeleport) const
{
    if (!bIsActive || bIsOnCooldown || !LinkedPortal.IsValid()) return false;

    float CurrentTime = GetWorld()->GetTimeSeconds();
    if (CurrentTime - LastTeleportTime < SpellDef->PortalCooldown)
        return false;

    if (LastTeleportedActor.Get() == ActorToTeleport)
        return false;

    if (SpellDef->bRequiresLineOfSight)
    {
        FHitResult Hit;
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(this);
        Params.AddIgnoredActor(ActorToTeleport);

        bool bHasLineOfSight = !GetWorld()->LineTraceTestByChannel(
            GetActorLocation(),
            LinkedPortal->GetActorLocation(),
            ECC_Visibility,
            Params);

        if (!bHasLineOfSight) return false;
    }

    return CheckTeleportConditions(ActorToTeleport);
}

bool APortalSigilActor::CheckTeleportConditions(AActor* ActorToTeleport) const
{
    ATMCharacter* TMChar = Cast<ATMCharacter>(ActorToTeleport);
    if (!TMChar) return true;

    FString RequiredState = SpellDef->AllowedMovementStateForPortal;
    if (!RequiredState.IsEmpty())
    {
        if (TMChar->CurrentStateName != RequiredState)
            return false;
    }

    switch (SpellDef->PortalActivation)
    {
    case EPortalActivation::Conditional:
        return TMChar->bWantsToTeleport;
    case EPortalActivation::Charged:
        return TMChar->GetManaPercentage() > 0.5f;
    default:
        return true;
    }
}

FVector APortalSigilActor::GetExitTransform(AActor* TeleportingActor) const
{
    if (!LinkedPortal.IsValid()) return FVector::ZeroVector;

    FVector ExitLocation = LinkedPortal->GetActorLocation();
    FVector PortalNormal = -GetActorForwardVector();
    ExitLocation += PortalNormal * 50.f;

    return ExitLocation;
}

bool APortalSigilActor::TeleportActor(AActor* ActorToTeleport)
{
    if (!CanTeleportActor(ActorToTeleport) || !LinkedPortal.IsValid())
        return false;

    FVector PreservedVelocity = FVector::ZeroVector;
    if (SpellDef->bMaintainsMomentum)
    {
        ACharacter* Char = Cast<ACharacter>(ActorToTeleport);
        if (Char && Char->GetCharacterMovement())
        {
            PreservedVelocity = Char->GetCharacterMovement()->Velocity;
        }
    }

    FVector ExitLocation = GetExitTransform(ActorToTeleport);

    if (LinkedPortal.IsValid())
    {
        FVector PortalNormal = -GetActorForwardVector();
        FVector ExitNormal = -LinkedPortal->GetActorForwardVector();
        OrientActorForExit(ActorToTeleport, PortalNormal, ExitNormal);
    }

    ActorToTeleport->SetActorLocation(ExitLocation, false, nullptr, ETeleportType::TeleportPhysics);

    if (SpellDef->bMaintainsMomentum && !PreservedVelocity.IsZero())
    {
        ACharacter* Char = Cast<ACharacter>(ActorToTeleport);
        if (Char && Char->GetCharacterMovement())
        {
            Char->GetCharacterMovement()->Velocity = PreservedVelocity;
        }
    }

    LastTeleportTime = GetWorld()->GetTimeSeconds();
    LastTeleportedActor = ActorToTeleport;
    bIsOnCooldown = true;

    GetWorldTimerManager().SetTimer(
        CooldownTimerHandle,
        this, &APortalSigilActor::OnCooldownComplete,
        SpellDef->PortalCooldown,
        false);

    ApplyTeleportEffects(ActorToTeleport, ExitLocation);
    OnPlayerTeleported.Broadcast(this, LinkedPortal.Get());

    return true;
}

void APortalSigilActor::OrientActorForExit(AActor* ActorToOrient, const FVector& PortalNormal, const FVector& ExitNormal)
{
    FRotator CurrentRotation = ActorToOrient->GetActorRotation();
    FVector Forward = ActorToOrient->GetActorForwardVector();

    FVector ForwardOnPlane = FVector::VectorPlaneProject(Forward, PortalNormal);
    ForwardOnPlane.Normalize();

    FQuat RotationQuat = FQuat::FindBetween(PortalNormal, ExitNormal);
    FVector NewForward = RotationQuat.RotateVector(ForwardOnPlane);

    ActorToOrient->SetActorRotation(NewForward.Rotation());
}

void APortalSigilActor::ApplyTeleportEffects(AActor* TeleportedActor, const FVector& ExitLocation)
{
    if (PortalEffect)
    {
        UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), PortalEffect->Template, ExitLocation);
    }

    ATMCharacter* TMChar = Cast<ATMCharacter>(TeleportedActor);
    if (TMChar)
    {
        OnTeleportVisuals(TeleportedActor, ExitLocation);
    }
}

void APortalSigilActor::OnTeleportTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (!OtherActor || OtherActor == this) return;

    if (SpellDef->PortalActivation == EPortalActivation::Delayed && bIsActive)
    {
        OnPortalActivationWarning();
    }

    TeleportActor(OtherActor);
}

void APortalSigilActor::OnCooldownComplete()
{
    bIsOnCooldown = false;
}