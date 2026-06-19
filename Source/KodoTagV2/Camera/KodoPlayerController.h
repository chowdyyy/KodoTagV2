// Kodo Tag: Survivor — UE Migration, Phase 3/4/5.
// Enhanced Input routing, build-mode blueprints, placement ghost, harvest
// routing, selection, and the public API the WC3-style HUD widget drives.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Actors/RunnerCharacter.h" // EKodoCastTarget (PendingCastType default initializer)
#include "KodoPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UMaterialInstanceDynamic;
class UStaticMesh;
class AKodoCameraPawn;
class ARunnerCharacter;
class AKodoStructureManager;
class AStaticMeshActor;
class AKodoCharacter;
class UKodoHudWidget;
class AKodoMapBootstrapper;
enum class EKodoResearch : uint8;

UENUM(BlueprintType)
enum class EKodoSelection : uint8
{
	None,
	Runner,
	Cell,
	Kodo
};

/** In-world map-editor paint tools (spatial editor pass). */
enum class EEditTool : uint8
{
	None,
	Ridge,
	Tree,
	Mine,
	Erase,
	KodoSpawn,
	RunnerSpawn,
	Merchant,
	ElevRaise,
	ElevLower,
	Ramp
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

	/** HUD passthrough: cast a hero skill by slot (Pass 2: 0/1 actives, 2 passive, 3 ultimate).
	 *  Pass 5: instant/self abilities fire immediately; target abilities enter click-to-target mode. */
	void CastHeroSkill(int32 Slot);

	/** Exit any pending targeted-cast state and restore the default cursor. */
	void CancelPendingCast();

	/** Slot of the ability currently awaiting a target (>= 0), or -1 when none — read by the HUD
	 *  to keep the armed ability's command-card button highlighted while targeting. */
	int32 GetPendingCastSlot() const { return PendingCastSlot; }

	/** TEST "Gun Mode": twin-stick aim. When ON, left-click no longer selects/builds — instead you
	 *  hold Left-Click to fire a pistol toward the cursor (hitscan, ~6 cells, 1 shot/sec). Read by the
	 *  HUD so the Gun Mode command-card button can reflect the toggle. */
	bool IsGunMode() const { return bGunMode; }

	/** Flip Gun Mode on/off (bound to G and the HUD button). Resets the fire timer. */
	void ToggleGunMode();

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

	// --- In-world map editor (spatial editor pass) ---

	/** Enter editor paint mode: clears the active blueprint; the match stays unstarted. */
	void EnterEditMode();
	/** Leave editor paint mode (back to normal play/selection). */
	void ExitEditMode();
	bool IsEditMode() const { return bEditMode; }

	/** Set the active paint tool. RampDir >= 0 also updates the stored ramp direction. */
	void SetEditTool(EEditTool Tool, int32 RampDir = -1);

	/** Active editor tool (consumed by the HUD palette for the active-tool highlight). */
	EEditTool GetActiveEditTool() const { return ActiveEditTool; }
	/** Stored ramp direction 0=E,1=W,2=S,3=N (consumed by the HUD for the active-dir highlight). */
	int32 GetEditRampDir() const { return EditRampDir; }

	/** Cached map bootstrapper (terrain visuals + layout save). */
	AKodoMapBootstrapper* GetBootstrapper();

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
	void OnCastSkill2(const FInputActionValue& Value);
	void OnCastSkill3(const FInputActionValue& Value);
	void OnQuitGame(const FInputActionValue& Value);
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

	/** G hotkey — toggles Gun Mode (mirrors the bool-action input pattern). */
	void OnToggleGunMode(const FInputActionValue& Value);

	/** TEST Gun Mode: hitscan a pistol shot toward the cursor along the ground plane (~6 cells,
	 *  ~30 dmg to the first kodo on the ray), face the hero toward the aim, and spawn muzzle/impact FX. */
	void FirePistol(ARunnerCharacter* R);

	void UpdateGhost();

	/** Apply the active editor tool at a grid cell (flags the terrain dirty; the rebuild is
	 *  coalesced once per frame in PlayerTick instead of running per edit). */
	void ApplyEditAt(const FIntPoint& Cell);

	/** Clear a single cell (type/elevation/ramp) — shared by the Erase tool and right-click erase. */
	void EraseCell(const FIntPoint& Cell);

	/** Positions a colored selection ring under the selected runner / structure / kodo. */
	void UpdateSelectionMarker();

	/** Per-frame hover cursor: sets CurrentMouseCursor by what's under the cursor (kodo -> crosshair,
	 *  own building / hero -> hand, otherwise default). Early-outs in edit / gun / targeting modes so
	 *  those keep their own cursor. */
	void UpdateHoverCursor();

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
	UPROPERTY() TObjectPtr<UInputAction> CastSkill2Action;
	UPROPERTY() TObjectPtr<UInputAction> CastSkill3Action;
	UPROPERTY() TObjectPtr<UInputAction> QuitAction;
	UPROPERTY() TObjectPtr<UInputAction> GunModeAction;
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

	// Targeted casting (Pass 5): while PendingCastSlot >= 0 the cursor is a crosshair and the next
	// left-click picks the target (kodo or ground per PendingCastType); right-click cancels.
	int32 PendingCastSlot = -1;
	EKodoCastTarget PendingCastType = EKodoCastTarget::Instant;

	UPROPERTY() TObjectPtr<AStaticMeshActor> GhostActor;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GhostMID;
	/** Cached engine shapes used to make the editor/build preview mirror the placed item (cube/cone/cylinder). */
	UPROPERTY() TObjectPtr<UStaticMesh> GhostCubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> GhostConeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> GhostCylinderMesh; // shooting-tower build ghost (towers render as a cylinder)
	UPROPERTY() TObjectPtr<AStaticMeshActor> SelMarkerActor;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SelMarkerMID;
	UPROPERTY() TObjectPtr<UKodoHudWidget> HudWidget;

	TWeakObjectPtr<ARunnerCharacter> CachedRunner;
	TWeakObjectPtr<AKodoStructureManager> CachedManager;
	TWeakObjectPtr<AKodoMapBootstrapper> CachedBootstrapper;

	// --- TEST Gun Mode (twin-stick aim) ---
	/** When true, left-click fires the pistol toward the cursor instead of selecting/building. */
	bool bGunMode = false;
	/** Counts down to the next allowed shot; firing resets it to 1.0 (1 shot/sec). */
	float GunFireTimer = 0.f;

	// --- Editor paint state ---
	bool bEditMode = false;
	EEditTool ActiveEditTool = EEditTool::Ridge;
	int32 EditRampDir = 3; // N
	/** Last cell painted during a held-mouse drag, so we don't repaint/rebuild the same cell each frame. */
	FIntPoint LastPaintedCell = FIntPoint(-1, -1);
	/** Cells edited during a frame; PlayerTick does ONE incremental per-cell visual update per cell (+neighbors). */
	TSet<FIntPoint> EditDirtyCells;
};
