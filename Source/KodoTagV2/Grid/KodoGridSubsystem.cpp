// Kodo Tag: Survivor — UE Migration, Phase 1.

#include "Grid/KodoGridSubsystem.h"
#include "Grid/KodoGridPathfinder.h"
#include "Core/KodoTagUnits.h"
#include "GameFramework/PlayerController.h"

void UKodoGridSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Cols = KodoUnits::GridCols;
	Rows = KodoUnits::GridRows;
	Cells.Init(FGridCell(), Cols * Rows);

	// Raised-base terrain elevation layer (E/RAMP map data); all ground (0) until the map file loads.
	ElevationLevel.Init(0, Cols * Rows);
	RampCell.Init(0, Cols * Rows);
	RampDir.Init(3, Cols * Rows);    // ascent direction per ramp cell (0=E 1=W 2=S 3=N)
	RampBotZ.Init(0.f, Cols * Rows); // ramp slope low/high world Z, set by the bootstrapper
	RampTopZ.Init(0.f, Cols * Rows);

	// Editor-configurable spawns + colors default to the prototype ground truth; the map
	// file (KodoMapLayout.txt) overrides any of these at load (editor color/spawn config).
	KodoSpawnCell = KodoUnits::KodoSpawnCell;
	RunnerSpawnCell = KodoUnits::RunnerSpawnCell;
	MerchantCell = KodoUnits::MerchantShopCell;

	MapColors.Init(FLinearColor::White, static_cast<int32>(EKodoMapColor::Count));
	MapColors[static_cast<int32>(EKodoMapColor::Ridge)]         = FLinearColor(0.42f, 0.40f, 0.37f);
	MapColors[static_cast<int32>(EKodoMapColor::Tree)]          = FLinearColor(0.06f, 0.24f, 0.08f);
	MapColors[static_cast<int32>(EKodoMapColor::Mine)]          = FLinearColor(0.85f, 0.65f, 0.15f);
	MapColors[static_cast<int32>(EKodoMapColor::Wall)]          = FLinearColor(0.37f, 0.25f, 0.12f);
	MapColors[static_cast<int32>(EKodoMapColor::CommandCenter)] = FLinearColor(0.92f, 0.9f, 0.85f);
	MapColors[static_cast<int32>(EKodoMapColor::Hero)]          = FLinearColor(0.2f, 0.69f, 1.f);
	MapColors[static_cast<int32>(EKodoMapColor::Kodo)]          = FLinearColor(1.f, 0.23f, 0.19f);
	MapColors[static_cast<int32>(EKodoMapColor::Ground)]        = FLinearColor(0.10f, 0.17f, 0.07f);

	Pathfinder = NewObject<UKodoGridPathfinder>(this, TEXT("KodoGridPathfinder"));
	Pathfinder->Configure(Cols, Rows);
}

void UKodoGridSubsystem::Deinitialize()
{
	OnCellChanged.Clear();
	Cells.Empty();
	Pathfinder = nullptr;
	Super::Deinitialize();
}

FIntPoint UKodoGridSubsystem::WorldToCell(const FVector& WorldLocation) const
{
	return FIntPoint(
		FMath::FloorToInt32(WorldLocation.X / KodoUnits::CellSizeUU),
		FMath::FloorToInt32(WorldLocation.Y / KodoUnits::CellSizeUU));
}

FVector UKodoGridSubsystem::CellToWorldCenter(const FIntPoint& Cell) const
{
	return FVector(
		(static_cast<float>(Cell.X) + 0.5f) * KodoUnits::CellSizeUU,
		(static_cast<float>(Cell.Y) + 0.5f) * KodoUnits::CellSizeUU,
		GetTerrainHeightUU(Cell));
}

float UKodoGridSubsystem::GetTerrainHeightUU(const FIntPoint& Cell) const
{
	if (!IsInBounds(Cell))
	{
		return 0.f; // JS getTerrainHeight returns 0 out of bounds (game.js:494)
	}
	return static_cast<float>(Cells[KodoGrid::CellIndex(Cell.X, Cell.Y, Cols)].Elevation) * KodoUnits::ElevationStepUU;
}

