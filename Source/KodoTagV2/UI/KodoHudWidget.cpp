// Kodo Tag: Survivor — UE Migration, Phase 5.

#include "UI/KodoHudWidget.h"
#include "Camera/KodoPlayerController.h"
#include "Camera/KodoCameraPawn.h"
#include "Actors/RunnerCharacter.h"
#include "Actors/KodoCharacter.h"
#include "Actors/KodoWaveController.h"
#include "Grid/KodoGridSubsystem.h"
#include "Grid/KodoStructureManager.h"
#include "Data/KodoStructureData.h"
#include "Core/KodoTagUnits.h"
#include "Core/KodoTagGameState.h"
#include "Core/KodoTagGameMode.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/ProgressBar.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "RHI.h"
#include "TextureResource.h"

namespace
{
	const FLinearColor PanelDark(0.05f, 0.035f, 0.02f, 0.94f);   // dark wood
	const FLinearColor PanelTrim(0.55f, 0.42f, 0.16f, 1.f);      // gold trim
	const FLinearColor TextGold(0.95f, 0.8f, 0.35f);
	const FLinearColor TextPale(0.85f, 0.85f, 0.8f);

	// Start-overlay selection styling.
	const FLinearColor SelectedBtn(0.18f, 0.55f, 0.24f);   // accent green = selected
	const FLinearColor UnselectedBtn(0.16f, 0.13f, 0.08f); // dark = unselected

	FColor MinimapColorForCell(const FGridCell& Cell)
	{
		switch (Cell.Type)
		{
		case ECellType::Cliff:        return FColor(125, 125, 130);
		case ECellType::Tree:         return FColor(25, 145, 55);
		case ECellType::Goldmine:     return FColor(235, 200, 40);
		case ECellType::Wall:         return FColor(155, 105, 50);
		case ECellType::MerchantShop: return FColor(200, 80, 255);
		case ECellType::Tower:
			return Cell.StructureId == FName("command_center") ? FColor::White : FColor(60, 200, 255);
		default:                      return FColor(32, 58, 32);
		}
	}
}

void UKodoHudWidget::InitHud(AKodoPlayerController* InController)
{
	Controller = InController;
}

UTextBlock* UKodoHudWidget::MakeText(const FString& Initial, const float FontSize, const FLinearColor& Color) const
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Initial));
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = FontSize;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Color));
	return Text;
}

void UKodoHudWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UKodoHudWidget::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = Root;

	// ===== TOP BAR =====
	UBorder* TopBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	TopBar->SetBrushColor(PanelDark);
	TopBar->SetPadding(FMargin(14.f, 6.f));
	if (UCanvasPanelSlot* TopSlot = Root->AddChildToCanvas(TopBar))
	{
		TopSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
		TopSlot->SetAlignment(FVector2D(0.f, 0.f));
		TopSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, 40.f));
	}
	UHorizontalBox* TopBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	TopBar->SetContent(TopBox);

	GoldText = MakeText(TEXT("Gold 0"), 16.f, TextGold);
	WoodText = MakeText(TEXT("Wood 0"), 16.f, FLinearColor(0.35f, 0.85f, 0.4f));
	WaveText = MakeText(TEXT("Wave 1/10"), 16.f, TextPale);
	TimerText = MakeText(TEXT(""), 16.f, FLinearColor(0.95f, 0.55f, 0.3f));
	// Gameplay-layer top-bar additions: survival clock, mode label, hero spell cooldown.
	MatchTimerText = MakeText(TEXT("SURVIVE 30:00"), 16.f, TextPale);
	ModeText = MakeText(TEXT("Mode: Maze"), 16.f, FLinearColor(0.7f, 0.85f, 0.95f));
	SpellCdText = MakeText(TEXT("Q: READY"), 16.f, FLinearColor(0.3f, 0.9f, 0.4f));
	for (UTextBlock* Text : { GoldText.Get(), WoodText.Get(), WaveText.Get(), TimerText.Get(),
	                          MatchTimerText.Get(), ModeText.Get(), SpellCdText.Get() })
	{
		if (UHorizontalBoxSlot* TextSlot = TopBox->AddChildToHorizontalBox(Text))
		{
			TextSlot->SetPadding(FMargin(0.f, 0.f, 36.f, 0.f));
			TextSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	// ===== BOTTOM BAR =====
	UBorder* BottomBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	BottomBar->SetBrushColor(PanelDark);
	BottomBar->SetPadding(FMargin(8.f));
	if (UCanvasPanelSlot* BottomSlot = Root->AddChildToCanvas(BottomBar))
	{
		BottomSlot->SetAnchors(FAnchors(0.f, 1.f, 1.f, 1.f));
		BottomSlot->SetAlignment(FVector2D(0.f, 1.f));
		BottomSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, 224.f));
	}
	UHorizontalBox* BottomBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	BottomBar->SetContent(BottomBox);

	// --- Left: minimap ---
	USizeBox* MinimapSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	MinimapSize->SetWidthOverride(204.f);
	MinimapSize->SetHeightOverride(204.f);
	UBorder* MinimapFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	MinimapFrame->SetBrushColor(PanelTrim);
	MinimapFrame->SetPadding(FMargin(2.f));
	MinimapImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	MinimapFrame->SetContent(MinimapImage);
	MinimapSize->SetContent(MinimapFrame);
	if (UHorizontalBoxSlot* MapSlot = BottomBox->AddChildToHorizontalBox(MinimapSize))
	{
		MapSlot->SetPadding(FMargin(2.f, 2.f, 10.f, 2.f));
		MapSlot->SetVerticalAlignment(VAlign_Center);
	}

	// --- Center: details ---
	UBorder* Details = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Details->SetBrushColor(FLinearColor(0.08f, 0.06f, 0.035f, 0.96f));
	Details->SetPadding(FMargin(10.f, 6.f));
	if (UHorizontalBoxSlot* DetailsSlot = BottomBox->AddChildToHorizontalBox(Details))
	{
		DetailsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		DetailsSlot->SetPadding(FMargin(0.f, 2.f, 10.f, 2.f));
	}
	UHorizontalBox* DetailsBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Details->SetContent(DetailsBox);

	// Portrait
	USizeBox* PortraitSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	PortraitSize->SetWidthOverride(96.f);
	PortraitSize->SetHeightOverride(96.f);
	PortraitBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	PortraitBorder->SetBrushColor(FLinearColor(0.f, 0.55f, 0.65f));
	PortraitBorder->SetHorizontalAlignment(HAlign_Center);
	PortraitBorder->SetVerticalAlignment(VAlign_Center);
	PortraitText = MakeText(TEXT("MK"), 28.f, FLinearColor::White);
	PortraitBorder->SetContent(PortraitText);
	PortraitSize->SetContent(PortraitBorder);
	if (UHorizontalBoxSlot* PortraitSlot = DetailsBox->AddChildToHorizontalBox(PortraitSize))
	{
		PortraitSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
		PortraitSlot->SetVerticalAlignment(VAlign_Center);
	}

	// Name + segmented HP + stats + inventory
	UVerticalBox* InfoBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	if (UHorizontalBoxSlot* InfoSlot = DetailsBox->AddChildToHorizontalBox(InfoBox))
	{
		InfoSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	NameText = MakeText(TEXT("Runner"), 16.f, TextGold);
	InfoBox->AddChildToVerticalBox(NameText);

	UHorizontalBox* HpBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* HpBoxSlot = InfoBox->AddChildToVerticalBox(HpBox))
	{
		HpBoxSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 2.f));
	}
	HpSegments.Reset();
	for (int32 i = 0; i < 20; ++i)
	{
		USizeBox* SegmentSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SegmentSize->SetWidthOverride(16.f);
		SegmentSize->SetHeightOverride(16.f);
		UBorder* Segment = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Segment->SetBrushColor(FLinearColor(0.12f, 0.12f, 0.12f));
		SegmentSize->SetContent(Segment);
		if (UHorizontalBoxSlot* SegmentSlot = HpBox->AddChildToHorizontalBox(SegmentSize))
		{
			SegmentSlot->SetPadding(FMargin(0.f, 0.f, 2.f, 0.f));
		}
		HpSegments.Add(Segment);
	}

	HpText = MakeText(TEXT("100 / 100"), 13.f, TextPale);
	InfoBox->AddChildToVerticalBox(HpText);

	// Mana bar (blue): only shown when the runner is selected (UpdateDetailsPanel toggles it).
	{
		USizeBox* ManaSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ManaSize->SetHeightOverride(10.f);
		ManaBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		ManaBar->SetFillColorAndOpacity(FLinearColor(0.2f, 0.5f, 1.f));
		ManaBar->SetPercent(1.f);
		ManaSize->SetContent(ManaBar);
		if (UVerticalBoxSlot* ManaSlot = InfoBox->AddChildToVerticalBox(ManaSize))
		{
			ManaSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
		}
	}

	StatsText = MakeText(TEXT(""), 12.f, FLinearColor(0.7f, 0.7f, 0.65f));
	if (UVerticalBoxSlot* StatsSlot = InfoBox->AddChildToVerticalBox(StatsText))
	{
		StatsSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 4.f));
	}

	UHorizontalBox* InventoryBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	InfoBox->AddChildToVerticalBox(InventoryBox);
	InventorySlots.Reset();
	for (int32 i = 0; i < 6; ++i)
	{
		USizeBox* SlotSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SlotSize->SetWidthOverride(40.f);
		SlotSize->SetHeightOverride(40.f);
		UBorder* ItemSlot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		ItemSlot->SetBrushColor(FLinearColor(0.16f, 0.13f, 0.08f));
		ItemSlot->SetHorizontalAlignment(HAlign_Center);
		ItemSlot->SetVerticalAlignment(VAlign_Center);
		ItemSlot->SetContent(MakeText(FString::FromInt(i + 1), 11.f, FLinearColor(0.4f, 0.37f, 0.3f)));
		SlotSize->SetContent(ItemSlot);
		if (UHorizontalBoxSlot* ItemBoxSlot = InventoryBox->AddChildToHorizontalBox(SlotSize))
		{
			ItemBoxSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
		}
		InventorySlots.Add(ItemSlot);
	}

	// --- Right: 3x4 command card ---
	USizeBox* CardSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	CardSize->SetWidthOverride(330.f);
	UBorder* CardFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	CardFrame->SetBrushColor(FLinearColor(0.08f, 0.06f, 0.035f, 0.96f));
	CardFrame->SetPadding(FMargin(6.f));
	CardGrid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass());
	CardGrid->SetSlotPadding(FMargin(3.f));
	CardFrame->SetContent(CardGrid);
	CardSize->SetContent(CardFrame);
	if (UHorizontalBoxSlot* CardSlot = BottomBox->AddChildToHorizontalBox(CardSize))
	{
		CardSlot->SetPadding(FMargin(0.f, 2.f, 2.f, 2.f));
	}

	CardButtons.Reset();
	CardLabels.Reset();
	CardProgress.Reset();
	CardActions.SetNum(12);
	CardIsResearch.Init(false, 12);
	CardResearchType.Init(EKodoResearch::Stun, 12);
	for (int32 i = 0; i < 12; ++i)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Button->SetBackgroundColor(FLinearColor(0.22f, 0.17f, 0.09f));

		// Stack the label over a thin, bottom-docked progress bar inside the button.
		// The progress bar is the per-slot research "loading bar"; collapsed unless that
		// slot is an in-progress research (driven each frame by UpdateCardProgress).
		UOverlay* CardOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());

		UProgressBar* Progress = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		Progress->SetFillColorAndOpacity(FLinearColor(1.f, 0.78f, 0.16f)); // amber fill
		Progress->SetPercent(0.f);
		Progress->SetVisibility(ESlateVisibility::Collapsed); // hidden until a research occupies this slot
		if (UOverlaySlot* ProgSlot = Cast<UOverlaySlot>(CardOverlay->AddChild(Progress)))
		{
			ProgSlot->SetHorizontalAlignment(HAlign_Fill);
			ProgSlot->SetVerticalAlignment(VAlign_Bottom);   // thin bar pinned to the bottom edge
			ProgSlot->SetPadding(FMargin(2.f, 0.f, 2.f, 2.f));
		}

		UTextBlock* Label = MakeText(TEXT(""), 11.f, TextPale);
		Label->SetJustification(ETextJustify::Center);
		Label->SetAutoWrapText(true);
		if (UOverlaySlot* LabelSlot = Cast<UOverlaySlot>(CardOverlay->AddChild(Label)))
		{
			LabelSlot->SetHorizontalAlignment(HAlign_Fill);
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}

		Button->AddChild(CardOverlay);
		if (UUniformGridSlot* GridSlot = CardGrid->AddChildToUniformGrid(Button, i / 4, i % 4))
		{
			GridSlot->SetHorizontalAlignment(HAlign_Fill);
			GridSlot->SetVerticalAlignment(VAlign_Fill);
		}
		switch (i) // bind the matching UFUNCTION
		{
		case 0: Button->OnClicked.AddDynamic(this, &UKodoHudWidget::OnCard0); break;
		case 1: Button->OnClicked.AddDynamic(this, &UKodoHudWidget::OnCard1); break;
		case 2: Button->OnClicked.AddDynamic(this, &UKodoHudWidget::OnCard2); break;
		case 3: Button->OnClicked.AddDynamic(this, &UKodoHudWidget::OnCard3); break;
		case 4: Button->OnClicked.AddDynamic(this, &UKodoHudWidget::OnCard4); break;
		case 5: Button->OnClicked.AddDynamic(this, &UKodoHudWidget::OnCard5); break;
		case 6: Button->OnClicked.AddDynamic(this, &UKodoHudWidget::OnCard6); break;
		case 7: Button->OnClicked.AddDynamic(this, &UKodoHudWidget::OnCard7); break;
		case 8: Button->OnClicked.AddDynamic(this, &UKodoHudWidget::OnCard8); break;
		case 9: Button->OnClicked.AddDynamic(this, &UKodoHudWidget::OnCard9); break;
		case 10: Button->OnClicked.AddDynamic(this, &UKodoHudWidget::OnCard10); break;
		case 11: Button->OnClicked.AddDynamic(this, &UKodoHudWidget::OnCard11); break;
		default: break;
		}
		CardButtons.Add(Button);
		CardLabels.Add(Label);
		CardProgress.Add(Progress);
	}

	// Minimap texture
	MinimapTexture = UTexture2D::CreateTransient(KodoUnits::GridCols, KodoUnits::GridRows, PF_B8G8R8A8);
	if (MinimapTexture)
	{
		MinimapTexture->Filter = TF_Nearest;
		MinimapTexture->SRGB = true;
		MinimapTexture->UpdateResource(); // create the GPU resource once; per-frame updates stream into it
		MinimapImage->SetBrushFromTexture(MinimapTexture);
	}

	BuildTestPanel(Root);

	// Gameplay-layer panels. Shop sits in the HUD layer; the two full-screen
	// overlays are added LAST with a high ZOrder so they cover everything.
	BuildShopPanel(Root);
	BuildEndOverlay(Root);
	BuildStartOverlay(Root);
}

