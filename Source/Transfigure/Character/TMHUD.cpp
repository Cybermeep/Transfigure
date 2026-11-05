// Source/Transfigure/Character/TMHUD.cpp

#include "Character/TMHUD.h"
#include "Character/TMCharacter.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/WeaponComponent.h"
#include "Transfiguration/TransfigurationCastingComponent.h"
#include "Transfiguration/TransfigurationDefinition.h"
#include "Transfiguration/TransfigurationEffectBase.h"
#include "Transfiguration/BuffEffectComponent.h"
#include "Transfiguration/SpellTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "Ringworld/RingLaunchComponent.h"
#include "Ringworld/RingLaunchPad.h"
#include "Ringworld/RingContentStreamer.h"

namespace
{
    const FLinearColor Panel(0.02f, 0.02f, 0.05f, 0.62f);
    const FLinearColor Dim(0.75f, 0.78f, 0.85f, 1.f);

    FString ArchetypeName(const UTransfigurationDefinition* Def)
    {
        if (!Def || !Def->EffectClass)
        {
            return Def && Def->bIsPersistentSigil ? TEXT("Persistent sigil") : TEXT("-");
        }
        FString N = Def->EffectClass->GetName();
        N.RemoveFromStart(TEXT("SpellEffect_"));
        N.RemoveFromEnd(TEXT("Effect"));
        N.RemoveFromStart(TEXT("BP_"));
        return N;
    }

    FString DisplayNameOf(const UTransfigurationDefinition* Def, FName Fallback)
    {
        return Def && !Def->DisplayName.IsEmpty() ? Def->DisplayName.ToString() : Fallback.ToString();
    }
}

double ATMHUD::RealNow() const
{
    return GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
}

void ATMHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;

    ATMCharacter* Char = Cast<ATMCharacter>(GetOwningPawn());
    if (!Char) return;
    const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);

    if (Char->IsSpellbookOpen())
    {
        DrawSpellbook(Char);
        DrawToasts();
        return;
    }

    // Top speed (ceiling run ready): fade in a light-blue border.
    const float BorderTarget = Char->CanStartCeilingRun() ? 1.f : 0.f;
    SpeedBorderAlpha = FMath::FInterpTo(SpeedBorderAlpha, BorderTarget, FApp::GetDeltaTime(), 6.f);
    if (SpeedBorderAlpha > 0.01f) DrawSpeedBorder(SpeedBorderAlpha);

    DrawHurt(Char);
    DrawDamagePopups();
    DrawLaunchInfo(Char, Center);

    if (Char->IsSpellWheelOpen())
    {
        DrawSpellWheel(Char, Center);
        return;
    }

    DrawCrosshair(Center);
    DrawPlayerStatus(Char);
    DrawSpellPanel(Char);
    DrawSpeedMeter(Char);
    DrawToasts();
}

// ── small drawing helpers ───────────────────────────────────────────────────

void ATMHUD::DrawCenteredText(const FString& Text, const FVector2D& Pos, const FLinearColor& Color, float Scale)
{
    UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
    float W = 0.f, H = 0.f;
    GetTextSize(Text, W, H, Font, Scale);
    // Dark drop shadow so pale colours (ice, lightning) read against bright walls and sky.
    const float Off = FMath::Max(1.f, 1.5f * Scale);
    DrawText(Text, FLinearColor(0.f, 0.f, 0.f, Color.A * 0.7f), Pos.X - W * 0.5f + Off, Pos.Y - H * 0.5f + Off, Font, Scale);
    DrawText(Text, Color, Pos.X - W * 0.5f, Pos.Y - H * 0.5f, Font, Scale);
}

void ATMHUD::DrawLeftText(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale)
{
    DrawText(Text, Color, X, Y, GEngine ? GEngine->GetMediumFont() : nullptr, Scale);
}

void ATMHUD::DrawBar(float X, float Y, float W, float H, float Fill, const FLinearColor& Color)
{
    DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), X - 2.f, Y - 2.f, W + 4.f, H + 4.f);
    DrawRect(FLinearColor(Color.R * 0.25f, Color.G * 0.25f, Color.B * 0.25f, 0.9f), X, Y, W, H);
    DrawRect(Color, X, Y, W * FMath::Clamp(Fill, 0.f, 1.f), H);
}

void ATMHUD::DrawFrame(float X, float Y, float W, float H, const FLinearColor& Color, float T)
{
    DrawRect(Color, X, Y, W, T);
    DrawRect(Color, X, Y + H - T, W, T);
    DrawRect(Color, X, Y, T, H);
    DrawRect(Color, X + W - T, Y, T, H);
}

