// Kodo Tag: Survivor — UE Migration, Phase 3/4/5.
// Enhanced Input routing, build-mode blueprints, placement ghost, harvest
// routing, selection, and the public API the WC3-style HUD widget drives.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "KodoPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UMaterialInstanceDynamic;
class AKodoCameraPawn;
class ARunnerCharacter;
class AKodoStructureManager;
class AStaticMeshActor;
class AKodoCharacter;
class UKodoHudWidget;
enum class EKodoResearch : uint8;

UENUM(BlueprintType)
enum class EKodoSelection : uint8
{
	None,
	Runner,
	Cell,
	Kodo
};

UCLASS()
class KODOTAGV2_API AKodoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AKodoPlayerController();

	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	// --- Public API consumed by UKodoHudWidget ---

	void SelectBlueprint(FName PresetId);
	void TryResearch(EKodoResearch Type);

	/** HUD passthrough: cast a hero skill by slot (0 = signature, 2 = researched active). */
	void CastHeroSkill(int32 Slot);

	ARunnerCharacter* GetRunner();
	AKodoStructureManager* GetStructureManager();
	AKodoCameraPawn* GetCameraPawn() const;

	EKodoSelection GetSelectionKind() const;
	FIntPoint GetSelectedCell() const { return SelectedCell; }
	AKodoCharacter* GetSelectedKodo() const { return SelectedKodo.Get(); }
	FName GetSelectedBlueprint() const { return SelectedBuildPreset; }
	void ClearSelection();

	/** Minimap pan: normalized [0..1] map position. */
	void PanCameraToNormalized(const FVector2D& Normalized);

protected:
	// --- Input handlers ---
	void OnPan(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);
	void OnCameraLock(const FInputActionValue& Value);
	void OnMoveCommand(const FInputActionValue& Value);
	void OnPlace(const FInputActionValue& Value);
	void OnSelectWall(const FInputActionValue& Value);
	void OnSelectCommandCenter(const FInputActionValue& Value);
	void OnSelectBasicTower(const FInputActionValue& Value);
	void OnCastSpell(const FInputActionValue& Value);
	void OnCastSkill3(const FInputActionValue& Value);
	void OnSetHeroClass1(const FInputActionValue& Value);
	void OnSetHeroClass2(const FInputActionValue& Value);
	void OnSetHeroClass3(const FInputActionValue& Value);
	void OnSetHeroClass4(const FInputActionValue& Value);
	void OnResearch1(const FInputActionValue& Value);
	void OnResearch2(const FInputActionValue& Value);
	void OnResearch3(const FInputActionValue& Value);
	void OnResearch4(const FInputActionValue& Value);
	void OnResearch5(const FInputActionValue& Value);
	void OnResearch6(const FInputActionValue& Value);
	void OnResearch7(const FInputActionValue& Value);

	void UpdateGhost();

	/** Positions a colored selection ring under the selected runner / structure / kodo. */
	void UpdateSelectionMarker();

	/** Walk-to-build: once the hero reaches a queued far build spot, place it and free the hero. */
	void TickPendingBuild();

	// --- Code-built input assets ---
	UPROPERTY() TObjectPtr<UInputMappingContext> DefaultContext;
	UPROPERTY() TObjectPtr<UInputAction> PanAction;
	UPROPERTY() TObjectPtr<UInputAction> ZoomAction;
	UPROPERTY() TObjectPtr<UInputAction> LockAction;
	UPROPERTY() TObjectPtr<UInputAction> MoveAction;
	UPROPERTY() TObjectPtr<UInputAction> PlaceAction;
	UPROPERTY() TObjectPtr<UInputAction> WallAction;
	UPROPERTY() TObjectPtr<UInputAction> CommandCenterAction;
	UPROPERTY() TObjectPtr<UInputAction> TowerAction;
	UPROPERTY() TObjectPtr<UInputAction> CastSpellAction;
	UPROPERTY() TObjectPtr<UInputAction> CastSkill3Action;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> HeroClassActions;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> ResearchActions;

	UPROPERTY(EditAnywhere, Category = "KodoInput")
	float EdgeScrollMarginPx = 12.f;

	FName SelectedBuildPreset;

	// Walk-to-build: when you place far away, the hero walks to the spot and starts it on arrival,
	// then is free (construction self-completes). Cleared on place / new order.
	FName PendingBuildPreset;
	FIntPoint PendingBuildCell = FIntPoint(-1, -1);
	FIntPoint PendingBuildOrigin = FIntPoint(-1, -1);

	// Selection (Phase 5 HUD).
	EKodoSelection SelectionKind = EKodoSelection::None;
	FIntPoint SelectedCell = FIntPoint(-1, -1);
	TWeakObjectPtr<AKodoCharacter> SelectedKodo;

	UPROPERTY() TObjectPtr<AStaticMeshActor> GhostActor;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GhostMID;
	UPROPERTY() TObjectPtr<AStaticMeshActor> SelMarkerActor;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SelMarkerMID;
	UPROPERTY() TObjectPtr<UKodoHudWidget> HudWidget;

	TWeakObjectPtr<ARunnerCharacter> CachedRunner;
	TWeakObjectPtr<AKodoStructureManager> CachedManager;
};