AKodoWaveController* UKodoHudWidget::ResolveWaveController() const
{
	UWorld* World = GetWorld();
	return World ? Cast<AKodoWaveController>(
		UGameplayStatics::GetActorOfClass(World, AKodoWaveController::StaticClass())) : nullptr;
}

void UKodoHudWidget::BuildTestPanel(UCanvasPanel* Root)
{
	// Floating debug panel, top-right under the resource bar: adjust a Kodo count and
	// spawn/clear them on demand for testing pathfinding, health bars, etc.
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TestPanel"));
	Panel->SetBrushColor(FLinearColor(0.05f, 0.035f, 0.02f, 0.92f));
	Panel->SetPadding(FMargin(8.f, 6.f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
	{
		PanelSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
		PanelSlot->SetAlignment(FVector2D(1.f, 0.f));
		PanelSlot->SetPosition(FVector2D(-12.f, 48.f));
		PanelSlot->SetAutoSize(true);
	}

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Col);

	UTextBlock* Title = MakeText(TEXT("TEST SPAWN"), 13.f, TextGold);
	Title->SetJustification(ETextJustify::Center);
	Col->AddChildToVerticalBox(Title);

	// Count adjuster row: [-]  count  [+]
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* RowSlot = Col->AddChildToVerticalBox(Row))
	{
		RowSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 4.f));
		RowSlot->SetHorizontalAlignment(HAlign_Center);
	}

	UButton* Minus = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Minus->SetBackgroundColor(FLinearColor(0.22f, 0.17f, 0.09f));
	Minus->AddChild(MakeText(TEXT("  -  "), 14.f, TextPale));
	Minus->OnClicked.AddDynamic(this, &UKodoHudWidget::OnTestMinus);
	Row->AddChildToHorizontalBox(Minus);

	TestCountText = MakeText(FString::FromInt(TestKodoCount), 15.f, FLinearColor::White);
	TestCountText->SetJustification(ETextJustify::Center);
	if (UHorizontalBoxSlot* CntSlot = Row->AddChildToHorizontalBox(TestCountText))
	{
		CntSlot->SetPadding(FMargin(12.f, 0.f, 12.f, 0.f));
		CntSlot->SetVerticalAlignment(VAlign_Center);
	}

	UButton* Plus = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Plus->SetBackgroundColor(FLinearColor(0.22f, 0.17f, 0.09f));
	Plus->AddChild(MakeText(TEXT("  +  "), 14.f, TextPale));
	Plus->OnClicked.AddDynamic(this, &UKodoHudWidget::OnTestPlus);
	Row->AddChildToHorizontalBox(Plus);

	// Spawn button
	UButton* Spawn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Spawn->SetBackgroundColor(FLinearColor(0.2f, 0.4f, 0.15f));
	UTextBlock* SpawnLabel = MakeText(TEXT("Spawn Kodos"), 13.f, FLinearColor::White);
	SpawnLabel->SetJustification(ETextJustify::Center);
	Spawn->AddChild(SpawnLabel);
	Spawn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnTestSpawn);
	Col->AddChildToVerticalBox(Spawn);

	// Clear button
	UButton* Clear = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Clear->SetBackgroundColor(FLinearColor(0.42f, 0.16f, 0.12f));
	UTextBlock* ClearLabel = MakeText(TEXT("Clear All"), 12.f, FLinearColor::White);
	ClearLabel->SetJustification(ETextJustify::Center);
	Clear->AddChild(ClearLabel);
	Clear->OnClicked.AddDynamic(this, &UKodoHudWidget::OnTestClear);
	if (UVerticalBoxSlot* ClearSlot = Col->AddChildToVerticalBox(Clear))
	{
		ClearSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	}
}

