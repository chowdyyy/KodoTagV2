// Kodo Tag: Survivor — UE Migration, Phase 5.

#include "UI/KodoHudWidget.h"
#include "Camera/KodoPlayerController.h"
#include "Camera/KodoCameraPawn.h"
#include "Actors/RunnerCharacter.h"
#include "Actors/KodoCharacter.h"
#include "Actors/KodoWaveController.h"
#include "Grid/KodoGridSubsystem.h"
#include "Grid/KodoStructureManager.h"
#include "Grid/KodoMapBootstrapper.h"
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

	/** Two-letter abbreviation for an inventory item (slot label). "-" when empty. */
	const TCHAR* KodoItemAbbrev(const EKodoItem Item)
	{
		switch (Item)
		{
		case EKodoItem::BootsOfSpeed:     return TEXT("BS");
		case EKodoItem::ClawsOfAttack:    return TEXT("CA");
		case EKodoItem::RingOfProtection: return TEXT("RP");
		case EKodoItem::PotionOfHealing:  return TEXT("HP");
		case EKodoItem::PotionOfMana:     return TEXT("MP");
		case EKodoItem::TomeOfExperience: return TEXT("XP");
		default:                          return TEXT("-");
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

	// ===== BOTTOM BAR ===== (stored as a member so edit mode can collapse it)
	BottomBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
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

	InventoryBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	InfoBox->AddChildToVerticalBox(InventoryBox);
	InventorySlots.Reset();
	InventoryLabels.Reset();
	InventoryButtons.Reset();

	for (int32 i = 0; i < 6; ++i)
	{
		USizeBox* SlotSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SlotSize->SetWidthOverride(40.f);
		SlotSize->SetHeightOverride(40.f);

		// Border (backing) -> Overlay -> { label, click button } so the slot shows the item name and
		// is clickable to use a consumable. The button sits on top, transparent, filling the slot.
		UBorder* ItemSlot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		ItemSlot->SetBrushColor(FLinearColor(0.16f, 0.13f, 0.08f));
		ItemSlot->SetHorizontalAlignment(HAlign_Center);
		ItemSlot->SetVerticalAlignment(VAlign_Center);

		UOverlay* SlotOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());

		UTextBlock* SlotLabel = MakeText(TEXT("-"), 10.f, FLinearColor(0.85f, 0.8f, 0.6f));
		SlotLabel->SetJustification(ETextJustify::Center);
		if (UOverlaySlot* LblSlot = SlotOverlay->AddChildToOverlay(SlotLabel))
		{
			LblSlot->SetHorizontalAlignment(HAlign_Center);
			LblSlot->SetVerticalAlignment(VAlign_Center);
		}

		UButton* SlotButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		SlotButton->SetBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f)); // transparent overlay click target
		// AddDynamic stringizes its argument, so bind a literally-named UFUNCTION per slot index.
		switch (i)
		{
		case 0: SlotButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnUseItem0); break;
		case 1: SlotButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnUseItem1); break;
		case 2: SlotButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnUseItem2); break;
		case 3: SlotButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnUseItem3); break;
		case 4: SlotButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnUseItem4); break;
		case 5: SlotButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnUseItem5); break;
		default: break;
		}
		if (UOverlaySlot* BtnSlot = SlotOverlay->AddChildToOverlay(SlotButton))
		{
			BtnSlot->SetHorizontalAlignment(HAlign_Fill);
			BtnSlot->SetVerticalAlignment(VAlign_Fill);
		}

		ItemSlot->SetContent(SlotOverlay);
		SlotSize->SetContent(ItemSlot);
		if (UHorizontalBoxSlot* ItemBoxSlot = InventoryBox->AddChildToHorizontalBox(SlotSize))
		{
			ItemBoxSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
		}
		InventorySlots.Add(ItemSlot);
		InventoryLabels.Add(SlotLabel);
		InventoryButtons.Add(SlotButton);
	}

	// --- Center: production / research queue panel ---
	// Fills the empty middle space. Shown only when the selected building is constructing or
	// researching (driven each frame by UpdateProductionPanel): a header, then one row per active
	// item — name, time elapsed/total, and a filling bar — so the whole queue is visible at a glance.
	ProductionPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	ProductionPanel->SetBrushColor(FLinearColor(0.10f, 0.08f, 0.04f, 0.96f)); // dark amber
	ProductionPanel->SetPadding(FMargin(10.f, 6.f));
	ProductionPanel->SetVerticalAlignment(VAlign_Center);
	{
		UVerticalBox* ProdColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		ProdHeaderText = MakeText(TEXT("In Production"), 15.f, TextGold);
		if (UVerticalBoxSlot* HSlot = ProdColumn->AddChildToVerticalBox(ProdHeaderText))
		{
			HSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 5.f));
		}

		ProdRows.Reset();
		ProdRowName.Reset();
		ProdRowTime.Reset();
		ProdRowBar.Reset();
		for (int32 r = 0; r < 6; ++r)
		{
			UVerticalBox* Row = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

			// Name (fill-left) + time (right).
			UHorizontalBox* RowHead = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			UTextBlock* RName = MakeText(TEXT(""), 12.f, FLinearColor(0.95f, 0.9f, 0.78f));
			RName->SetJustification(ETextJustify::Left);
			if (UHorizontalBoxSlot* NSlot = RowHead->AddChildToHorizontalBox(RName))
			{
				NSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				NSlot->SetHorizontalAlignment(HAlign_Left);
				NSlot->SetVerticalAlignment(VAlign_Center);
			}
			UTextBlock* RTime = MakeText(TEXT(""), 11.f, FLinearColor(1.f, 0.82f, 0.3f));
			RTime->SetJustification(ETextJustify::Right);
			if (UHorizontalBoxSlot* TSlot = RowHead->AddChildToHorizontalBox(RTime))
			{
				TSlot->SetHorizontalAlignment(HAlign_Right);
				TSlot->SetVerticalAlignment(VAlign_Center);
			}
			Row->AddChildToVerticalBox(RowHead);

			// Thin filling bar.
			USizeBox* BarSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			BarSize->SetHeightOverride(12.f);
			UProgressBar* RBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
			RBar->SetFillColorAndOpacity(FLinearColor(1.f, 0.78f, 0.16f)); // amber fill
			RBar->SetPercent(0.f);
			BarSize->SetContent(RBar);
			Row->AddChildToVerticalBox(BarSize);

			Row->SetVisibility(ESlateVisibility::Collapsed);
			if (UVerticalBoxSlot* RowSlot = ProdColumn->AddChildToVerticalBox(Row))
			{
				RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
			}

			ProdRows.Add(Row);
			ProdRowName.Add(RName);
			ProdRowTime.Add(RTime);
			ProdRowBar.Add(RBar);
		}

		ProductionPanel->SetContent(ProdColumn);
	}
	ProductionPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (UHorizontalBoxSlot* ProdSlot = DetailsBox->AddChildToHorizontalBox(ProductionPanel))
	{
		ProdSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ProdSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		ProdSlot->SetVerticalAlignment(VAlign_Center);
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
	CardBaseColor.Init(FLinearColor(0.22f, 0.17f, 0.09f), 12);
	CardFlashTimer.Init(0.f, 12);
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

		// Big centered cooldown countdown, drawn ON TOP of the label while an ability recharges
		// (driven each frame by UpdateAbilityCooldowns). Collapsed when off cooldown.
		UTextBlock* CdText = MakeText(TEXT(""), 22.f, FLinearColor(1.f, 0.95f, 0.4f));
		CdText->SetJustification(ETextJustify::Center);
		CdText->SetVisibility(ESlateVisibility::Collapsed);
		if (UOverlaySlot* CdSlot = Cast<UOverlaySlot>(CardOverlay->AddChild(CdText)))
		{
			CdSlot->SetHorizontalAlignment(HAlign_Center);
			CdSlot->SetVerticalAlignment(VAlign_Center);
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
		CardCooldownText.Add(CdText);
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
	BuildEditorPalette(Root);
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

	// Hero selection: "CHOOSE YOUR HERO" + a row of 6 buttons (name + melee/ranged tag).
	UTextBlock* HeroTitle = MakeText(TEXT("CHOOSE YOUR HERO"), 18.f, TextGold);
	HeroTitle->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* HeroTitleSlot = Col->AddChildToVerticalBox(HeroTitle))
	{
		HeroTitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
		HeroTitleSlot->SetHorizontalAlignment(HAlign_Center);
	}

	UHorizontalBox* HeroRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* HeroRowSlot = Col->AddChildToVerticalBox(HeroRow))
	{
		HeroRowSlot->SetHorizontalAlignment(HAlign_Center);
		HeroRowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f));
	}

	HeroButtons.Reset();
	HeroChoices.Reset();
	// Parallel: class enum, display name, and ranged flag (melee/ranged tag).
	const EKodoHeroClass HeroEnums[6] = {
		EKodoHeroClass::MountainKing, EKodoHeroClass::Blademaster, EKodoHeroClass::Archmage,
		EKodoHeroClass::FarSeer, EKodoHeroClass::Paladin, EKodoHeroClass::Dreadlord };
	const TCHAR* HeroNames[6] = {
		TEXT("Mountain King"), TEXT("Blademaster"), TEXT("Archmage"),
		TEXT("Far Seer"), TEXT("Paladin"), TEXT("Dreadlord") };
	const bool HeroRanged[6] = { false, false, true, true, false, true };

	for (int32 i = 0; i < 6; ++i)
	{
		HeroChoices.Add(HeroEnums[i]);

		UButton* HeroBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		const FString HeroLabelStr = FString::Printf(TEXT("%s\n(%s)"), HeroNames[i],
		                                             HeroRanged[i] ? TEXT("ranged") : TEXT("melee"));
		UTextBlock* HeroLbl = MakeText(HeroLabelStr, 14.f, FLinearColor::White);
		HeroLbl->SetJustification(ETextJustify::Center);
		HeroBtn->AddChild(HeroLbl);
		switch (i)
		{
		case 0: HeroBtn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnPickHero0); break;
		case 1: HeroBtn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnPickHero1); break;
		case 2: HeroBtn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnPickHero2); break;
		case 3: HeroBtn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnPickHero3); break;
		case 4: HeroBtn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnPickHero4); break;
		case 5: HeroBtn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnPickHero5); break;
		default: break;
		}
		if (UHorizontalBoxSlot* S = HeroRow->AddChildToHorizontalBox(HeroBtn)) { S->SetPadding(FMargin(5.f)); }
		HeroButtons.Add(HeroBtn);
	}

	// START + MAP EDITOR row.
	UHorizontalBox* ActionRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* ActionRowSlot = Col->AddChildToVerticalBox(ActionRow))
	{
		ActionRowSlot->SetHorizontalAlignment(HAlign_Center);
	}

	UButton* Start = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Start->SetBackgroundColor(FLinearColor(0.2f, 0.45f, 0.18f));
	UTextBlock* StartLbl = MakeText(TEXT("   START   "), 24.f, FLinearColor::White);
	StartLbl->SetJustification(ETextJustify::Center);
	Start->AddChild(StartLbl);
	Start->OnClicked.AddDynamic(this, &UKodoHudWidget::OnClickStart);
	if (UHorizontalBoxSlot* StartSlot = ActionRow->AddChildToHorizontalBox(Start))
	{
		StartSlot->SetPadding(FMargin(8.f, 0.f, 8.f, 0.f));
	}

	EditMapButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	EditMapButton->SetBackgroundColor(FLinearColor(0.18f, 0.28f, 0.45f));
	UTextBlock* EditLbl = MakeText(TEXT("  Map Editor  "), 18.f, FLinearColor::White);
	EditLbl->SetJustification(ETextJustify::Center);
	EditMapButton->AddChild(EditLbl);
	EditMapButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnClickEditMap);
	if (UHorizontalBoxSlot* EditSlot = ActionRow->AddChildToHorizontalBox(EditMapButton))
	{
		EditSlot->SetPadding(FMargin(8.f, 0.f, 8.f, 0.f));
		EditSlot->SetVerticalAlignment(VAlign_Center);
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
	for (int32 i = 0; i < HeroButtons.Num(); ++i)
	{
		if (HeroButtons[i] && HeroChoices.IsValidIndex(i))
		{
			HeroButtons[i]->SetBackgroundColor(SelHero == HeroChoices[i] ? SelectedBtn : UnselectedBtn);
		}
	}
}

