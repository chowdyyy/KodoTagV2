// Kodo Tag: Survivor — UE Migration, Creator-feedback pass.
// Overhead unit health bar, constructed fully in C++ (code-only project, no .uasset).
// Driven by a screen-space UWidgetComponent on each Kodo; the owner pushes HP via
// SetHealth() only when it changes, so 200+ bars stay cheap.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "KodoHealthBarWidget.generated.h"

class UProgressBar;

UCLASS()
class KODOTAGV2_API UKodoHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

	/** Set 0..1 fill and recolor green/orange/red to match the HUD details panel. */
	void SetHealth(float Fraction);

protected:
	void BuildTree();

	UPROPERTY() TObjectPtr<UProgressBar> Bar;
};