void UKodoHudWidget::OnTestSpawn()
{
	if (AKodoWaveController* WC = ResolveWaveController())
	{
		WC->SpawnTestKodos(TestKodoCount);
	}
}

void UKodoHudWidget::OnTestClear()
{
	if (AKodoWaveController* WC = ResolveWaveController())
	{
		WC->ClearAllKodos();
	}
}

void UKodoHudWidget::OnTestPlus()
{
	TestKodoCount = FMath::Clamp(TestKodoCount + 5, 1, 200);
	if (TestCountText)
	{
		TestCountText->SetText(FText::FromString(FString::FromInt(TestKodoCount)));
	}
}

void UKodoHudWidget::OnTestMinus()
{
	TestKodoCount = FMath::Clamp(TestKodoCount - 5, 1, 200);
	if (TestCountText)
	{
		TestCountText->SetText(FText::FromString(FString::FromInt(TestKodoCount)));
	}
}

// =====================================================================================
// Gameplay layer: start overlay, merchant shop, end overlay (+ their build helpers).
// =====================================================================================

void UKodoHudWidget::BuildStartOverlay(UCanvasPanel* Root)
{
	// Full-screen blocking border, added last with a high ZOrder so it covers the HUD.
	StartOverlay = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StartOverlay"));
	StartOverlay->SetBrushColor(FLinearColor(0.04f, 0.05f, 0.04f, 0.96f));
	StartOverlay->SetPadding(FMargin(0.f));
	StartOverlay->SetHorizontalAlignment(HAlign_Center);
	StartOverlay->SetVerticalAlignment(VAlign_Center);
	if (UCanvasPanelSlot* OverlaySlot = Root->AddChildToCanvas(StartOverlay))
	{
		OverlaySlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		OverlaySlot->SetOffsets(FMargin(0.f, 0.f, 0.f, 0.f));
		OverlaySlot->SetZOrder(100);
	}

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	StartOverlay->SetContent(Col);

	// Title.
	UTextBlock* Title = MakeText(TEXT("KODO TAG — SURVIVE 30 MINUTES"), 30.f, TextGold);
	Title->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* TitleSlot = Col->AddChildToVerticalBox(Title))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f));
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
	}

	// Mode row: [ Maze ] [ God ].
	UHorizontalBox* ModeRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* ModeRowSlot = Col->AddChildToVerticalBox(ModeRow))
	{
		ModeRowSlot->SetHorizontalAlignment(HAlign_Center);
	}

	ModeMazeButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* MazeLbl = MakeText(TEXT("  Maze  "), 18.f, FLinearColor::White);
	MazeLbl->SetJustification(ETextJustify::Center);
	ModeMazeButton->AddChild(MazeLbl);
	ModeMazeButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnPickMaze);
	if (UHorizontalBoxSlot* S = ModeRow->AddChildToHorizontalBox(ModeMazeButton)) { S->SetPadding(FMargin(6.f)); }

	ModeGodButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* GodLbl = MakeText(TEXT("  God  "), 18.f, FLinearColor::White);
	GodLbl->SetJustification(ETextJustify::Center);
	ModeGodButton->AddChild(GodLbl);
	ModeGodButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnPickGod);
	if (UHorizontalBoxSlot* S = ModeRow->AddChildToHorizontalBox(ModeGodButton)) { S->SetPadding(FMargin(6.f)); }

	ModeBunkerButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* BunkerLbl = MakeText(TEXT("  Bunker  "), 18.f, FLinearColor::White);
	BunkerLbl->SetJustification(ETextJustify::Center);
	ModeBunkerButton->AddChild(BunkerLbl);
	ModeBunkerButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnPickBunker);
	if (UHorizontalBoxSlot* S = ModeRow->AddChildToHorizontalBox(ModeBunkerButton)) { S->SetPadding(FMargin(6.f)); }

	// Mode subtitle.
	UTextBlock* ModeHint = MakeText(TEXT("God: buffed hero & towers, more kodos  |  Bunker: kodos siege buildings, towers +100% dmg"), 13.f,
	                                FLinearColor(0.7f, 0.7f, 0.65f));
	ModeHint->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* HintSlot = Col->AddChildToVerticalBox(ModeHint))
	{
		HintSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 20.f));
		HintSlot->SetHorizontalAlignment(HAlign_Center);
	}

	// Difficulty row: [ Easy ][ Normal ][ Hard ][ Insane ].
	UHorizontalBox* DiffRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* DiffRowSlot = Col->AddChildToVerticalBox(DiffRow))
	{
		DiffRowSlot->SetHorizontalAlignment(HAlign_Center);
		DiffRowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f));
	}

	DiffEasyButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* EasyLbl = MakeText(TEXT(" Easy "), 16.f, FLinearColor::White);
	EasyLbl->SetJustification(ETextJustify::Center);
	DiffEasyButton->AddChild(EasyLbl);
	DiffEasyButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnPickEasy);
	if (UHorizontalBoxSlot* S = DiffRow->AddChildToHorizontalBox(DiffEasyButton)) { S->SetPadding(FMargin(5.f)); }

	DiffNormalButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* NormalLbl = MakeText(TEXT(" Normal "), 16.f, FLinearColor::White);
	NormalLbl->SetJustification(ETextJustify::Center);
	DiffNormalButton->AddChild(NormalLbl);
	DiffNormalButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnPickNormal);
	if (UHorizontalBoxSlot* S = DiffRow->AddChildToHorizontalBox(DiffNormalButton)) { S->SetPadding(FMargin(5.f)); }

	DiffHardButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* HardLbl = MakeText(TEXT(" Hard "), 16.f, FLinearColor::White);
	HardLbl->SetJustification(ETextJustify::Center);
	DiffHardButton->AddChild(HardLbl);
	DiffHardButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnPickHard);
	if (UHorizontalBoxSlot* S = DiffRow->AddChildToHorizontalBox(DiffHardButton)) { S->SetPadding(FMargin(5.f)); }

	DiffInsaneButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* InsaneLbl = MakeText(TEXT(" Insane "), 16.f, FLinearColor::White);
	InsaneLbl->SetJustification(ETextJustify::Center);
	DiffInsaneButton->AddChild(InsaneLbl);
	DiffInsaneButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnPickInsane);
	if (UHorizontalBoxSlot* S = DiffRow->AddChildToHorizontalBox(DiffInsaneButton)) { S->SetPadding(FMargin(5.f)); }

	// START button.
	UButton* Start = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Start->SetBackgroundColor(FLinearColor(0.2f, 0.45f, 0.18f));
	UTextBlock* StartLbl = MakeText(TEXT("   START   "), 24.f, FLinearColor::White);
	StartLbl->SetJustification(ETextJustify::Center);
	Start->AddChild(StartLbl);
	Start->OnClicked.AddDynamic(this, &UKodoHudWidget::OnClickStart);
	if (UVerticalBoxSlot* StartSlot = Col->AddChildToVerticalBox(Start))
	{
		StartSlot->SetHorizontalAlignment(HAlign_Center);
	}

	RestyleStartButtons();
}

void UKodoHudWidget::RestyleStartButtons() const
{
	if (ModeMazeButton) { ModeMazeButton->SetBackgroundColor(SelMode == EKodoGameMode::Maze ? SelectedBtn : UnselectedBtn); }
	if (ModeGodButton)  { ModeGodButton->SetBackgroundColor(SelMode == EKodoGameMode::God ? SelectedBtn : UnselectedBtn); }
	if (ModeBunkerButton) { ModeBunkerButton->SetBackgroundColor(SelMode == EKodoGameMode::Bunker ? SelectedBtn : UnselectedBtn); }
	if (DiffEasyButton)   { DiffEasyButton->SetBackgroundColor(SelDiff == EKodoDifficulty::Easy ? SelectedBtn : UnselectedBtn); }
	if (DiffNormalButton) { DiffNormalButton->SetBackgroundColor(SelDiff == EKodoDifficulty::Normal ? SelectedBtn : UnselectedBtn); }
	if (DiffHardButton)   { DiffHardButton->SetBackgroundColor(SelDiff == EKodoDifficulty::Hard ? SelectedBtn : UnselectedBtn); }
	if (DiffInsaneButton) { DiffInsaneButton->SetBackgroundColor(SelDiff == EKodoDifficulty::Insane ? SelectedBtn : UnselectedBtn); }
}

