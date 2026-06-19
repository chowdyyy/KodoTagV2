// Kodo Tag: Survivor — UE Migration, Phase 1.
// Port of initGrid() (game.js:500-722): three pocket bases with perimeter
// cliffs + entrance gaps, 2x2 gold mines with flanking trees, and the 2x2
// merchant tent. Base layouts are DATA (editable per-instance), defaulted to
// the prototype's exact values (TDD §1.2, §5). Visuals are blockout-grade
// HISM instances, replaced by Nanite meshes in Phase 5.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Grid/KodoGridTypes.h"
#include "KodoMapBootstrapper.generated.h"

class UKodoGridSubsystem;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMeshComponent;

UENUM()
enum class EKodoEntranceSide : uint8
{
	North, // gap in the top wall (y = Top), span over X
	South, // gap in the bottom wall (y = Bottom), span over X
	East,  // gap in the right wall (x = Right), span over Y
	West   // gap in the left wall (x = Left), span over Y
};

USTRUCT()
struct FKodoEntranceSpan
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "KodoMap")
	EKodoEntranceSide Side = EKodoEntranceSide::North;

	UPROPERTY(EditAnywhere, Category = "KodoMap")
	int32 Start = 0;

	UPROPERTY(EditAnywhere, Category = "KodoMap")
	int32 End = 0;
};

USTRUCT()
struct FPocketBaseDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "KodoMap") int32 Left = 0;
	UPROPERTY(EditAnywhere, Category = "KodoMap") int32 Right = 0;
	UPROPERTY(EditAnywhere, Category = "KodoMap") int32 Top = 0;
	UPROPERTY(EditAnywhere, Category = "KodoMap") int32 Bottom = 0;

	UPROPERTY(EditAnywhere, Category = "KodoMap")
	TArray<FKodoEntranceSpan> Entrances;

	/** 2x2 gold mine master cells (top-left). */
	UPROPERTY(EditAnywhere, Category = "KodoMap")
	TArray<FIntPoint> Mines;

	/** Marks the player's intended starting base. The CC itself is PLAYER-BUILT (150g/50w), never pre-spawned (TDD §5). */
	UPROPERTY(EditAnywhere, Category = "KodoMap")
	bool bHasCommandCenter = false;
};

UCLASS()
class KODOTAGV2_API AKodoMapBootstrapper : public AActor
{
	GENERATED_BODY()

public:
	AKodoMapBootstrapper();

	virtual void BeginPlay() override;

	/**
	 * In-world map editor backend (Feature 1): serialize the CURRENT live grid state to
	 * Content/Config/KodoMapLayout.txt in the exact format LoadLayoutFromFile parses, so the
	 * map round-trips. Returns true on a successful write, false (and logs a warning) otherwise.
	 */
	bool SaveLayoutToFile(const UKodoGridSubsystem& Grid) const;

	/**
	 * In-world map editor backend (Feature 2): refresh the blockout terrain visuals to match the
	 * current grid after edits — clears every terrain HISM then re-runs BuildVisuals (re-adds all
	 * instances + re-tints). Player-built Wall/Tower structures are managed separately by the
	 * StructureManager via OnCellChanged and are NOT touched here.
	 */
	void RebuildTerrainVisuals(UKodoGridSubsystem& Grid);

	/**
	 * Incremental editor update: refresh ONLY the given cell's two HISM instances (ground + feature)
	 * in place, recycling stable slots so single-cell edits never reshuffle HISM indices.
	 */
	void UpdateCellVisual(UKodoGridSubsystem& Grid, const FIntPoint& Cell);

	/**
	 * Incremental editor update for an edited cell + its 8 neighbors (a ridge wall's height and a
	 * ramp's slope depend on neighbor elevation, so neighbors must be re-evaluated too).
	 */
	void UpdateCellRegion(UKodoGridSubsystem& Grid, const FIntPoint& Center);

protected:
	/** Writes all structures into the grid subsystem (the data pass). */
	void BuildGrid(UKodoGridSubsystem& Grid);

	/**
	 * Load the real Warcraft III Kodo Tag layout (cliffs/trees/mines) from
	 * Content/Config/KodoMapLayout.txt, extracted from the original .w3x. Returns
	 * false (and BuildGrid falls back to the procedural bases) if the file is missing.
	 */
	bool LoadLayoutFromFile(UKodoGridSubsystem& Grid);

	/** Adds one HISM instance per occupied cell (the visual pass); also writes ramp walk-surface Z. */
	void BuildVisuals(UKodoGridSubsystem& Grid);

