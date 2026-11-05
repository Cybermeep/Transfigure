// Source/Transfigure/Ringworld/RingLaunchComponent.cpp

#include "RingLaunchComponent.h"
#include "RingLaunchPad.h"
#include "RingWorldSettings.h"
#include "RingContentStreamer.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Character/TMCharacter.h"
#include "Grapple/GrappleComponent.h"
#include "States/TMState_Jump.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

URingLaunchComponent::URingLaunchComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

ATMCharacter* URingLaunchComponent::GetCharacter() const
{
    return Cast<ATMCharacter>(GetOwner());
}

void URingLaunchComponent::SetActivePad(ARingLaunchPad* Pad)
{
    if (ActivePad.Get() == Pad) return;
    ActivePad = Pad;
    bTargetValid = false;
    TargetPad = nullptr;
    if (Pad) RefreshKnownPads();
    UE_LOG(LogTemp, Warning, TEXT("[TM] Launch pad %s"), Pad ? TEXT("entered: look up at the ring and click where to land") : TEXT("left"));
}

void URingLaunchComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (bFlying)
    {
        UpdateFlight(DeltaTime);
    }
    else if (ActivePad.IsValid())
    {
        UpdateTarget();
    }
}

void URingLaunchComponent::RefreshKnownPads()
{
    KnownPads.Reset();
    for (TActorIterator<ARingLaunchPad> It(GetWorld()); It; ++It)
    {
        KnownPads.Add(*It);
    }
}

void URingLaunchComponent::UpdateTarget()
{
    bTargetValid = false;
    TargetPad = nullptr;
    ATMCharacter* Char = GetCharacter();
    APlayerController* PC = Char ? Cast<APlayerController>(Char->GetController()) : nullptr;
    if (!PC) return;

    FVector CamLoc;
    FRotator CamRot;
    PC->GetPlayerViewPoint(CamLoc, CamRot);
    // Aiming close to another pad snaps onto it: that's how you get back.
    const FVector ViewDir = CamRot.Vector();
    const double SnapCos = FMath::Cos(FMath::DegreesToRadians(PadSnapDegrees));
    double BestCos = SnapCos;
    for (const TWeakObjectPtr<ARingLaunchPad>& PadPtr : KnownPads)
    {
        ARingLaunchPad* Pad = PadPtr.Get();
        if (!Pad || Pad == ActivePad.Get()) continue;
        const FVector ToPad = Pad->GetActorLocation() - CamLoc;
        const double Cos = FVector::DotProduct(ToPad.GetSafeNormal(), ViewDir);
        if (Cos > BestCos && ToPad.Size() >= ActivePad->MinLaunchDistance)
        {
            BestCos = Cos;
            TargetPad = Pad;
        }
    }
    if (ARingLaunchPad* Pad = TargetPad.Get())
    {
        Target = Pad->GetActorLocation();
        TargetCoords = URingWorldGeometry::WorldToRing(URingWorldSettings::GetSpec(), Target);
        TargetDistance = FVector::Dist(Char->GetActorLocation(), Target);
        bTargetValid = true;
        return;
    }

    FVector Hit;
    FRingCoords Coords;
    if (!URingWorldGeometry::RaycastRingSurface(URingWorldSettings::GetSpec(), CamLoc, ViewDir, Hit, Coords)) return;

    TargetDistance = FVector::Dist(Char->GetActorLocation(), Hit);
    if (TargetDistance < ActivePad->MinLaunchDistance) return;
    Target = Hit;
    TargetCoords = Coords;
    bTargetValid = true;
}

void URingLaunchComponent::SpawnReturnPad()
{
    const FVector Up = -URingWorldGeometry::GetGravityDirection(URingWorldSettings::GetSpec(), Target);
    FVector Fwd = FVector::VectorPlaneProject(FVector::YAxisVector, Up).GetSafeNormal();
    if (Fwd.IsNearlyZero()) Fwd = FVector::CrossProduct(Up, FVector::XAxisVector).GetSafeNormal();
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ARingLaunchPad* Pad = GetWorld()->SpawnActor<ARingLaunchPad>(ARingLaunchPad::StaticClass(),
        Target + Up * 10.f, FRotationMatrix::MakeFromXZ(Fwd, Up).Rotator(), Params);
    if (!Pad) return;
    Pad->PadName = FString::Printf(TEXT("Landing %d"), ++LandingCount);
    ReturnPads.Add(Pad);
    while (ReturnPads.Num() > MaxReturnPads)
    {
        if (ARingLaunchPad* Old = ReturnPads[0].Get()) Old->Destroy();
        ReturnPads.RemoveAt(0);
    }
    UE_LOG(LogTemp, Warning, TEXT("[TM] Return pad '%s' placed at the landing spot"), *Pad->PadName);
}

