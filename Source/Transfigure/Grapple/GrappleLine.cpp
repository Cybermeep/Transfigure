// Source/TMovement/Private/Grapple/GrappleLine.cpp

#include "Grapple/GrappleLine.h"
#include "Components/SplineComponent.h"
#include "NiagaraComponent.h"
#include "Components/DecalComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Enemy/BaseEnemy.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"




AGrappleLine::AGrappleLine()
{
    PrimaryActorTick.bCanEverTick = true;

    SplineComponent = CreateDefaultSubobject<USplineComponent>(TEXT("SplineComponent"));
    SetRootComponent(SplineComponent);

    NiagaraEffect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("NiagaraEffect"));
    NiagaraEffect->SetupAttachment(RootComponent);

    TrailDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("TrailDecal"));
    TrailDecal->SetupAttachment(RootComponent);
}

void AGrappleLine::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    TimeRemaining -= DeltaTime;

    if (TimeRemaining <= 0.0f && bIsValid)
    {
        ExpireLine();
    }
}

void AGrappleLine::InitializeLine(const FGrappleLineData& InLineData)
{
    LineData = InLineData;
    TimeRemaining = LineData.Lifetime;
    bIsValid = true;

    // Set up spline points
    SplineComponent->ClearSplinePoints();
    SplineComponent->AddSplinePoint(LineData.StartPoint, ESplineCoordinateSpace::World);
    SplineComponent->AddSplinePoint(LineData.EndPoint, ESplineCoordinateSpace::World);

    OnLineCreated(LineData);
    StartLifespan();

    // Apply elemental effects immediately
    ApplyElementalTrail();
}

void AGrappleLine::StartLifespan()
{
    GetWorldTimerManager().SetTimer(
        ExpirationTimer,
        this, &AGrappleLine::ExpireLine,
        LineData.Lifetime,
        false);
}

void AGrappleLine::ExpireLine()
{
    bIsValid = false;
    OnLineExpired();
    Destroy();
}

void AGrappleLine::ApplyElementalTrail()
{
    // Apply elemental effect along the spline
    float SplineLength = SplineComponent->GetSplineLength();
    int32 NumPoints = FMath::Max(5, FMath::FloorToInt(SplineLength / 100.0f));

    for (int32 i = 0; i <= NumPoints; i++)
    {
        float Distance = (SplineLength / NumPoints) * i;
        FVector Location = SplineComponent->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
        ApplyElementalEffectAtLocation(Location);
    }
}

void AGrappleLine::ApplyElementalEffectAtLocation(FVector Location)
{
    switch (LineData.Element)
    {
    case ETransfigurationElement::Fire:
        ApplyFireEffect(Location);
        break;
    case ETransfigurationElement::Ice:
        ApplyIceEffect(Location);
        break;
    case ETransfigurationElement::Lightning:
        ApplyLightningEffect(Location);
        break;
    case ETransfigurationElement::Void:
        ApplyVoidEffect(Location);
        break;
    case ETransfigurationElement::Earth:
        ApplyEarthEffect(Location);
        break;
    default:
        break;
    }

    OnElementalEffect(Location, LineData.Element);
}

void AGrappleLine::ApplyFireEffect(FVector Location)
{
    if (!NiagaraEffect) return;

    // Set Niagara parameter so Blueprint/asset can drive fire visuals
    NiagaraEffect->SetVariableLinearColor(
        FName("ElementColor"),
        FLinearColor(1.f, 0.3f, 0.f, 1.f)); // orange

    // Damage enemies in a small radius at this point along the line
    UWorld* World = GetWorld();
    if (!World) return;

    TArray<AActor*> IgnoredActors;
    IgnoredActors.Add(this);

    // Radial damage — BaseEnemy picks this up via TakePoolDamage override
    UGameplayStatics::ApplyRadialDamage(
        World,
        5.f,                    // damage per trail point
        Location,
        60.f,                   // radius
        UDamageType::StaticClass(),
        IgnoredActors,
        this,
        nullptr,
        false);
}

void AGrappleLine::ApplyIceEffect(FVector Location)
{
    if (!NiagaraEffect) return;

    NiagaraEffect->SetVariableLinearColor(
        FName("ElementColor"),
        FLinearColor(0.4f, 0.8f, 1.f, 1.f)); // light blue

    UWorld* World = GetWorld();
    if (!World) return;

    // Find enemies in radius and slow them via movement component
    TArray<FOverlapResult> Overlaps;
    FCollisionShape Sphere = FCollisionShape::MakeSphere(80.f);
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    World->OverlapMultiByChannel(
        Overlaps,
        Location,
        FQuat::Identity,
        ECC_Pawn,
        Sphere,
        Params);

    for (const FOverlapResult& Overlap : Overlaps)
    {
        ABaseEnemy* Enemy = Cast<ABaseEnemy>(Overlap.GetActor());
        if (!Enemy) continue;

        // Slow enemy movement by 50% for 2 seconds via movement component
        UCharacterMovementComponent* CMC =
            Enemy->FindComponentByClass<UCharacterMovementComponent>();
        if (CMC)
        {
            float OriginalSpeed = CMC->MaxWalkSpeed;
            CMC->MaxWalkSpeed = OriginalSpeed * 0.5f;

            // Restore speed after delay
            FTimerHandle RestoreTimer;
            FTimerDelegate RestoreDelegate;
            RestoreDelegate.BindLambda([CMC, OriginalSpeed]()
                {
                    if (CMC)
                        CMC->MaxWalkSpeed = OriginalSpeed;
                });
            World->GetTimerManager().SetTimer(RestoreTimer, RestoreDelegate, 2.f, false);
        }
    }
}

