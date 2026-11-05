// Source/Transfigure/Transfiguration/SpellZone.cpp

#include "Transfiguration/SpellZone.h"
#include "Transfiguration/SpellFX.h"
#include "Transfiguration/SpellStructure.h"
#include "Transfiguration/TransfigurationDefinition.h"
#include "Enemy/BaseEnemy.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RingFrameUtils.h"

ASpellZone::ASpellZone()
{
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

ASpellZone* ASpellZone::Start(UWorld* World, EMode InMode, UTransfigurationDefinition* InDef, AActor* InInstigator,
    const FVector& InOrigin, const FVector& InLineFrom, float OverrideDuration)
{
    if (!World || !InDef) return nullptr;
    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ASpellZone* Z = World->SpawnActor<ASpellZone>(ASpellZone::StaticClass(), InOrigin, FRotator::ZeroRotator, SP);
    if (!Z) return nullptr;
    Z->Def = InDef;
    Z->Instigator = InInstigator;
    // Damage etc. fixed at cast time (so the temporal-clap bonus sticks if it set it off).
    Z->Params = SpellHit::FromDefinition(InDef, InInstigator);
    Z->Mode = InMode;
    Z->Origin = InOrigin;
    Z->LineFrom = InLineFrom;
    Z->Duration = OverrideDuration > 0.f ? OverrideDuration
        : InMode == EMode::Delayed ? FMath::Max(InDef->Delay, 0.05f)
        : InMode == EMode::Line ? FMath::Max(FVector::Dist(InLineFrom, InOrigin) / FMath::Max(InDef->LineSpeed, 100.f), 0.05f)
        : InMode == EMode::DashBurst ? 0.45f
        : FMath::Max(InDef->Duration, 0.1f);

    const FLinearColor C = Z->Params.Color;
    const FVector Up = URingFrameUtils::GetUpAt(InOrigin);
    switch (InMode)
    {
    case EMode::Aura:
    case EMode::Pull:
        Z->Visual = ASpellFX::SpawnDisc(World, Z->Center(), Up, Z->Params.OuterRadius, C, Z->Duration);
        if (Z->Visual)
        {
            Z->Visual->SetPersistent(true);
            Z->Visual->SetIntensity(InMode == EMode::Pull ? 4.f : 2.5f);
        }
        break;
    case EMode::Delayed:
        // Something falls in from high above while you wait.
        ASpellFX::SpawnFalling(World, InOrigin + Up * 6000.f, InOrigin, FMath::Max(Z->Params.InnerRadius * 0.35f, 80.f), C, Z->Duration);
        ASpellFX::SpawnDisc(World, InOrigin, Up, Z->Params.OuterRadius, C, Z->Duration);
        break;
    default:
        break;
    }
    // Pulls mark everything they'll grab straight away.
    if (InMode == EMode::Pull && InDef->Status != ESpellStatus::None)
    {
        for (ABaseEnemy* E : SpellHit::FindEnemies(World, InOrigin, Z->Params.OuterRadius * 1.5f, InInstigator, true))
        {
            FSpellHitParams Mark = Z->Params;
            Mark.Damage = 0.f;
            Mark.Knockback = Mark.Launch = 0.f;
            SpellHit::ApplySingle(InInstigator, E, E->GetActorLocation(), Mark);
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("[TM] Spell zone %s: %s for %.1fs"), *InDef->SpellID.ToString(),
        InMode == EMode::Aura ? TEXT("aura") : InMode == EMode::Pull ? TEXT("pull") : InMode == EMode::Line ? TEXT("line")
        : InMode == EMode::Delayed ? TEXT("delayed") : TEXT("dash"), Z->Duration);
    return Z;
}

FVector ASpellZone::Center() const
{
    if (Def && Def->bFollowCaster && Instigator.IsValid()) return Instigator->GetActorLocation();
    return Origin;
}

void ASpellZone::Hit(const FVector& At)
{
    SpellHit::ApplyArea(GetWorld(), Instigator.Get(), At, Params);
}

void ASpellZone::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Def)
    {
        Destroy();
        return;
    }
    Age += DeltaSeconds;
    UWorld* World = GetWorld();
    const FVector C = Center();
    if (Visual) Visual->SetActorLocation(C + URingFrameUtils::GetUpAt(C) * 4.f);

    switch (Mode)
    {
    case EMode::Aura:
        if (Age >= NextTick)
        {
            NextTick += FMath::Max(Def->TickInterval, 0.05f);
            if (Def->bOneRandomTargetPerTick)
            {
                TArray<ABaseEnemy*> Targets = SpellHit::FindEnemies(World, C, Params.OuterRadius, Instigator.Get(), true);
                if (Targets.Num() > 0)
                {
                    ABaseEnemy* T = Targets[FMath::RandRange(0, Targets.Num() - 1)];
                    ASpellFX::SpawnBeam(World, C + URingFrameUtils::GetUpAt(C) * 600.f, T->GetActorLocation(), Params.Color, 16.f, 0.2f);
                    SpellHit::ApplySingle(Instigator.Get(), T, T->GetActorLocation(), Params);
                }
            }
            else
            {
                Hit(C);
            }
        }
        break;

    case EMode::Pull:
        for (ABaseEnemy* E : SpellHit::FindEnemies(World, C, Params.OuterRadius * 1.5f, Instigator.Get(), true))
        {
            const FVector Up = URingFrameUtils::GetUpFor(E);
            const FVector ToCenter = URingFrameUtils::StripVertical(C - E->GetActorLocation(), Up);
            if (ToCenter.Size() < 60.f) continue;
            UCharacterMovementComponent* CMC = E->GetCharacterMovement();
            CMC->Velocity += ToCenter.GetSafeNormal() * Def->PullStrength * DeltaSeconds;
            if (CMC->IsMovingOnGround()) CMC->SetMovementMode(MOVE_Falling);   // let it slide
        }
        SpellHit::PushLoose(World, C, Params.OuterRadius, -Def->PullStrength * DeltaSeconds);
        if (Age >= Duration)
        {
            FSpellHitParams Collapse = Params;
            Collapse.Knockback = FMath::Max(Collapse.Knockback, 600.f);
            SpellHit::ApplyArea(World, Instigator.Get(), C, Collapse);
            ASpellFX::SpawnBurst(World, C, Params.OuterRadius, Params.Color, 0.4f);
        }
        break;

    case EMode::Line:
    {
        const FVector Dir = (Origin - LineFrom).GetSafeNormal();
        const float Total = FVector::Dist(LineFrom, Origin);
        LineTravelled = FMath::Min(Total, LineTravelled + FMath::Max(Def->LineSpeed, 100.f) * DeltaSeconds);
        while (NextLineStrike <= LineTravelled)
        {
            const FVector P = LineFrom + Dir * NextLineStrike;
            const FVector Up = URingFrameUtils::GetUpAt(P);
            FSpellHitParams Small = Params;
            Small.OuterRadius = FMath::Max(Def->LineSpacing, Params.InnerRadius);
            SpellHit::ApplyArea(World, Instigator.Get(), P, Small);
            ASpellFX::SpawnColumn(World, P, Up, Def->LineSpacing * 0.35f, FMath::Max(Def->StructureSize.Z, 150.f), Params.Color, 0.45f);
            NextLineStrike += FMath::Max(Def->LineSpacing, 50.f);
        }
        if (LineTravelled >= Total) Age = Duration;
        break;
    }

    case EMode::Delayed:
        if (Age >= Duration)
        {
            Hit(Origin);
            ASpellFX::SpawnBurst(World, Origin, Params.OuterRadius, Params.Color, 0.5f);
        }
        break;

    case EMode::DashBurst:
        if (Age >= Duration)
        {
            Hit(C);
            ASpellFX::SpawnBurst(World, C, Params.OuterRadius, Params.Color, 0.35f);
        }
        break;
    }

    if (Age >= Duration) Destroy();
}

void ASpellZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (Visual) Visual->Destroy();
    Super::EndPlay(EndPlayReason);
}