TArray<FString> ATMHUD::WrapText(const FString& Text, float MaxWidth, float Scale)
{
    TArray<FString> Lines;
    TArray<FString> Words;
    Text.ParseIntoArrayWS(Words);
    UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
    FString Line;
    for (const FString& Word : Words)
    {
        const FString Try = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
        float W = 0.f, H = 0.f;
        GetTextSize(Try, W, H, Font, Scale);
        if (W > MaxWidth && !Line.IsEmpty())
        {
            Lines.Add(Line);
            Line = Word;
        }
        else
        {
            Line = Try;
        }
    }
    if (!Line.IsEmpty()) Lines.Add(Line);
    return Lines;
}

// ── world-space / feedback ─────────────────────────────────────────────────

void ATMHUD::DrawSpeedBorder(float Alpha)
{
    // Layered strips: bright at the edge, fading inward, with a slow shimmer.
    const float W = Canvas->ClipX;
    const float H = Canvas->ClipY;
    const float Pulse = 0.85f + 0.15f * FMath::Sin(static_cast<float>(FApp::GetCurrentTime()) * 4.f);
    const int32 NumLayers = 6;
    for (int32 i = 0; i < NumLayers; ++i)
    {
        const float T = 3.f + i * 5.f;
        const float A = Alpha * Pulse * (0.32f - i * 0.045f);
        if (A <= 0.f) break;
        const FLinearColor C(0.45f, 0.85f, 1.f, A);
        DrawRect(C, 0.f, 0.f, W, T);
        DrawRect(C, 0.f, H - T, W, T);
        DrawRect(C, 0.f, T, T, H - 2.f * T);
        DrawRect(C, W - T, T, T, H - 2.f * T);
    }
}

void ATMHUD::DrawHurt(const ATMCharacter* Char)
{
    const float SinceHurt = static_cast<float>(RealNow() - Char->GetLastHurtTime());
    const float Low = Char->GetMaxHealth() > 0.f ? 1.f - Char->GetHealth() / Char->GetMaxHealth() : 0.f;
    float A = SinceHurt < 0.4f ? (1.f - SinceHurt / 0.4f) * 0.35f : 0.f;
    if (Low > 0.7f) A = FMath::Max(A, (Low - 0.7f) / 0.3f * (0.18f + 0.08f * FMath::Sin(static_cast<float>(RealNow()) * 5.f)));
    if (A <= 0.01f) return;
    const float W = Canvas->ClipX, H = Canvas->ClipY;
    for (int32 i = 0; i < 6; ++i)
    {
        const float T = 10.f + i * 18.f;
        const FLinearColor C(0.8f, 0.f, 0.f, A * (1.f - i / 6.f) * 0.5f);
        DrawRect(C, 0.f, 0.f, W, T);
        DrawRect(C, 0.f, H - T, W, T);
        DrawRect(C, 0.f, T, T, H - 2.f * T);
        DrawRect(C, W - T, T, T, H - 2.f * T);
    }
}

void ATMHUD::DrawCrosshair(const FVector2D& Center)
{
    DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.9f), Center.X - 2.f, Center.Y - 2.f, 4.f, 4.f);

    UCombatFeedbackSubsystem* Feedback = UCombatFeedbackSubsystem::Get(this);
    if (!Feedback) return;
    const double Now = Feedback->Now();
    const float SinceKill = static_cast<float>(Now - Feedback->GetLastKillTime());
    const float SinceHit = static_cast<float>(Now - Feedback->GetLastHitTime());
    const float SinceCrit = static_cast<float>(Now - Feedback->GetLastCritTime());
    const bool bKill = SinceKill < 0.35f;
    const bool bCrit = SinceCrit < 0.2f;
    if (!bKill && !bCrit && SinceHit > 0.15f) return;
    // Hit marker: four short diagonals (gold for a headshot, red and bigger for a kill).
    const float A = bKill ? 1.f - SinceKill / 0.35f : bCrit ? 1.f - SinceCrit / 0.2f : 1.f - SinceHit / 0.15f;
    const FLinearColor C = bKill ? FLinearColor(1.f, 0.2f, 0.15f, A) : bCrit ? FLinearColor(1.f, 0.8f, 0.2f, A) : FLinearColor(1.f, 1.f, 1.f, A);
    const float In = bKill ? 9.f : bCrit ? 8.f : 7.f, Out = bKill ? 20.f : bCrit ? 17.f : 14.f;
    for (int32 i = 0; i < 4; ++i)
    {
        const float SX = (i & 1) ? 1.f : -1.f, SY = (i & 2) ? 1.f : -1.f;
        DrawLine(Center.X + SX * In, Center.Y + SY * In, Center.X + SX * Out, Center.Y + SY * Out, C, 2.f);
    }
}