void AGrappleLine::ApplyLightningEffect(FVector Location)
{
    if (!NiagaraEffect) return;

    NiagaraEffect->SetVariableLinearColor(
        FName("ElementColor"),
        FLinearColor(0.9f, 0.9f, 0.2f, 1.f)); // yellow

    UWorld* World = GetWorld();
    if (!World) return;

    // Higher damage than fire, chains to nearby enemies
    TArray<FOverlapResult> Overlaps;
    FCollisionShape Sphere = FCollisionShape::MakeSphere(150.f); // wider chain radius
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    World->OverlapMultiByChannel(
        Overlaps,
        Location,
        FQuat::Identity,
        ECC_Pawn,
        Sphere,
        Params);

    for (const FOverlapResult& Overlap : Overlaps)
    {
        ABaseEnemy* Enemy = Cast<ABaseEnemy>(Overlap.GetActor());
        if (!Enemy) continue;

        // Apply damage and briefly stun via movement
        Enemy->TakePoolDamage(15.f, Location);

        UCharacterMovementComponent* CMC =
            Enemy->FindComponentByClass<UCharacterMovementComponent>();
        if (CMC)
        {
            CMC->DisableMovement();

            FTimerHandle StunTimer;
            FTimerDelegate StunDelegate;
            StunDelegate.BindLambda([CMC]()
                {
                    if (CMC)
                        CMC->SetMovementMode(MOVE_Walking);
                });
            World->GetTimerManager().SetTimer(StunTimer, StunDelegate, 0.75f, false);
        }
    }
}

void AGrappleLine::ApplyVoidEffect(FVector Location)
{
    if (!NiagaraEffect) return;

    NiagaraEffect->SetVariableLinearColor(
        FName("ElementColor"),
        FLinearColor(0.3f, 0.f, 0.5f, 1.f)); // deep purple

    UWorld* World = GetWorld();
    if (!World) return;

    // Void pulls nearby enemies toward the line point (gravitational distortion)
    TArray<FOverlapResult> Overlaps;
    FCollisionShape Sphere = FCollisionShape::MakeSphere(200.f);
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    World->OverlapMultiByChannel(
        Overlaps,
        Location,
        FQuat::Identity,
        ECC_Pawn,
        Sphere,
        Params);

    for (const FOverlapResult& Overlap : Overlaps)
    {
        ACharacter* Character = Cast<ACharacter>(Overlap.GetActor());
        if (!Character) continue;

        // Pull toward line point
        FVector PullDirection = (Location - Character->GetActorLocation()).GetSafeNormal();
        UCharacterMovementComponent* CMC = Character->GetCharacterMovement();
        if (CMC)
        {
            CMC->AddImpulse(PullDirection * 600.f, true);
        }

        // Also apply moderate damage
        ABaseEnemy* Enemy = Cast<ABaseEnemy>(Character);
        if (Enemy)
        {
            Enemy->TakePoolDamage(8.f, Location);
        }
    }
}

void AGrappleLine::ApplyEarthEffect(FVector Location)
{
    if (!NiagaraEffect) return;

    NiagaraEffect->SetVariableLinearColor(
        FName("ElementColor"),
        FLinearColor(0.4f, 0.25f, 0.1f, 1.f)); // brown

    UWorld* World = GetWorld();
    if (!World) return;

    // Earth knocks enemies away with heavy impact
    TArray<FOverlapResult> Overlaps;
    FCollisionShape Sphere = FCollisionShape::MakeSphere(120.f);
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    World->OverlapMultiByChannel(
        Overlaps,
        Location,
        FQuat::Identity,
        ECC_Pawn,
        Sphere,
        Params);

    for (const FOverlapResult& Overlap : Overlaps)
    {
        ABaseEnemy* Enemy = Cast<ABaseEnemy>(Overlap.GetActor());
        if (!Enemy) continue;

        // Heavy damage + knockback away from line point
        Enemy->TakePoolDamage(20.f, Location);

        FVector KnockbackDir = (Enemy->GetActorLocation() - Location).GetSafeNormal();
        KnockbackDir.Z = 0.4f; // slight upward angle
        KnockbackDir.Normalize();

        UCharacterMovementComponent* CMC =
            Enemy->FindComponentByClass<UCharacterMovementComponent>();
        if (CMC)
        {
            CMC->AddImpulse(KnockbackDir * 1200.f, true);
        }
    }
}

bool AGrappleLine::CanConnectTo(AGrappleLine* OtherLine) const
{
    if (!OtherLine || !OtherLine->IsLineValid()) return false;

    // Check if lines are close enough to intersect
    float Distance = FVector::Dist(LineData.EndPoint, OtherLine->GetLineData().StartPoint);
    return Distance < 200.0f;
}

void AGrappleLine::CreateWebIntersection(AGrappleLine* OtherLine)
{
    if (!CanConnectTo(OtherLine)) return;

    FVector IntersectionPoint = (LineData.EndPoint + OtherLine->GetLineData().StartPoint) / 2.0f;

    const TArray<AGrappleLine*> ConnectedLines = { this, OtherLine };

    OnWebIntersectionCreated(IntersectionPoint, ConnectedLines);
}

void AGrappleLine::OnWebIntersectionCreated(FVector IntersectionPoint, const TArray<AGrappleLine*>& ConnectedLines)
{
    // Override in Blueprint or subclass
}