void UKodoHudWidget::OnPickMaze()   { SelMode = EKodoGameMode::Maze; RestyleStartButtons(); }
void UKodoHudWidget::OnPickGod()    { SelMode = EKodoGameMode::God;  RestyleStartButtons(); }
void UKodoHudWidget::OnPickBunker() { SelMode = EKodoGameMode::Bunker; RestyleStartButtons(); }
void UKodoHudWidget::OnPickEasy()   { SelDiff = EKodoDifficulty::Easy;   RestyleStartButtons(); }
void UKodoHudWidget::OnPickNormal() { SelDiff = EKodoDifficulty::Normal; RestyleStartButtons(); }
void UKodoHudWidget::OnPickHard()   { SelDiff = EKodoDifficulty::Hard;   RestyleStartButtons(); }
void UKodoHudWidget::OnPickInsane() { SelDiff = EKodoDifficulty::Insane; RestyleStartButtons(); }

void UKodoHudWidget::OnClickStart()
{
	if (AKodoTagGameMode* GM = GetWorld()->GetAuthGameMode<AKodoTagGameMode>())
	{
		GM->BeginMatch(SelMode, SelDiff);
	}
	if (StartOverlay)
	{
		StartOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UKodoHudWidget::BuildShopPanel(UCanvasPanel* Root)
{
	// Bottom-center, floating above the command card. Collapsed until the runner is
	// near the merchant (see UpdateShopPanel).
	ShopPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ShopPanel"));
	ShopPanel->SetBrushColor(FLinearColor(0.07f, 0.05f, 0.09f, 0.95f));
	ShopPanel->SetPadding(FMargin(10.f, 8.f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(ShopPanel))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 1.f));
		PanelSlot->SetPosition(FVector2D(0.f, -240.f));
		PanelSlot->SetAutoSize(true);
	}

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	ShopPanel->SetContent(Col);

	UTextBlock* Title = MakeText(TEXT("MERCHANT"), 15.f, FLinearColor(0.8f, 0.5f, 1.f));
	Title->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* TitleSlot = Col->AddChildToVerticalBox(Title))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	BootsButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	BootsButton->SetBackgroundColor(FLinearColor(0.22f, 0.17f, 0.09f));
	BootsLabel = MakeText(TEXT("Boots of Speed — 150g"), 14.f, FLinearColor::White);
	BootsLabel->SetJustification(ETextJustify::Center);
	BootsButton->AddChild(BootsLabel);
	BootsButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnClickBuyBoots);
	Col->AddChildToVerticalBox(BootsButton);

	UTextBlock* Hint = MakeText(TEXT("+25% move speed"), 11.f, FLinearColor(0.7f, 0.7f, 0.65f));
	Hint->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* HintSlot = Col->AddChildToVerticalBox(Hint))
	{
		HintSlot->SetHorizontalAlignment(HAlign_Center);
		HintSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	}

	ShopPanel->SetVisibility(ESlateVisibility::Collapsed);
}

void UKodoHudWidget::OnClickBuyBoots()
{
	AKodoPlayerController* PC = Controller.Get();
	if (ARunnerCharacter* Runner = PC ? PC->GetRunner() : nullptr)
	{
		Runner->BuyBoots(); // handles spend + already-owned guard; panel auto-hides next tick
	}
}

void UKodoHudWidget::BuildEndOverlay(UCanvasPanel* Root)
{
	// Full-screen victory/defeat overlay, collapsed by default, just under the start overlay.
	EndOverlay = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("EndOverlay"));
	EndOverlay->SetBrushColor(FLinearColor(0.03f, 0.03f, 0.04f, 0.94f));
	EndOverlay->SetHorizontalAlignment(HAlign_Center);
	EndOverlay->SetVerticalAlignment(VAlign_Center);
	if (UCanvasPanelSlot* OverlaySlot = Root->AddChildToCanvas(EndOverlay))
	{
		OverlaySlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		OverlaySlot->SetOffsets(FMargin(0.f, 0.f, 0.f, 0.f));
		OverlaySlot->SetZOrder(90);
	}

	EndText = MakeText(TEXT(""), 34.f, FLinearColor::White);
	EndText->SetJustification(ETextJustify::Center);
	EndText->SetAutoWrapText(true);
	EndOverlay->SetContent(EndText);

	EndOverlay->SetVisibility(ESlateVisibility::Collapsed);
}

void UKodoHudWidget::UpdateCardProgress() const
{
	// Per-frame fill pass for the command-card research loading bars. The card itself only
	// rebuilds on context change (RebuildCommandCardIfNeeded), so this animates the fill and
	// shows/hides each slot's bar based on whether its research is currently in progress.
	AKodoPlayerController* PC = Controller.Get();
	const AKodoStructureManager* Manager = PC ? PC->GetStructureManager() : nullptr;

	for (int32 i = 0; i < CardButtons.Num(); ++i)
	{
		if (!CardProgress.IsValidIndex(i) || !CardProgress[i])
		{
			continue;
		}

		float Frac = -1.f;
		if (Manager && CardIsResearch.IsValidIndex(i) && CardIsResearch[i] && CardResearchType.IsValidIndex(i))
		{
			float Remaining = 0.f;
			Frac = Manager->GetResearchProgress(CardResearchType[i], Remaining);
		}

		if (Frac >= 0.f)
		{
			CardProgress[i]->SetVisibility(ESlateVisibility::Visible);
			CardProgress[i]->SetPercent(FMath::Clamp(Frac, 0.f, 1.f));
		}
		else
		{
			CardProgress[i]->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

// =====================================================================================
// Gameplay layer: per-frame update helpers.
// =====================================================================================

void UKodoHudWidget::UpdateStartOverlay() const
{
	if (!StartOverlay)
	{
		return;
	}
	const AKodoTagGameState* GS = GetWorld()->GetGameState<AKodoTagGameState>();
	const bool bStarted = GS && GS->bMatchStarted;
	StartOverlay->SetVisibility(bStarted ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

void UKodoHudWidget::UpdateMatchHud() const
{
	const AKodoTagGameState* GS = GetWorld()->GetGameState<AKodoTagGameState>();
	const AKodoTagGameMode* GM = GetWorld()->GetAuthGameMode<AKodoTagGameMode>();

	// Survival clock.
	if (MatchTimerText)
	{
		float Remaining = 1800.f; // pre-match: full 30:00
		if (GS && GS->bMatchStarted && GM)
		{
			Remaining = GM->GetMatchTimeRemaining();
		}
		const int32 Minutes = FMath::FloorToInt(Remaining / 60.f);
		const int32 Seconds = FMath::FloorToInt(Remaining) % 60;
		MatchTimerText->SetText(FText::FromString(
			FString::Printf(TEXT("SURVIVE %02d:%02d"), Minutes, Seconds)));
		MatchTimerText->SetColorAndOpacity(FSlateColor(
			Remaining < 60.f ? FLinearColor(1.f, 0.25f, 0.2f) : TextPale));
	}

	// Mode label.
	if (ModeText)
	{
		const EKodoGameMode Mode = GS ? GS->GameMode : EKodoGameMode::Maze;
		const TCHAR* ModeName = (Mode == EKodoGameMode::God) ? TEXT("Mode: God")
			: (Mode == EKodoGameMode::Bunker) ? TEXT("Mode: Bunker") : TEXT("Mode: Maze");
		ModeText->SetText(FText::FromString(ModeName));
	}

	// Hero spell cooldown.
	if (SpellCdText)
	{
		AKodoPlayerController* PC = Controller.Get();
		const ARunnerCharacter* Runner = PC ? PC->GetRunner() : nullptr;
		if (!Runner)
		{
			SpellCdText->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			SpellCdText->SetVisibility(ESlateVisibility::Visible);
			const float Cd = Runner->GetSpellCooldownRemaining();
			if (Cd <= 0.f)
			{
				SpellCdText->SetText(FText::FromString(TEXT("Q: READY")));
				SpellCdText->SetColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.9f, 0.4f)));
			}
			else
			{
				SpellCdText->SetText(FText::FromString(FString::Printf(TEXT("Q: %.1fs"), Cd)));
				SpellCdText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.62f, 0.04f)));
			}
		}
	}
}

