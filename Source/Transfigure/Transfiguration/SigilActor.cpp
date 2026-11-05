// Source/TMovement/Private/Transfiguration/SigilActor.cpp

#include "Transfiguration/SigilActor.h"
#include "Transfiguration/TransfigurationEffectBase.h"
#include "Components/SphereComponent.h"
#include "Components/DecalComponent.h"
#include "TimerManager.h"
#include "Transfiguration/SpellFX.h"
#include "ProceduralMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Transfiguration/TransfigurationCastingComponent.h"

ASigilActor::ASigilActor()
{
    PrimaryActorTick.bCanEverTick = true;

    TriggerSphere = CreateDefaultSubobject<USphereComponent>(TEXT("TriggerSphere"));
    SetRootComponent(TriggerSphere);
    TriggerSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    SigilDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("SigilDecal"));
    SigilDecal->SetupAttachment(RootComponent);

    Glyph = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Glyph"));
    Glyph->SetupAttachment(RootComponent);
    Glyph->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Glyph->SetCastShadow(false);

    Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
    Light->SetupAttachment(RootComponent);
    Light->SetRelativeLocation(FVector(40.f, 0.f, 0.f));   // just off the surface (actor X = surface normal)
    Light->SetIntensity(2500.f);
    Light->SetAttenuationRadius(450.f);
    Light->SetCastShadows(false);
}

void ASigilActor::BuildGlyph(float RingRadius)
{
    // Lies flat on the surface: the actor faces along the surface normal (X), so build in local YZ.
    TArray<FVector> V;
    TArray<int32> Tri;
    auto P = [](float R, float A) { return FVector(1.f, R * FMath::Cos(A), R * FMath::Sin(A)); };
    auto AddQuad = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D)
    {
        const int32 I = V.Num();
        V.Append({ A, B, C, D });
        Tri.Append({ I, I + 1, I + 2, I, I + 2, I + 3 });
        Tri.Append({ I, I + 2, I + 1, I, I + 3, I + 2 });   // both sides
    };
    // Outer ring at the spell's reach.
    const int32 N = 64;
    const float Outer = FMath::Max(RingRadius, 60.f), Inner = Outer - FMath::Clamp(Outer * 0.05f, 6.f, 18.f);
    for (int32 i = 0; i < N; ++i)
    {
        const float A0 = 2.f * PI * i / N, A1 = 2.f * PI * (i + 1) / N;
        AddQuad(P(Inner, A0), P(Outer, A0), P(Outer, A1), P(Inner, A1));
    }
    // Inner glyph: a six-pointed star outline and a small ring, a constant size.
    const float S = 55.f;
    for (int32 i = 0; i < 6; ++i)
    {
        const float A0 = PI / 3.f * i, A1 = A0 + PI / 3.f * 2.f;   // every other point: two triangles
        const FVector From = P(S, A0), To = P(S, A1);
        const FVector Side = FVector::CrossProduct(FVector::XAxisVector, (To - From).GetSafeNormal()) * 2.5f;
        AddQuad(From - Side, From + Side, To + Side, To - Side);
    }
    for (int32 i = 0; i < 32; ++i)
    {
        const float A0 = 2.f * PI * i / 32, A1 = 2.f * PI * (i + 1) / 32;
        AddQuad(P(S * 0.32f, A0), P(S * 0.4f, A0), P(S * 0.4f, A1), P(S * 0.32f, A1));
    }
    Glyph->CreateMeshSection(0, V, Tri, TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);
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

    // Visual: ring at what the spell reaches (trigger radius for traps, blast radius otherwise).
    const bool bTrap = SpellDef->TriggerType == ESigilTriggerType::ProximityEnemy || SpellDef->TriggerType == ESigilTriggerType::ProximityAny
        || SpellDef->TriggerType == ESigilTriggerType::Stepped;
    float RingRadius = bTrap ? SpellDef->TriggerRadius : FMath::Max(SpellDef->FalloffRadius, SpellDef->ImpactRadius);
    if (SpellDef->bIsPersistentSigil) RingRadius = SpellDef->TriggerRadius;
    BuildGlyph(RingRadius);
    const FLinearColor Color = SpellDef->GetSpellColor();
    if (UMaterialInterface* Glow = ASpellFX::GetGlowMaterial())
    {
        GlyphMID = UMaterialInstanceDynamic::Create(Glow, this);
        GlyphMID->SetVectorParameterValue(TEXT("Color"), Color);
        GlyphMID->SetScalarParameterValue(TEXT("Intensity"), 3.f);
        GlyphMID->SetScalarParameterValue(TEXT("Opacity"), 1.f);
        Glyph->SetMaterial(0, GlyphMID);
    }
    Light->SetLightColor(Color);

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
    VisualAge += DeltaTime;
    if (!GlyphMID) return;
    if (FiredAge >= 0.f)
    {
        // Fired: flash bright, ring flies outward, fade out (the actor dies after 0.5 s).
        FiredAge += DeltaTime;
        const float T = FMath::Clamp(FiredAge / 0.45f, 0.f, 1.f);
        GlyphMID->SetScalarParameterValue(TEXT("Intensity"), FMath::Lerp(6.f, 0.f, T));
        Glyph->SetRelativeScale3D(FVector(1.f, 1.f + T * 0.6f, 1.f + T * 0.6f));
        Light->SetIntensity(FMath::Lerp(12000.f, 0.f, T));
        return;
    }
    // Armed: slow breathing glow; fades in when placed.
    const float In = FMath::Clamp(VisualAge / 0.25f, 0.f, 1.f);
    const float Pulse = 1.1f + 0.45f * FMath::Sin(VisualAge * 3.f);
    GlyphMID->SetScalarParameterValue(TEXT("Intensity"), Pulse * In);
    Light->SetIntensity((1800.f + 700.f * FMath::Sin(VisualAge * 3.f)) * In);
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

void ASigilActor::ConsumeForCombo()
{
    bArmed = false;
    GetWorldTimerManager().ClearTimer(TTLTimerHandle);
    GetWorldTimerManager().ClearTimer(AutoFireTimerHandle);
    FiredAge = 0.f;
    SetActorEnableCollision(false);
    if (SigilDecal) SigilDecal->SetVisibility(false);
    SetLifeSpan(0.5f);
}

void ASigilActor::ExecuteSpellEffect()
{
    if (!SpellDef || !SpellDef->EffectClass) return;

    // Another sigil close by? The two may fire together as a combo instead.
    if (AActor* CasterActor = Caster.Get())
    {
        if (UTransfigurationCastingComponent* Casting = CasterActor->FindComponentByClass<UTransfigurationCastingComponent>())
        {
            if (Casting->TryFireCombo(this)) return;
        }
    }

    bArmed = false;
    UE_LOG(LogTemp, Warning, TEXT("[TM] Sigil fired: %s at %s"), *SpellDef->SpellID.ToString(), *GetActorLocation().ToString());

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

    // Keep the glyph visible for its flash; hide everything else.
    FiredAge = 0.f;
    SetActorEnableCollision(false);
    if (SigilDecal) SigilDecal->SetVisibility(false);
    SetLifeSpan(0.5f);
}