void ATMHUD::DrawDamagePopups()
{
    UCombatFeedbackSubsystem* Feedback = UCombatFeedbackSubsystem::Get(this);
    APlayerController* PC = GetOwningPlayerController();
    if (!Feedback || !PC) return;
    FVector CamLoc;
    FRotator CamRot;
    PC->GetPlayerViewPoint(CamLoc, CamRot);
    const double Now = Feedback->Now();
    for (const FDamagePopup& P : Feedback->GetPopups())
    {
        if (FVector::DotProduct(P.Location - CamLoc, CamRot.Vector()) <= 0.f) continue;
        const float T = static_cast<float>((Now - P.Time) / UCombatFeedbackSubsystem::PopupLife);
        const FVector S = Project(P.Location);
        const float Scale = (P.bKill ? 1.5f : 1.f) * (P.bCrit ? 1.3f : 1.f)
            * FMath::GetMappedRangeValueClamped(FVector2D(5.f, 120.f), FVector2D(0.9f, 1.6f), P.Amount);
        const FLinearColor Base = P.bCrit ? FLinearColor(1.f, 0.82f, 0.25f) : P.Color;
        const FLinearColor C(Base.R, Base.G, Base.B, 1.f - T * T);
        DrawCenteredText(FString::Printf(TEXT("%d"), FMath::RoundToInt(P.Amount)), FVector2D(S.X + P.Drift * T, S.Y - 50.f * T), C, Scale);
    }
}

void ATMHUD::DrawToasts()
{
    UCombatFeedbackSubsystem* Feedback = UCombatFeedbackSubsystem::Get(this);
    if (!Feedback) return;
    const double Now = Feedback->Now();
    float Y = 90.f;
    for (const FHudToast& T : Feedback->GetToasts())
    {
        const float Age = static_cast<float>(Now - T.Time);
        const float A = Age > UCombatFeedbackSubsystem::ToastLife - 0.5f ? (UCombatFeedbackSubsystem::ToastLife - Age) / 0.5f : 1.f;
        DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f * A), Canvas->ClipX * 0.5f - 220.f, Y - 16.f, 440.f, 32.f);
        DrawCenteredText(T.Text, FVector2D(Canvas->ClipX * 0.5f, Y), FLinearColor(T.Color.R, T.Color.G, T.Color.B, A), 1.15f);
        Y += 38.f;
    }
}

// ── status panels ───────────────────────────────────────────────────────────

void ATMHUD::DrawPlayerStatus(const ATMCharacter* Char)
{
    const float X = 40.f, Y = Canvas->ClipY - 96.f, W = 300.f;
    DrawRect(Panel, X - 12.f, Y - 34.f, W + 24.f, 92.f);
    const float HealthFill = Char->GetMaxHealth() > 0.f ? Char->GetHealth() / Char->GetMaxHealth() : 0.f;
    DrawBar(X, Y, W, 16.f, HealthFill, FLinearColor(0.85f, 0.18f, 0.2f));
    DrawLeftText(FString::Printf(TEXT("%.0f / %.0f"), Char->GetHealth(), Char->GetMaxHealth()), X + 6.f, Y - 1.f, FLinearColor::White, 0.85f);
    const float ManaFill = Char->MaxMana > 0.f ? Char->CurrentMana / Char->MaxMana : 0.f;
    DrawBar(X, Y + 26.f, W, 12.f, ManaFill, FLinearColor(0.3f, 0.55f, 1.f));
    DrawLeftText(FString::Printf(TEXT("Mana %.0f"), Char->CurrentMana), X + 6.f, Y + 23.f, FLinearColor::White, 0.75f);

    // Buffs and weapon infusion, as small tags above the bars.
    FString BuffTags;
    if (const UBuffEffectComponent* Buffs = Char->BuffEffects)
    {
        if (Buffs->HasBuff(EBuffType::Speed))   BuffTags += TEXT("HASTE   ");
        if (Buffs->HasBuff(EBuffType::Damage))  BuffTags += TEXT("MIGHT   ");
        if (Buffs->HasBuff(EBuffType::Defense)) BuffTags += TEXT("ARMOUR   ");
        if (Buffs->HasBuff(EBuffType::Jump))    BuffTags += TEXT("LEAP   ");
    }
    if (!BuffTags.IsEmpty()) DrawLeftText(BuffTags, X, Y - 28.f, FLinearColor(0.7f, 1.f, 0.7f), 0.85f);
    if (Char->WeaponComponent && Char->WeaponComponent->IsWeaponInfused())
    {
        const ETransfigurationElement E = Char->WeaponComponent->GetCurrentInfusion();
        DrawLeftText(FString::Printf(TEXT("GUN: %s"), *USpellLibrary::GetElementName(E).ToUpper()), X + W - 110.f, Y - 28.f,
            USpellLibrary::GetElementColor(E), 0.85f);
    }
}

