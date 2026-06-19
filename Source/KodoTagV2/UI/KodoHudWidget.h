// Kodo Tag: Survivor — UE Migration, Phase 5.
// WC3-style HUD, constructed fully in C++ (code-only project):
//  - top resource bar (gold/wood + carried, wave, phase timer)
//  - bottom panel: minimap | unit details (portrait, segmented HP, stats,
//    inventory slots) | 3x4 command card (context: runner/build/spire/CC).
// Mimics the prototype's layout (index.html sidebar + game.js renderBuildGrid).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Grid/KodoGridTypes.h"
#include "Core/KodoTagGameState.h"     // EKodoGameMode
#include "Actors/KodoWaveController.h" // EKodoDifficulty
#include "Actors/RunnerCharacter.h"    // EKodoItem (shop + inventory)
#include "Grid/KodoStructureManager.h" // EKodoResearch
#include "KodoHudWidget.generated.h"

class AKodoPlayerController;
class AKodoWaveController;
class UTexture2D;
class UTextBlock;
class UBorder;
class UButton;
class UImage;
class UProgressBar;
class UUniformGridPanel;
class UCanvasPanel;
class UHorizontalBox;
class UVerticalBox;

UCLASS()
class KODOTAGV2_API UKodoHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitHud(AKodoPlayerController* InController);

	/** Briefly highlight a command-card slot (hotkey-press feedback so a keypress mirrors a click). */
	void FlashCardButton(int32 SlotIndex);

	/** Build-hotkey feedback (W/C/T): open the build submenu, force a card rebuild next frame, and
	 *  flash the matching submenu card so pressing the hotkey visibly reacts like clicking the button. */
	void OpenBuildSubmenuFlash(int32 CardIndex);

	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

