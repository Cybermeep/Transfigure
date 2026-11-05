// Source/Transfigure/Transfiguration/SpellStructure.cpp

#include "Transfiguration/SpellStructure.h"
#include "Transfiguration/SpellFX.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

TArray<TWeakObjectPtr<ASpellStructure>> ASpellStructure::All;

ASpellStructure::ASpellStructure()
{
    PrimaryActorTick.bCanEverTick = true;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    Mesh->SetupAttachment(Root);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    Mesh->SetCanEverAffectNavigation(false);
}

void ASpellStructure::Init(ESpellStructureShape InShape, const FVector& SizeCm, const FLinearColor& Color, float Lifetime, bool bInSlippery)
{
    Shape = InShape;
    bSlippery = bInSlippery;
    Life = FMath::Max(Lifetime, 0.5f);
    Height = FMath::Max(SizeCm.Z, 10.f);

    const bool bRound = Shape == ESpellStructureShape::Pillar || Shape == ESpellStructureShape::Spike;
    Mesh->SetStaticMesh(ASpellFX::GetShape(bRound ? TEXT("Cylinder") : TEXT("Cube")));
    if (bRound)
    {
        const float D = FMath::Max(SizeCm.X, 20.f);
        Mesh->SetRelativeScale3D(FVector(D / 100.f, D / 100.f, Height / 100.f));
    }
    else
    {
        Mesh->SetRelativeScale3D(FVector(FMath::Max(SizeCm.X, 10.f) / 100.f, FMath::Max(SizeCm.Y, 10.f) / 100.f, Height / 100.f));
    }
    // Engine shapes are centred: lift by half the height so the base sits on the surface.
    MeshBase = FVector(0.f, 0.f, Height * 0.5f - 20.f);
    RiseTime = Shape == ESpellStructureShape::Spike ? 0.08f : 0.25f;
    Mesh->SetRelativeLocation(MeshBase - FVector(0.f, 0.f, Height));

    if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
    {
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
        MID->SetVectorParameterValue(TEXT("Color"), Color);
        Mesh->SetMaterial(0, MID);
    }

    All.RemoveAll([](const TWeakObjectPtr<ASpellStructure>& S) { return !S.IsValid(); });
    All.Add(this);
    while (All.Num() > MaxStructures)
    {
        if (ASpellStructure* Old = All[0].Get())
        {
            Old->Age = FMath::Max(Old->Age, Old->Life);   // start sinking
        }
        All.RemoveAt(0);
    }
}

void ASpellStructure::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;

    float Offset = 0.f;   // how far below its risen spot (cm)
    if (Age < RiseTime)
    {
        const float T = Age / RiseTime;
        Offset = Height * (1.f - T) * (1.f - T);
    }
    else if (Age > Life)
    {
        const float T = FMath::Clamp((Age - Life) / SinkTime, 0.f, 1.f);
        Offset = Height * T;
        if (T >= 1.f)
        {
            Destroy();
            return;
        }
    }
    Mesh->SetRelativeLocation(MeshBase - FVector(0.f, 0.f, Offset));
}

void ASpellStructure::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    All.RemoveAll([this](const TWeakObjectPtr<ASpellStructure>& S) { return !S.IsValid() || S.Get() == this; });
    Super::EndPlay(EndPlayReason);
}