void ATMHUD::DrawSpellPanel(const ATMCharacter* Char)
{
    const UTransfigurationCastingComponent* Casting = Char->TransfigurationCasting;
    if (!Casting) return;
    const FName Id = Char->DrawSigilSpellID;
    const UTransfigurationDefinition* Def = Casting->GetSpellDefinition(Id);
    const FLinearColor Color = Def ? Def->GetSpellColor() : FLinearColor::White;

    const float W = 330.f, H = 92.f;
    const float X = Canvas->ClipX - W - 40.f, Y = Canvas->ClipY - H - 30.f;
    DrawRect(Panel, X, Y, W, H);
    DrawRect(Color, X, Y, 6.f, H);
    DrawLeftText(DisplayNameOf(Def, Id), X + 18.f, Y + 8.f, Color, 1.35f);

    const float Cost = Casting->GetSpellManaCost(Id);
    const float Cooldown = Casting->GetCooldownRemaining(Id);
    const bool bAfford = Char->HasEnoughMana(Cost);
    FString Line = FString::Printf(TEXT("%.0f mana"), Cost);
    if (Def) Line += FString::Printf(TEXT("   %s"), *UEnum::GetDisplayValueAsText(Def->TriggerType).ToString());
    DrawLeftText(Line, X + 18.f, Y + 40.f, bAfford ? Dim : FLinearColor(1.f, 0.35f, 0.3f), 0.9f);
    if (Cooldown > 0.f && Def && Def->CooldownDuration > 0.f)
    {
        DrawBar(X + 18.f, Y + 62.f, W - 36.f, 6.f, 1.f - Cooldown / Def->CooldownDuration, Color * 0.8f);
    }
    DrawLeftText(FString::Printf(TEXT("Sigils %d/%d"), Casting->GetActiveSigilCount(), Casting->MaxActiveSigils), X + W - 110.f, Y + 8.f, Dim, 0.85f);
    DrawLeftText(TEXT("Q place   hold Q wheel   B spellbook"), X + 18.f, Y + H - 22.f, FLinearColor(0.6f, 0.6f, 0.65f), 0.75f);
}

void ATMHUD::DrawSpeedMeter(const ATMCharacter* Char)
{
    const float Alpha = Char->GetSpeedFOVAlpha();
    const float Speed = Char->GetCharacterMovement() ? Char->GetCharacterMovement()->Velocity.Size() / 100.f : 0.f;
    const float W = 240.f, X = Canvas->ClipX * 0.5f - W * 0.5f, Y = Canvas->ClipY - 46.f;
    const FLinearColor C = FMath::Lerp(FLinearColor(0.8f, 0.8f, 0.8f), FLinearColor(0.45f, 0.85f, 1.f), Alpha);
    DrawBar(X, Y, W, 5.f, Alpha, C);
    DrawCenteredText(FString::Printf(TEXT("%.0f m/s"), Speed), FVector2D(Canvas->ClipX * 0.5f, Y - 14.f), C, 0.9f);
}