void UKodoGridSubsystem::SetElevation(const FIntPoint& Cell, const int32 Level)
{
	if (!IsInBounds(Cell))
	{
		return;
	}
	ElevationLevel[KodoGrid::CellIndex(Cell.X, Cell.Y, Cols)] =
		static_cast<uint8>(FMath::Clamp(Level, 0, 255));
}

void UKodoGridSubsystem::SetRamp(const FIntPoint& Cell, const int32 Dir)
{
	if (!IsInBounds(Cell))
	{
		return;
	}
	const int32 Idx = KodoGrid::CellIndex(Cell.X, Cell.Y, Cols);
	RampCell[Idx] = 1;
	RampDir[Idx] = static_cast<int8>(FMath::Clamp(Dir, 0, 3)); // 0=E 1=W 2=S 3=N (ascent dir)
}

void UKodoGridSubsystem::ClearRamp(const FIntPoint& Cell)
{
	if (!IsInBounds(Cell))
	{
		return;
	}
	const int32 Idx = KodoGrid::CellIndex(Cell.X, Cell.Y, Cols);
	RampCell[Idx] = 0;
	RampDir[Idx] = static_cast<int8>(-1);
}

int32 UKodoGridSubsystem::GetElevationLevel(const FIntPoint& Cell) const
{
	if (!IsInBounds(Cell))
	{
		return 0;
	}
	return static_cast<int32>(ElevationLevel[KodoGrid::CellIndex(Cell.X, Cell.Y, Cols)]);
}

bool UKodoGridSubsystem::IsRamp(const FIntPoint& Cell) const
{
	if (!IsInBounds(Cell))
	{
		return false;
	}
	return RampCell[KodoGrid::CellIndex(Cell.X, Cell.Y, Cols)] != 0;
}

void UKodoGridSubsystem::SetRampSlope(const FIntPoint& Cell, const float BotZ, const float TopZ)
{
	if (!IsInBounds(Cell))
	{
		return;
	}
	const int32 Idx = KodoGrid::CellIndex(Cell.X, Cell.Y, Cols);
	RampBotZ[Idx] = BotZ;
	RampTopZ[Idx] = TopZ;
}

int32 UKodoGridSubsystem::GetRampDir(const FIntPoint& Cell) const
{
	if (!IsInBounds(Cell))
	{
		return -1;
	}
	const int32 Idx = KodoGrid::CellIndex(Cell.X, Cell.Y, Cols);
	return RampCell[Idx] != 0 ? static_cast<int32>(RampDir[Idx]) : -1;
}

float UKodoGridSubsystem::GetElevationZ(const FIntPoint& Cell) const
{
	// Ramp cells: return the slope's centre height. Flat cells: level * step.
	if (IsInBounds(Cell))
	{
		const int32 Idx = KodoGrid::CellIndex(Cell.X, Cell.Y, Cols);
		if (RampCell[Idx] != 0)
		{
			return 0.5f * (RampBotZ[Idx] + RampTopZ[Idx]);
		}
	}
	return GetElevationLevel(Cell) * KodoUnits::ElevationLevelStepUU;
}

float UKodoGridSubsystem::GetElevationZAtWorld(const FVector& World) const
{
	// Continuous height: on a ramp, interpolate along the ascent axis across the cell so units
	// and buildings ride the slope smoothly instead of snapping to a flat per-cell height.
	const FIntPoint Cell = WorldToCell(World);
	if (IsInBounds(Cell))
	{
		const int32 Idx = KodoGrid::CellIndex(Cell.X, Cell.Y, Cols);
		if (RampCell[Idx] != 0)
		{
			const float Lx = World.X / KodoUnits::CellSizeUU - Cell.X; // 0..1 within the cell
			const float Ly = World.Y / KodoUnits::CellSizeUU - Cell.Y;
			float Frac = 0.5f; // fraction toward the high (ascent) side
			switch (RampDir[Idx])
			{
			case 0:  Frac = Lx;        break; // ascend +X (E)
			case 1:  Frac = 1.f - Lx;  break; // ascend -X (W)
			case 2:  Frac = Ly;        break; // ascend +Y (S)
			default: Frac = 1.f - Ly;  break; // ascend -Y (N)
			}
			return FMath::Lerp(RampBotZ[Idx], RampTopZ[Idx], FMath::Clamp(Frac, 0.f, 1.f));
		}
	}
	return GetElevationZ(Cell);
}

