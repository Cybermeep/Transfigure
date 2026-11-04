// Source/Transfigure/Character/TMHUD.h
//
// Placeholder HUD drawn with the canvas (no UMG assets needed):
// crosshair, current spell / health / mana, and the hold-Q spell wheel.
// ATMCharacter installs it on its PlayerController automatically.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TMHUD.generated.h"

UCLASS()
class TRANSFIGURE_API ATMHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;

private:
    void DrawSpellWheel(const class ATMCharacter* Char, const FVector2D& Center);
    void DrawCenteredText(const FString& Text, const FVector2D& Pos, const FLinearColor& Color, float Scale = 1.f);
};