void ATMHUD::DrawLaunchInfo(const ATMCharacter* Char, const FVector2D& Center)
{
    const URingLaunchComponent* Launch = Char->LaunchComponent;
    if (!Launch) return;
    const FLinearColor Ice(0.55f, 0.9f, 1.f, 1.f);
    if (Launch->IsFlying())
    {
        const ARingContentStreamer* Streamer = ARingContentStreamer::Get(GetWorld());
        const int32 Pending = Streamer ? Streamer->GetPendingCount() : 0;
        DrawCenteredText(FString::Printf(TEXT("IN FLIGHT  -  landing in %.1fs%s"), Launch->GetFlightTimeLeft(),
            Pending > 0 ? *FString::Printf(TEXT("  -  building destination (%d left)"), Pending) : TEXT("  -  destination ready")),
            FVector2D(Center.X, Center.Y + 80.f), Ice, 1.2f);
        return;
    }
    if (!Launch->GetActivePad()) return;

    // Other pads: dim markers with names, so you can find your way back.
    FVector CamLoc;
    FRotator CamRot;
    GetOwningPlayerController()->GetPlayerViewPoint(CamLoc, CamRot);
    for (const TWeakObjectPtr<ARingLaunchPad>& PadPtr : Launch->GetKnownPads())
    {
        const ARingLaunchPad* Pad = PadPtr.Get();
        if (!Pad || Pad == Launch->GetActivePad()) continue;
        if (FVector::DotProduct(Pad->GetActorLocation() - CamLoc, CamRot.Vector()) <= 0.f) continue;
        const FVector P = Project(Pad->GetActorLocation());
        const FLinearColor Gold(1.f, 0.85f, 0.4f, 0.8f);
        DrawRect(Gold, P.X - 5.f, P.Y - 5.f, 10.f, 10.f);
        DrawCenteredText(FString::Printf(TEXT("%s  %s km"), *Pad->PadName,
            *FText::AsNumber(FMath::RoundToInt(FVector::Dist(Pad->GetActorLocation(), CamLoc) / 1e5)).ToString()),
            FVector2D(P.X, P.Y - 18.f), Gold, 0.9f);
    }

    if (Launch->HasTarget())
    {
        if (FVector::DotProduct(Launch->GetTarget() - CamLoc, CamRot.Vector()) > 0.f)
        {
            const FVector Screen = Project(Launch->GetTarget());
            const float S = 14.f;
            DrawRect(Ice, Screen.X - S, Screen.Y - 1.5f, S * 2.f, 3.f);
            DrawRect(Ice, Screen.X - 1.5f, Screen.Y - S, 3.f, S * 2.f);
            DrawRect(FLinearColor(Ice.R, Ice.G, Ice.B, 0.35f), Screen.X - S, Screen.Y - S, S * 2.f, S * 2.f);
        }
        DrawCenteredText(FString::Printf(TEXT("LAUNCH  -  %s%s km  -  click to fly there"),
            Launch->GetTargetPad() ? *FString::Printf(TEXT("%s  -  "), *Launch->GetTargetPad()->PadName) : TEXT(""),
            *FText::AsNumber(FMath::RoundToInt(Launch->GetTargetDistance() / 1e5)).ToString()),
            FVector2D(Center.X, Center.Y + 60.f), Ice, 1.2f);
    }
    else
    {
        DrawCenteredText(TEXT("Launch pad: look up at the ring to pick a landing spot"), FVector2D(Center.X, Center.Y + 60.f), Ice, 1.1f);
    }
}

// ── spell wheel ─────────────────────────────────────────────────────────────

void ATMHUD::DrawSpellWheel(const ATMCharacter* Char, const FVector2D& Center)
{
    const TArray<FName>& Spells = Char->GetSpellWheelOptions();
    const int32 Highlighted = Char->GetSpellWheelHighlighted();
    const UTransfigurationCastingComponent* Casting = Char->TransfigurationCasting;
    const float Radius = FMath::Min(Canvas->ClipX, Canvas->ClipY) * 0.36f;

    DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.4f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
    if (Spells.Num() == 0 || !Casting)
    {
        DrawCenteredText(TEXT("No spells equipped - press B for the spellbook"), Center, FLinearColor::Red, 1.4f);
        return;
    }

    const float Step = 2.f * PI / Spells.Num();
    for (int32 i = 0; i < Spells.Num(); ++i)
    {
        const UTransfigurationDefinition* Def = Casting->GetSpellDefinition(Spells[i]);
        const FLinearColor Color = Def ? Def->GetSpellColor() : FLinearColor::White;
        const float Angle = -PI * 0.5f + Step * i;   // first slot at the top, clockwise
        const FVector2D Pos = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
        const bool bHi = (i == Highlighted);
        const float BW = bHi ? 190.f : 165.f, BH = bHi ? 60.f : 50.f;

        const float Cost = Casting->GetSpellManaCost(Spells[i]);
        const bool bAfford = Char->HasEnoughMana(Cost);
        const float Cooldown = Casting->GetCooldownRemaining(Spells[i]);

        DrawRect(FLinearColor(0.03f, 0.03f, 0.06f, bHi ? 0.92f : 0.75f), Pos.X - BW * 0.5f, Pos.Y - BH * 0.5f, BW, BH);
        DrawFrame(Pos.X - BW * 0.5f, Pos.Y - BH * 0.5f, BW, BH, bHi ? Color : Color * 0.6f, bHi ? 3.f : 2.f);
        if (Cooldown > 0.f && Def && Def->CooldownDuration > 0.f)
        {
            DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), Pos.X - BW * 0.5f, Pos.Y - BH * 0.5f, BW * (Cooldown / Def->CooldownDuration), BH);
        }
        DrawCenteredText(DisplayNameOf(Def, Spells[i]), FVector2D(Pos.X, Pos.Y - 9.f), bHi ? Color : Color * 0.9f, bHi ? 1.25f : 1.05f);
        const FString Sub = Cooldown > 0.f ? FString::Printf(TEXT("%.1fs"), Cooldown) : FString::Printf(TEXT("%.0f mana"), Cost);
        DrawCenteredText(Sub, FVector2D(Pos.X, Pos.Y + 15.f), bAfford ? Dim : FLinearColor(1.f, 0.35f, 0.3f), 0.8f);
    }

    const FVector2D Cursor = Char->GetSpellWheelCursor();
    DrawLine(Center.X, Center.Y, Center.X + Cursor.X * Radius * 0.8f, Center.Y + Cursor.Y * Radius * 0.8f, FLinearColor::White, 2.f);
    if (Spells.IsValidIndex(Highlighted))
    {
        if (const UTransfigurationDefinition* Def = Casting->GetSpellDefinition(Spells[Highlighted]))
        {
            float Y = Center.Y - 30.f;
            for (const FString& L : WrapText(Def->Description.ToString(), Radius * 0.9f, 0.85f))
            {
                DrawCenteredText(L, FVector2D(Center.X, Y), Dim, 0.85f);
                Y += 20.f;
            }
        }
    }
    DrawCenteredText(TEXT("Release Q to select"), FVector2D(Center.X, Center.Y + Radius + 70.f), FLinearColor(0.8f, 0.8f, 0.8f));
}

