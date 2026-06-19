// Kodo Tag: Survivor — UE Migration, Phase 1.
// World-scoped owner of the 160x160 grid: storage, coordinate transforms,
// occupancy queries, mutation choke point, cursor deprojection (TDD §4).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KodoGridTypes.h"
#include "KodoGridSubsystem.generated.h"

class APlayerController;
class UKodoGridPathfinder;

/** Named map tints the editor can override (editor color/spawn config). */
UENUM()
enum class EKodoMapColor : uint8
{
	Ridge,         // cliff/ridge instances
	Tree,          // tree instances
	Mine,          // gold-mine instances
	Wall,          // player-built wall structures
	CommandCenter, // command center structures
	Hero,          // runner/hero body
	Kodo,          // kodo bodies
	Ground,        // grass ground plane
	Count
};

/** Fired from SetCell — pathfinder invalidation (P2), placement holograms (P4), minimap. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnKodoCellChanged, const FIntPoint& /*Cell*/, const FGridCell& /*NewState*/);

UCLASS()
class KODOTAGV2_API UKodoGridSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- Coordinate transforms (TDD §2.2, §4.2) ---

	/** floor(World / CellSize) — matches JS Math.floor(px / tileSize). */
	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	FIntPoint WorldToCell(const FVector& WorldLocation) const;

	/** Cell center: ((c + 0.5) * CellSize) on X/Y, Z = terrain height. */
	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	FVector CellToWorldCenter(const FIntPoint& Cell) const;

	/** Elevation * ElevationStepUU; 0 when out of bounds (game.js:491-498). */
	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	float GetTerrainHeightUU(const FIntPoint& Cell) const;

	// --- Raised-base terrain elevation layer (E/RAMP map data; independent of FGridCell::Elevation) ---

	/** Set a cell's raised-base elevation level (0 = ground). Bounds-checked. */
	void SetElevation(const FIntPoint& Cell, int32 Level);

	/** Mark a cell as a walkable ramp with an ascent direction (0=E 1=W 2=S 3=N). Bounds-checked. */
	void SetRamp(const FIntPoint& Cell, int32 Dir);

	/** Clear a cell's walkable-ramp flag (ramp off, dir -1). Bounds-checked. */
	void ClearRamp(const FIntPoint& Cell);

	/** Set a ramp cell's slope low/high world Z (bootstrapper slope pass). */
	void SetRampSlope(const FIntPoint& Cell, float BotZ, float TopZ);

	/** Ramp ascent direction (0=E 1=W 2=S 3=N), or -1 if the cell is not a ramp. */
	int32 GetRampDir(const FIntPoint& Cell) const;

	/** Raised-base elevation level for a cell (0 when out of bounds). */
	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	int32 GetElevationLevel(const FIntPoint& Cell) const;

	/** True if the cell is a walkable ramp. */
	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	bool IsRamp(const FIntPoint& Cell) const;

	/** World Z of the raised ground at a cell = GetElevationLevel * ElevationLevelStepUU (0 OOB). */
	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	float GetElevationZ(const FIntPoint& Cell) const;

	/** Continuous elevation sampler for smooth unit movement: WorldToCell then GetElevationZ. */
	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	float GetElevationZAtWorld(const FVector& World) const;

	// --- Queries ---

	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	bool IsInBounds(const FIntPoint& Cell) const;

	/** C++ fast path. Returns a shared default cell when out of bounds. */
	const FGridCell& GetCell(const FIntPoint& Cell) const;

	/** Resolve a cell to the master/top-left cell of its multi-cell structure (itself if 1x1). */
	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	FIntPoint GetMasterCell(const FIntPoint& Cell) const;

	/** Clear a whole structure (all footprint cells of the master), e.g. on sell. */
	UFUNCTION(BlueprintCallable, Category = "KodoGrid")
	void ClearStructure(const FIntPoint& Cell);

	/** Blueprint-friendly copy. */
	UFUNCTION(BlueprintPure, Category = "KodoGrid", DisplayName = "Get Cell")
	FGridCell GetCellState(const FIntPoint& Cell) const;

	/** Port of pathfinding.js isCellBlockedForSize: any cell of the Size x Size footprint blocked or OOB. */
	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	bool IsCellBlockedForSize(const FIntPoint& TopLeft, int32 Size = 1) const;

	// --- Mutation (single choke point; fires OnCellChanged) ---

	void SetCell(const FIntPoint& Cell, const FGridCell& NewState);

	/**
	 * Apply structural damage to a cell (Kodo chewing, entities.js:1030-1049).
	 * On HP <= 0 the cell is cleared to Empty (destroyStructure equivalent for
	 * 1x1 wall/tower cells — the only types Kodos chew).
	 * @return true if the structure was destroyed.
	 */
	UFUNCTION(BlueprintCallable, Category = "KodoGrid")
	bool DamageCell(const FIntPoint& Cell, float Damage);

	// --- Pathfinding (forwards to the owned UKodoGridPathfinder) ---

	UFUNCTION(BlueprintCallable, Category = "KodoGrid")
	bool FindPath(const FIntPoint& Start, const FIntPoint& End, int32 Size, TArray<FKodoPathStep>& OutSteps) const;

	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	FIntPoint FindClosest2x2ReachableCell(const FIntPoint& Start, const FIntPoint& Target) const;

	/**
	 * Global per-frame pathfinding throttle. Returns true (and consumes one slot) if
	 * fewer than MaxPerFrame A* recalcs have run this frame, false otherwise. Stops 100+
	 * Kodos from all repathing on the same frame when the Runner crosses a cell.
	 */
	bool TryConsumePathBudget(int32 MaxPerFrame);

	// --- Shared flow fields (one Dijkstra per footprint, reused by every Kodo) ---

	/** Recompute the size-1 and size-2 distance fields toward Target if stale (at most once per frame). */
	void EnsureFlowField(const FIntPoint& Target);

	/** True if Cell can reach the current flow-field Target with the given footprint size. */
	bool IsFieldReachable(int32 Size, const FIntPoint& Cell) const;

	/** Build a shortest path (start-excluded) from Start by following the cached next-hop field. */
	bool BuildFieldPath(int32 Size, const FIntPoint& Start, TArray<FKodoPathStep>& OutSteps) const;

	/**
	 * Nearest Command Center MASTER cell (game.js findNearestCC:1474, isCCCenter only).
	 * @return false if no CC exists.
	 */
	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	bool FindNearestCommandCenter(const FIntPoint& From, FIntPoint& OutCell) const;

	/**
	 * Bunker mode: nearest player STRUCTURE cell (Wall/Tower, command_center included) to From,
	 * by octile distance. The indestructible admin_tower is excluded so kodos never target it.
	 * @return false if no chewable structure exists.
	 */
	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	bool FindNearestStructureCell(const FIntPoint& From, FIntPoint& OutCell) const;

	/** Any command_center cell within a +/- Radius box of From (deposit check, game.js:1094-1107). */
	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	bool IsNearCommandCenter(const FIntPoint& From, int32 Radius = 2) const;

	/** Any cell of the given StructureId within a +/- Radius box of From. */
	UFUNCTION(BlueprintPure, Category = "KodoGrid")
	bool IsNearStructure(const FIntPoint& From, FName StructureId, int32 Radius = 2) const;

	// --- Input helpers ---

	/**
	 * Deprojects the mouse cursor through the ground plane (Z = 0 in Phase 1) to a grid cell.
	 * @return true if the cursor resolves to an in-bounds cell.
	 */
	UFUNCTION(BlueprintCallable, Category = "KodoGrid")
	bool DeprojectCursorToCell(APlayerController* PlayerController, FIntPoint& OutCell) const;

	// --- Access ---

	const TArray<FGridCell>& GetCells() const { return Cells; }
	UKodoGridPathfinder* GetPathfinder() const { return Pathfinder; }

	int32 GetCols() const { return Cols; }
	int32 GetRows() const { return Rows; }

	// --- Editor-authored spawn points + map colors (KodoMapLayout.txt). Defaulted to the
	// existing KodoUnits constants / hard-coded tints; overridden by the map file at load. ---

	FIntPoint GetKodoSpawnCell() const { return KodoSpawnCell; }
	void SetKodoSpawnCell(const FIntPoint& Cell) { KodoSpawnCell = Cell; }

	FIntPoint GetRunnerSpawnCell() const { return RunnerSpawnCell; }
	void SetRunnerSpawnCell(const FIntPoint& Cell) { RunnerSpawnCell = Cell; }

	FIntPoint GetMerchantCell() const { return MerchantCell; }
	void SetMerchantCell(const FIntPoint& Cell) { MerchantCell = Cell; }

	/** Map tint for the named element (editor color config). */
	FLinearColor GetMapColor(EKodoMapColor Which) const
	{
		const int32 Idx = static_cast<int32>(Which);
		return MapColors.IsValidIndex(Idx) ? MapColors[Idx] : FLinearColor::White;
	}
	void SetMapColor(EKodoMapColor Which, const FLinearColor& Color)
	{
		const int32 Idx = static_cast<int32>(Which);
		if (MapColors.IsValidIndex(Idx)) { MapColors[Idx] = Color; }
	}

	FOnKodoCellChanged OnCellChanged;

