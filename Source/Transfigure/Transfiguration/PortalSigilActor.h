// Source/TMovement/Public/Transfiguration/PortalSigilActor.h

#pragma once

#include "CoreMinimal.h"
#include "Transfiguration/SigilActor.h"
#include "Transfiguration/TransfigurationTypes.h"
#include "PortalSigilActor.generated.h"

class UBillboardComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlayerTeleported, APortalSigilActor*, SourcePortal, APortalSigilActor*, DestinationPortal);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPortalActivated, APortalSigilActor*, Portal);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPortalDeactivated, APortalSigilActor*, Portal);

UCLASS()
class TRANSFIGURE_API APortalSigilActor : public ASigilActor
{
    GENERATED_BODY()

public:
    APortalSigilActor();

    virtual void Initialize(UTransfigurationDefinition* InSpell, AActor* InCaster) override;
    virtual void Tick(float DeltaTime) override;

    // Portal linking
    UFUNCTION(BlueprintCallable, Category = "Portal")
        bool LinkToPortal(APortalSigilActor* OtherPortal);

    UFUNCTION(BlueprintCallable, Category = "Portal")
        void UnlinkPortal();

    UFUNCTION(BlueprintPure, Category = "Portal")
        APortalSigilActor* GetLinkedPortal() const { return LinkedPortal.Get(); }

    UFUNCTION(BlueprintPure, Category = "Portal")
        bool IsLinked() const { return LinkedPortal.IsValid(); }

    // Portal state
    UFUNCTION(BlueprintCallable, Category = "Portal")
        void ActivatePortal();

    UFUNCTION(BlueprintCallable, Category = "Portal")
        void DeactivatePortal();

    UFUNCTION(BlueprintPure, Category = "Portal")
        bool IsPortalActive() const { return bIsActive; }

    // Teleportation
    UFUNCTION(BlueprintCallable, Category = "Portal")
        bool TeleportActor(AActor* ActorToTeleport);

    UFUNCTION(BlueprintPure, Category = "Portal")
        bool CanTeleportActor(AActor* ActorToTeleport) const;

    UFUNCTION(BlueprintPure, Category = "Portal")
        FVector GetExitTransform(AActor* TeleportingActor) const;

    // Components
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
        TObjectPtr<UBillboardComponent> PortalBillboard;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
        TObjectPtr<UParticleSystemComponent> PortalEffect;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
        TObjectPtr<USphereComponent> TeleportTrigger;

    // Delegates
    UPROPERTY(BlueprintAssignable)
        FOnPlayerTeleported OnPlayerTeleported;

    UPROPERTY(BlueprintAssignable)
        FOnPortalActivated OnPortalActivated;

    UPROPERTY(BlueprintAssignable)
        FOnPortalDeactivated OnPortalDeactivated;

    // Blueprint implementable events
    UFUNCTION(BlueprintImplementableEvent, Category = "Portal|VFX")
        void OnPortalLinkEstablished(APortalSigilActor* OtherPortal);

    UFUNCTION(BlueprintImplementableEvent, Category = "Portal|VFX")
        void OnPortalLinkBroken();

    UFUNCTION(BlueprintImplementableEvent, Category = "Portal|VFX")
        void OnTeleportVisuals(AActor* TeleportedActor, FVector ExitLocation);

    UFUNCTION(BlueprintImplementableEvent, Category = "Portal|VFX")
        void OnPortalChargeStart(float ChargeTime);

    UFUNCTION(BlueprintImplementableEvent, Category = "Portal|VFX")
        void OnPortalActivationWarning();

protected:
    UPROPERTY()
        TWeakObjectPtr<APortalSigilActor> LinkedPortal;

    UPROPERTY()
        TWeakObjectPtr<AActor> LastTeleportedActor;

    UPROPERTY()
        FTimerHandle ActivationDelayHandle;

    UPROPERTY()
        FTimerHandle CooldownTimerHandle;

    bool bIsActive = false;
    bool bIsOnCooldown = false;
    float LastTeleportTime = 0.f;

    UFUNCTION()
        void OnTeleportTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
            UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
            const FHitResult& SweepResult);

    UFUNCTION()
        void OnCooldownComplete();

    bool CheckTeleportConditions(AActor* ActorToTeleport) const;
    void ApplyTeleportEffects(AActor* TeleportedActor, const FVector& ExitLocation);
    void OrientActorForExit(AActor* ActorToOrient, const FVector& PortalNormal, const FVector& ExitNormal);
};