// ── spellbook ───────────────────────────────────────────────────────────────

void ATMHUD::DrawSpellDetails(const UTransfigurationDefinition* Def, float X, float Y, float W)
{
    if (!Def) return;
    const FLinearColor C = Def->GetSpellColor();
    DrawLeftText(DisplayNameOf(Def, Def->SpellID), X, Y, C, 1.6f);
    Y += 38.f;
    DrawLeftText(FString::Printf(TEXT("%s  -  %s"), *USpellLibrary::GetElementName(Def->bIsHealingSigil || Def->bIsBuffSigil ? ETransfigurationElement::None : Def->Element),
        *ArchetypeName(Def)), X, Y, Dim, 0.95f);
    Y += 30.f;
    for (const FString& L : WrapText(Def->Description.ToString(), W, 0.9f))
    {
        DrawLeftText(L, X, Y, FLinearColor::White, 0.9f);
        Y += 21.f;
    }
    Y += 12.f;

    auto Stat = [&](const FString& Name, const FString& Value)
    {
        DrawLeftText(Name, X, Y, Dim, 0.85f);
        DrawLeftText(Value, X + 140.f, Y, FLinearColor::White, 0.85f);
        Y += 21.f;
    };
    if (Def->BaseDamage > 0.f) Stat(TEXT("Damage"), FString::Printf(TEXT("%.0f%s"), Def->BaseDamage, Def->bScalesWithSpeed ? TEXT("  (more at speed)") : TEXT("")));
    Stat(TEXT("Area"), FString::Printf(TEXT("%.0f m"), FMath::Max(Def->FalloffRadius, Def->ImpactRadius) / 100.f));
    Stat(TEXT("Mana"), FString::Printf(TEXT("%.0f"), Def->ManaCost));
    if (Def->CooldownDuration > 0.f) Stat(TEXT("Cooldown"), FString::Printf(TEXT("%.1fs"), Def->CooldownDuration));
    Stat(TEXT("Trigger"), UEnum::GetDisplayValueAsText(Def->TriggerType).ToString());
    if (Def->Status != ESpellStatus::None) Stat(TEXT("Status"), USpellLibrary::GetStatusName(Def->Status));
    if (Def->Duration > 0.f) Stat(TEXT("Lasts"), FString::Printf(TEXT("%.1fs"), Def->Duration));
    if (Def->Delay > 0.f) Stat(TEXT("Delay"), FString::Printf(TEXT("%.1fs"), Def->Delay));
    if (Def->Launch > 0.f || Def->CasterLaunch > 0.f) Stat(TEXT("Launch"), Def->CasterLaunch > 0.f ? TEXT("enemies and you") : TEXT("enemies"));
    if (Def->bSeversNearestLimb) Stat(TEXT("Special"), TEXT("tears off a limb"));
}