protected:
	// Command card buttons route through fixed UFUNCTIONs into CardActions.
	UFUNCTION() void OnCard0();  UFUNCTION() void OnCard1();  UFUNCTION() void OnCard2();
	UFUNCTION() void OnCard3();  UFUNCTION() void OnCard4();  UFUNCTION() void OnCard5();
	UFUNCTION() void OnCard6();  UFUNCTION() void OnCard7();  UFUNCTION() void OnCard8();
	UFUNCTION() void OnCard9();  UFUNCTION() void OnCard10(); UFUNCTION() void OnCard11();
	void HandleCard(int32 Index);

	// Debug test-spawn panel.
	UFUNCTION() void OnTestSpawn();
	UFUNCTION() void OnTestClear();
	UFUNCTION() void OnTestPlus();
	UFUNCTION() void OnTestMinus();
	void BuildTestPanel(UCanvasPanel* Root);
	AKodoWaveController* ResolveWaveController() const;

	// --- Start overlay (mode + difficulty selection) ---
	UFUNCTION() void OnPickMaze();
	UFUNCTION() void OnPickGod();
	UFUNCTION() void OnPickBunker();
	UFUNCTION() void OnPickEasy();
	UFUNCTION() void OnPickNormal();
	UFUNCTION() void OnPickHard();
	UFUNCTION() void OnPickInsane();
	// One handler per hero button (AddDynamic needs an arg-less UFUNCTION); each sets SelHero = HeroChoices[n].
	UFUNCTION() void OnPickHero0(); UFUNCTION() void OnPickHero1(); UFUNCTION() void OnPickHero2();
	UFUNCTION() void OnPickHero3(); UFUNCTION() void OnPickHero4(); UFUNCTION() void OnPickHero5();
	void HandlePickHero(int32 Index);
	UFUNCTION() void OnClickStart();
	void BuildStartOverlay(UCanvasPanel* Root);
	void RestyleStartButtons() const;

	// --- Merchant shop ---
	UFUNCTION() void OnClickBuyBoots();
	// One handler per shop slot (AddDynamic needs an arg-less UFUNCTION); each buys ShopItems[n].
	UFUNCTION() void OnBuyItem0(); UFUNCTION() void OnBuyItem1(); UFUNCTION() void OnBuyItem2();
	UFUNCTION() void OnBuyItem3(); UFUNCTION() void OnBuyItem4(); UFUNCTION() void OnBuyItem5();
	void HandleBuyItem(int32 Index);
	void BuildShopPanel(UCanvasPanel* Root);

	// --- Hero inventory slots (use a consumable on click) ---
	UFUNCTION() void OnUseItem0(); UFUNCTION() void OnUseItem1(); UFUNCTION() void OnUseItem2();
	UFUNCTION() void OnUseItem3(); UFUNCTION() void OnUseItem4(); UFUNCTION() void OnUseItem5();
	void HandleUseItem(int32 Index);

	// --- In-world map editor palette (spatial editor pass) ---
	UFUNCTION() void OnClickEditMap();   // start-overlay entry: enter edit mode
	UFUNCTION() void OnEditPan();        // Pan/None: scroll the map without placing anything
	UFUNCTION() void OnEditRidge();
	UFUNCTION() void OnEditTree();
	UFUNCTION() void OnEditMine();
	UFUNCTION() void OnEditErase();
	UFUNCTION() void OnEditKodoSpawn();
	UFUNCTION() void OnEditRunnerSpawn();
	UFUNCTION() void OnEditMerchant();
	UFUNCTION() void OnEditRaise();
	UFUNCTION() void OnEditLower();
	UFUNCTION() void OnEditRamp();       // selects Ramp tool, keeps current direction
	UFUNCTION() void OnEditRampN();
	UFUNCTION() void OnEditRampE();
	UFUNCTION() void OnEditRampS();
	UFUNCTION() void OnEditRampW();
	UFUNCTION() void OnEditSave();
	UFUNCTION() void OnEditPlay();
	void BuildEditorPalette(UCanvasPanel* Root);
	void UpdateEditorPalette() const;
	/** Show/hide gameplay chrome (the bottom minimap/details/command bar) based on edit mode. */
	void UpdateEditorChrome() const;

	// --- Victory / defeat overlay ---
	void BuildEndOverlay(UCanvasPanel* Root);

	// --- Per-slot research loading bars (in the command card) ---
	void UpdateCardProgress() const;

	/** Per-frame: overlay a live cooldown countdown on the hero's ability buttons (cards 0/1/3). */
	void UpdateAbilityCooldowns() const;

	/** Production progress panel above the command card (construction / research: name, time, fill bar, queue). */
	void UpdateProductionPanel() const;

	// Per-frame update helpers for the new gameplay-layer widgets.
	void UpdateMatchHud() const;
	void UpdateShopPanel() const;
	void UpdateEndOverlay() const;
	void UpdateStartOverlay() const;

	void BuildTree();
	UTextBlock* MakeText(const FString& Initial, float FontSize, const FLinearColor& Color) const;
	void UpdateTopBar() const;
	void UpdateDetailsPanel() const;
	void RebuildCommandCardIfNeeded();
	void SetCardButton(int32 Index, const FString& Label, bool bEnabled, TFunction<void()> Action, const FString& Tooltip = TEXT(""));
	FString MakeContextKey() const;
	void RebuildMinimapTexture() const;
	bool MinimapNormalizedFromAbsolute(const FVector2D& AbsolutePosition, FVector2D& OutNormalized) const;

	// --- Widget refs (UPROPERTY keeps them alive) ---
	UPROPERTY() TObjectPtr<UTextBlock> GoldText;
	UPROPERTY() TObjectPtr<UTextBlock> WoodText;
	UPROPERTY() TObjectPtr<UTextBlock> WaveText;
	UPROPERTY() TObjectPtr<UTextBlock> TimerText;
	UPROPERTY() TObjectPtr<UImage> MinimapImage;
	UPROPERTY() TObjectPtr<UBorder> PortraitBorder;
	UPROPERTY() TObjectPtr<UTextBlock> PortraitText;
	UPROPERTY() TObjectPtr<UTextBlock> NameText;
	UPROPERTY() TArray<TObjectPtr<UBorder>> HpSegments;
	UPROPERTY() TObjectPtr<UTextBlock> HpText;
	UPROPERTY() TObjectPtr<UTextBlock> StatsText;
	/** Whole inventory row; collapsed when a building/kodo is selected so no stray slots show. */
	UPROPERTY() TObjectPtr<UHorizontalBox> InventoryBox;
	UPROPERTY() TArray<TObjectPtr<UBorder>> InventorySlots;
	/** Per-slot text label (item short name or "-") and the click button overlaid on each slot. */
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> InventoryLabels;
	UPROPERTY() TArray<TObjectPtr<UButton>> InventoryButtons;
	UPROPERTY() TObjectPtr<UUniformGridPanel> CardGrid;
	UPROPERTY() TArray<TObjectPtr<UButton>> CardButtons;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> CardLabels;
	/** Per-slot research "loading bar", overlaid on each card button; sized like CardButtons. */
	UPROPERTY() TArray<TObjectPtr<UProgressBar>> CardProgress;
	/** Per-slot big centered cooldown countdown, overlaid on each card button (hero ability slots). */
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> CardCooldownText;

	// --- Production progress panel (center of the bottom bar; shows the producing building's queue) ---
	UPROPERTY() TObjectPtr<UBorder> ProductionPanel;            // collapsed unless the selected building is producing
	UPROPERTY() TObjectPtr<UTextBlock> ProdHeaderText;          // "In Production"
	UPROPERTY() TArray<TObjectPtr<UVerticalBox>> ProdRows;      // one row per active item (toggled per frame)
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> ProdRowName;     // item name
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> ProdRowTime;     // "elapsed / totals"
	UPROPERTY() TArray<TObjectPtr<UProgressBar>> ProdRowBar;    // filling progress bar
	UPROPERTY() TObjectPtr<UTexture2D> MinimapTexture;
	UPROPERTY() TObjectPtr<UTextBlock> TestCountText;

	// --- Mana bar in the details panel (blue, mirrors the segmented HP bar) ---
	UPROPERTY() TObjectPtr<UProgressBar> ManaBar;

	// --- Top-bar gameplay-layer text (match timer / mode / spell CD) ---
	UPROPERTY() TObjectPtr<UTextBlock> MatchTimerText;
	UPROPERTY() TObjectPtr<UTextBlock> ModeText;
	UPROPERTY() TObjectPtr<UTextBlock> SpellCdText;

	// --- Start overlay (mode + difficulty selection) ---
	UPROPERTY() TObjectPtr<UBorder> StartOverlay;
	UPROPERTY() TObjectPtr<UButton> ModeMazeButton;
	UPROPERTY() TObjectPtr<UButton> ModeGodButton;
	UPROPERTY() TObjectPtr<UButton> ModeBunkerButton;
	UPROPERTY() TObjectPtr<UButton> DiffEasyButton;
	UPROPERTY() TObjectPtr<UButton> DiffNormalButton;
	UPROPERTY() TObjectPtr<UButton> DiffHardButton;
	UPROPERTY() TObjectPtr<UButton> DiffInsaneButton;
	/** "Choose your hero" row: one button per EKodoHeroClass; parallel arrays drive label + highlight. */
	UPROPERTY() TArray<TObjectPtr<UButton>> HeroButtons;
	TArray<EKodoHeroClass> HeroChoices;

	// --- Merchant shop ---
	UPROPERTY() TObjectPtr<UBorder> ShopPanel;
	UPROPERTY() TObjectPtr<UButton> BootsButton;     // retained: now the first (Boots) shop button
	UPROPERTY() TObjectPtr<UTextBlock> BootsLabel;   // retained: now the first (Boots) shop label
	/** One purchasable item per shop button; parallel arrays drive label/affordability per frame. */
	UPROPERTY() TArray<TObjectPtr<UButton>> ShopButtons;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> ShopLabels;
	TArray<EKodoItem> ShopItems;

	// --- In-world map editor palette ---
	UPROPERTY() TObjectPtr<UButton> EditMapButton;     // on the start overlay
	UPROPERTY() TObjectPtr<UBorder> EditorPalette;     // shown only while IsEditMode()
	UPROPERTY() TObjectPtr<UTextBlock> EditorMsgText;  // "Map saved" feedback
	float EditorMsgTimer = 0.f;                        // seconds left to show EditorMsgText

	// Tool buttons kept for the active-tool highlight (UpdateEditorPalette restyles them).
	UPROPERTY() TObjectPtr<UButton> ToolPanButton;
	UPROPERTY() TObjectPtr<UButton> ToolRidgeButton;
	UPROPERTY() TObjectPtr<UButton> ToolTreeButton;
	UPROPERTY() TObjectPtr<UButton> ToolMineButton;
	UPROPERTY() TObjectPtr<UButton> ToolEraseButton;
	UPROPERTY() TObjectPtr<UButton> ToolKodoSpawnButton;
	UPROPERTY() TObjectPtr<UButton> ToolRunnerSpawnButton;
	UPROPERTY() TObjectPtr<UButton> ToolMerchantButton;
	UPROPERTY() TObjectPtr<UButton> ToolRaiseButton;
	UPROPERTY() TObjectPtr<UButton> ToolLowerButton;
	UPROPERTY() TObjectPtr<UButton> ToolRampButton;
	// Ramp direction buttons (highlight the active dir: 0=E,1=W,2=S,3=N).
	UPROPERTY() TObjectPtr<UButton> RampNButton;
	UPROPERTY() TObjectPtr<UButton> RampEButton;
	UPROPERTY() TObjectPtr<UButton> RampSButton;
	UPROPERTY() TObjectPtr<UButton> RampWButton;

	// The bottom gameplay bar (minimap | details | command card); collapsed in edit mode.
	UPROPERTY() TObjectPtr<UBorder> BottomBar;

	// --- Victory / defeat overlay ---
	UPROPERTY() TObjectPtr<UBorder> EndOverlay;
	UPROPERTY() TObjectPtr<UTextBlock> EndText;

	/** How many Kodos the test-spawn button drops. */
	int32 TestKodoCount = 20;

	// --- Start-overlay selection state ---
	EKodoGameMode SelMode = EKodoGameMode::Maze;
	EKodoDifficulty SelDiff = EKodoDifficulty::Normal;
	EKodoHeroClass SelHero = EKodoHeroClass::MountainKing;

	TArray<TFunction<void()>> CardActions;

	/** Each card's normal (CardIconColor) background, captured in SetCardButton (sized 12). Restored
	 *  by UpdateAbilityCooldowns when a slot leaves cooldown / loses its flash/armed highlight. */
	TArray<FLinearColor> CardBaseColor;
	/** Per-slot keypress "flash" countdown (sized 12). FlashCardButton sets it; the per-frame button
	 *  pass decrements it and tints the card bright while > 0. Mutable: written from a const update. */
	mutable TArray<float> CardFlashTimer;

	/** Per-slot research flagging (sized 12): which card slots show a research loading bar, and of what type.
	 *  Set in RebuildCommandCardIfNeeded's research branches; consumed each frame by UpdateCardProgress. */
	TArray<bool> CardIsResearch;
	TArray<EKodoResearch> CardResearchType;

	TWeakObjectPtr<AKodoPlayerController> Controller;
	FString CurrentContextKey;
	bool bBuildSubmenu = false;
	bool bDraggingMinimap = false;

	/** Seconds since the last minimap rebuild; the full-grid scan + GPU upload runs at ~12 Hz, not per frame. */
	float MinimapRebuildAccum = 0.f;
};
