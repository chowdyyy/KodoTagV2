// Kodo Tag: Survivor — UE Migration, Phase 1.
// Line-for-line behavioral port of js/engine/pathfinding.js.

#include "Grid/KodoGridPathfinder.h"
#include "Algo/Reverse.h"

namespace
{
	/** Cost of one diagonal step relative to a cardinal step. */
	constexpr float KodoSqrt2 = 1.41421356f;

	/** The pooled node type now lives on UKodoGridPathfinder (reused across calls). */
	using FPathNode = UKodoGridPathfinder::FPathNode;

	/**
	 * Direct port of the JS BinaryHeap (pathfinding.js:7-94), scored on F.
	 * Ported verbatim (including >= / < comparisons and rescore-by-linear-search)
	 * so equal-F tie-breaking matches the prototype exactly.
	 */
	class FJsBinaryHeap
	{
	public:
		void Push(FPathNode* Element)
		{
			Content.Add(Element);
			BubbleUp(Content.Num() - 1);
		}

		FPathNode* Pop()
		{
			FPathNode* Result = Content[0];
			FPathNode* End = Content.Pop();
			if (Content.Num() > 0)
			{
				Content[0] = End;
				SinkDown(0);
			}
			return Result;
		}

		int32 Size() const { return Content.Num(); }

		void RescoreElement(FPathNode* Node)
		{
			const int32 Index = Content.Find(Node);
			if (Index != INDEX_NONE)
			{
				BubbleUp(Index);
				SinkDown(Index);
			}
		}

	private:
		static float Score(const FPathNode* Node) { return Node->F; }

		void BubbleUp(int32 N)
		{
			FPathNode* Element = Content[N];
			const float ElementScore = Score(Element);
			while (N > 0)
			{
				const int32 ParentN = ((N + 1) / 2) - 1; // floor((n+1)/2)-1
				FPathNode* Parent = Content[ParentN];
				if (ElementScore >= Score(Parent))
				{
					break;
				}
				Content[ParentN] = Element;
				Content[N] = Parent;
				N = ParentN;
			}
		}

		void SinkDown(int32 N)
		{
			const int32 Length = Content.Num();
			FPathNode* Element = Content[N];
			const float ElemScore = Score(Element);

			while (true)
			{
				const int32 Child2N = (N + 1) * 2;
				const int32 Child1N = Child2N - 1;
				int32 Swap = INDEX_NONE;
				float Child1Score = 0.f;

				if (Child1N < Length)
				{
					Child1Score = Score(Content[Child1N]);
					if (Child1Score < ElemScore)
					{
						Swap = Child1N;
					}
				}
				if (Child2N < Length)
				{
					const float Child2Score = Score(Content[Child2N]);
					if (Child2Score < (Swap == INDEX_NONE ? ElemScore : Child1Score))
					{
						Swap = Child2N;
					}
				}

				if (Swap == INDEX_NONE)
				{
					break;
				}
				Content[N] = Content[Swap];
				Content[Swap] = Element;
				N = Swap;
			}
		}

		TArray<FPathNode*> Content;
	};
}

void UKodoGridPathfinder::Configure(const int32 InCols, const int32 InRows)
{
	Cols = InCols;
	Rows = InRows;
}

float UKodoGridPathfinder::Heuristic(const FIntPoint& A, const FIntPoint& B)
{
	// Octile distance — admissible for 8-direction movement where a diagonal step
	// costs sqrt(2) and a cardinal step costs 1: max(dx,dy) + (sqrt(2)-1)*min(dx,dy).
	const int32 Dx = FMath::Abs(A.X - B.X);
	const int32 Dy = FMath::Abs(A.Y - B.Y);
	const int32 Hi = FMath::Max(Dx, Dy);
	const int32 Lo = FMath::Min(Dx, Dy);
	return static_cast<float>(Hi) + (KodoSqrt2 - 1.f) * static_cast<float>(Lo);
}