void UKodoHudWidget::UpdateShopPanel() const
{
	if (!ShopPanel)
	{
		return;
	}

	const AKodoTagGameState* GS = GetWorld()->GetGameState<AKodoTagGameState>();
	AKodoPlayerController* PC = Controller.Get();
	ARunnerCharacter* Runner = PC ? PC->GetRunner() : nullptr;
	const UKodoGridSubsystem* Grid = GetWorld()->GetSubsystem<UKodoGridSubsystem>();

	bool bShow = false;
	if (GS && GS->bMatchStarted && Runner && Grid && !Runner->HasBoots())
	{
		const FIntPoint RunnerCell = Runner->GetGridCell();
		const FIntPoint MerchantCell = Grid->GetMerchantCell();
		const int32 ChebDist = FMath::Max(FMath::Abs(RunnerCell.X - MerchantCell.X),
		                                  FMath::Abs(RunnerCell.Y - MerchantCell.Y));
		bShow = ChebDist <= 3;
	}

	if (!bShow)
	{
		ShopPanel->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	ShopPanel->SetVisibility(ESlateVisibility::Visible);

	const bool bCanAfford = GS && GS->Gold >= static_cast<float>(ARunnerCharacter::BootsCost);
	if (BootsButton)
	{
		BootsButton->SetIsEnabled(bCanAfford);
	}
	if (BootsLabel)
	{
		BootsLabel->SetText(FText::FromString(
			bCanAfford ? TEXT("Boots of Speed — 150g") : TEXT("Boots of Speed — need 150g")));
	}
}

void UKodoHudWidget::UpdateEndOverlay() const
{
	if (!EndOverlay)
	{
		return;
	}
	const AKodoTagGameMode* GM = GetWorld()->GetAuthGameMode<AKodoTagGameMode>();
	if (GM && GM->IsGameOver())
	{
		EndOverlay->SetVisibility(ESlateVisibility::Visible);
		if (EndText)
		{
			if (GM->IsVictory())
			{
				EndText->SetText(FText::FromString(TEXT("VICTORY — You survived 30 minutes!")));
				EndText->SetColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.9f, 0.4f)));
			}
			else
			{
				EndText->SetText(FText::FromString(TEXT("GAME OVER — The runner was eaten.")));
				EndText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.25f, 0.2f)));
			}
		}
	}
	else
	{
		EndOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UKodoHudWidget::OnCard0() { HandleCard(0); }
void UKodoHudWidget::OnCard1() { HandleCard(1); }
void UKodoHudWidget::OnCard2() { HandleCard(2); }
void UKodoHudWidget::OnCard3() { HandleCard(3); }
void UKodoHudWidget::OnCard4() { HandleCard(4); }
void UKodoHudWidget::OnCard5() { HandleCard(5); }
void UKodoHudWidget::OnCard6() { HandleCard(6); }
void UKodoHudWidget::OnCard7() { HandleCard(7); }
void UKodoHudWidget::OnCard8() { HandleCard(8); }
void UKodoHudWidget::OnCard9() { HandleCard(9); }
void UKodoHudWidget::OnCard10() { HandleCard(10); }
void UKodoHudWidget::OnCard11() { HandleCard(11); }

void UKodoHudWidget::HandleCard(const int32 Index)
{
	if (CardActions.IsValidIndex(Index) && CardActions[Index])
	{
		CardActions[Index]();
		CurrentContextKey.Empty(); // force card refresh
	}
}

// Simple color-coded "icon" per command action, derived from its label so every card button
// (build / research / morph / admin / sell) gets a category-colored tile with zero call-site churn.
static FLinearColor CardIconColor(const FString& L)
{
	auto Has = [&L](const TCHAR* S) { return L.Contains(S); };
	if (Has(TEXT("Spawn")))                                              return FLinearColor(0.75f, 0.16f, 0.13f); // spawn kodo — crimson
	if (Has(TEXT("Sell")))                                               return FLinearColor(0.45f, 0.12f, 0.12f); // sell — dark red
	if (Has(TEXT("Difficulty")) || Has(TEXT("Spawning")))                return FLinearColor(0.34f, 0.34f, 0.40f); // admin control — slate
	if (Has(TEXT("Gold")) || Has(TEXT("Lumber Axes")) || Has(TEXT("Masonry"))) return FLinearColor(0.86f, 0.66f, 0.16f); // economy tech — gold
	if (Has(TEXT("Stun")) || Has(TEXT("Mortar")) || Has(TEXT("Multishot")) || Has(TEXT("Decay")) || Has(TEXT("Bow")) || Has(TEXT("Spores"))) return FLinearColor(0.30f, 0.46f, 0.86f); // combat research — steel blue
	if (Has(TEXT("Command")))                                            return FLinearColor(0.86f, 0.86f, 0.90f); // command center — white
	if (Has(TEXT("Upgrade Center")))                                     return FLinearColor(0.12f, 0.62f, 0.64f); // upgrade center — teal
	if (Has(TEXT("Magic")))                                              return FLinearColor(0.35f, 0.55f, 0.95f); // magical wall — blue
	if (Has(TEXT("Wall")))                                               return FLinearColor(0.46f, 0.33f, 0.18f); // wall — brown
	if (Has(TEXT("Frost")))                                              return FLinearColor(0.42f, 0.72f, 0.96f); // frost — ice
	if (Has(TEXT("Mill")) || Has(TEXT("Mine")) || Has(TEXT("Shaft")))    return FLinearColor(0.52f, 0.42f, 0.16f); // economy building
	if (Has(TEXT("AoE")) || Has(TEXT("Aura")) || Has(TEXT("Multi")))     return FLinearColor(0.60f, 0.36f, 0.76f); // special towers — purple
	if (Has(TEXT("Arrow")) || Has(TEXT("Tower")))                        return FLinearColor(0.52f, 0.56f, 0.62f); // towers — steel
	return FLinearColor(0.30f, 0.40f, 0.28f); // default — olive
}

void UKodoHudWidget::SetCardButton(const int32 Index, const FString& Label, const bool bEnabled,
                                   TFunction<void()> Action, const FString& Tooltip)
{
	if (!CardButtons.IsValidIndex(Index))
	{
		return;
	}
	CardButtons[Index]->SetVisibility(ESlateVisibility::Visible);
	CardButtons[Index]->SetIsEnabled(bEnabled);
	CardButtons[Index]->SetBackgroundColor(CardIconColor(Label)); // color-coded action "icon" tile
	// Hover pop-over (e.g. "Requires research at the Upgrade Center" on a grayed-out button).
	CardButtons[Index]->SetToolTipText(Tooltip.IsEmpty() ? FText::GetEmpty() : FText::FromString(Tooltip));
	CardLabels[Index]->SetText(FText::FromString(Label));
	CardActions[Index] = MoveTemp(Action);
}

FString UKodoHudWidget::MakeContextKey() const
{
	AKodoPlayerController* PC = Controller.Get();
	if (!PC)
	{
		return TEXT("none");
	}
	FString Key = FString::Printf(TEXT("%d|%d|%s|%d"),
	                              static_cast<int32>(PC->GetSelectionKind()), bBuildSubmenu ? 1 : 0,
	                              *PC->GetSelectedBlueprint().ToString(), 0);
	if (PC->GetSelectionKind() == EKodoSelection::Cell)
	{
		const UKodoGridSubsystem* Grid = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
		const FGridCell Cell = Grid ? Grid->GetCell(PC->GetSelectedCell()) : FGridCell();
		Key += FString::Printf(TEXT("|%s|%d|%d,%d"), *Cell.StructureId.ToString(), Cell.Level,
		                       PC->GetSelectedCell().X, PC->GetSelectedCell().Y);
	}
	if (const AKodoTagGameState* GS = GetWorld()->GetGameState<AKodoTagGameState>())
	{
		Key += FString::Printf(TEXT("|%d%d%d%d%d%d%d"), GS->Upgrades.bStunUnlocked, GS->Upgrades.bAoeUnlocked,
		                       GS->Upgrades.bMultishotUnlocked, GS->Upgrades.bAuraUnlocked,
		                       GS->Upgrades.MasonryLvl, GS->Upgrades.AxeLvl, GS->Upgrades.GoldBonusLvl);
	}
	// Hero-skill unlocks affect both the Upgrade Center research card and the runner skill card.
	if (const ARunnerCharacter* Runner = PC->GetRunner())
	{
		Key += FString::Printf(TEXT("|hs%d%d%d%d"), Runner->IsSkill2Unlocked(), Runner->IsSkill3Unlocked(),
		                       static_cast<int32>(Runner->GetHeroClass()), Runner->IsManaRegenUpgraded());
	}
	return Key;
}

void UKodoHudWidget::RebuildCommandCardIfNeeded()
{
	const FString Key = MakeContextKey();
	if (Key == CurrentContextKey)
	{
		return;
	}
	CurrentContextKey = Key;

	// Clear all 12. Research flagging resets here too; only the research branches re-flag slots,
	// so any non-research context leaves every slot's loading bar collapsed (via UpdateCardProgress).
	for (int32 i = 0; i < 12; ++i)
	{
		if (CardButtons.IsValidIndex(i))
		{
			CardButtons[i]->SetVisibility(ESlateVisibility::Hidden);
			CardActions[i] = nullptr;
		}
		if (CardIsResearch.IsValidIndex(i)) { CardIsResearch[i] = false; }
	}

	AKodoPlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}
	TWeakObjectPtr<AKodoPlayerController> WeakPC = PC;
	UKodoHudWidget* Self = this;

	const EKodoSelection Kind = PC->GetSelectionKind();

	if (Kind == EKodoSelection::Cell)
	{
		const UKodoGridSubsystem* Grid = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
		const FIntPoint Cell = PC->GetSelectedCell();
		const FGridCell State = Grid ? Grid->GetCell(Cell) : FGridCell();
		const AKodoTagGameState* GS = GetWorld()->GetGameState<AKodoTagGameState>();

		if (State.StructureId == FName("admin_tower"))
		{
			// Admin Tower control panel: spawn specific kodo types, toggle the auto-spawner,
			// and step difficulty up. Actions reach the live wave controller via ResolveWaveController.
			struct FSpawnRow { const TCHAR* Label; EKodoType Type; };
			const FSpawnRow SpawnRows[4] = {
				{ TEXT("Spawn Standard"), EKodoType::Standard },
				{ TEXT("Spawn Speed"),    EKodoType::Speed },
				{ TEXT("Spawn Tank"),     EKodoType::Tank },
				{ TEXT("Spawn Blink"),    EKodoType::Blink },
			};
			for (int32 i = 0; i < 4; ++i)
			{
				const EKodoType Type = SpawnRows[i].Type;
				SetCardButton(i, SpawnRows[i].Label, true, [Self, Type]
				{
					if (AKodoWaveController* WC = Self->ResolveWaveController()) { WC->SpawnKodoOfType(Type); }
				});
			}

			// Stop / Resume toggle — label reflects the current auto-spawner state.
			const AKodoWaveController* WCRead = ResolveWaveController();
			const bool bEnabled = WCRead ? WCRead->IsSpawningEnabled() : true;
			SetCardButton(4, bEnabled ? TEXT("Stop Spawning") : TEXT("Resume Spawning"), true, [Self]
			{
				if (AKodoWaveController* WC = Self->ResolveWaveController())
				{
					WC->SetSpawningEnabled(!WC->IsSpawningEnabled());
				}
			});

			// Difficulty + (Easy->Normal->Hard->Insane, clamped).
			SetCardButton(5, TEXT("Difficulty +"), true, [Self]
			{
				if (AKodoWaveController* WC = Self->ResolveWaveController()) { WC->StepDifficultyUp(); }
			});
		}
		else if (State.StructureId == FName("upgrade_center"))
		{
			// Upgrade Center: OFFENSIVE tower-upgrade research (combat) + hero-skill unlocks.
			struct FResearchRow { const TCHAR* Label; EKodoResearch Type; };
			const FResearchRow Rows[7] = {
				{ TEXT("Stun Powder\n150g 80w"), EKodoResearch::Stun },
				{ TEXT("Mortar Shells\n200g 100w"), EKodoResearch::Aoe },
				{ TEXT("Multishot Bow\n220g 120w"), EKodoResearch::Multishot },
				{ TEXT("Decay Spores\n250g 150w"), EKodoResearch::Aura },
				{ TEXT("Hero Skill 2\n120g 60w"), EKodoResearch::HeroSkill2 },
				{ TEXT("Hero Skill 3\n200g 100w"), EKodoResearch::HeroSkill3 },
				{ TEXT("Mana Regen+\n200g"), EKodoResearch::ManaRegen },
			};
			// Reflect already-unlocked hero skills so the buttons read/lock correctly.
			ARunnerCharacter* SkillRunner = WeakPC.IsValid() ? WeakPC->GetRunner() : nullptr;
			const bool bS2 = SkillRunner && SkillRunner->IsSkill2Unlocked();
			const bool bS3 = SkillRunner && SkillRunner->IsSkill3Unlocked();
			const bool bMana = SkillRunner && SkillRunner->IsManaRegenUpgraded();
			for (int32 i = 0; i < 7; ++i)
			{
				const EKodoResearch Type = Rows[i].Type;
				bool bEnabled = true;
				FString Label = Rows[i].Label;
				FString Tip;
				if (Type == EKodoResearch::HeroSkill2 && bS2) { bEnabled = false; Label = TEXT("Hero Skill 2\nUnlocked"); }
				if (Type == EKodoResearch::HeroSkill3 && bS3) { bEnabled = false; Label = TEXT("Hero Skill 3\nUnlocked"); }
				if (Type == EKodoResearch::ManaRegen && bMana) { bEnabled = false; Label = TEXT("Mana Regen+\nResearched"); Tip = TEXT("Already researched"); }
				SetCardButton(i, Label, bEnabled, [WeakPC, Type]
				{
					if (WeakPC.IsValid()) { WeakPC->TryResearch(Type); }
				}, Tip);
				// Flag this slot so UpdateCardProgress draws a loading bar while this research is in progress.
				if (CardIsResearch.IsValidIndex(i)) { CardIsResearch[i] = true; CardResearchType[i] = Type; }
			}
		}
		else if (State.StructureId == FName("command_center"))
		{
			// Command Center: ECONOMY / support research (gold, wood-harvest, building HP).
			struct FResearchRow { const TCHAR* Label; EKodoResearch Type; };
			const FResearchRow Rows[3] = {
				{ TEXT("Gold Bonus\n100g 40w"), EKodoResearch::GoldBonus },
				{ TEXT("Lumber Axes\n80g 30w"), EKodoResearch::Axe },
				{ TEXT("Masonry HP\n120g 60w"), EKodoResearch::Masonry },
			};
			for (int32 i = 0; i < 3; ++i)
			{
				const EKodoResearch Type = Rows[i].Type;
				SetCardButton(i, Rows[i].Label, true, [WeakPC, Type]
				{
					if (WeakPC.IsValid()) { WeakPC->TryResearch(Type); }
				});
				// Flag this slot so UpdateCardProgress draws a loading bar while this research is in progress.
				if (CardIsResearch.IsValidIndex(i)) { CardIsResearch[i] = true; CardResearchType[i] = Type; }
			}
		}
		else if (State.StructureId == FName("basic_tower"))
		{
			// Morph card (game.js:2424-2433).
			struct FMorphRow { const TCHAR* Label; const TCHAR* Id; bool bUnlocked; const TCHAR* LockTip; };
			const FMorphRow Rows[8] = {
				{ TEXT("Arrow 10g"), TEXT("arrow"), true, TEXT("") },
				{ TEXT("Frost 25g"), TEXT("frost"), true, TEXT("") },
				{ TEXT("Stun 40g"), TEXT("stun"), GS && GS->Upgrades.bStunUnlocked, TEXT("Requires research at the Upgrade Center") },
				{ TEXT("AoE 55g"), TEXT("aoe"), GS && GS->Upgrades.bAoeUnlocked, TEXT("Requires research at the Upgrade Center") },
				{ TEXT("Multi 60g"), TEXT("multishot"), GS && GS->Upgrades.bMultishotUnlocked, TEXT("Requires research at the Upgrade Center") },
				{ TEXT("Aura 80g"), TEXT("aura"), GS && GS->Upgrades.bAuraUnlocked, TEXT("Requires research at the Upgrade Center") },
				{ TEXT("Lumber Mill\n60g 20w"), TEXT("lumber_mill"), true, TEXT("") },
				{ TEXT("Mine Shaft\n80g 50w"), TEXT("mine_shaft"), State.bWasGoldMine, TEXT("Must be built on a gold mine") },
			};
			for (int32 i = 0; i < 8; ++i)
			{
				const FName TargetId(Rows[i].Id);
				const FString LockTip = Rows[i].bUnlocked ? FString() : FString(Rows[i].LockTip);
				SetCardButton(i, Rows[i].Label, Rows[i].bUnlocked, [WeakPC, Cell, TargetId]
				{
					if (WeakPC.IsValid())
					{
						if (AKodoStructureManager* Manager = WeakPC->GetStructureManager())
						{
							Manager->MorphBasicTower(Cell, TargetId);
						}
					}
				}, LockTip);
			}
			SetCardButton(11, TEXT("Sell 60%"), true, [WeakPC, Cell]
			{
				if (WeakPC.IsValid())
				{
					if (AKodoStructureManager* Manager = WeakPC->GetStructureManager())
					{
						Manager->SellStructure(Cell);
						WeakPC->ClearSelection();
					}
				}
			});
		}
		else if (State.Type == ECellType::Wall || State.Type == ECellType::Tower)
		{
			// Tier upgrade + (1x1 only) sell.
			const FKodoStructurePreset* Preset = KodoStructures::Find(State.StructureId);
			const bool bCanTier = Preset && State.Level < 3 && State.Level < Preset->Levels.Num();
			FString TierLabel = TEXT("Max Tier");
			if (bCanTier)
			{
				const FKodoStructureStats Next = KodoStructures::GetStatsForLevel(State.StructureId, State.Level + 1);
				const float Cost = State.StructureId == FName("wall") ? 0.f : static_cast<float>(Next.GoldCost);
				TierLabel = FString::Printf(TEXT("Upgrade Tier\n%.0fg"), Cost);
			}
			SetCardButton(0, TierLabel, bCanTier, [WeakPC, Cell]
			{
				if (WeakPC.IsValid())
				{
					if (AKodoStructureManager* Manager = WeakPC->GetStructureManager())
					{
						Manager->UpgradeStructureTier(Cell);
					}
				}
			});
			const bool b2x2 = State.StructureId == FName("command_center") ||
				State.StructureId == FName("lumber_mill") || State.StructureId == FName("mine_shaft") ||
				State.StructureId == FName("upgrade_center");
			if (!b2x2)
			{
				SetCardButton(3, TEXT("Sell 60%"), true, [WeakPC, Cell]
				{
					if (WeakPC.IsValid())
					{
						if (AKodoStructureManager* Manager = WeakPC->GetStructureManager())
						{
							Manager->SellStructure(Cell);
							WeakPC->ClearSelection();
						}
					}
				});
			}
		}
		// Trees/mines/cliffs/tent: info only, no buttons.
	}
	else // Runner card (also the default card when nothing is selected)
	{
		if (bBuildSubmenu)
		{
			SetCardButton(0, TEXT("Wall (W)\nfree"), true, [WeakPC, Self]
			{
				if (WeakPC.IsValid()) { WeakPC->SelectBlueprint(FName("wall")); }
				Self->bBuildSubmenu = false;
			});
			SetCardButton(1, TEXT("Cmd Center (C)\nfree"), true, [WeakPC, Self]
			{
				if (WeakPC.IsValid()) { WeakPC->SelectBlueprint(FName("command_center")); }
				Self->bBuildSubmenu = false;
			});
			SetCardButton(2, TEXT("Basic Spire (T)\n20g"), true, [WeakPC, Self]
			{
				if (WeakPC.IsValid()) { WeakPC->SelectBlueprint(FName("basic_tower")); }
				Self->bBuildSubmenu = false;
			});
			SetCardButton(3, TEXT("Magic Wall\n5g"), true, [WeakPC, Self]
			{
				if (WeakPC.IsValid()) { WeakPC->SelectBlueprint(FName("magic_wall")); }
				Self->bBuildSubmenu = false;
			});
			SetCardButton(4, TEXT("Upgrade Center\n50g"), true, [WeakPC, Self]
			{
				if (WeakPC.IsValid()) { WeakPC->SelectBlueprint(FName("upgrade_center")); }
				Self->bBuildSubmenu = false;
			});
			SetCardButton(11, TEXT("Back"), true, [Self] { Self->bBuildSubmenu = false; });
		}
		else
		{
			// Per-class skill names for the 3-slot layout.
			const ARunnerCharacter* SkillRunner = WeakPC.IsValid() ? WeakPC->GetRunner() : nullptr;
			const EKodoHeroClass HClass = SkillRunner ? SkillRunner->GetHeroClass() : EKodoHeroClass::MountainKing;
			const bool bS2 = SkillRunner && SkillRunner->IsSkill2Unlocked();
			const bool bS3 = SkillRunner && SkillRunner->IsSkill3Unlocked();

			const TCHAR* Slot1Name = TEXT("Spell");
			const TCHAR* Slot2Name = TEXT("Passive");
			const TCHAR* Slot3Name = TEXT("Skill 3");
			switch (HClass)
			{
			case EKodoHeroClass::MountainKing:
				Slot1Name = TEXT("Thunder Clap"); Slot2Name = TEXT("Toughness"); Slot3Name = TEXT("Avatar"); break;
			case EKodoHeroClass::DeathKnight:
				Slot1Name = TEXT("Death Coil"); Slot2Name = TEXT("Structure Regen"); Slot3Name = TEXT("Death Pact"); break;
			case EKodoHeroClass::Blademaster:
				Slot1Name = TEXT("Wind Walk"); Slot2Name = TEXT("Swiftness"); Slot3Name = TEXT("Blink"); break;
			default:
				Slot1Name = TEXT("Deploy Bot"); Slot2Name = TEXT("Repair Mastery"); Slot3Name = TEXT("Rocket Salvo"); break;
			}

			// Slot 1: signature active (Q), always available; costs mana.
			SetCardButton(0, FString::Printf(TEXT("%s (Q)\nactive"), Slot1Name), true, [WeakPC]
			{
				if (WeakPC.IsValid()) { WeakPC->CastHeroSkill(0); }
			});

			// Slot 2: passive. Enabled (not grayed) when active; grayed when not yet researched.
			SetCardButton(1, Slot2Name, bS2, [] {},
			              bS2 ? TEXT("Passive — always active") : TEXT("Requires research at the Upgrade Center"));

			// Slot 3: researched active (R). Castable only when unlocked; grayed + tooltip otherwise.
			if (bS3)
			{
				SetCardButton(2, FString::Printf(TEXT("%s (R)\nactive"), Slot3Name), true, [WeakPC]
				{
					if (WeakPC.IsValid()) { WeakPC->CastHeroSkill(2); }
				});
			}
			else
			{
				SetCardButton(2, Slot3Name, false, [] {}, TEXT("Requires research at the Upgrade Center"));
			}

			SetCardButton(3, TEXT("Build (B)"), true, [Self] { Self->bBuildSubmenu = true; });
		}
	}
}

