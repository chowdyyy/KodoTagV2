// Kodo Tag: Survivor — UE Migration.
// Phase 5: the canvas-drawn minimap that used to live here moved into the
// UMG HUD (UKodoHudWidget), which owns the minimap texture, dots, and pan
// interaction. This AHUD remains as the GameMode's HUD class for any future
// immediate-mode debug drawing.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KodoHUD.generated.h"

UCLASS()
class KODOTAGV2_API AKodoHUD : public AHUD
{
	GENERATED_BODY()
};
