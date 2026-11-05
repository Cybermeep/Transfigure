// Source/Transfigure/Transfiguration/SpellFX.cpp

#include "Transfiguration/SpellFX.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

ASpellFX::ASpellFX()
{
    PrimaryActorTick.bCanEverTick = true;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);
    Mesh->SetCanEverAffectNavigation(false);
    Ring = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ring"));
    Ring->SetupAttachment(Mesh);
    Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ring->SetCastShadow(false);
}

UMaterialInterface* ASpellFX::GetGlowMaterial()
{
    static TWeakObjectPtr<UMaterialInterface> Cached;
    if (!Cached.IsValid())
    {
        UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Spells/Materials/M_SpellGlow.M_SpellGlow"), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!M) M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        Cached = M;
    }
    return Cached.Get();
}

UStaticMesh* ASpellFX::GetShape(const TCHAR* Name)
{
    static TMap<FString, TWeakObjectPtr<UStaticMesh>> Cache;
    TWeakObjectPtr<UStaticMesh>& Slot = Cache.FindOrAdd(Name);
    if (!Slot.IsValid())
    {
        Slot = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
    }
    return Slot.Get();
}

static float KindIntensity(int32 Kind)
{
    // Burst, Beam, Column, Disc, Falling
    static const float I[] = { 1.6f, 8.f, 2.f, 1.6f, 3.f };
    return I[FMath::Clamp(Kind, 0, 4)];
}

void ASpellFX::Setup(EKind InKind, UStaticMesh* Shape, const FLinearColor& Color, float InLife)
{
    Kind = InKind;
    Life = FMath::Max(InLife, 0.05f);
    FXColor = Color;
    if (Shape && InKind != EKind::Disc) Mesh->SetStaticMesh(Shape);
    if (InKind == EKind::Disc)
    {
        // Unit ring (radius 50 = the engine cylinder's) in the actor's XY plane, so scaling matches.
        TArray<FVector> V;
        TArray<int32> T;
        const int32 N = 72;
        for (int32 i = 0; i <= N; ++i)
        {
            const float A = 2.f * PI * i / N;
            V.Add(FVector(FMath::Cos(A) * 46.5f, FMath::Sin(A) * 46.5f, 0.f));
            V.Add(FVector(FMath::Cos(A) * 50.f, FMath::Sin(A) * 50.f, 0.f));
        }
        for (int32 i = 0; i < N; ++i)
        {
            const int32 a = i * 2, b = a + 1, c = a + 3, d = a + 2;
            T.Append({ a, b, c, a, c, d, a, c, b, a, d, c });
        }
        Ring->CreateMeshSection(0, V, T, TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);
    }
    if (UMaterialInterface* Glow = GetGlowMaterial())
    {
        MID = UMaterialInstanceDynamic::Create(Glow, this);
        MID->SetVectorParameterValue(TEXT("Color"), Color);
        MID->SetScalarParameterValue(TEXT("Intensity"), KindIntensity(static_cast<int32>(InKind)));
        MID->SetScalarParameterValue(TEXT("Opacity"), 1.f);
        Mesh->SetMaterial(0, MID);
        Ring->SetMaterial(0, MID);
    }
}

void ASpellFX::SetIntensity(float Intensity)
{
    if (MID) MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
}

void ASpellFX::SetNearFade(float Amount)
{
    if (MID) MID->SetScalarParameterValue(TEXT("NearFade"), Amount);
}

static ASpellFX* SpawnFXActor(UWorld* World, const FVector& Location, const FRotator& Rotation)
{
    if (!World) return nullptr;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return World->SpawnActor<ASpellFX>(ASpellFX::StaticClass(), Location, Rotation, Params);
}

ASpellFX* ASpellFX::SpawnBurst(UWorld* World, const FVector& Location, float Radius, const FLinearColor& Color, float InLife)
{
    ASpellFX* FX = SpawnFXActor(World, Location, FRotator::ZeroRotator);
    if (!FX) return nullptr;
    FX->Setup(EKind::Burst, GetShape(TEXT("Sphere")), Color, InLife);
    FX->BaseScale = FVector(Radius * 2.f / 100.f);   // engine sphere is 100 cm across
    FX->SetActorScale3D(FX->BaseScale * 0.3f);
    return FX;
}

