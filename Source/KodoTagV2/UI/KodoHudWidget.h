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

UCLASS()
class KODOTAGV2_API UKodoHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitHud(AKodoPlayerController* InController);

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
	UFUNCTION() void OnClickStart();
	void BuildStartOverlay(UCanvasPanel* Root);
	void RestyleStartButtons() const;

	// --- Merchant shop ---
	UFUNCTION() void OnClickBuyBoots();
	void BuildShopPanel(UCanvasPanel* Root);

	// --- Victory / defeat overlay ---
	void BuildEndOverlay(UCanvasPanel* Root);

	// --- Per-slot research loading bars (in the command card) ---
	void UpdateCardProgress() const;

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
	UPROPERTY() TArray<TObjectPtr<UBorder>> InventorySlots;
	UPROPERTY() TObjectPtr<UUniformGridPanel> CardGrid;
	UPROPERTY() TArray<TObjectPtr<UButton>> CardButtons;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> CardLabels;
	/** Per-slot research "loading bar", overlaid on each card button; sized like CardButtons. */
	UPROPERTY() TArray<TObjectPtr<UProgressBar>> CardProgress;
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

	// --- Merchant shop ---
	UPROPERTY() TObjectPtr<UBorder> ShopPanel;
	UPROPERTY() TObjectPtr<UButton> BootsButton;
	UPROPERTY() TObjectPtr<UTextBlock> BootsLabel;

	// --- Victory / defeat overlay ---
	UPROPERTY() TObjectPtr<UBorder> EndOverlay;
	UPROPERTY() TObjectPtr<UTextBlock> EndText;

	/** How many Kodos the test-spawn button drops. */
	int32 TestKodoCount = 20;

	// --- Start-overlay selection state ---
	EKodoGameMode SelMode = EKodoGameMode::Maze;
	EKodoDifficulty SelDiff = EKodoDifficulty::Normal;

	TArray<TFunction<void()>> CardActions;

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
