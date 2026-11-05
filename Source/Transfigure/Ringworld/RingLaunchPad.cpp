// Source/Transfigure/Ringworld/RingLaunchPad.cpp

#include "RingLaunchPad.h"
#include "RingLaunchComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ARingLaunchPad::ARingLaunchPad()
{
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    Pad = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pad"));
    Pad->SetupAttachment(Root);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (Cylinder.Succeeded())
    {
        Pad->SetStaticMesh(Cylinder.Object);
    }
    // Engine cylinder is 100 cm across and 100 cm tall, centred: a 6 m disc, 20 cm thick, sitting on the floor.
    Pad->SetRelativeScale3D(FVector(6.f, 6.f, 0.2f));
    Pad->SetCollisionProfileName(TEXT("BlockAll"));

    Zone = CreateDefaultSubobject<USphereComponent>(TEXT("Zone"));
    Zone->SetupAttachment(Root);
    Zone->InitSphereRadius(350.f);
    Zone->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
    Zone->SetGenerateOverlapEvents(true);
    Zone->OnComponentBeginOverlap.AddDynamic(this, &ARingLaunchPad::OnZoneBegin);
    Zone->OnComponentEndOverlap.AddDynamic(this, &ARingLaunchPad::OnZoneEnd);
}

void ARingLaunchPad::OnZoneBegin(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
    if (URingLaunchComponent* Launch = Other ? Other->FindComponentByClass<URingLaunchComponent>() : nullptr)
    {
        Launch->SetActivePad(this);
    }
}

void ARingLaunchPad::OnZoneEnd(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32)
{
    if (URingLaunchComponent* Launch = Other ? Other->FindComponentByClass<URingLaunchComponent>() : nullptr)
    {
        if (Launch->GetActivePad() == this) Launch->SetActivePad(nullptr);
    }
}