bool UKodoGridSubsystem::IsInBounds(const FIntPoint& Cell) const
{
	return Cell.X >= 0 && Cell.X < Cols && Cell.Y >= 0 && Cell.Y < Rows;
}

const FGridCell& UKodoGridSubsystem::GetCell(const FIntPoint& Cell) const
{
	static const FGridCell DefaultCell;
	if (!IsInBounds(Cell))
	{
		return DefaultCell;
	}
	return Cells[KodoGrid::CellIndex(Cell.X, Cell.Y, Cols)];
}

FGridCell UKodoGridSubsystem::GetCellState(const FIntPoint& Cell) const
{
	return GetCell(Cell);
}

bool UKodoGridSubsystem::IsCellBlockedForSize(const FIntPoint& TopLeft, const int32 Size) const
{
	return KodoGrid::IsCellBlockedForSize(Cells, Cols, Rows, TopLeft.X, TopLeft.Y, Size);
}

void UKodoGridSubsystem::SetCell(const FIntPoint& Cell, const FGridCell& NewState)
{
	if (!IsInBounds(Cell))
	{
		return;
	}
	FGridCell& Stored = Cells[KodoGrid::CellIndex(Cell.X, Cell.Y, Cols)];
	Stored = NewState;
	bFlowDirty = true; // walls/towers changed → flow fields must recompute
	OnCellChanged.Broadcast(Cell, Stored);
}

bool UKodoGridSubsystem::DamageCell(const FIntPoint& Cell, const float Damage)
{
	if (!IsInBounds(Cell))
	{
		return false;
	}
	FGridCell State = GetCell(Cell);
	if (State.Type == ECellType::Empty)
	{
		return false;
	}

	State.Hp = FMath::Max(0.f, State.Hp - Damage);
	if (State.Hp <= 0.f)
	{
		// A breached cell takes the whole building down with it, so a chewed 2x2
		// wall/tower clears as a unit instead of leaving a partial L-shape.
		ClearStructureFootprint(GetMasterCell(Cell)); // destroyed -> back to empty grass
		return true;
	}
	SetCell(Cell, State);
	return false;
}

FIntPoint UKodoGridSubsystem::GetMasterCell(const FIntPoint& Cell) const
{
	if (!IsInBounds(Cell))
	{
		return Cell;
	}
	const FGridCell& C = GetCell(Cell);
	if (C.MasterCell.X >= 0 && C.MasterCell.Y >= 0 && IsInBounds(C.MasterCell))
	{
		return C.MasterCell;
	}
	return Cell;
}

void UKodoGridSubsystem::ClearStructureFootprint(const FIntPoint& Master)
{
	// Multi-cell structures are 2x2 with the master at the top-left; clear the
	// master plus any sibling that points back at it. 1x1 clears only itself.
	for (int32 Dx = 0; Dx <= 1; ++Dx)
	{
		for (int32 Dy = 0; Dy <= 1; ++Dy)
		{
			const FIntPoint T(Master.X + Dx, Master.Y + Dy);
			if (!IsInBounds(T))
			{
				continue;
			}
			const FGridCell& C = GetCell(T);
			if ((T == Master || C.MasterCell == Master) && C.Type != ECellType::Empty)
			{
				SetCell(T, FGridCell());
			}
		}
	}
}

void UKodoGridSubsystem::ClearStructure(const FIntPoint& Cell)
{
	ClearStructureFootprint(GetMasterCell(Cell));
}

bool UKodoGridSubsystem::FindPath(const FIntPoint& Start, const FIntPoint& End, const int32 Size,
                                  TArray<FKodoPathStep>& OutSteps) const
{
	OutSteps.Reset();
	return Pathfinder ? Pathfinder->FindPath(Cells, Start, End, Size, OutSteps) : false;
}

FIntPoint UKodoGridSubsystem::FindClosest2x2ReachableCell(const FIntPoint& Start, const FIntPoint& Target) const
{
	return Pathfinder ? Pathfinder->FindClosest2x2ReachableCell(Cells, Start, Target) : Start;
}

