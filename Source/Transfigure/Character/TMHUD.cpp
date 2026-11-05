// Source/Transfigure/Character/TMHUD.cpp

#include "Character/TMHUD.h"
#include "Character/TMCharacter.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Misc/App.h"

void ATMHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;

    const ATMCharacter* Char = Cast<ATMCharacter>(GetOwningPawn());
    if (!Char) return;

    const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);

    if (Char->IsSpellWheelOpen())
    {
        DrawSpellWheel(Char, Center);
        return;
    }

    // Top speed (ceiling run ready): fade in a light-blue border.
    const float BorderTarget = Char->CanStartCeilingRun() ? 1.f : 0.f;
    SpeedBorderAlpha = FMath::FInterpTo(SpeedBorderAlpha, BorderTarget, FApp::GetDeltaTime(), 6.f);
    if (SpeedBorderAlpha > 0.01f)
    {
        DrawSpeedBorder(SpeedBorderAlpha);
    }

    // Crosshair: shots, grapples and sigils all aim along the screen centre.
    DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.9f), Center.X - 2.f, Center.Y - 2.f, 4.f, 4.f);

    // Status line.
    const FString Status = FString::Printf(TEXT("Spell: %s   (tap Q place, hold Q wheel)      Health %.0f / %.0f      Mana %.0f / %.0f"),
        *Char->DrawSigilSpellID.ToString(), Char->GetHealth(), Char->GetMaxHealth(), Char->CurrentMana, Char->MaxMana);
    DrawCenteredText(Status, FVector2D(Center.X, Canvas->ClipY - 40.f), FLinearColor::White);
}

void ATMHUD::DrawSpeedBorder(float Alpha)
{
    // Layered strips: bright at the edge, fading inward, with a slow shimmer.
    const float W = Canvas->ClipX;
    const float H = Canvas->ClipY;
    const float Pulse = 0.85f + 0.15f * FMath::Sin(static_cast<float>(FApp::GetCurrentTime()) * 4.f);
    const int32 NumLayers = 6;
    for (int32 i = 0; i < NumLayers; ++i)
    {
        const float T = 3.f + i * 5.f;   // strip thickness grows inward
        const float A = Alpha * Pulse * (0.32f - i * 0.045f);
        if (A <= 0.f) break;
        const FLinearColor C(0.45f, 0.85f, 1.f, A);
        DrawRect(C, 0.f, 0.f, W, T);
        DrawRect(C, 0.f, H - T, W, T);
        DrawRect(C, 0.f, T, T, H - 2.f * T);
        DrawRect(C, W - T, T, T, H - 2.f * T);
    }
}

void ATMHUD::DrawSpellWheel(const ATMCharacter* Char, const FVector2D& Center)
{
    const TArray<FName>& Spells = Char->GetSpellWheelOptions();
    const int32 Highlighted = Char->GetSpellWheelHighlighted();
    const float Radius = FMath::Min(Canvas->ClipX, Canvas->ClipY) * 0.2f;

    // Dim the screen so the wheel reads clearly.
    DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);

    if (Spells.Num() == 0)
    {
        DrawCenteredText(TEXT("No spells in DA_SpellRegistry"), Center, FLinearColor::Red, 1.5f);
        return;
    }

    const float Step = 2.f * PI / Spells.Num();
    for (int32 i = 0; i < Spells.Num(); ++i)
    {
        // First spell at the top, going clockwise.
        const float Angle = -PI * 0.5f + Step * i;
        const FVector2D Pos = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
        const bool bHi = (i == Highlighted);

        if (bHi)
        {
            DrawRect(FLinearColor(1.f, 0.8f, 0.2f, 0.35f), Pos.X - 90.f, Pos.Y - 22.f, 180.f, 44.f);
        }
        DrawCenteredText(Spells[i].ToString(), Pos, bHi ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor::White, bHi ? 1.6f : 1.2f);
    }

    // Line from the centre toward where the mouse is pointing.
    const FVector2D Cursor = Char->GetSpellWheelCursor();
    DrawLine(Center.X, Center.Y, Center.X + Cursor.X * Radius, Center.Y + Cursor.Y * Radius, FLinearColor::White, 2.f);
    DrawCenteredText(TEXT("Release Q to select"), FVector2D(Center.X, Center.Y + Radius + 60.f), FLinearColor(0.8f, 0.8f, 0.8f));
}

void ATMHUD::DrawCenteredText(const FString& Text, const FVector2D& Pos, const FLinearColor& Color, float Scale)
{
    UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
    float W = 0.f, H = 0.f;
    GetTextSize(Text, W, H, Font, Scale);
    DrawText(Text, Color, Pos.X - W * 0.5f, Pos.Y - H * 0.5f, Font, Scale);
}