void UKodoHudWidget::UpdateTopBar() const
{
	const AKodoTagGameState* GS = GetWorld()->GetGameState<AKodoTagGameState>();
	AKodoPlayerController* PC = Controller.Get();
	const ARunnerCharacter* Runner = PC ? const_cast<AKodoPlayerController*>(PC)->GetRunner() : nullptr;

	if (GS && GoldText && WoodText)
	{
		const int32 PackGold = Runner ? Runner->GetHeldGold() : 0;
		const int32 PackWood = Runner ? Runner->GetHeldWood() : 0;
		GoldText->SetText(FText::FromString(FString::Printf(TEXT("Gold %.0f (+%d pack)"), GS->Gold, PackGold)));
		WoodText->SetText(FText::FromString(FString::Printf(TEXT("Wood %.0f (+%d pack)"), GS->Wood, PackWood)));
	}

	if (const AKodoWaveController* Waves = Cast<AKodoWaveController>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AKodoWaveController::StaticClass())))
	{
		if (WaveText)
		{
			WaveText->SetText(FText::FromString(
				FString::Printf(TEXT("Wave %d/%d — %d kodos"), Waves->GetCurrentWave(), Waves->GetMaxWaves(),
				                Waves->GetAliveKodoCount())));
		}
		if (TimerText)
		{
			const float Time = Waves->IsPrepPhase() ? Waves->GetPrepRemaining() : Waves->GetWaveTimeRemaining();
			TimerText->SetText(FText::FromString(
				FString::Printf(TEXT("%s %02d:%02d"), Waves->IsPrepPhase() ? TEXT("PREP") : TEXT("WAVE"),
				                FMath::FloorToInt(Time / 60.f), FMath::FloorToInt(Time) % 60)));
		}
	}
}

