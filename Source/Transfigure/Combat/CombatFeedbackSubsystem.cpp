// Source/Transfigure/Combat/CombatFeedbackSubsystem.cpp

#include "Combat/CombatFeedbackSubsystem.h"
#include "Transfiguration/SpellTypes.h"
#include "Character/TMCharacter.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

UCombatFeedbackSubsystem* UCombatFeedbackSubsystem::Get(const UObject* WorldContext)
{
    const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    return World ? World->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr;
}

double UCombatFeedbackSubsystem::Now() const
{
    // Real time: numbers and toasts read the same during the temporal clap's slow-mo.
    return GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
}

void UCombatFeedbackSubsystem::ReportDamage(const FVector& Location, float Amount, ETransfigurationElement Element, bool bKill)
{
    if (Amount < 0.5f) return;
    FDamagePopup P;
    P.Location = Location;
    P.Amount = Amount;
    P.Color = Element == ETransfigurationElement::None ? FLinearColor(1.f, 1.f, 0.92f) : USpellLibrary::GetElementColor(Element);
    P.Time = Now();
    P.bKill = bKill;
    P.Drift = FMath::FRandRange(-30.f, 30.f);
    P.bCrit = bNextCrit;
    if (bNextCrit) LastCritTime = P.Time;
    bNextCrit = false;
    Popups.Add(P);
    if (Popups.Num() > 64) Popups.RemoveAt(0);

    LastHitTime = P.Time;
    if (bKill) LastKillTime = P.Time;
}

void UCombatFeedbackSubsystem::AddToast(const FString& Text, const FLinearColor& Color)
{
    // Same message again just refreshes it.
    for (FHudToast& T : Toasts)
    {
        if (T.Text == Text)
        {
            T.Time = Now();
            return;
        }
    }
    FHudToast T;
    T.Text = Text;
    T.Color = Color;
    T.Time = Now();
    Toasts.Add(T);
    if (Toasts.Num() > 5) Toasts.RemoveAt(0);
}

void UCombatFeedbackSubsystem::AddShake(float Trauma)
{
    if (ATMCharacter* Char = Cast<ATMCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
    {
        Char->AddCameraShake(Trauma);
    }
}

const TArray<FDamagePopup>& UCombatFeedbackSubsystem::GetPopups()
{
    const double T = Now();
    Popups.RemoveAll([T](const FDamagePopup& P) { return T - P.Time > PopupLife; });
    return Popups;
}

const TArray<FHudToast>& UCombatFeedbackSubsystem::GetToasts()
{
    const double T = Now();
    Toasts.RemoveAll([T](const FHudToast& Toast) { return T - Toast.Time > ToastLife; });
    return Toasts;
}