void ATMHUD::DrawSpellbook(ATMCharacter* Char)
{
    BookHits.Reset();
    const UTransfigurationCastingComponent* Casting = Char->TransfigurationCasting;
    const float W = Canvas->ClipX, H = Canvas->ClipY;
    DrawRect(FLinearColor(0.01f, 0.01f, 0.03f, 0.82f), 0.f, 0.f, W, H);
    DrawLeftText(TEXT("SPELLBOOK"), 50.f, 34.f, FLinearColor(0.9f, 0.85f, 1.f), 2.f);
    DrawLeftText(TEXT("Click a spell, then a wheel slot below to put it there.  Click a slot with nothing picked to clear it.  B to close."),
        50.f, 82.f, Dim, 0.85f);
    if (!Casting) return;

    float MouseX = 0.f, MouseY = 0.f;
    if (APlayerController* PC = GetOwningPlayerController()) PC->GetMousePosition(MouseX, MouseY);
    const FVector2D Mouse(MouseX, MouseY);
    BookHover = NAME_None;

    // Columns by element.
    static const ETransfigurationElement Order[] = { ETransfigurationElement::Fire, ETransfigurationElement::Ice,
        ETransfigurationElement::Lightning, ETransfigurationElement::Earth, ETransfigurationElement::Arcane,
        ETransfigurationElement::Void, ETransfigurationElement::None };
    const int32 NumCols = UE_ARRAY_COUNT(Order);
    const float GridX = 50.f, GridY = 130.f, GridW = W * 0.66f - GridX;
    const float ColW = GridW / NumCols, TileH = 40.f;

    TMap<int32, TArray<FName>> ByCol;
    for (const FName& Id : Casting->GetKnownSpellIDs())
    {
        const UTransfigurationDefinition* Def = Casting->GetSpellDefinition(Id);
        const ETransfigurationElement E = !Def || Def->bIsHealingSigil || Def->bIsBuffSigil ? ETransfigurationElement::None : Def->Element;
        int32 Col = NumCols - 1;
        for (int32 c = 0; c < NumCols; ++c)
        {
            if (Order[c] == E) { Col = c; break; }
        }
        ByCol.FindOrAdd(Col).Add(Id);
    }

    for (int32 c = 0; c < NumCols; ++c)
    {
        const float X = GridX + c * ColW;
        const FLinearColor EC = USpellLibrary::GetElementColor(Order[c]);
        DrawLeftText(USpellLibrary::GetElementName(Order[c]).ToUpper(), X + 6.f, GridY, EC, 1.0f);
        float Y = GridY + 32.f;
        for (const FName& Id : ByCol.FindRef(c))
        {
            const UTransfigurationDefinition* Def = Casting->GetSpellDefinition(Id);
            const FBox2D Rect(FVector2D(X + 2.f, Y), FVector2D(X + ColW - 6.f, Y + TileH - 4.f));
            const bool bHover = Rect.IsInside(Mouse);
            const bool bSel = Id == BookSelected;
            const bool bOnWheel = Casting->EquippedSpells.Contains(Id);
            if (bHover) BookHover = Id;
            DrawRect(FLinearColor(EC.R * 0.18f, EC.G * 0.18f, EC.B * 0.18f, bHover || bSel ? 0.95f : 0.7f), Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y);
            if (bSel) DrawFrame(Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y, FLinearColor(1.f, 0.9f, 0.3f), 3.f);
            else if (bHover) DrawFrame(Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y, EC, 2.f);
            const FLinearColor TextC = FMath::Lerp(EC, FLinearColor::White, bOnWheel ? 0.45f : 0.25f);
            DrawLeftText(DisplayNameOf(Def, Id), Rect.Min.X + 8.f, Rect.Min.Y + 9.f, TextC, 0.85f);
            if (bOnWheel) DrawRect(EC, Rect.Max.X - 10.f, Rect.Min.Y + 14.f, 6.f, 6.f);   // dot = on the wheel
            BookHits.Add({ Rect, false, Id, INDEX_NONE });
            Y += TileH;
        }
    }

    // Details of the hovered (or picked) spell.
    const FName Shown = !BookHover.IsNone() ? BookHover : BookSelected;
    const float DX = W * 0.68f, DW = W - DX - 50.f;
    DrawRect(Panel, DX - 16.f, GridY - 10.f, DW + 32.f, H * 0.62f);
    if (!Shown.IsNone())
    {
        DrawSpellDetails(Casting->GetSpellDefinition(Shown), DX, GridY, DW);
    }
    else
    {
        // Combos: place two sigils close together and set one off.
        const FLinearColor Unknown(0.5f, 0.5f, 0.55f);
        DrawLeftText(TEXT("COMBOS"), DX, GridY, FLinearColor(0.9f, 0.85f, 1.f), 1.2f);
        DrawLeftText(TEXT("Place two sigils close together, then set one off."), DX, GridY + 28.f, Dim, 0.8f);
        float Y = GridY + 58.f;
        for (const UTransfigurationDefinition* C : Casting->GetComboSpells())
        {
            const bool bFound = Casting->DiscoveredCombos.Contains(C->SpellID);
            DrawLeftText(FString::Printf(TEXT("%s + %s"), *USpellLibrary::GetElementName(C->ComboElementA),
                *USpellLibrary::GetElementName(C->ComboElementB)), DX, Y, Dim, 0.8f);
            DrawLeftText(bFound ? C->DisplayName.ToString() : FString(TEXT("???")), DX + 170.f, Y, bFound ? C->GetSpellColor() : Unknown, 0.8f);
            Y += 20.f;
        }
        const bool bOver = Casting->DiscoveredCombos.Contains(FName(TEXT("Overcharge")));
        const bool bAmp = Casting->DiscoveredCombos.Contains(FName(TEXT("Amplify")));
        DrawLeftText(TEXT("Same element twice"), DX, Y, Dim, 0.8f);
        DrawLeftText(bOver ? TEXT("Overcharge") : TEXT("???"), DX + 170.f, Y, bOver ? FLinearColor::White : Unknown, 0.8f);
        Y += 20.f;
        DrawLeftText(TEXT("Arcane + any"), DX, Y, Dim, 0.8f);
        DrawLeftText(bAmp ? TEXT("Amplify") : TEXT("???"), DX + 170.f, Y,
            bAmp ? USpellLibrary::GetElementColor(ETransfigurationElement::Arcane) : Unknown, 0.8f);
        Y += 30.f;
        DrawLeftText(TEXT("Hover a spell to read about it."), DX, Y, Dim, 0.8f);
    }

    // The wheel's 8 slots.
    const int32 Slots = UTransfigurationCastingComponent::WheelSlots;
    const float SW = FMath::Min(170.f, (W - 100.f) / Slots - 10.f), SH = 58.f;
    const float SX0 = W * 0.5f - (Slots * (SW + 10.f) - 10.f) * 0.5f, SY = H - SH - 40.f;
    DrawLeftText(TEXT("SPELL WHEEL"), SX0, SY - 30.f, FLinearColor(0.9f, 0.85f, 1.f), 1.f);
    for (int32 s = 0; s < Slots; ++s)
    {
        const FName Id = Casting->EquippedSpells.IsValidIndex(s) ? Casting->EquippedSpells[s] : NAME_None;
        const UTransfigurationDefinition* Def = Id.IsNone() ? nullptr : Casting->GetSpellDefinition(Id);
        const FLinearColor C = Def ? Def->GetSpellColor() : FLinearColor(0.4f, 0.4f, 0.45f);
        const float X = SX0 + s * (SW + 10.f);
        const FBox2D Rect(FVector2D(X, SY), FVector2D(X + SW, SY + SH));
        const bool bHover = Rect.IsInside(Mouse);
        DrawRect(FLinearColor(0.03f, 0.03f, 0.06f, 0.9f), X, SY, SW, SH);
        DrawFrame(X, SY, SW, SH, bHover ? FLinearColor(1.f, 0.9f, 0.3f) : C * 0.7f, bHover ? 3.f : 2.f);
        DrawLeftText(FString::Printf(TEXT("%d"), s + 1), X + 6.f, SY + 4.f, Dim, 0.7f);
        DrawCenteredText(Def ? DisplayNameOf(Def, Id) : TEXT("empty"), FVector2D(X + SW * 0.5f, SY + SH * 0.55f), C, 0.9f);
        BookHits.Add({ Rect, true, Id, s });
    }
    if (!BookSelected.IsNone())
    {
        const UTransfigurationDefinition* Sel = Casting->GetSpellDefinition(BookSelected);
        DrawLeftText(FString::Printf(TEXT("Picked: %s  -  now click a slot"), *DisplayNameOf(Sel, BookSelected)),
            SX0 + 200.f, SY - 30.f, FLinearColor(1.f, 0.9f, 0.3f), 1.f);
    }
}