void UKodoGridSubsystem::EnsureFlowField(const FIntPoint& Target)
{
	if (!Pathfinder)
	{
		return;
	}
	// At most one recompute per frame (the first caller does it; the rest reuse it).
	if (GFrameCounter == FlowComputeFrame)
	{
		return;
	}
	// Nothing changed since the last compute → keep the cached fields.
	if (Target == FlowTarget && !bFlowDirty && Field1Dist.Num() == Cols * Rows)
	{
		return;
	}
	// Throttle: the full-grid Dijkstra (x2) is the heaviest tick cost on the big 300x300 map, and
	// it would otherwise fire every frame while the runner moves or Kodos chew walls. Cap it to a
	// min interval; Kodos repath every ~0.35-0.6 s, so a field up to ~0.2 s stale is unnoticeable.
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Field1Dist.Num() == Cols * Rows && (Now - LastFlowComputeTime) < 0.2f)
	{
		return; // reuse the (slightly stale) cached fields until the interval elapses
	}
	Pathfinder->ComputeDistanceField(Cells, Target, 1, Field1Dist, Field1Next);
	Pathfinder->ComputeDistanceField(Cells, Target, 2, Field2Dist, Field2Next);
	FlowTarget = Target;
	bFlowDirty = false;
	FlowComputeFrame = GFrameCounter;
	LastFlowComputeTime = Now;
}

bool UKodoGridSubsystem::IsFieldReachable(const int32 Size, const FIntPoint& Cell) const
{
	if (!IsInBounds(Cell))
	{
		return false;
	}
	const TArray<float>& Dist = (Size >= 2) ? Field2Dist : Field1Dist;
	if (Dist.Num() != Cols * Rows)
	{
		return false;
	}
	return Dist[KodoGrid::CellIndex(Cell.X, Cell.Y, Cols)] < TNumericLimits<float>::Max();
}

bool UKodoGridSubsystem::BuildFieldPath(const int32 Size, const FIntPoint& Start, TArray<FKodoPathStep>& OutSteps) const
{
	OutSteps.Reset();
	if (!IsInBounds(Start))
	{
		return false;
	}
	const TArray<float>& Dist = (Size >= 2) ? Field2Dist : Field1Dist;
	const TArray<int32>& Next = (Size >= 2) ? Field2Next : Field1Next;
	if (Dist.Num() != Cols * Rows || Next.Num() != Cols * Rows)
	{
		return false;
	}

	int32 Cur = KodoGrid::CellIndex(Start.X, Start.Y, Cols);
	if (Dist[Cur] >= TNumericLimits<float>::Max())
	{
		return false; // unreachable for this footprint
	}

	int32 Guard = 0;
	const int32 MaxSteps = Cols * Rows;
	while (Next[Cur] != -1 && Guard++ < MaxSteps)
	{
		const int32 NextIdx = Next[Cur];
		FKodoPathStep Step;
		Step.Cell = FIntPoint(NextIdx % Cols, NextIdx / Cols);
		Step.bIsBlocked = false; // field only covers unblocked cells
		OutSteps.Add(Step);
		Cur = NextIdx;
	}
	return OutSteps.Num() > 0;
}

bool UKodoGridSubsystem::TryConsumePathBudget(const int32 MaxPerFrame)
{
	// Reset the counter whenever the engine advances to a new frame.
	if (GFrameCounter != PathBudgetFrame)
	{
		PathBudgetFrame = GFrameCounter;
		PathBudgetUsed = 0;
	}
	if (PathBudgetUsed >= MaxPerFrame)
	{
		return false;
	}
	++PathBudgetUsed;
	return true;
}

bool UKodoGridSubsystem::FindNearestCommandCenter(const FIntPoint& From, FIntPoint& OutCell) const
{
	static const FName CommandCenterId(TEXT("command_center"));
	bool bFound = false;
	float BestDist = TNumericLimits<float>::Max();

	for (int32 X = 0; X < Cols; ++X)
	{
		for (int32 Y = 0; Y < Rows; ++Y)
		{
			const FGridCell& Cell = Cells[KodoGrid::CellIndex(X, Y, Cols)];
			if (Cell.Type == ECellType::Tower && Cell.StructureId == CommandCenterId &&
			    Cell.MasterCell == FIntPoint(X, Y)) // master/center cell only
			{
				const float Dist = FVector2D(X - From.X, Y - From.Y).Size();
				if (Dist < BestDist)
				{
					BestDist = Dist;
					OutCell = FIntPoint(X, Y);
					bFound = true;
				}
			}
		}
	}
	return bFound;
}