void UKodoHudWidget::OnPickMaze()   { SelMode = EKodoGameMode::Maze; RestyleStartButtons(); }
void UKodoHudWidget::OnPickGod()    { SelMode = EKodoGameMode::God;  RestyleStartButtons(); }
void UKodoHudWidget::OnPickBunker() { SelMode = EKodoGameMode::Bunker; RestyleStartButtons(); }
void UKodoHudWidget::OnPickEasy()   { SelDiff = EKodoDifficulty::Easy;   RestyleStartButtons(); }
void UKodoHudWidget::OnPickNormal() { SelDiff = EKodoDifficulty::Normal; RestyleStartButtons(); }
void UKodoHudWidget::OnPickHard()   { SelDiff = EKodoDifficulty::Hard;   RestyleStartButtons(); }
void UKodoHudWidget::OnPickInsane() { SelDiff = EKodoDifficulty::Insane; RestyleStartButtons(); }

void UKodoHudWidget::OnPickHero0() { HandlePickHero(0); }
void UKodoHudWidget::OnPickHero1() { HandlePickHero(1); }
void UKodoHudWidget::OnPickHero2() { HandlePickHero(2); }
void UKodoHudWidget::OnPickHero3() { HandlePickHero(3); }
void UKodoHudWidget::OnPickHero4() { HandlePickHero(4); }
void UKodoHudWidget::OnPickHero5() { HandlePickHero(5); }

void UKodoHudWidget::HandlePickHero(const int32 Index)
{
	if (HeroChoices.IsValidIndex(Index))
	{
		SelHero = HeroChoices[Index];
		RestyleStartButtons();
	}
}