bool UKodoGridPathfinder::FindPath(const TArray<FGridCell>& Cells, const FIntPoint Start, const FIntPoint End,
                                   const int32 Size, TArray<FKodoPathStep>& OutSteps) const
{
	OutSteps.Reset();

	if (!ensure(Cols > 0 && Rows > 0 && Cells.Num() == Cols * Rows))
	{
		return false;
	}

	// Basic bounds checking (pathfinding.js:135-138)
	if (Start.X < 0 || Start.X >= Cols || Start.Y < 0 || Start.Y >= Rows ||
	    End.X < 0 || End.X >= Cols || End.Y < 0 || End.Y >= Rows)
	{
		return false;
	}

	// Limit A* target for 2x2 size so the top-left never lands out of bounds (pathfinding.js:141-146)
	int32 TargetX = End.X;
	int32 TargetY = End.Y;
	if (Size > 1)
	{
		TargetX = FMath::Min(Cols - Size, TargetX);
		TargetY = FMath::Min(Rows - Size, TargetY);
	}

	// Reuse a pooled node grid instead of allocating Cols*Rows nodes every call. A
	// per-search generation stamp means any node with Gen != SearchGen is treated as
	// fresh, so we only ever initialize the handful of cells a search actually touches
	// — the key fix for stutter with 100+ Kodos each recalculating paths.
	if (NodePool.Num() != Cols * Rows)
	{
		NodePool.SetNum(Cols * Rows);
		for (FPathNode& N : NodePool) { N.Gen = 0; }
		SearchGen = 0;
	}
	if (++SearchGen == 0) // wrapped around: make every node stale again, then use gen 1
	{
		for (FPathNode& N : NodePool) { N.Gen = 0; }
		SearchGen = 1;
	}

	const uint32 Gen = SearchGen;
	auto Touch = [this, &Cells, Gen](const int32 X, const int32 Y) -> FPathNode*
	{
		const int32 Index = KodoGrid::CellIndex(X, Y, Cols);
		FPathNode& N = NodePool[Index];
		if (N.Gen != Gen)
		{
			N.Gen = Gen;
			N.X = X;
			N.Y = Y;
			N.Type = Cells[Index].Type;
			N.G = 0.f; N.H = 0.f; N.F = 0.f;
			N.Parent = nullptr;
			N.bOpened = false; N.bClosed = false;
		}
		return &N;
	};

	FJsBinaryHeap OpenSet;
	FPathNode* StartNode = Touch(Start.X, Start.Y);
	const FIntPoint EndCell(TargetX, TargetY);

	StartNode->bOpened = true;
	OpenSet.Push(StartNode);

	// 8-direction movement (creator feedback: allow diagonals). Cardinals first, then
	// diagonals, for stable tie-breaking. Diagonal steps cost sqrt(2)x and are only
	// allowed when both orthogonally-adjacent cells are also clear, so units never cut
	// through a wall corner.
	static const FIntPoint Dirs[8] = {
		FIntPoint(0, -1), FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0),  // N, E, S, W
		FIntPoint(1, -1), FIntPoint(1, 1), FIntPoint(-1, 1), FIntPoint(-1, -1) // NE, SE, SW, NW
	};

	while (OpenSet.Size() > 0)
	{
		FPathNode* Current = OpenSet.Pop();

		// Reached destination (pathfinding.js:169-178)
		if (Current->X == TargetX && Current->Y == TargetY)
		{
			const FPathNode* Curr = Current;
			while (Curr->Parent)
			{
				FKodoPathStep Step;
				Step.Cell = FIntPoint(Curr->X, Curr->Y);
				Step.bIsBlocked = KodoGrid::IsCellBlockedForSize(Cells, Cols, Rows, Curr->X, Curr->Y, Size);
				OutSteps.Add(Step);
				Curr = Curr->Parent;
			}
			Algo::Reverse(OutSteps); // start -> end, excluding start itself
			return true;
		}

		Current->bClosed = true;

		for (const FIntPoint& Dir : Dirs)
		{
			const int32 Nx = Current->X + Dir.X;
			const int32 Ny = Current->Y + Dir.Y;

			// Grid boundary factoring unit size (pathfinding.js:197)
			if (!(Nx >= 0 && Nx <= Cols - Size && Ny >= 0 && Ny <= Rows - Size))
			{
				continue;
			}

			FPathNode* Neighbor = Touch(Nx, Ny);
			if (Neighbor->bClosed)
			{
				continue;
			}
			if (KodoGrid::IsCellBlockedForSize(Cells, Cols, Rows, Neighbor->X, Neighbor->Y, Size))
			{
				continue;
			}

			const bool bDiagonal = (Dir.X != 0 && Dir.Y != 0);
			if (bDiagonal)
			{
				// No corner-cutting: both orthogonal cells the diagonal "passes between"
				// must be clear for the footprint, else the unit would clip a wall corner.
				const bool bSideXBlocked =
					KodoGrid::IsCellBlockedForSize(Cells, Cols, Rows, Current->X + Dir.X, Current->Y, Size);
				const bool bSideYBlocked =
					KodoGrid::IsCellBlockedForSize(Cells, Cols, Rows, Current->X, Current->Y + Dir.Y, Size);
				if (bSideXBlocked || bSideYBlocked)
				{
					continue;
				}
			}

			// Movement weight: single-cell weight, or max over the footprint (pathfinding.js:206-223)
			float Weight = 1.f;
			if (Size == 1)
			{
				Weight = Neighbor->GetWeight();
			}
			else
			{
				float MaxCellWeight = 1.f;
				for (int32 Dx = 0; Dx < Size; ++Dx)
				{
					for (int32 Dy = 0; Dy < Size; ++Dy)
					{
						// Read weight straight from the grid (avoids touching footprint nodes).
						const float W = KodoGrid::GetCellTypeWeight(
							Cells[KodoGrid::CellIndex(Neighbor->X + Dx, Neighbor->Y + Dy, Cols)].Type);
						if (W > MaxCellWeight)
						{
							MaxCellWeight = W;
						}
					}
				}
				Weight = MaxCellWeight;
			}

			// Diagonal steps travel sqrt(2)x the distance, so cost sqrt(2)x as much.
			const float TentativeG = Current->G + (bDiagonal ? Weight * KodoSqrt2 : Weight);

			if (!Neighbor->bOpened)
			{
				Neighbor->Parent = Current;
				Neighbor->G = TentativeG;
				Neighbor->H = Heuristic(FIntPoint(Neighbor->X, Neighbor->Y), EndCell);
				Neighbor->F = Neighbor->G + Neighbor->H;
				Neighbor->bOpened = true;
				OpenSet.Push(Neighbor);
			}
			else if (TentativeG < Neighbor->G)
			{
				Neighbor->Parent = Current;
				Neighbor->G = TentativeG;
				Neighbor->F = Neighbor->G + Neighbor->H;
				OpenSet.RescoreElement(Neighbor);
			}
		}
	}

	return false; // No path found (JS: null)
}