	/**
	 * Per-cell render evaluator — the single source of truth for what a cell renders, shared by the
	 * full BuildVisuals pass and the incremental UpdateCellVisual path. A cell produces at most a
	 * GROUND instance (ridge wall / ramp wedge / raised platform) and a FEATURE instance (tree/mine/
	 * tent). Ramp cells still write their walk-surface Z via SetRampSlope (side effect, as before).
	 */
	struct FCellRender
	{
		UHierarchicalInstancedStaticMeshComponent* GroundHism = nullptr;
		FTransform GroundXform;
		bool bHasGround = false;

		UHierarchicalInstancedStaticMeshComponent* FeatureHism = nullptr;
		FTransform FeatureXform;
		bool bHasFeature = false;
	};
	FCellRender EvaluateCell(UKodoGridSubsystem& Grid, const FIntPoint& Coord);

	/** Places ONE indestructible 2x2 admin_tower control panel near the top-right corner
	 *  (highest X / lowest Y) on the first empty buildable 2x2 footprint found scanning inward. */
	void PlaceAdminTower(UKodoGridSubsystem& Grid);

	static bool IsEntrance(const TArray<FKodoEntranceSpan>& Entrances, EKodoEntranceSide Side, int32 Coord);

	/** Prototype base table, game.js:524-559. */
	UPROPERTY(EditAnywhere, Category = "KodoMap")
	TArray<FPocketBaseDefinition> Bases;

	/** Merchant tent 2x2 master cell, game.js:704-706. */
	UPROPERTY(EditAnywhere, Category = "KodoMap")
	FIntPoint MerchantShopCell;

	UPROPERTY(VisibleAnywhere, Category = "KodoMap") TObjectPtr<UStaticMeshComponent> GroundMesh;
	UPROPERTY(VisibleAnywhere, Category = "KodoMap") TObjectPtr<UHierarchicalInstancedStaticMeshComponent> CliffInstances;
	UPROPERTY(VisibleAnywhere, Category = "KodoMap") TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TreeInstances;
	UPROPERTY(VisibleAnywhere, Category = "KodoMap") TObjectPtr<UHierarchicalInstancedStaticMeshComponent> MineInstances;
	UPROPERTY(VisibleAnywhere, Category = "KodoMap") TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TentInstances;

	/** Phase 5: alternating grass-tone tiles breaking up the flat ground color. */
	UPROPERTY(VisibleAnywhere, Category = "KodoMap") TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GrassToneInstances;

	// --- Stable per-cell instance bookkeeping (incremental editor updates) ---
	// For each HISM, CellToInst maps a cell -> its instance index, and FreeSlots holds recycled
	// (hidden) slots. We NEVER call RemoveInstance (it shifts indices and would corrupt the maps);
	// removed cells are hidden far below the map and their slot returned to FreeSlots for reuse.
	TMap<FIntPoint, int32> CliffCellToInst;     TArray<int32> CliffFreeSlots;
	TMap<FIntPoint, int32> TreeCellToInst;      TArray<int32> TreeFreeSlots;
	TMap<FIntPoint, int32> MineCellToInst;      TArray<int32> MineFreeSlots;
	TMap<FIntPoint, int32> TentCellToInst;      TArray<int32> TentFreeSlots;
	TMap<FIntPoint, int32> GrassToneCellToInst; TArray<int32> GrassToneFreeSlots;

	/** Set (add/update) the instance for Cell on Hism to Xform, reusing a free slot when available. */
	void SetSlot(UHierarchicalInstancedStaticMeshComponent* Hism, TMap<FIntPoint, int32>& CellToInst,
	             TArray<int32>& FreeSlots, const FIntPoint& Cell, const FTransform& Xform);

	/** Hide + recycle Cell's instance on Hism (never RemoveInstance — indices must stay stable). */
	void ClearSlot(UHierarchicalInstancedStaticMeshComponent* Hism, TMap<FIntPoint, int32>& CellToInst,
	               TArray<int32>& FreeSlots, const FIntPoint& Cell);

	/** Engine tintable material used as the base for all colored instances (has a "Color" param). */
	UPROPERTY() TObjectPtr<UMaterialInterface> TintBaseMaterial;

	/** Tints a component by assigning a dynamic instance of the engine tintable material. */
	void TintComponent(class UPrimitiveComponent* Component, const FLinearColor& Color);

	/** Phase 5: twilight lighting rig (Lumen GI is enabled project-wide in DefaultEngine.ini). */
	void SetupAtmosphere();
};