void UKodoHudWidget::OnClickStart()
{
	// Apply the chosen hero to the runner before the match starts.
	if (AKodoPlayerController* PC = Controller.Get())
	{
		if (ARunnerCharacter* R = PC->GetRunner())
		{
			R->SetHeroClass(SelHero);
		}
	}
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

	// One button per purchasable item (every EKodoItem except None). Labels + affordability are
	// refreshed each frame in UpdateShopPanel; ShopItems[n] tells the handler which item slot n buys.
	ShopButtons.Reset();
	ShopLabels.Reset();
	ShopItems.Reset();
	ShopItems = {
		EKodoItem::BootsOfSpeed,
		EKodoItem::ClawsOfAttack,
		EKodoItem::RingOfProtection,
		EKodoItem::PotionOfHealing,
		EKodoItem::PotionOfMana,
		EKodoItem::TomeOfExperience
	};

	// AddDynamic stringizes its function-pointer argument, so each slot must bind a literally-named
	// UFUNCTION (no loop variable) — switch on the index like the command-card buttons do.
	for (int32 i = 0; i < ShopItems.Num(); ++i)
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Btn->SetBackgroundColor(FLinearColor(0.22f, 0.17f, 0.09f));
		UTextBlock* Lbl = MakeText(TEXT(""), 13.f, FLinearColor::White);
		Lbl->SetJustification(ETextJustify::Center);
		Btn->AddChild(Lbl);
		switch (i)
		{
		case 0: Btn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnBuyItem0); break;
		case 1: Btn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnBuyItem1); break;
		case 2: Btn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnBuyItem2); break;
		case 3: Btn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnBuyItem3); break;
		case 4: Btn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnBuyItem4); break;
		case 5: Btn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnBuyItem5); break;
		default: break;
		}
		if (UVerticalBoxSlot* BtnSlot = Col->AddChildToVerticalBox(Btn))
		{
			BtnSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
		}
		ShopButtons.Add(Btn);
		ShopLabels.Add(Lbl);
	}

	// Keep the legacy Boots refs pointing at slot 0 so older code/UI still resolves.
	BootsButton = ShopButtons.Num() > 0 ? ShopButtons[0].Get() : nullptr;
	BootsLabel = ShopLabels.Num() > 0 ? ShopLabels[0].Get() : nullptr;

	ShopPanel->SetVisibility(ESlateVisibility::Collapsed);
}

void UKodoHudWidget::HandleBuyItem(const int32 Index)
{
	if (!ShopItems.IsValidIndex(Index))
	{
		return;
	}
	AKodoPlayerController* PC = Controller.Get();
	if (ARunnerCharacter* Runner = PC ? PC->GetRunner() : nullptr)
	{
		Runner->BuyItem(ShopItems[Index]); // handles affordability/owned/full guards + spend
	}
}

void UKodoHudWidget::OnClickBuyBoots() { HandleBuyItem(0); } // legacy entry -> Boots (slot 0)
void UKodoHudWidget::OnBuyItem0() { HandleBuyItem(0); }
void UKodoHudWidget::OnBuyItem1() { HandleBuyItem(1); }
void UKodoHudWidget::OnBuyItem2() { HandleBuyItem(2); }
void UKodoHudWidget::OnBuyItem3() { HandleBuyItem(3); }
void UKodoHudWidget::OnBuyItem4() { HandleBuyItem(4); }
void UKodoHudWidget::OnBuyItem5() { HandleBuyItem(5); }

void UKodoHudWidget::HandleUseItem(const int32 Index)
{
	AKodoPlayerController* PC = Controller.Get();
	if (ARunnerCharacter* Runner = PC ? PC->GetRunner() : nullptr)
	{
		Runner->UseInventorySlot(Index); // consumables apply + are removed; passives no-op
	}
}

void UKodoHudWidget::OnUseItem0() { HandleUseItem(0); }
void UKodoHudWidget::OnUseItem1() { HandleUseItem(1); }
void UKodoHudWidget::OnUseItem2() { HandleUseItem(2); }
void UKodoHudWidget::OnUseItem3() { HandleUseItem(3); }
void UKodoHudWidget::OnUseItem4() { HandleUseItem(4); }
void UKodoHudWidget::OnUseItem5() { HandleUseItem(5); }

// =====================================================================================
// In-world map editor palette (spatial editor pass).
// =====================================================================================

void UKodoHudWidget::BuildEditorPalette(UCanvasPanel* Root)
{
	// Left-edge vertical tool palette. Collapsed unless the controller IsEditMode()
	// (toggled each frame by UpdateEditorPalette).
	EditorPalette = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("EditorPalette"));
	EditorPalette->SetBrushColor(PanelDark);
	EditorPalette->SetPadding(FMargin(8.f, 8.f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(EditorPalette))
	{
		PanelSlot->SetAnchors(FAnchors(0.f, 0.5f, 0.f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.f, 0.5f));
		PanelSlot->SetPosition(FVector2D(8.f, 0.f));
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetZOrder(80);
	}

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	EditorPalette->SetContent(Col);

	UTextBlock* Title = MakeText(TEXT("MAP EDITOR"), 15.f, TextGold);
	Title->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* TitleSlot = Col->AddChildToVerticalBox(Title))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
	}

	// Top hint line explaining the two mouse buttons.
	UTextBlock* Hint = MakeText(TEXT("Left-click: paint   Right-click: erase"), 11.f, FLinearColor(0.7f, 0.7f, 0.65f));
	Hint->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* HintSlot = Col->AddChildToVerticalBox(Hint))
	{
		HintSlot->SetHorizontalAlignment(HAlign_Center);
		HintSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	// Small section header label, e.g. "TERRAIN".
	auto MakeSectionLabel = [this, Col](const TCHAR* Label)
	{
		UTextBlock* Lbl = MakeText(Label, 10.f, TextGold);
		if (UVerticalBoxSlot* LblSlot = Col->AddChildToVerticalBox(Lbl))
		{
			LblSlot->SetPadding(FMargin(0.f, 7.f, 0.f, 1.f));
			LblSlot->SetHorizontalAlignment(HAlign_Left);
		}
	};

	// A full-width tool button. AddDynamic stringizes its function-pointer argument, so each
	// binding must name its UFUNCTION literally (no loop variable) — done at the call sites below.
	auto MakeToolButton = [this, Col](const TCHAR* Label) -> UButton*
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Btn->SetBackgroundColor(FLinearColor(0.22f, 0.17f, 0.09f));
		UTextBlock* Lbl = MakeText(Label, 13.f, TextPale);
		Lbl->SetJustification(ETextJustify::Center);
		Btn->AddChild(Lbl);
		if (UVerticalBoxSlot* BtnSlot = Col->AddChildToVerticalBox(Btn))
		{
			BtnSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
			BtnSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Btn;
	};

	// --- NAVIGATE --- (Pan: move around the map without placing anything)
	MakeSectionLabel(TEXT("NAVIGATE"));
	ToolPanButton = MakeToolButton(TEXT("Pan (no paint)"));
	ToolPanButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditPan);

	// --- TERRAIN ---
	MakeSectionLabel(TEXT("TERRAIN"));
	ToolRidgeButton = MakeToolButton(TEXT("Ridge"));
	ToolRidgeButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditRidge);
	ToolTreeButton = MakeToolButton(TEXT("Tree"));
	ToolTreeButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditTree);
	ToolMineButton = MakeToolButton(TEXT("Gold Mine"));
	ToolMineButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditMine);
	ToolEraseButton = MakeToolButton(TEXT("Erase"));
	ToolEraseButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditErase);

	// --- SPAWNS ---
	MakeSectionLabel(TEXT("SPAWNS"));
	ToolKodoSpawnButton = MakeToolButton(TEXT("Kodo"));
	ToolKodoSpawnButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditKodoSpawn);
	ToolRunnerSpawnButton = MakeToolButton(TEXT("Runner"));
	ToolRunnerSpawnButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditRunnerSpawn);
	ToolMerchantButton = MakeToolButton(TEXT("Merchant"));
	ToolMerchantButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditMerchant);

	// --- ELEVATION ---
	MakeSectionLabel(TEXT("ELEVATION"));
	ToolRaiseButton = MakeToolButton(TEXT("Raise"));
	ToolRaiseButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditRaise);
	ToolLowerButton = MakeToolButton(TEXT("Lower"));
	ToolLowerButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditLower);

	// --- RAMP ---
	MakeSectionLabel(TEXT("RAMP"));
	ToolRampButton = MakeToolButton(TEXT("Ramp"));
	ToolRampButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditRamp);

	// Ramp direction row: N E S W.
	UHorizontalBox* RampRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* RampRowSlot = Col->AddChildToVerticalBox(RampRow))
	{
		RampRowSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		RampRowSlot->SetHorizontalAlignment(HAlign_Center);
	}
	auto MakeDirButton = [this, RampRow](const TCHAR* Label) -> UButton*
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Btn->SetBackgroundColor(FLinearColor(0.16f, 0.13f, 0.08f));
		UTextBlock* Lbl = MakeText(Label, 12.f, TextPale);
		Lbl->SetJustification(ETextJustify::Center);
		Btn->AddChild(Lbl);
		if (UHorizontalBoxSlot* BtnSlot = RampRow->AddChildToHorizontalBox(Btn))
		{
			BtnSlot->SetPadding(FMargin(1.f, 0.f, 1.f, 0.f));
		}
		return Btn;
	};
	RampNButton = MakeDirButton(TEXT(" N "));
	RampNButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditRampN);
	RampEButton = MakeDirButton(TEXT(" E "));
	RampEButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditRampE);
	RampSButton = MakeDirButton(TEXT(" S "));
	RampSButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditRampS);
	RampWButton = MakeDirButton(TEXT(" W "));
	RampWButton->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditRampW);

	// --- FILE ---
	MakeSectionLabel(TEXT("FILE"));
	// Save + Done row.
	UHorizontalBox* SaveRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* SaveRowSlot = Col->AddChildToVerticalBox(SaveRow))
	{
		SaveRowSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		SaveRowSlot->SetHorizontalAlignment(HAlign_Center);
	}

	UButton* SaveBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	SaveBtn->SetBackgroundColor(FLinearColor(0.2f, 0.4f, 0.15f));
	UTextBlock* SaveLbl = MakeText(TEXT(" Save "), 14.f, FLinearColor::White);
	SaveLbl->SetJustification(ETextJustify::Center);
	SaveBtn->AddChild(SaveLbl);
	SaveBtn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditSave);
	if (UHorizontalBoxSlot* S = SaveRow->AddChildToHorizontalBox(SaveBtn)) { S->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f)); }

	UButton* PlayBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	PlayBtn->SetBackgroundColor(FLinearColor(0.18f, 0.28f, 0.45f));
	UTextBlock* PlayLbl = MakeText(TEXT(" Done - to Menu "), 14.f, FLinearColor::White);
	PlayLbl->SetJustification(ETextJustify::Center);
	PlayBtn->AddChild(PlayLbl);
	PlayBtn->OnClicked.AddDynamic(this, &UKodoHudWidget::OnEditPlay);
	if (UHorizontalBoxSlot* S = SaveRow->AddChildToHorizontalBox(PlayBtn)) { S->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f)); }

	// Feedback line ("Map saved").
	EditorMsgText = MakeText(TEXT(""), 12.f, FLinearColor(0.3f, 0.9f, 0.4f));
	EditorMsgText->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* MsgSlot = Col->AddChildToVerticalBox(EditorMsgText))
	{
		MsgSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		MsgSlot->SetHorizontalAlignment(HAlign_Center);
	}

	EditorPalette->SetVisibility(ESlateVisibility::Collapsed);
}