FIntPoint UKodoGridPathfinder::FindClosest2x2ReachableCell(const TArray<FGridCell>& Cells, const FIntPoint Start,
                                                           const FIntPoint Target) const
{
	// BFS, FIFO via head index (JS used Array.shift) — pathfinding.js:272-317
	TArray<FIntPoint> Queue;
	int32 Head = 0;
	TSet<FIntPoint> Visited;

	Queue.Add(Start);
	Visited.Add(Start);

	FIntPoint ClosestNode = Start;
	int32 MinDistance = FMath::Abs(Start.X - Target.X) + FMath::Abs(Start.Y - Target.Y);

	// 8-direction BFS to match the diagonal-capable A* (creator feedback).
	static const FIntPoint Dirs[8] = {
		FIntPoint(0, -1), FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0),
		FIntPoint(1, -1), FIntPoint(1, 1), FIntPoint(-1, 1), FIntPoint(-1, -1)
	};

	while (Head < Queue.Num())
	{
		const FIntPoint Current = Queue[Head++];

		const int32 Dist = FMath::Abs(Current.X - Target.X) + FMath::Abs(Current.Y - Target.Y);
		if (Dist < MinDistance)
		{
			MinDistance = Dist;
			ClosestNode = Current;
		}

		for (const FIntPoint& Dir : Dirs)
		{
			const int32 Nx = Current.X + Dir.X;
			const int32 Ny = Current.Y + Dir.Y;

			if (Nx >= 0 && Nx <= Cols - 2 && Ny >= 0 && Ny <= Rows - 2)
			{
				const FIntPoint Key(Nx, Ny);
				if (!Visited.Contains(Key))
				{
					Visited.Add(Key);

					// Diagonals: forbid corner-cutting — both orthogonal 2x2 footprints
					// must be clear too (mirrors FindPath).
					if (Dir.X != 0 && Dir.Y != 0)
					{
						if (KodoGrid::IsCellBlockedForSize(Cells, Cols, Rows, Current.X + Dir.X, Current.Y, 2) ||
						    KodoGrid::IsCellBlockedForSize(Cells, Cols, Rows, Current.X, Current.Y + Dir.Y, 2))
						{
							continue;
						}
					}

					// Only enqueue if the full 2x2 footprint is unblocked (pathfinding.js:308)
					if (!KodoGrid::IsCellBlockedForSize(Cells, Cols, Rows, Nx, Ny, 2))
					{
						Queue.Add(Key);
					}
				}
			}
		}
	}

	return ClosestNode;
}