void UKodoHudWidget::UpdateDetailsPanel() const
{
	AKodoPlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}

	float Hp = 0.f, MaxHp = 1.f;
	FString Name = TEXT("—"), Portrait = TEXT("?"), Stats;
	FLinearColor PortraitColor(0.2f, 0.2f, 0.2f);

	const bool bRunnerSelected = PC->GetSelectionKind() != EKodoSelection::Cell;

	if (PC->GetSelectionKind() == EKodoSelection::Cell)
	{
		const UKodoGridSubsystem* Grid = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
		const FGridCell State = Grid ? Grid->GetCell(PC->GetSelectedCell()) : FGridCell();
		const FKodoStructureStats LevelStats = KodoStructures::GetStatsForLevel(State.StructureId, State.Level);

		Hp = State.Hp;
		MaxHp = FMath::Max(1.f, State.MaxHp);
		Name = LevelStats.DisplayName.IsEmpty() ? State.StructureId.ToString().ToUpper() : LevelStats.DisplayName;
		Portrait = State.StructureId.ToString().Left(2).ToUpper();
		PortraitColor = FLinearColor(0.35f, 0.3f, 0.2f);
		if (State.Type == ECellType::Tree) { Name = TEXT("Ancient Oak"); Portrait = TEXT("TR"); }
		if (State.Type == ECellType::Goldmine) { Name = TEXT("Secure Gold Deposit"); Portrait = TEXT("AU"); }
		if (State.Type == ECellType::Cliff) { Name = TEXT("Granite Cliff Face"); Portrait = TEXT("CL"); }
		if (State.Type == ECellType::MerchantShop) { Name = TEXT("Merchant Tent"); Portrait = TEXT("MT"); }

		Stats = FString::Printf(TEXT("Level %d   Damage %.0f   Range %.1f   Armor —%s"),
		                        State.Level, LevelStats.Damage, LevelStats.RangeTiles,
		                        State.bUnderConstruction ? TEXT("   [UNDER CONSTRUCTION]") : TEXT(""));
	}
	else
	{
		// Runner (default).
		if (const ARunnerCharacter* Runner = PC->GetRunner())
		{
			Hp = Runner->GetHp();
			MaxHp = FMath::Max(1.f, Runner->GetMaxHp());
			const FString ClassName = UEnum::GetDisplayValueAsText(Runner->GetHeroClass()).ToString();
			Name = FString::Printf(TEXT("Runner — %s (Lv %d)"), *ClassName, Runner->GetHeroLevel());
			switch (Runner->GetHeroClass())
			{
			case EKodoHeroClass::MountainKing: Portrait = TEXT("MK"); PortraitColor = FLinearColor(0.f, 0.55f, 0.65f); break;
			case EKodoHeroClass::DeathKnight:  Portrait = TEXT("DK"); PortraitColor = FLinearColor(0.25f, 0.35f, 0.2f); break;
			case EKodoHeroClass::Blademaster:  Portrait = TEXT("BM"); PortraitColor = FLinearColor(0.6f, 0.3f, 0.25f); break;
			default:                           Portrait = TEXT("TK"); PortraitColor = FLinearColor(0.5f, 0.35f, 0.5f); break;
			}
			Stats = FString::Printf(TEXT("Lv %d  XP %d/%d   Mana %.0f/%.0f   Speed %.0f   Spell CD %.1fs   Pack %d/50"),
			                        Runner->GetHeroLevel(), Runner->GetHeroXp(), Runner->GetXpForNextLevel(),
			                        Runner->GetMana(), Runner->GetMaxMana(),
			                        Runner->GetMoveSpeed(), Runner->GetSpellCooldownRemaining(),
			                        Runner->GetHeldGold() + Runner->GetHeldWood());
		}
	}

	if (NameText) { NameText->SetText(FText::FromString(Name)); }
	if (PortraitText) { PortraitText->SetText(FText::FromString(Portrait)); }
	if (PortraitBorder) { PortraitBorder->SetBrushColor(PortraitColor); }
	if (HpText) { HpText->SetText(FText::FromString(FString::Printf(TEXT("%.0f / %.0f"), Hp, MaxHp))); }
	if (StatsText) { StatsText->SetText(FText::FromString(Stats)); }

	// Segmented HP bar with the prototype's color thresholds (game.js:1633).
	const float Ratio = FMath::Clamp(Hp / MaxHp, 0.f, 1.f);
	const int32 Filled = FMath::CeilToInt(Ratio * HpSegments.Num());
	FLinearColor FillColor(0.19f, 0.82f, 0.35f);                       // green
	if (Ratio <= 0.30f) { FillColor = FLinearColor(1.f, 0.23f, 0.19f); } // red
	else if (Ratio <= 0.55f) { FillColor = FLinearColor(1.f, 0.62f, 0.04f); } // orange
	for (int32 i = 0; i < HpSegments.Num(); ++i)
	{
		if (HpSegments[i])
		{
			HpSegments[i]->SetBrushColor(i < Filled ? FillColor : FLinearColor(0.12f, 0.12f, 0.12f));
		}
	}

	// Mana bar: only meaningful for the hero. Show + fill it when the runner is selected,
	// hide it for structure/cell selections.
	if (ManaBar)
	{
		const ARunnerCharacter* ManaRunner = bRunnerSelected ? PC->GetRunner() : nullptr;
		if (ManaRunner)
		{
			ManaBar->SetVisibility(ESlateVisibility::HitTestInvisible);
			const float ManaMax = ManaRunner->GetMaxMana();
			ManaBar->SetPercent(ManaMax > 0.f ? ManaRunner->GetMana() / ManaMax : 0.f);
		}
		else
		{
			ManaBar->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UKodoHudWidget::RebuildMinimapTexture() const
{
	const UKodoGridSubsystem* Grid = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
	if (!Grid || !MinimapTexture)
	{
		return;
	}
	constexpr int32 Cols = KodoUnits::GridCols;
	constexpr int32 Rows = KodoUnits::GridRows;

	// Build into a heap buffer the render thread owns until the GPU upload completes.
	const int32 PixelCount = Cols * Rows;
	FColor* Pixels = new FColor[PixelCount];

	for (int32 Y = 0; Y < Rows; ++Y)
	{
		for (int32 X = 0; X < Cols; ++X)
		{
			Pixels[Y * Cols + X] = MinimapColorForCell(Grid->GetCell(FIntPoint(X, Y)));
		}
	}

	const auto BurnDot = [&](const FVector& WorldLocation, const FColor& Color, const int32 Radius)
	{
		const int32 Cx = FMath::Clamp(FMath::FloorToInt(WorldLocation.X / KodoUnits::CellSizeUU), 0, Cols - 1);
		const int32 Cy = FMath::Clamp(FMath::FloorToInt(WorldLocation.Y / KodoUnits::CellSizeUU), 0, Rows - 1);
		for (int32 Dy = -Radius; Dy <= Radius; ++Dy)
		{
			for (int32 Dx = -Radius; Dx <= Radius; ++Dx)
			{
				const int32 Px = Cx + Dx, Py = Cy + Dy;
				if (Px >= 0 && Px < Cols && Py >= 0 && Py < Rows)
				{
					Pixels[Py * Cols + Px] = Color;
				}
			}
		}
	};

	for (TActorIterator<AKodoCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsDying())
		{
			BurnDot(It->GetActorLocation(),
			        It->GetKodoType() == EKodoType::Blink ? FColor(0, 240, 255) : FColor(255, 40, 25), 1);
		}
	}
	for (TActorIterator<ARunnerCharacter> It(GetWorld()); It; ++It)
	{
		BurnDot(It->GetActorLocation(), FColor(30, 255, 80), 1);
	}

	// Camera view rectangle outline.
	if (const AKodoPlayerController* PC = Controller.Get())
	{
		if (const AKodoCameraPawn* Cam = Cast<AKodoCameraPawn>(PC->GetPawn()))
		{
			const float Zoom = Cam->GetZoomFactor();
			const int32 HalfW = FMath::RoundToInt(55.f * Zoom * 0.5f);
			const int32 HalfH = FMath::RoundToInt(31.f * Zoom * 0.5f);
			const int32 Cx = FMath::FloorToInt(Cam->GetActorLocation().X / KodoUnits::CellSizeUU);
			const int32 Cy = FMath::FloorToInt(Cam->GetActorLocation().Y / KodoUnits::CellSizeUU);
			const FColor Frame(255, 255, 255);
			for (int32 X = Cx - HalfW; X <= Cx + HalfW; ++X)
			{
				if (X >= 0 && X < Cols)
				{
					if (Cy - HalfH >= 0 && Cy - HalfH < Rows) { Pixels[(Cy - HalfH) * Cols + X] = Frame; }
					if (Cy + HalfH >= 0 && Cy + HalfH < Rows) { Pixels[(Cy + HalfH) * Cols + X] = Frame; }
				}
			}
			for (int32 Y = Cy - HalfH; Y <= Cy + HalfH; ++Y)
			{
				if (Y >= 0 && Y < Rows)
				{
					if (Cx - HalfW >= 0 && Cx - HalfW < Cols) { Pixels[Y * Cols + (Cx - HalfW)] = Frame; }
					if (Cx + HalfW >= 0 && Cx + HalfW < Cols) { Pixels[Y * Cols + (Cx + HalfW)] = Frame; }
				}
			}
		}
	}

	// Stream straight into the existing GPU texture — no resource reallocation, so there's
	// no per-frame render-thread stall (the cause of the choppy on-screen movement).
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Cols, Rows);
	MinimapTexture->UpdateTextureRegions(
		0, 1, Region, Cols * sizeof(FColor), sizeof(FColor), reinterpret_cast<uint8*>(Pixels),
		[](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
		{
			delete[] reinterpret_cast<FColor*>(SrcData);
			delete Regions;
		});
}

bool UKodoHudWidget::MinimapNormalizedFromAbsolute(const FVector2D& AbsolutePosition, FVector2D& OutNormalized) const
{
	if (!MinimapImage)
	{
		return false;
	}
	const FGeometry& Geometry = MinimapImage->GetCachedGeometry();
	if (!Geometry.IsUnderLocation(AbsolutePosition))
	{
		return false;
	}
	const FVector2D Local = Geometry.AbsoluteToLocal(AbsolutePosition);
	const FVector2D Size = Geometry.GetLocalSize();
	if (Size.X <= 0.f || Size.Y <= 0.f)
	{
		return false;
	}
	OutNormalized = FVector2D(FMath::Clamp(Local.X / Size.X, 0.f, 1.f), FMath::Clamp(Local.Y / Size.Y, 0.f, 1.f));
	return true;
}

FReply UKodoHudWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	FVector2D Normalized;
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton &&
	    MinimapNormalizedFromAbsolute(InMouseEvent.GetScreenSpacePosition(), Normalized))
	{
		if (AKodoPlayerController* PC = Controller.Get())
		{
			PC->PanCameraToNormalized(Normalized);
		}
		bDraggingMinimap = true;
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UKodoHudWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingMinimap && InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		FVector2D Normalized;
		if (MinimapNormalizedFromAbsolute(InMouseEvent.GetScreenSpacePosition(), Normalized))
		{
			if (AKodoPlayerController* PC = Controller.Get())
			{
				PC->PanCameraToNormalized(Normalized);
			}
		}
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UKodoHudWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingMinimap)
	{
		bDraggingMinimap = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UKodoHudWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UpdateTopBar();
	UpdateDetailsPanel();
	RebuildCommandCardIfNeeded();

	// Gameplay-layer (game modes / win timer / merchant / win-lose) widgets.
	UpdateStartOverlay();
	UpdateMatchHud();
	UpdateShopPanel();
	UpdateEndOverlay();
	UpdateCardProgress();

	// The minimap redraw scans all 25,600 cells and re-uploads the texture to the GPU;
	// at ~12 Hz it's visually identical but far cheaper than doing it every frame.
	MinimapRebuildAccum += InDeltaTime;
	if (MinimapRebuildAccum >= 0.08f)
	{
		MinimapRebuildAccum = 0.f;
		RebuildMinimapTexture();
	}
}
