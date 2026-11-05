// Source/Transfigure/Combat/CombatFeedbackSubsystem.h
//
// Collects combat feedback for the HUD (SPELL_PLAN A8, B4): floating damage
// numbers, hit / kill markers on the crosshair, toasts, and camera shake requests.
// Anything can report into it; ATMHUD draws it.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Transfiguration/TransfigurationTypes.h"
#include "CombatFeedbackSubsystem.generated.h"

USTRUCT()
struct FDamagePopup
{
    GENERATED_BODY()

    FVector Location = FVector::ZeroVector;
    float Amount = 0.f;
    FLinearColor Color = FLinearColor::White;
    double Time = 0.0;
    bool bKill = false;
    /** Headshot: drawn gold and bigger. */
    bool bCrit = false;
    /** Random sideways drift so stacked numbers don't overlap. */
    float Drift = 0.f;
};

USTRUCT()
struct FHudToast
{
    GENERATED_BODY()

    FString Text;
    FLinearColor Color = FLinearColor::White;
    double Time = 0.0;
};

UCLASS()
class TRANSFIGURE_API UCombatFeedbackSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    static UCombatFeedbackSubsystem* Get(const UObject* WorldContext);

    /** An enemy took damage. Element None = gun / physical (white). */
    void ReportDamage(const FVector& Location, float Amount, ETransfigurationElement Element, bool bKill);

    /** Short message at the top of the screen. */
    void AddToast(const FString& Text, const FLinearColor& Color = FLinearColor::White);

    /** Ask the player's camera to shake (0..1, adds up, decays). */
    void AddShake(float Trauma);

    /** The next damage reported is a headshot (the gun calls this just before applying it). */
    void MarkNextHitCrit() { bNextCrit = true; }
    void ClearNextHitCrit() { bNextCrit = false; }
    double GetLastCritTime() const { return LastCritTime; }

    double GetLastHitTime() const { return LastHitTime; }
    double GetLastKillTime() const { return LastKillTime; }
    double Now() const;

    /** Drops old entries and returns what's still showing. */
    const TArray<FDamagePopup>& GetPopups();
    const TArray<FHudToast>& GetToasts();

    static constexpr double PopupLife = 0.9;
    static constexpr double ToastLife = 2.8;

private:
    UPROPERTY()
    TArray<FDamagePopup> Popups;

    UPROPERTY()
    TArray<FHudToast> Toasts;

    double LastHitTime = -100.0;
    double LastKillTime = -100.0;
    double LastCritTime = -100.0;
    bool bNextCrit = false;
};