void ATMHUD::SpellbookClick()
{
    ATMCharacter* Char = Cast<ATMCharacter>(GetOwningPawn());
    UTransfigurationCastingComponent* Casting = Char ? Char->TransfigurationCasting.Get() : nullptr;
    APlayerController* PC = GetOwningPlayerController();
    if (!Casting || !PC) return;
    float MX = 0.f, MY = 0.f;
    PC->GetMousePosition(MX, MY);
    const FVector2D Mouse(MX, MY);
    for (const FBookHit& Hit : BookHits)
    {
        if (!Hit.Rect.IsInside(Mouse)) continue;
        if (!Hit.bSlot)
        {
            BookSelected = BookSelected == Hit.Spell ? NAME_None : Hit.Spell;
        }
        else if (!BookSelected.IsNone())
        {
            Casting->EquipSpell(Hit.Slot, BookSelected);
            if (UCombatFeedbackSubsystem* F = UCombatFeedbackSubsystem::Get(this))
            {
                const UTransfigurationDefinition* Def = Casting->GetSpellDefinition(BookSelected);
                F->AddToast(FString::Printf(TEXT("%s -> slot %d"), *DisplayNameOf(Def, BookSelected), Hit.Slot + 1), Def ? Def->GetSpellColor() : FLinearColor::White);
            }
            BookSelected = NAME_None;
        }
        else
        {
            Casting->EquipSpell(Hit.Slot, NAME_None);
        }
        return;
    }
}