void UKodoGridPathfinder::ComputeDistanceField(const TArray<FGridCell>& Cells, const FIntPoint Target,
                                               const int32 Size, TArray<float>& OutDist, TArray<int32>& OutNext) const
{
	const int32 N = Cols * Rows;
	OutDist.Init(TNumericLimits<float>::Max(), N);
	OutNext.Init(-1, N);

	if (Cols <= 0 || Rows <= 0 || Cells.Num() != N)
	{
		return;
	}

	// Clamp the seed for multi-cell footprints, mirroring FindPath.
	int32 Tx = Target.X;
	int32 Ty = Target.Y;
	if (Size > 1)
	{
		Tx = FMath::Min(Cols - Size, Tx);
		Ty = FMath::Min(Rows - Size, Ty);
	}
	if (Tx < 0 || Ty < 0 || Tx >= Cols || Ty >= Rows)
	{
		return;
	}

	const int32 SeedIdx = KodoGrid::CellIndex(Tx, Ty, Cols);
	OutDist[SeedIdx] = 0.f;

	// Lazy-deletion binary heap of cell indices keyed on OutDist.
	TArray<int32> Heap;
	Heap.Reserve(256);
	auto Cheaper = [&OutDist](const int32 A, const int32 B) { return OutDist[A] < OutDist[B]; };
	Heap.HeapPush(SeedIdx, Cheaper);

	TArray<uint8> Done;
	Done.Init(0, N);

	static const FIntPoint Dirs[8] = {
		FIntPoint(0, -1), FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0),
		FIntPoint(1, -1), FIntPoint(1, 1), FIntPoint(-1, 1), FIntPoint(-1, -1)
	};

	while (Heap.Num() > 0)
	{
		int32 Cur = INDEX_NONE;
		Heap.HeapPop(Cur, Cheaper);
		if (Done[Cur])
		{
			continue; // stale heap entry
		}
		Done[Cur] = 1;

		const int32 Cx = Cur % Cols;
		const int32 Cy = Cur / Cols;
		const float Cd = OutDist[Cur];

		for (const FIntPoint& Dir : Dirs)
		{
			const int32 Nx = Cx + Dir.X;
			const int32 Ny = Cy + Dir.Y;
			if (!(Nx >= 0 && Nx <= Cols - Size && Ny >= 0 && Ny <= Rows - Size))
			{
				continue;
			}
			if (KodoGrid::IsCellBlockedForSize(Cells, Cols, Rows, Nx, Ny, Size))
			{
				continue;
			}

			const bool bDiagonal = (Dir.X != 0 && Dir.Y != 0);
			if (bDiagonal)
			{
				// Same no-corner-cut rule as FindPath.
				if (KodoGrid::IsCellBlockedForSize(Cells, Cols, Rows, Cx + Dir.X, Cy, Size) ||
				    KodoGrid::IsCellBlockedForSize(Cells, Cols, Rows, Cx, Cy + Dir.Y, Size))
				{
					continue;
				}
			}

			const int32 NIdx = KodoGrid::CellIndex(Nx, Ny, Cols);
			if (Done[NIdx])
			{
				continue;
			}
			const float Nd = Cd + (bDiagonal ? KodoSqrt2 : 1.f);
			if (Nd < OutDist[NIdx])
			{
				OutDist[NIdx] = Nd;
				OutNext[NIdx] = Cur; // step toward Target = move to the cell that relaxed us
				Heap.HeapPush(NIdx, Cheaper);
			}
		}
	}
}
