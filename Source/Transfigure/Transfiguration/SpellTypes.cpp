// Source/Transfigure/Transfiguration/SpellTypes.cpp

#include "Transfiguration/SpellTypes.h"

FLinearColor USpellLibrary::GetElementColor(ETransfigurationElement Element)
{
    switch (Element)
    {
    case ETransfigurationElement::Fire:      return FLinearColor(1.00f, 0.38f, 0.08f);
    case ETransfigurationElement::Ice:       return FLinearColor(0.45f, 0.85f, 1.00f);
    case ETransfigurationElement::Lightning: return FLinearColor(1.00f, 0.92f, 0.25f);
    case ETransfigurationElement::Arcane:    return FLinearColor(0.75f, 0.45f, 1.00f);
    case ETransfigurationElement::Earth:     return FLinearColor(0.70f, 0.52f, 0.30f);
    case ETransfigurationElement::Void:      return FLinearColor(0.45f, 0.10f, 0.75f);
    default:                                 return FLinearColor(0.85f, 1.00f, 0.80f);   // support / none
    }
}

FString USpellLibrary::GetElementName(ETransfigurationElement Element)
{
    switch (Element)
    {
    case ETransfigurationElement::Fire:      return TEXT("Fire");
    case ETransfigurationElement::Ice:       return TEXT("Ice");
    case ETransfigurationElement::Lightning: return TEXT("Lightning");
    case ETransfigurationElement::Arcane:    return TEXT("Arcane");
    case ETransfigurationElement::Earth:     return TEXT("Earth");
    case ETransfigurationElement::Void:      return TEXT("Void");
    default:                                 return TEXT("Support");
    }
}

FLinearColor USpellLibrary::GetStatusColor(ESpellStatus Status)
{
    switch (Status)
    {
    case ESpellStatus::Burning:   return FLinearColor(1.0f, 0.35f, 0.05f);
    case ESpellStatus::Chilled:   return FLinearColor(0.4f, 0.8f, 1.0f);
    case ESpellStatus::Shocked:   return FLinearColor(1.0f, 0.95f, 0.3f);
    case ESpellStatus::Marked:    return FLinearColor(0.85f, 0.55f, 1.0f);
    case ESpellStatus::Staggered: return FLinearColor(0.8f, 0.6f, 0.35f);
    case ESpellStatus::Voided:    return FLinearColor(0.5f, 0.1f, 0.9f);
    default:                      return FLinearColor::White;
    }
}

FString USpellLibrary::GetStatusName(ESpellStatus Status)
{
    switch (Status)
    {
    case ESpellStatus::Burning:   return TEXT("Burning");
    case ESpellStatus::Chilled:   return TEXT("Chilled");
    case ESpellStatus::Shocked:   return TEXT("Shocked");
    case ESpellStatus::Marked:    return TEXT("Marked");
    case ESpellStatus::Staggered: return TEXT("Staggered");
    case ESpellStatus::Voided:    return TEXT("Voided");
    default:                      return TEXT("");
    }
}