ASpellFX* ASpellFX::SpawnBeam(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color, float Width, float InLife)
{
    const FVector D = To - From;
    const float Len = D.Size();
    if (Len < 1.f) return nullptr;
    // Cylinder axis is local Z: point it along the beam.
    ASpellFX* FX = SpawnFXActor(World, (From + To) * 0.5f, FRotationMatrix::MakeFromZ(D / Len).Rotator());
    if (!FX) return nullptr;
    FX->Setup(EKind::Beam, GetShape(TEXT("Cylinder")), Color, InLife);
    FX->BaseScale = FVector(Width / 100.f, Width / 100.f, Len / 100.f);
    FX->SetActorScale3D(FX->BaseScale);
    return FX;
}

ASpellFX* ASpellFX::SpawnColumn(UWorld* World, const FVector& Base, const FVector& Up, float Radius, float Height, const FLinearColor& Color, float InLife)
{
    ASpellFX* FX = SpawnFXActor(World, Base + Up * Height * 0.5f, FRotationMatrix::MakeFromZ(Up).Rotator());
    if (!FX) return nullptr;
    FX->Setup(EKind::Column, GetShape(TEXT("Cylinder")), Color, InLife);
    FX->BaseScale = FVector(Radius * 2.f / 100.f, Radius * 2.f / 100.f, Height / 100.f);
    FX->SetActorScale3D(FVector(FX->BaseScale.X, FX->BaseScale.Y, FX->BaseScale.Z * 0.1f));
    return FX;
}

ASpellFX* ASpellFX::SpawnDisc(UWorld* World, const FVector& Center, const FVector& Normal, float Radius, const FLinearColor& Color, float InLife)
{
    ASpellFX* FX = SpawnFXActor(World, Center + Normal * 4.f, FRotationMatrix::MakeFromZ(Normal).Rotator());
    if (!FX) return nullptr;
    FX->Setup(EKind::Disc, GetShape(TEXT("Cylinder")), Color, InLife);
    FX->BaseScale = FVector(Radius * 2.f / 100.f, Radius * 2.f / 100.f, 0.03f);
    FX->SetActorScale3D(FX->BaseScale);
    return FX;
}

ASpellFX* ASpellFX::SpawnFalling(UWorld* World, const FVector& From, const FVector& To, float Radius, const FLinearColor& Color, float Time)
{
    ASpellFX* FX = SpawnFXActor(World, From, FRotator::ZeroRotator);
    if (!FX) return nullptr;
    FX->Setup(EKind::Falling, GetShape(TEXT("Sphere")), Color, Time);
    FX->FallFrom = From;
    FX->FallTo = To;
    FX->FallRadius = Radius;
    FX->BaseScale = FVector(Radius * 2.f / 100.f);
    FX->SetActorScale3D(FX->BaseScale);
    return FX;
}

void ASpellFX::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;
    const float T = FMath::Clamp(Age / Life, 0.f, 1.f);

    switch (Kind)
    {
    case EKind::Burst:
        SetActorScale3D(BaseScale * FMath::Lerp(0.3f, 1.f, FMath::Sqrt(T)));
        break;
    case EKind::Column:
        SetActorScale3D(FVector(BaseScale.X * (1.f - 0.5f * T), BaseScale.Y * (1.f - 0.5f * T), BaseScale.Z * FMath::Min(T * 4.f, 1.f)));
        break;
    case EKind::Falling:
        SetActorLocation(FMath::Lerp(FallFrom, FallTo, T * T));
        if (T >= 1.f)
        {
            SpawnBurst(GetWorld(), FallTo, FallRadius * 3.f, FXColor, 0.45f);
            Destroy();
            return;
        }
        return;   // no fade while falling
    default:
        break;
    }

    if (!bPersistent)
    {
        if (MID) MID->SetScalarParameterValue(TEXT("Opacity"), 1.f - T);
        if (T >= 1.f) Destroy();
    }
}