bool URingLaunchComponent::TryLaunch()
{
    if (!HasTarget()) return false;
    ATMCharacter* Char = GetCharacter();
    if (!Char) return false;

    // Let go of anything else first.
    if (Char->GrappleComponent) Char->GrappleComponent->ReleaseGrapple();
    if (Char->IsCeilingRunning()) Char->TransitionTo<TMState_Fall>();

    const FRingWorldSpec& Spec = URingWorldSettings::GetSpec();
    const float Half = Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    StartPos = Char->GetActorLocation();
    StartUp = -URingWorldGeometry::GetGravityDirection(Spec, StartPos);
    EndUp = -URingWorldGeometry::GetGravityDirection(Spec, Target);
    EndPos = Target + EndUp * (Half + ArrivalHeight);

    // "Up" turns about the spin axis (+Y) from here to there. Exactly opposite: turn the way you're looking.
    const double SinA = FVector::DotProduct(FVector::YAxisVector, FVector::CrossProduct(StartUp, EndUp));
    const double CosA = FVector::DotProduct(StartUp, EndUp);
    TurnAngle = FMath::Atan2(SinA, CosA);
    if (FMath::Abs(SinA) < 1e-4 && CosA < 0.0)
    {
        const FVector Spinward = FVector::CrossProduct(FVector::YAxisVector, StartUp);
        TurnAngle = FVector::DotProduct(Target - StartPos, Spinward) >= 0.0 ? UE_DOUBLE_PI : -UE_DOUBLE_PI;
    }

    const double Dist = FVector::Dist(StartPos, EndPos);
    FlightTime = FMath::Lerp(FlightTimeMin, FlightTimeMax, FMath::Clamp(Dist / (2.0 * Spec.Radius), 0.0, 1.0));
    ArcHeight = FMath::Max<double>(MinArcHeight, Dist * ArcHeightPerDistance);
    Elapsed = 0.f;
    PrevPos = StartPos;
    bFlying = true;

    Char->GetCharacterMovement()->SetMovementMode(MOVE_None);
    Char->SetSurfaceUpDirect(StartUp, true);
    Char->AddFOVKick(LaunchFOVKick);

    // Build the destination in the background while we fly, keeping the landing spot clear.
    if (ARingContentStreamer* Streamer = ARingContentStreamer::Get(GetWorld()))
    {
        Streamer->AddClearPoint(Target);
        Streamer->SetFocusOverride(true, Target);
    }

    UE_LOG(LogTemp, Warning, TEXT("[TM] LAUNCH: %.0f km to %s (arc %.0f km / lateral %.1f km), %.1fs flight"),
        Dist / 1e5, TargetPad.IsValid() ? *TargetPad->PadName : TEXT("open ring"), TargetCoords.Arc / 1e5, TargetCoords.Lateral / 1e5, FlightTime);
    return true;
}

void URingLaunchComponent::UpdateFlight(float DeltaTime)
{
    ATMCharacter* Char = GetCharacter();
    if (!Char)
    {
        bFlying = false;
        return;
    }
    auto* CMC = Char->GetCharacterMovement();

    Elapsed += DeltaTime;
    const double T = FMath::Clamp(Elapsed / FMath::Max(FlightTime, 0.1f), 0.0, 1.0);
    const double S = T * T * T * (T * (6.0 * T - 15.0) + 10.0);   // ease in and out

    // Body "up" turns smoothly from the start's ring-up to the landing spot's.
    const FVector Up = FQuat(FVector::YAxisVector, TurnAngle * S).RotateVector(StartUp);
    const FVector Pos = FMath::Lerp(StartPos, EndPos, S) + Up * (ArcHeight * 4.0 * T * (1.0 - T));

    Char->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
    CMC->Velocity = DeltaTime > 0.f ? (Pos - PrevPos) / DeltaTime : FVector::ZeroVector;
    PrevPos = Pos;
    Char->SetSurfaceUpDirect(Up, true);
    Char->SteerViewToward(Target, DeltaTime, ViewSteerRate);

    if (T >= 1.0)
    {
        // Arrive: back on ring gravity, dropping onto the spot.
        bFlying = false;
        Char->SetSurfaceUpDirect(FVector::ZeroVector, false);
        CMC->SetMovementMode(MOVE_Falling);
        CMC->Velocity = -EndUp * LandingSpeed;
        Char->AddFOVKick(8.f);
        ActivePad = nullptr;
        if (ARingContentStreamer* Streamer = ARingContentStreamer::Get(GetWorld()))
        {
            Streamer->SetFocusOverride(false);
        }
        if (!TargetPad.IsValid())
        {
            SpawnReturnPad();
        }
        UE_LOG(LogTemp, Warning, TEXT("[TM] LANDED at arc %.0f km / lateral %.1f km"), TargetCoords.Arc / 1e5, TargetCoords.Lateral / 1e5);
    }
}