void UKodoHudWidget::OnClickEditMap()
{
	if (AKodoPlayerController* PC = Controller.Get())
	{
		PC->EnterEditMode();
	}
	if (StartOverlay)
	{
		StartOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
	// The editor palette becomes visible via UpdateEditorPalette (IsEditMode check).
}

void UKodoHudWidget::OnEditPan()         { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::None); } }
void UKodoHudWidget::OnEditRidge()       { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::Ridge); } }
void UKodoHudWidget::OnEditTree()        { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::Tree); } }
void UKodoHudWidget::OnEditMine()        { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::Mine); } }
void UKodoHudWidget::OnEditErase()       { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::Erase); } }
void UKodoHudWidget::OnEditKodoSpawn()   { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::KodoSpawn); } }
void UKodoHudWidget::OnEditRunnerSpawn() { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::RunnerSpawn); } }
void UKodoHudWidget::OnEditMerchant()    { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::Merchant); } }
void UKodoHudWidget::OnEditRaise()       { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::ElevRaise); } }
void UKodoHudWidget::OnEditLower()       { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::ElevLower); } }
void UKodoHudWidget::OnEditRamp()        { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::Ramp); } }
void UKodoHudWidget::OnEditRampN()       { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::Ramp, 3); } }
void UKodoHudWidget::OnEditRampE()       { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::Ramp, 0); } }
void UKodoHudWidget::OnEditRampS()       { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::Ramp, 2); } }
void UKodoHudWidget::OnEditRampW()       { if (AKodoPlayerController* PC = Controller.Get()) { PC->SetEditTool(EEditTool::Ramp, 1); } }

void UKodoHudWidget::OnEditSave()
{
	AKodoPlayerController* PC = Controller.Get();
	UKodoGridSubsystem* Grid = GetWorld() ? GetWorld()->GetSubsystem<UKodoGridSubsystem>() : nullptr;
	bool bSaved = false;
	if (PC && Grid)
	{
		if (AKodoMapBootstrapper* Boot = PC->GetBootstrapper())
		{
			bSaved = Boot->SaveLayoutToFile(*Grid);
		}
	}
	if (EditorMsgText)
	{
		EditorMsgText->SetText(FText::FromString(bSaved ? TEXT("Map saved") : TEXT("Save failed")));
		EditorMsgText->SetColorAndOpacity(FSlateColor(bSaved ? FLinearColor(0.3f, 0.9f, 0.4f) : FLinearColor(1.f, 0.3f, 0.2f)));
		EditorMsgTimer = 3.f;
	}
}

void UKodoHudWidget::OnEditPlay()
{
	if (AKodoPlayerController* PC = Controller.Get())
	{
		PC->ExitEditMode();
	}
	// Return to the start overlay so the player picks mode/difficulty and presses Start;
	// the edited map is already live in the grid, so the match just uses it.
	if (StartOverlay)
	{
		StartOverlay->SetVisibility(ESlateVisibility::Visible);
	}
	// Editor palette hides next tick via UpdateEditorPalette (IsEditMode is now false).
}

