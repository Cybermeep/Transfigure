// Source/Transfigure/Private/Transfiguration/TransfigurationGameInstance.cpp

#include "Transfiguration/TransfigurationGameInstance.h"

void UTransfigurationGameInstance::Init()
{
    Super::Init();
    // SpellRegistry is assigned as a DataAsset in BP_TransfigurationGameInstance
    // class defaults — no runtime NewObject needed
}