// Source/Transfigure/Character/TMHUD.h
//
// Canvas HUD (no UMG assets needed). ATMCharacter installs it automatically.
//
//   bottom-left    health and mana bars, active buffs
//   bottom-right   selected spell (element colour, cost, cooldown), sigil count, key hints
//   bottom-centre  speed meter (fills toward ceiling-run speed)
//   centre         crosshair with hit / kill marker; floating damage numbers; toasts on top
//   edges          hurt flash and low-health vignette; light-blue border at ceiling-run speed
//   hold Q         spell wheel (equipped spells, element colours, cost, cooldown)
//   B              spellbook: known spells by element, details, click to set the wheel

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TMHUD.generated.h"

class ATMCharacter;
class UTransfigurationDefinition;

UCLASS()
class TRANSFIGURE_API ATMHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;

    /** Left click while the spellbook is open (called by ATMCharacter). */
    void SpellbookClick();

private:
    void DrawSpeedBorder(float Alpha);
    float SpeedBorderAlpha = 0.f;

    void DrawLaunchInfo(const ATMCharacter* Char, const FVector2D& Center);
    void DrawCrosshair(const FVector2D& Center);
    void DrawDamagePopups();
    void DrawToasts();
    void DrawHurt(const ATMCharacter* Char);
    void DrawPlayerStatus(const ATMCharacter* Char);
    void DrawSpellPanel(const ATMCharacter* Char);
    void DrawSpeedMeter(const ATMCharacter* Char);
    void DrawSpellWheel(const ATMCharacter* Char, const FVector2D& Center);
    void DrawSpellbook(ATMCharacter* Char);
    void DrawSpellDetails(const UTransfigurationDefinition* Def, float X, float Y, float W);

    void DrawCenteredText(const FString& Text, const FVector2D& Pos, const FLinearColor& Color, float Scale = 1.f);
    void DrawLeftText(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale = 1.f);
    void DrawBar(float X, float Y, float W, float H, float Fill, const FLinearColor& Color);
    void DrawFrame(float X, float Y, float W, float H, const FLinearColor& Color, float Thickness = 2.f);
    TArray<FString> WrapText(const FString& Text, float MaxWidth, float Scale);
    double RealNow() const;

    // Spellbook hit-testing (rebuilt every frame it draws).
    struct FBookHit
    {
        FBox2D Rect;
        bool bSlot = false;
        FName Spell;
        int32 Slot = INDEX_NONE;
    };
    TArray<FBookHit> BookHits;
    FName BookSelected;
    FName BookHover;
};