void UKodoHudWidget::UpdateEditorPalette() const
{
	if (!EditorPalette)
	{
		return;
	}
	const AKodoPlayerController* PC = Controller.Get();
	const bool bEdit = PC && PC->IsEditMode();
	EditorPalette->SetVisibility(bEdit ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (!bEdit)
	{
		return;
	}

	// Active-tool highlight: the selected tool's button gets an accent green, the rest stay neutral
	// (mirrors RestyleStartButtons). Ramp-direction buttons highlight the active direction.
	const FLinearColor ToolAccent(0.27f, 0.4f, 0.18f);   // selected
	const FLinearColor ToolNeutral(0.22f, 0.17f, 0.09f); // unselected (matches MakeToolButton default)
	const FLinearColor DirAccent(0.27f, 0.4f, 0.18f);
	const FLinearColor DirNeutral(0.16f, 0.13f, 0.08f);  // matches MakeDirButton default

	const EEditTool Active = PC->GetActiveEditTool();
	auto Style = [&](UButton* Btn, EEditTool Tool)
	{
		if (Btn) { Btn->SetBackgroundColor(Active == Tool ? ToolAccent : ToolNeutral); }
	};
	Style(ToolPanButton, EEditTool::None);
	Style(ToolRidgeButton, EEditTool::Ridge);
	Style(ToolTreeButton, EEditTool::Tree);
	Style(ToolMineButton, EEditTool::Mine);
	Style(ToolEraseButton, EEditTool::Erase);
	Style(ToolKodoSpawnButton, EEditTool::KodoSpawn);
	Style(ToolRunnerSpawnButton, EEditTool::RunnerSpawn);
	Style(ToolMerchantButton, EEditTool::Merchant);
	Style(ToolRaiseButton, EEditTool::ElevRaise);
	Style(ToolLowerButton, EEditTool::ElevLower);
	Style(ToolRampButton, EEditTool::Ramp);

	// Ramp direction: only emphasize when the Ramp tool is active. Dir codes: 0=E,1=W,2=S,3=N.
	const bool bRamp = Active == EEditTool::Ramp;
	const int32 Dir = PC->GetEditRampDir();
	auto StyleDir = [&](UButton* Btn, int32 D)
	{
		if (Btn) { Btn->SetBackgroundColor((bRamp && Dir == D) ? DirAccent : DirNeutral); }
	};
	StyleDir(RampNButton, 3);
	StyleDir(RampEButton, 0);
	StyleDir(RampSButton, 2);
	StyleDir(RampWButton, 1);
}

void UKodoHudWidget::UpdateEditorChrome() const
{
	if (!BottomBar)
	{
		return;
	}
	const AKodoPlayerController* PC = Controller.Get();
	const bool bEdit = PC && PC->IsEditMode();
	// Hide the gameplay command/details/minimap bar while sculpting the map; restore it on exit.
	BottomBar->SetVisibility(bEdit ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
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

	// War-Altar hero-stat upgrades are TIMED + queued on the hero (not in the structure manager's
	// research list). When the selected building is the war_altar, drive cards 0..4 from the hero's
	// per-stat upgrade progress (front item fills live; queued shows 0). Cards map 1:1 to the war_altar
	// stat-button order: 0=Damage 1=Armor 2=AttackSpeed 3=ManaRegen 4=MaxHealth.
	ARunnerCharacter* WarAltarRunner = nullptr;
	if (PC && PC->GetSelectionKind() == EKodoSelection::Cell)
	{
		const UKodoGridSubsystem* Grid = GetWorld() ? GetWorld()->GetSubsystem<UKodoGridSubsystem>() : nullptr;
		const FGridCell Cell = Grid ? Grid->GetCell(PC->GetSelectedCell()) : FGridCell();
		if (Cell.StructureId == FName("war_altar"))
		{
			WarAltarRunner = PC->GetRunner();
		}
	}
	static const EKodoHeroStat StatForCard[5] = {
		EKodoHeroStat::Damage, EKodoHeroStat::Armor, EKodoHeroStat::AttackSpeed,
		EKodoHeroStat::ManaRegen, EKodoHeroStat::MaxHealth
	};

	for (int32 i = 0; i < CardButtons.Num(); ++i)
	{
		if (!CardProgress.IsValidIndex(i) || !CardProgress[i])
		{
			continue;
		}

		float Frac = -1.f;
		if (WarAltarRunner && i >= 0 && i < 5)
		{
			float StatFrac = 0.f, StatRem = 0.f;
			if (WarAltarRunner->GetStatUpgradeProgress(StatForCard[i], StatFrac, StatRem))
			{
				Frac = StatFrac; // front item: live fill; queued-but-not-started: 0
			}
		}
		else if (Manager && CardIsResearch.IsValidIndex(i) && CardIsResearch[i] && CardResearchType.IsValidIndex(i))
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

void UKodoHudWidget::FlashCardButton(const int32 SlotIndex)
{
	// Brief, clearly-visible keypress/click highlight on a command-card slot (consumed by the per-frame
	// flash pass in UpdateAbilityCooldowns). ~0.25s so the bright tint registers on a quick keypress.
	if (CardFlashTimer.IsValidIndex(SlotIndex))
	{
		CardFlashTimer[SlotIndex] = 0.25f;
	}
}

void UKodoHudWidget::OpenBuildSubmenuFlash(const int32 CardIndex)
{
	// Build-hotkey (W/C/T) feedback: switch the runner command card to the build submenu, clear the
	// cached context key so RebuildCommandCardIfNeeded rebuilds to the submenu next frame, then flash
	// the matching submenu card so the menu visibly reacts exactly like a button click would.
	bBuildSubmenu = true;
	CurrentContextKey.Empty();
	FlashCardButton(CardIndex);
}

void UKodoHudWidget::UpdateAbilityCooldowns() const
{
	// Overlay a live countdown on the hero's active-ability buttons (cards 0/1/3) while they recharge,
	// and drive their per-frame tint (armed > flash > on-cooldown-gray > base color).
	//
	// FIXED GATE: the runner ability card is the DEFAULT card whenever selection != Cell (RebuildCommand-
	// CardIfNeeded's `else` branch), so it shows for BOTH Runner and None selections — not only Runner.
	AKodoPlayerController* PC = Controller.Get();
	ARunnerCharacter* R = PC ? PC->GetRunner() : nullptr;
	const bool bRunnerCard = PC && R && !bBuildSubmenu && !PC->IsEditMode() &&
	                         PC->GetSelectionKind() != EKodoSelection::Cell;

	const float Delta = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	const int32 PendingSlot = PC ? PC->GetPendingCastSlot() : -1;

	// Highlight palettes.
	const FLinearColor ArmedTint(0.25f, 0.85f, 0.95f);  // steady cyan — awaiting target
	const FLinearColor FlashTint(0.9f, 0.85f, 0.45f);   // bright — keypress/click flash
	const FLinearColor CooldownGray(0.12f, 0.12f, 0.12f);

	for (int32 i = 0; i < CardCooldownText.Num(); ++i)
	{
		UTextBlock* Cd = CardCooldownText[i];
		if (!Cd) { continue; }

		// Active ability slots map 1:1 to cards 0/1/3 (card 2 is the passive; 4/5 are Attack/Build).
		const bool bAbilityCard = (i == 0 || i == 1 || i == 3);

		// Decay the flash timer every frame regardless of context (guarded).
		if (CardFlashTimer.IsValidIndex(i) && CardFlashTimer[i] > 0.f)
		{
			CardFlashTimer[i] = FMath::Max(0.f, CardFlashTimer[i] - Delta);
		}

		float Remaining = 0.f;
		if (bRunnerCard && bAbilityCard) { Remaining = R->GetSkillCooldownRemaining(i); }
		const bool bOnCooldown = Remaining > 0.05f;

		if (bOnCooldown)
		{
			Cd->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), FMath::CeilToFloat(Remaining))));
			Cd->SetVisibility(ESlateVisibility::HitTestInvisible);
			// Also drive the slot's bar as a "recharge" fill (fills up as the ability becomes ready).
			if (CardProgress.IsValidIndex(i) && CardProgress[i])
			{
				const float Total = R->GetAbilityCooldown(i);
				const float Frac = (Total > 0.f) ? (1.f - Remaining / Total) : 0.f;
				CardProgress[i]->SetVisibility(ESlateVisibility::Visible);
				CardProgress[i]->SetPercent(FMath::Clamp(Frac, 0.f, 1.f));
			}
		}
		else
		{
			Cd->SetVisibility(ESlateVisibility::Collapsed);
		}

		if (!CardButtons.IsValidIndex(i) || !CardButtons[i])
		{
			continue;
		}

		const bool bFlashing = CardFlashTimer.IsValidIndex(i) && CardFlashTimer[i] > 0.f;

		// Per-frame button tint for the runner ability cards (0/1/3): armed > flash > cooldown gray > base.
		if (bRunnerCard && bAbilityCard)
		{
			const bool bArmed = (PendingSlot == i);
			const FLinearColor Base = CardBaseColor.IsValidIndex(i) ? CardBaseColor[i] : FLinearColor(0.30f, 0.40f, 0.28f);

			FLinearColor Tint;
			if (bArmed)         { Tint = ArmedTint; }
			else if (bFlashing) { Tint = FlashTint; }
			else if (bOnCooldown){ Tint = CooldownGray; }
			else                { Tint = Base; }

			CardButtons[i]->SetBackgroundColor(Tint);
			continue;
		}

		// Any OTHER visible card (e.g. the BUILD submenu cards): only TOUCH the color while it's
		// flashing — override to the bright flash tint, then let SetCardButton's base color (restored
		// on the next rebuild) take back over once the flash decays. This keeps research/cooldown
		// coloring untouched (gated on bFlashing) while making W/C/T visibly react on the build menu.
		if (bFlashing && CardButtons[i]->GetVisibility() == ESlateVisibility::Visible)
		{
			CardButtons[i]->SetBackgroundColor(FlashTint);
		}
		else if (CardFlashTimer.IsValidIndex(i) && CardFlashTimer[i] <= 0.f &&
		         CardBaseColor.IsValidIndex(i) && CardButtons[i]->GetVisibility() == ESlateVisibility::Visible)
		{
			// Flash just ended on a non-ability card: restore its normal color (a non-research card
			// won't otherwise rebuild every frame, so without this the bright tint would linger).
			CardButtons[i]->SetBackgroundColor(CardBaseColor[i]);
		}
	}
}

