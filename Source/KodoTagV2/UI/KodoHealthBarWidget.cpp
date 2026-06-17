// Kodo Tag: Survivor — UE Migration, Creator-feedback pass.

#include "UI/KodoHealthBarWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

void UKodoHealthBarWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UKodoHealthBarWidget::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HealthRoot"));
	WidgetTree->RootWidget = Root;

	Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthBar"));
	Bar->SetPercent(1.f);
	// Dark backing so the bar reads against bright terrain; green fill by default.
	Bar->SetFillColorAndOpacity(FLinearColor(0.2f, 0.85f, 0.25f));

	FProgressBarStyle Style = Bar->GetWidgetStyle();
	Style.BackgroundImage.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	Bar->SetWidgetStyle(Style);

	if (UCanvasPanelSlot* BarSlot = Root->AddChildToCanvas(Bar))
	{
		// Fill the whole widget-component draw area, leaving a 2px frame for the border.
		BarSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BarSlot->SetOffsets(FMargin(2.f, 2.f, 2.f, 2.f));
	}
}

void UKodoHealthBarWidget::SetHealth(const float Fraction)
{
	if (!Bar)
	{
		return;
	}
	const float Clamped = FMath::Clamp(Fraction, 0.f, 1.f);
	Bar->SetPercent(Clamped);

	// Green / orange / red thresholds, matching the HUD details-panel HP bar.
	FLinearColor Fill(0.2f, 0.85f, 0.25f);
	if (Clamped <= 0.3f)
	{
		Fill = FLinearColor(0.9f, 0.15f, 0.12f);
	}
	else if (Clamped <= 0.6f)
	{
		Fill = FLinearColor(0.95f, 0.6f, 0.1f);
	}
	Bar->SetFillColorAndOpacity(Fill);
}
