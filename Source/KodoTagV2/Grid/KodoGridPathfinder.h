// Kodo Tag: Survivor — UE Migration, Phase 1.
// Faithful port of js/engine/pathfinding.js (A* + binary heap + 2x2 footprint support).
// The JS BinaryHeap is ported directly (not Algo::Heap*) to guarantee identical
// tie-breaking and therefore byte-identical paths for the golden parity tests (TDD §9).

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "KodoGridTypes.h"
#include "KodoGridPathfinder.generated.h"

UCLASS()
class KODOTAGV2_API UKodoGridPathfinder : public UObject
{
	GENERATED_BODY()

public:
	/** Must be called before use (UKodoGridSubsystem does this in Initialize). */
	void Configure(int32 InCols, int32 InRows);

	/**
	 * Port of Pathfinder.findPath (pathfinding.js:133-244).
	 * @param Cells   Row-major grid state (Y * Cols + X), Cols*Rows entries.
	 * @param Start   Start cell (unit's current cell / 2x2 top-left).
	 * @param End     Goal cell. For Size > 1 it is clamped to Cols/Rows - Size, as in JS.
	 * @param Size    Unit footprint: 1 (Runner, Speed Kodo) or 2 (Standard/Tank/Blink Kodo).
	 * @param OutSteps Path from start to end, EXCLUDING the start cell (JS contract).
	 *                 Empty + true return means "already at the goal".
	 * @return false where JS returned null (no path / out of bounds).
	 */
	bool FindPath(const TArray<FGridCell>& Cells, FIntPoint Start, FIntPoint End, int32 Size,
	              TArray<FKodoPathStep>& OutSteps) const;

	/**
	 * Port of findClosest2x2ReachableCell (pathfinding.js:272-317): BFS over unblocked
	 * 2x2 footprints from Start, returning the visited cell with minimum Manhattan
	 * distance to Target (Start itself if nothing closer is reachable).
	 */
	FIntPoint FindClosest2x2ReachableCell(const TArray<FGridCell>& Cells, FIntPoint Start, FIntPoint Target) const;

	/** Octile-distance heuristic — admissible for 8-direction (diagonal) movement. */
	static float Heuristic(const FIntPoint& A, const FIntPoint& B);

	/**
	 * Dijkstra distance + next-hop field from Target over Size-walkable cells (8-dir,
	 * diagonal sqrt(2), no corner-cutting — matching FindPath). Computed ONCE and shared
	 * by every unit of that footprint, so 100+ Kodos all get the shortest path to the
	 * Runner instantly the moment it moves, with no per-unit A*.
	 * @param OutDist  cost-to-Target per cell; TNumericLimits<float>::Max() = unreachable.
	 * @param OutNext  flat index of the next cell toward Target (-1 at Target / unreachable).
	 */
	void ComputeDistanceField(const TArray<FGridCell>& Cells, FIntPoint Target, int32 Size,
	                          TArray<float>& OutDist, TArray<int32>& OutNext) const;

	int32 GetCols() const { return Cols; }
	int32 GetRows() const { return Rows; }

	/** Pooled A* node. Public so the internal binary heap can reference it. */
	struct FPathNode
	{
		int32 X = 0;
		int32 Y = 0;
		ECellType Type = ECellType::Empty;
		float G = 0.f;
		float H = 0.f;
		float F = 0.f;
		FPathNode* Parent = nullptr;
		bool bOpened = false;
		bool bClosed = false;
		/** Search id this node was last reset for; avoids clearing all Cols*Rows nodes per call. */
		uint32 Gen = 0;

		float GetWeight() const { return KodoGrid::GetCellTypeWeight(Type); }
	};

private:
	int32 Cols = 0;
	int32 Rows = 0;

	/** Reused across calls (sized once); pointers stay valid because we never resize mid-search. */
	mutable TArray<FPathNode> NodePool;
	mutable uint32 SearchGen = 0;
};