bool UKodoGridSubsystem::FindNearestStructureCell(const FIntPoint& From, FIntPoint& OutCell) const
{
	// Bunker mode targeting: scan for the nearest player-built Wall/Tower cell (the command
	// center is a Tower cell, so it's included). Skip the indestructible admin_tower so the
	// siege never piles onto it. Octile distance keeps the nearest pick diagonal-aware.
	static const FName AdminTowerId(TEXT("admin_tower"));
	bool bFound = false;
	float BestDist = TNumericLimits<float>::Max();

	for (int32 X = 0; X < Cols; ++X)
	{
		for (int32 Y = 0; Y < Rows; ++Y)
		{
			const FGridCell& Cell = Cells[KodoGrid::CellIndex(X, Y, Cols)];
			if ((Cell.Type == ECellType::Wall || Cell.Type == ECellType::Tower) &&
			    Cell.StructureId != AdminTowerId)
			{
				const int32 Dx = FMath::Abs(X - From.X);
				const int32 Dy = FMath::Abs(Y - From.Y);
				const float Dist = static_cast<float>(FMath::Max(Dx, Dy)) + 0.41f * FMath::Min(Dx, Dy);
				if (Dist < BestDist)
				{
					BestDist = Dist;
					OutCell = FIntPoint(X, Y);
					bFound = true;
				}
			}
		}
	}
	return bFound;
}

bool UKodoGridSubsystem::IsNearCommandCenter(const FIntPoint& From, const int32 Radius) const
{
	static const FName CommandCenterId(TEXT("command_center"));
	for (int32 Dx = -Radius; Dx <= Radius; ++Dx)
	{
		for (int32 Dy = -Radius; Dy <= Radius; ++Dy)
		{
			const FIntPoint Test(From.X + Dx, From.Y + Dy);
			if (IsInBounds(Test))
			{
				const FGridCell& Cell = GetCell(Test);
				if (Cell.Type == ECellType::Tower && Cell.StructureId == CommandCenterId)
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool UKodoGridSubsystem::IsNearStructure(const FIntPoint& From, const FName StructureId, const int32 Radius) const
{
	for (int32 Dx = -Radius; Dx <= Radius; ++Dx)
	{
		for (int32 Dy = -Radius; Dy <= Radius; ++Dy)
		{
			const FIntPoint Test(From.X + Dx, From.Y + Dy);
			if (IsInBounds(Test) && GetCell(Test).StructureId == StructureId)
			{
				return true;
			}
		}
	}
	return false;
}

bool UKodoGridSubsystem::DeprojectCursorToCell(APlayerController* PlayerController, FIntPoint& OutCell) const
{
	OutCell = FIntPoint::NoneValue;
	if (!PlayerController)
	{
		return false;
	}

	FVector Origin, Direction;
	if (!PlayerController->DeprojectMousePositionToWorld(Origin, Direction))
	{
		return false;
	}

	if (FMath::IsNearlyZero(Direction.Z))
	{
		return false;
	}

	// Elevation-aware cursor trace: raised bases and ramps sit above Z = 0, so a single Z = 0
	// intersection lands on the wrong cell (offset by the camera tilt). Intersect the ray with
	// the terrain-height plane iteratively — converges in a couple of steps for a top-down camera.
	double PlaneZ = 0.0;
	FVector Hit = Origin;
	for (int32 Iter = 0; Iter < 4; ++Iter)
	{
		const double T = (PlaneZ - Origin.Z) / Direction.Z;
		if (T < 0.0)
		{
			return false; // looking away from the ground
		}
		Hit = Origin + Direction * T;
		const FIntPoint Cell = WorldToCell(Hit);
		const double GroundZ = IsInBounds(Cell) ? static_cast<double>(GetElevationZAtWorld(Hit)) : 0.0;
		if (FMath::Abs(GroundZ - PlaneZ) < 1.0)
		{
			break; // converged onto the visible ground height
		}
		PlaneZ = GroundZ;
	}

	OutCell = WorldToCell(Hit);
	return IsInBounds(OutCell);
}