void UKodoHudWidget::UpdateProductionPanel() const
{
	// Center-of-bar production readout: when the selected building is constructing, researching, or
	// upgrading, list every active item — name, elapsed/total time, and a filling bar — so the whole
	// queue is visible at a glance. Purely selection + manager state (no hero proximity).
	if (!ProductionPanel)
	{
		return;
	}

	struct FProdEntry { FString Name; float Frac; float Rem; float Total; };
	TArray<FProdEntry> Actives;

	AKodoPlayerController* PC = Controller.Get();
	AKodoStructureManager* Mgr = PC ? PC->GetStructureManager() : nullptr;
	if (PC && Mgr && !PC->IsEditMode())
	{
		// A) Construction: a selected cell that is currently being built.
		if (PC->GetSelectionKind() == EKodoSelection::Cell)
		{
			float Frac = 0.f, Rem = 0.f, Total = 0.f;
			if (Mgr->GetConstructionProgress(PC->GetSelectedCell(), Frac, Rem, Total))
			{
				Actives.Add({ TEXT("Under construction"), Frac, Rem, Total });
			}
		}

		// B) Research / upgrade: every active item among the selected building's flagged slots.
		TArray<EKodoResearch> Relevant;
		for (int32 i = 0; i < CardIsResearch.Num(); ++i)
		{
			if (CardIsResearch[i] && CardResearchType.IsValidIndex(i))
			{
				Relevant.AddUnique(CardResearchType[i]); // dedup
			}
		}
		for (const EKodoResearch Type : Relevant)
		{
			FString Name;
			float Frac = 0.f, Rem = 0.f, Total = 0.f;
			if (Mgr->GetResearchStatus(Type, Name, Frac, Rem, Total))
			{
				Actives.Add({ Name, Frac, Rem, Total });
			}
		}
	}

	if (Actives.Num() == 0)
	{
		ProductionPanel->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// Soonest-to-finish first (all run in parallel, so this orders the queue by completion).
	Actives.Sort([](const FProdEntry& A, const FProdEntry& B) { return A.Rem < B.Rem; });

	for (int32 r = 0; r < ProdRows.Num(); ++r)
	{
		if (!ProdRows[r]) { continue; }
		if (r < Actives.Num())
		{
			const FProdEntry& E = Actives[r];
			if (ProdRowName.IsValidIndex(r) && ProdRowName[r]) { ProdRowName[r]->SetText(FText::FromString(E.Name)); }
			if (ProdRowTime.IsValidIndex(r) && ProdRowTime[r]) { ProdRowTime[r]->SetText(FText::FromString(FString::Printf(TEXT("%.0f / %.0fs"), E.Total - E.Rem, E.Total))); }
			if (ProdRowBar.IsValidIndex(r) && ProdRowBar[r])   { ProdRowBar[r]->SetPercent(FMath::Clamp(E.Frac, 0.f, 1.f)); }
			ProdRows[r]->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			ProdRows[r]->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	ProductionPanel->SetVisibility(ESlateVisibility::Visible);
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
	// While painting in the editor the start overlay stays hidden (the editor palette owns the
	// screen); Play re-shows it. Otherwise it follows the match-started flag.
	const AKodoPlayerController* PC = Controller.Get();
	if (PC && PC->IsEditMode())
	{
		StartOverlay->SetVisibility(ESlateVisibility::Collapsed);
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

	// Show the shop whenever the match is running and the runner stands near the merchant — there are
	// multiple items now, so we no longer hide it once any single item (Boots) is owned.
	bool bShow = false;
	if (GS && GS->bMatchStarted && Runner && Grid)
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

	// Per-item label + affordability/owned gating.
	const float Gold = GS ? GS->Gold : 0.f;
	for (int32 i = 0; i < ShopItems.Num(); ++i)
	{
		const EKodoItem Item = ShopItems[i];
		const FKodoItemDef& Def = KodoItemDef(Item);
		const bool bOwned = !Def.bConsumable && Runner && Runner->HasItem(Item);
		const bool bFull = Runner && Runner->GetInventoryCount() >= Runner->GetInventorySize();
		const bool bAfford = Gold >= static_cast<float>(Def.Cost);
		// Enable only if affordable, not already owned (passives), and there's room.
		const bool bEnabled = bAfford && !bOwned && !bFull;

		if (ShopButtons.IsValidIndex(i) && ShopButtons[i])
		{
			ShopButtons[i]->SetIsEnabled(bEnabled);
		}
		if (ShopLabels.IsValidIndex(i) && ShopLabels[i])
		{
			FString LabelText = FString::Printf(TEXT("%s — %dg"), *Def.Name, Def.Cost);
			if (bOwned)        { LabelText += TEXT(" (owned)"); }
			else if (bFull)    { LabelText += TEXT(" (full)"); }
			else if (!bAfford) { LabelText += TEXT(" (need gold)"); }
			ShopLabels[i]->SetText(FText::FromString(LabelText));
			ShopLabels[i]->SetColorAndOpacity(FSlateColor(
				bEnabled ? FLinearColor::White : FLinearColor(0.5f, 0.5f, 0.5f)));
		}
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
	const FLinearColor IconColor = CardIconColor(Label);
	CardButtons[Index]->SetBackgroundColor(IconColor); // color-coded action "icon" tile
	// Remember this card's normal color so UpdateAbilityCooldowns can restore it after graying /
	// flashing / arming an ability slot (guarded — CardBaseColor is sized 12 alongside the buttons).
	if (CardBaseColor.IsValidIndex(Index))
	{
		CardBaseColor[Index] = IconColor;
	}
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
	// Hero class + level are included so the runner command card rebuilds when the chosen hero
	// changes OR the hero levels up (leveling unlocks ability slots — Pass 4).
	if (const ARunnerCharacter* Runner = PC->GetRunner())
	{
		Key += FString::Printf(TEXT("|hs%d%d%d%d|hc%d|hl%d"), Runner->IsSkill2Unlocked(), Runner->IsSkill3Unlocked(),
		                       static_cast<int32>(Runner->GetHeroClass()), Runner->IsManaRegenUpgraded(),
		                       static_cast<int32>(Runner->GetHeroClass()), Runner->GetHeroLevel());
		// War-Altar hero-upgrade levels: include the sum so the war_altar card re-renders (cost/level
		// labels + affordability) the moment a stat is purchased.
		const int32 HeroUpgradeSum =
			Runner->GetHeroStatLevel(EKodoHeroStat::Damage) +
			Runner->GetHeroStatLevel(EKodoHeroStat::Armor) +
			Runner->GetHeroStatLevel(EKodoHeroStat::AttackSpeed) +
			Runner->GetHeroStatLevel(EKodoHeroStat::ManaRegen) +
			Runner->GetHeroStatLevel(EKodoHeroStat::MaxHealth);
		// Also include the total queued-upgrade count so the war_altar card re-renders the moment an
		// upgrade is queued or a queued upgrade finishes (labels: "+N queued", cost, enable state).
		const int32 HeroQueuedSum =
			Runner->GetQueuedStatCount(EKodoHeroStat::Damage) +
			Runner->GetQueuedStatCount(EKodoHeroStat::Armor) +
			Runner->GetQueuedStatCount(EKodoHeroStat::AttackSpeed) +
			Runner->GetQueuedStatCount(EKodoHeroStat::ManaRegen) +
			Runner->GetQueuedStatCount(EKodoHeroStat::MaxHealth);
		Key += FString::Printf(TEXT("|hu%d|hq%d"), HeroUpgradeSum, HeroQueuedSum);
	}
	// TEST Gun Mode flag so the runner command card rebuilds (and the Gun Mode button relabels) on toggle.
	Key += FString::Printf(TEXT("|gm%d"), PC->IsGunMode() ? 1 : 0);
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
			// Command Center: ECONOMY / support research (gold, wood-harvest, building HP, wall tech).
			struct FResearchRow { const TCHAR* Label; EKodoResearch Type; };
			const FResearchRow Rows[4] = {
				{ TEXT("Gold Bonus\n100g 40w"), EKodoResearch::GoldBonus },
				{ TEXT("Lumber Axes\n80g 30w"), EKodoResearch::Axe },
				{ TEXT("Masonry HP\n120g 60w"), EKodoResearch::Masonry },
				{ TEXT("Magic Wall\n150g 100w"), EKodoResearch::MagicWall },
			};
			// Reflect the one-time Magic Wall unlock so its button reads/locks correctly.
			const bool bMagicWall = GS && GS->Upgrades.bMagicWallUnlocked;
			for (int32 i = 0; i < 4; ++i)
			{
				const EKodoResearch Type = Rows[i].Type;
				bool bEnabled = true;
				FString Label = Rows[i].Label;
				FString Tip;
				if (Type == EKodoResearch::MagicWall && bMagicWall)
				{
					bEnabled = false; Label = TEXT("Magic Wall\nUnlocked"); Tip = TEXT("Already researched");
				}
				SetCardButton(i, Label, bEnabled, [WeakPC, Type]
				{
					if (WeakPC.IsValid()) { WeakPC->TryResearch(Type); }
				}, Tip);
				// Flag this slot so UpdateCardProgress draws a loading bar while this research is in progress.
				if (CardIsResearch.IsValidIndex(i)) { CardIsResearch[i] = true; CardResearchType[i] = Type; }
			}
		}
		else if (State.StructureId == FName("war_altar"))
		{
			// War Altar: permanent hero stat upgrades (combat viability). Five buttons; each shows the
			// current level / cost, locks at MAX, greys out when unaffordable, and on click buys one
			// level via the hero's UpgradeHeroStat. Reads live levels/costs through the runner getters.
			ARunnerCharacter* R = WeakPC.IsValid() ? WeakPC->GetRunner() : nullptr;
			const float Gold = GS ? GS->Gold : 0.f;

			struct FStatRow { const TCHAR* Name; EKodoHeroStat Stat; const TCHAR* Tip; };
			const FStatRow Rows[5] = {
				{ TEXT("Damage"),       EKodoHeroStat::Damage,      TEXT("+8 attack damage per level.") },
				{ TEXT("Armor"),        EKodoHeroStat::Armor,       TEXT("-10% incoming damage per level (max 50%).") },
				{ TEXT("Attack Speed"), EKodoHeroStat::AttackSpeed, TEXT("-12% attack interval per level (max -60%).") },
				{ TEXT("Mana Regen"),   EKodoHeroStat::ManaRegen,   TEXT("+2 mana per second per level.") },
				{ TEXT("Max HP"),       EKodoHeroStat::MaxHealth,   TEXT("+75 max HP per level (heals you on purchase).") },
			};

			for (int32 i = 0; i < 5; ++i)
			{
				const EKodoHeroStat Stat = Rows[i].Stat;
				const int32 Level = R ? R->GetHeroStatLevel(Stat) : 0;          // APPLIED level
				const int32 Queued = R ? R->GetQueuedStatCount(Stat) : 0;       // pending (timed) upgrades
				const int32 MaxLevel = R ? R->GetHeroStatMaxLevel() : 5;
				// "Full" once applied + queued reaches the cap (no more can be bought/queued).
				const bool bFull = (Level + Queued) >= MaxLevel;
				const int32 Cost = R ? R->GetHeroStatCost(Stat) : 0;            // counts queued already

				// First line: "Name Lv X/5" (X = applied level) + " (+N)" when upgrades are queued.
				FString TopLine = FString::Printf(TEXT("%s Lv %d/%d"), Rows[i].Name, Level, MaxLevel);
				if (Queued > 0)
				{
					TopLine += FString::Printf(TEXT(" (+%d)"), Queued);
				}

				FString Label;
				if (bFull)
				{
					Label = FString::Printf(TEXT("%s\nMAX"), *TopLine);
				}
				else
				{
					Label = FString::Printf(TEXT("%s\n%dg"), *TopLine, Cost);
				}

				const bool bAffordable = !bFull && R && Gold >= static_cast<float>(Cost);
				const bool bEnabled = !bFull && bAffordable;

				SetCardButton(i, Label, bEnabled, [WeakPC, Stat]
				{
					if (ARunnerCharacter* Runner = WeakPC.IsValid() ? WeakPC->GetRunner() : nullptr)
					{
						Runner->UpgradeHeroStat(Stat);
					}
				}, FString(Rows[i].Tip));
			}
			// 2x2 building: no sell button (consistent with the Upgrade Center, also 2x2).
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
			// (Magic Wall removed as a buildable: it's now the wall's final tier, unlocked by the
			//  Magic Wall research at the Command Center.)
			SetCardButton(4, TEXT("Upgrade Center\n50g"), true, [WeakPC, Self]
			{
				if (WeakPC.IsValid()) { WeakPC->SelectBlueprint(FName("upgrade_center")); }
				Self->bBuildSubmenu = false;
			});
			SetCardButton(5, TEXT("War Altar\n150g"), true, [WeakPC, Self]
			{
				if (WeakPC.IsValid()) { WeakPC->SelectBlueprint(FName("war_altar")); }
				Self->bBuildSubmenu = false;
			});
			SetCardButton(11, TEXT("Back"), true, [Self] { Self->bBuildSubmenu = false; });
		}
		else
		{
			// Data-driven 4-slot ability kit (Pass 4). Availability is by hero LEVEL, OR via the
			// Upgrade-Center research (slot 1 = Hero Skill 2, slot 3 = Hero Skill 3) — see IsAbilityUnlocked.
			ARunnerCharacter* R = WeakPC.IsValid() ? WeakPC->GetRunner() : nullptr;

			// Ability slot S -> card index S (slot0->card0, slot1->card1, passive(2)->card2, ult(3)->card3).
			for (int32 S = 0; S < 4; ++S)
			{
				const FString Name = R ? R->GetAbilityName(S) : FString();
				const bool bUnlocked = R && R->IsAbilityUnlocked(S);
				const bool bPassive = R && R->IsAbilitySlotPassive(S);
				const int32 UnlockLvl = R ? R->GetAbilityUnlockLevel(S) : 99;

				const TCHAR* KeyHint = (S == 0) ? TEXT("Q") : (S == 1) ? TEXT("E") : (S == 3) ? TEXT("R") : TEXT("");

				// Hover tooltip: name, what it does, then cost/cooldown (or the passive/locked note).
				FString Tip = Name;
				if (R) { Tip += TEXT("\n") + R->GetAbilityDescription(S); }
				if (bPassive)
				{
					Tip += bUnlocked ? TEXT("\n\nPassive — always active")
					                 : FString::Printf(TEXT("\n\nUnlocks at level %d"), UnlockLvl);
				}
				else
				{
					Tip += FString::Printf(TEXT("\n\nMana: %.0f    Cooldown: %.0fs"),
					                       R ? R->GetAbilityManaCost(S) : 0.f, R ? R->GetAbilityCooldown(S) : 0.f);
					if (!bUnlocked)
					{
						Tip += FString::Printf(TEXT("\nUnlocks at level %d"), UnlockLvl);
						// Slots 1 & 3 can also be unlocked early by research at the Upgrade Center.
						if (S == 1) { Tip += TEXT(" (or research Hero Skill 2)"); }
						else if (S == 3) { Tip += TEXT(" (or research Hero Skill 3)"); }
					}
				}

				if (bPassive)
				{
					SetCardButton(S, Name, bUnlocked, [] {}, Tip);
				}
				else if (bUnlocked)
				{
					SetCardButton(S, FString::Printf(TEXT("%s (%s)\nactive"), *Name, KeyHint), true, [WeakPC, S]
					{
						if (WeakPC.IsValid()) { WeakPC->CastHeroSkill(S); }
					}, Tip);
				}
				else
				{
					SetCardButton(S, Name, false, [] {}, Tip);
				}
			}

			// Attack the nearest kodo (or right-click a kodo / your own building).
			SetCardButton(4, TEXT("Attack"), true, [WeakPC]
			{
				if (WeakPC.IsValid())
				{
					if (ARunnerCharacter* R = WeakPC->GetRunner()) { R->AttackNearestKodo(); }
				}
			}, TEXT("Attack the nearest Kodo (or right-click a Kodo / your building)"));

			// Build submenu entry (moved from the old slot-3 Build button).
			SetCardButton(5, TEXT("Build (B)"), true, [Self] { Self->bBuildSubmenu = true; });

			// TEST Gun Mode toggle (card 6): hold Left-Click to shoot toward the cursor. Label reflects state.
			const bool bGunOn = WeakPC.IsValid() && WeakPC->IsGunMode();
			SetCardButton(6, bGunOn ? TEXT("Gun Mode ON\n(G)") : TEXT("Gun Mode\n(G)"), true, [WeakPC]
			{
				if (WeakPC.IsValid()) { WeakPC->ToggleGunMode(); }
			}, TEXT("Toggle aim mode: hold Left-Click to shoot toward the cursor (~6 tiles, 1/s)"));
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
			case EKodoHeroClass::MountainKing: Portrait = TEXT("MK"); PortraitColor = FLinearColor(0.f, 0.55f, 0.65f);    break;
			case EKodoHeroClass::Blademaster:  Portrait = TEXT("BM"); PortraitColor = FLinearColor(0.6f, 0.3f, 0.25f);    break;
			case EKodoHeroClass::Archmage:     Portrait = TEXT("AM"); PortraitColor = FLinearColor(0.46f, 0.62f, 0.93f);  break;
			case EKodoHeroClass::FarSeer:      Portrait = TEXT("FS"); PortraitColor = FLinearColor(0.42f, 0.72f, 0.45f);  break;
			case EKodoHeroClass::Paladin:      Portrait = TEXT("PA"); PortraitColor = FLinearColor(0.92f, 0.82f, 0.45f);  break;
			case EKodoHeroClass::Dreadlord:    Portrait = TEXT("DL"); PortraitColor = FLinearColor(0.55f, 0.30f, 0.62f);  break;
			default:                           Portrait = TEXT("??"); PortraitColor = FLinearColor(0.5f, 0.35f, 0.5f);    break;
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

	// Inventory row: only meaningful for the hero. Show the held items (short name, or "-" for empty)
	// when the runner is selected; collapse the whole row for building/kodo selections so no stray
	// slots/numbers show.
	if (InventoryBox)
	{
		const ARunnerCharacter* InvRunner = bRunnerSelected ? PC->GetRunner() : nullptr;
		if (InvRunner)
		{
			InventoryBox->SetVisibility(ESlateVisibility::Visible);
			for (int32 i = 0; i < InventoryLabels.Num(); ++i)
			{
				const EKodoItem Item = InvRunner->GetInventoryItem(i);
				if (InventoryLabels[i])
				{
					InventoryLabels[i]->SetText(FText::FromString(KodoItemAbbrev(Item)));
					InventoryLabels[i]->SetColorAndOpacity(FSlateColor(Item == EKodoItem::None
						? FLinearColor(0.4f, 0.37f, 0.3f) : FLinearColor(0.9f, 0.85f, 0.65f)));
				}
				if (InventorySlots.IsValidIndex(i) && InventorySlots[i])
				{
					InventorySlots[i]->SetBrushColor(Item == EKodoItem::None
						? FLinearColor(0.16f, 0.13f, 0.08f) : FLinearColor(0.26f, 0.21f, 0.10f));
				}
			}
		}
		else
		{
			InventoryBox->SetVisibility(ESlateVisibility::Collapsed);
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
	UpdateAbilityCooldowns();
	UpdateProductionPanel();
	UpdateEditorPalette();
	UpdateEditorChrome();

	// Fade the editor "Map saved" message after a few seconds.
	if (EditorMsgTimer > 0.f)
	{
		EditorMsgTimer -= InDeltaTime;
		if (EditorMsgTimer <= 0.f && EditorMsgText)
		{
			EditorMsgText->SetText(FText::GetEmpty());
		}
	}

	// The minimap redraw scans the whole grid (now 90,000 cells on the 300x300 map) and re-uploads
	// the texture to the GPU. At 4x the cells this got expensive at 12 Hz, so it runs at ~5 Hz —
	// visually fine for a minimap and a clear CPU/upload saving on the big map.
	MinimapRebuildAccum += InDeltaTime;
	if (MinimapRebuildAccum >= 0.2f)
	{
		MinimapRebuildAccum = 0.f;
		RebuildMinimapTexture();
	}
}