private:
	UPROPERTY()
	TObjectPtr<UKodoGridPathfinder> Pathfinder;

	/** Clears every cell of the 2x2 footprint owned by Master (master + siblings). */
	void ClearStructureFootprint(const FIntPoint& Master);

	/** Flat row-major cell array, index = Y * Cols + X (TDD §4.1). */
	TArray<FGridCell> Cells;

	/** Raised-base elevation layer (parallel to Cells): level per cell, 0 = ground. */
	TArray<uint8> ElevationLevel;
	/** Walkable-ramp flag per cell (parallel to Cells): 1 = ramp. */
	TArray<uint8> RampCell;
	/** Ramp ascent direction per cell (0=E 1=W 2=S 3=N). */
	TArray<int8> RampDir;
	/** Ramp slope low/high world Z per cell (the slab's -dir and +dir edge heights). */
	TArray<float> RampBotZ;
	TArray<float> RampTopZ;

	int32 Cols = 0;
	int32 Rows = 0;

	// Editor-authored spawn cells + map colors. Initialized to the KodoUnits constants
	// and current hard-coded tints in Initialize(), then overridden by the map file.
	FIntPoint KodoSpawnCell = FIntPoint::ZeroValue;
	FIntPoint RunnerSpawnCell = FIntPoint::ZeroValue;
	FIntPoint MerchantCell = FIntPoint::ZeroValue;
	TArray<FLinearColor> MapColors; // indexed by EKodoMapColor

	/** Pathfinding budget bookkeeping (reset when the engine frame counter advances). */
	uint64 PathBudgetFrame = 0;
	int32 PathBudgetUsed = 0;

	// Cached flow fields (distance + next-hop) for footprints 1 and 2.
	TArray<float> Field1Dist, Field2Dist;
	TArray<int32> Field1Next, Field2Next;
	FIntPoint FlowTarget = FIntPoint(-9999, -9999);
	uint64 FlowComputeFrame = (uint64)-1;
	/** World time of the last flow-field recompute. The field is a full Dijkstra over the whole
	 *  grid (x2 fields); on the big 300x300 map that's expensive, so recomputes are throttled to
	 *  a min interval — Kodos repath every ~0.35-0.6 s anyway, so a slightly stale field is fine. */
	float LastFlowComputeTime = -100.f;
	/** Set when the grid changes (wall built/destroyed) so the field recomputes. */
	bool bFlowDirty = true;
};
