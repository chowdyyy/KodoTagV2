// Kodo Tag: Survivor — UE Migration, Phase 1.
// Grid cell data model. Mirrors the JS cell object (game.js:509-519) exactly.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "KodoGridTypes.generated.h"

class AActor;

/**
 * Cell occupancy type.
 * JS string equivalents: 'empty','wall','tower','tree','goldmine','cliff','merchant_shop'.
 */
UENUM(BlueprintType)
enum class ECellType : uint8
{
	Empty        UMETA(DisplayName = "Empty"),
	Wall         UMETA(DisplayName = "Wall"),
	Tower        UMETA(DisplayName = "Tower"),
	Tree         UMETA(DisplayName = "Tree"),
	Goldmine     UMETA(DisplayName = "Goldmine"),
	Cliff        UMETA(DisplayName = "Cliff"),
	MerchantShop UMETA(DisplayName = "Merchant Shop")
};

/** One cell of the 160x160 grid (game.js:509-519). */
USTRUCT(BlueprintType)
struct FGridCell
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoGrid")
	ECellType Type = ECellType::Empty;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoGrid")
	float Hp = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoGrid")
	float MaxHp = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoGrid")
	int32 Level = 0;

	/** JS: cell.id — 'cliff','tree','goldmine','command_center', preset ids, ... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoGrid")
	FName StructureId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoGrid")
	float CooldownTimer = 0.f;

	/** Terrain paint id; prototype default 2 = "Lush Lordaeron Grass" (game.js:518). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoGrid")
	uint8 TextureId = 2;

	/** elevationGrid mirror (game.js:502, 520). World height = Elevation * KodoUnits::ElevationStepUU. (int32: int8 is not Blueprint-supported.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoGrid")
	int32 Elevation = 0;

	/** JS masterX/masterY for multi-cell structures (2x2 goldmine/shop/CC). (-1,-1) = not part of one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoGrid")
	FIntPoint MasterCell = FIntPoint(-1, -1);

	/** Under-construction structures take 2.0x chew damage (entities.js:1030-1032) and don't shoot/produce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoGrid")
	uint8 bUnderConstruction : 1;

	/** Cell was a goldmine before a tower was built on it (game.js:1984, basic_tower/mine_shaft rules). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoGrid")
	uint8 bWasGoldMine : 1;

	/** Visual/logic actor occupying this cell (tower, wall, tree, ...). */
	UPROPERTY()
	TWeakObjectPtr<AActor> OccupyingActor;

	FGridCell()
		: bUnderConstruction(false)
		, bWasGoldMine(false)
	{
	}
};

/** One step of a computed path. JS: {x, y, isBlocked} (pathfinding.js:174). */
USTRUCT(BlueprintType)
struct FKodoPathStep
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KodoGrid")
	FIntPoint Cell = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "KodoGrid")
	bool bIsBlocked = false;
};

/**
 * Shared grid helpers used by both UKodoGridSubsystem and UKodoGridPathfinder.
 * Kept as free inline functions so the pathfinder has no dependency on the subsystem.
 */
namespace KodoGrid
{
	/** Flat index into the cell array: row-major, index = Y * Cols + X. */
	FORCEINLINE int32 CellIndex(const int32 X, const int32 Y, const int32 Cols)
	{
		return Y * Cols + X;
	}

	/**
	 * Snap a cell to the coarse 2x2 "build tile" lattice (top-left of its 4-block tile).
	 * Every 2x2 building lives on this lattice so placement reads as whole tiles, not
	 * arbitrary half-tile offsets (creator feedback: "tiles of 4 blocks, buildings 2x2").
	 */
	FORCEINLINE FIntPoint SnapToTile(const FIntPoint& Cell)
	{
		return FIntPoint(Cell.X & ~1, Cell.Y & ~1);
	}

	/**
	 * A* traversal weights — literal port of GridNode.getWeight() (pathfinding.js:109-116).
	 * NOTE: in the prototype these weights are effectively latent (blocked cells are skipped
	 * before weighting, pathfinding.js:204); ported verbatim for behavioral parity.
	 */
	FORCEINLINE float GetCellTypeWeight(const ECellType Type)
	{
		switch (Type)
		{
		case ECellType::Cliff:    return 1000000.f; // unbreachable barrier
		case ECellType::Wall:     return 1500.f;
		case ECellType::Tower:    return 3000.f;
		case ECellType::Tree:     return 6000.f;
		case ECellType::Goldmine: return 20000.f;
		default:                  return 1.f;       // empty / walkable
		}
	}

	/**
	 * Blocking types — port of isCellBlockedForSize's type list (pathfinding.js:256).
	 * NOTE: the JS checks the string 'shop' but the game writes 'merchant_shop', so the
	 * tent was accidentally walkable in the prototype. Resolved per approved TDD §4.2:
	 * MerchantShop IS blocking (matches the build-exclusion intent, game.js:1891).
	 */
	FORCEINLINE bool IsBlockingType(const ECellType Type)
	{
		return Type == ECellType::Wall
			|| Type == ECellType::Tower
			|| Type == ECellType::Tree
			|| Type == ECellType::Goldmine
			|| Type == ECellType::MerchantShop
			|| Type == ECellType::Cliff;
	}

	/** Port of Pathfinder.isCellBlockedForSize (pathfinding.js:249-262): true if any cell of the Size x Size footprint with top-left (X,Y) is blocked or out of bounds. */
	inline bool IsCellBlockedForSize(const TArray<FGridCell>& Cells, const int32 Cols, const int32 Rows,
	                                 const int32 X, const int32 Y, const int32 Size)
	{
		for (int32 Dx = 0; Dx < Size; ++Dx)
		{
			for (int32 Dy = 0; Dy < Size; ++Dy)
			{
				const int32 Tx = X + Dx;
				const int32 Ty = Y + Dy;
				if (Tx < 0 || Ty < 0 || Tx >= Cols || Ty >= Rows)
				{
					return true;
				}
				if (IsBlockingType(Cells[CellIndex(Tx, Ty, Cols)].Type))
				{
					return true;
				}
			}
		}
		return false;
	}
}
