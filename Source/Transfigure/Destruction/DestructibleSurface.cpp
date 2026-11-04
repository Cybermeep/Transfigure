// Source/TMovement/Private/Destruction/DestructibleSurface.cpp

#include "Destruction/DestructibleSurface.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Components/PrimitiveComponent.h"

ADestructibleSurface::ADestructibleSurface()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 1.0f;
}

void ADestructibleSurface::BeginPlay()
{
    Super::BeginPlay();

    // Collect all primitive components to apply damage visuals
    TArray<UPrimitiveComponent*> PrimitiveComponents;
    GetComponents<UPrimitiveComponent>(PrimitiveComponents);

    for (UPrimitiveComponent* Comp : PrimitiveComponents)
    {
        for (int32 i = 0; i < Comp->GetNumMaterials(); i++)
        {
            UMaterialInterface* Material = Comp->GetMaterial(i);
            if (Material)
            {
                UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(Material, this);
                Comp->SetMaterial(i, DynamicMaterial);
                MaterialInstances.Add(DynamicMaterial);
            }
        }
    }
}

void ADestructibleSurface::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    CleanupOldDamageZones();
    ApplyDamageVisuals();
}

bool ADestructibleSurface::CanPlaceSigilAtLocation(FVector Location) const
{
    float CurrentTime = GetWorld()->GetTimeSeconds();

    for (const FDamageZone& Zone : DamageZones)
    {
        float Distance = FVector::Dist(Location, Zone.Location);
        if (Distance < Zone.Radius)
        {
            float DamageFactor = 1.0f - (Distance / Zone.Radius);
            float EffectiveDamage = Zone.DamageAmount * DamageFactor;

            if (EffectiveDamage > MaxDamagePerLocation * 0.5f)
            {
                return false;
            }
        }
    }

    return true;
}

void ADestructibleSurface::RegisterDestruction(FVector Location, float Radius)
{
    FDamageZone NewZone;
    NewZone.Location = Location;
    NewZone.DamageAmount = MaxDamagePerLocation;
    NewZone.DamageTime = GetWorld()->GetTimeSeconds();
    NewZone.Radius = Radius;

    DamageZones.Add(NewZone);

    if (DestructionEffect)
    {
        UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), DestructionEffect, Location);
    }

    ApplyDamageVisuals();
}

float ADestructibleSurface::GetSurfaceIntegrity(FVector Location) const
{
    float TotalDamage = 0.f;
    float CurrentTime = GetWorld()->GetTimeSeconds();

    for (const FDamageZone& Zone : DamageZones)
    {
        float Distance = FVector::Dist(Location, Zone.Location);
        if (Distance < Zone.Radius)
        {
            float Age = CurrentTime - Zone.DamageTime;
            float RegenerationFactor = FMath::Clamp(1.0f - (Age / RegenerationTime), 0.0f, 1.0f);
            float DamageFactor = 1.0f - (Distance / Zone.Radius);
            TotalDamage += Zone.DamageAmount * DamageFactor * RegenerationFactor;
        }
    }

    return FMath::Clamp(1.0f - (TotalDamage / MaxDamagePerLocation), 0.0f, 1.0f);
}

void ADestructibleSurface::RepairSurface(float RepairAmount)
{
    if (DamageZones.Num() == 0) return;

    // Reduce damage on all zones
    for (FDamageZone& Zone : DamageZones)
    {
        Zone.DamageAmount = FMath::Max(0.0f, Zone.DamageAmount - (MaxDamagePerLocation * RepairAmount));
    }

    // Remove fully repaired zones
    DamageZones.RemoveAll([](const FDamageZone& Zone) {
        return Zone.DamageAmount <= 0.0f;
        });

    StartRepair();
}

void ADestructibleSurface::CleanupOldDamageZones()
{
    float CurrentTime = GetWorld()->GetTimeSeconds();

    DamageZones.RemoveAll([CurrentTime, this](const FDamageZone& Zone) {
        return (CurrentTime - Zone.DamageTime) > RegenerationTime;
        });
}

void ADestructibleSurface::ApplyDamageVisuals()
{
    if (MaterialInstances.Num() == 0) return;

    // Calculate average damage across surface
    float TotalIntegrity = 0.0f;
    int32 SamplePoints = 5;

    for (int32 i = 0; i < SamplePoints; i++)
    {
        FVector SampleLocation = GetActorLocation() + FVector(
            FMath::FRandRange(-500.0f, 500.0f),
            FMath::FRandRange(-500.0f, 500.0f),
            FMath::FRandRange(-500.0f, 500.0f)
        );
        TotalIntegrity += GetSurfaceIntegrity(SampleLocation);
    }

    float AverageIntegrity = TotalIntegrity / SamplePoints;

    // Apply to materials
    for (UMaterialInstanceDynamic* MatInst : MaterialInstances)
    {
        if (MatInst)
        {
            MatInst->SetScalarParameterValue(FName("Damage"), 1.0f - AverageIntegrity);
        }
    }
}

void ADestructibleSurface::StartRepair()
{
    GetWorldTimerManager().SetTimer(
        RepairTimerHandle,
        this, &ADestructibleSurface::CompleteRepair,
        RepairCooldown,
        false);
}

void ADestructibleSurface::CompleteRepair()
{
    // Reset to undamaged state
    DamageZones.Empty();
    ApplyDamageVisuals();
}