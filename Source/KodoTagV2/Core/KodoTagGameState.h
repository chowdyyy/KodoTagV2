// Kodo Tag: Survivor — UE Migration, Phase 3.
// Global player economy + CC research state (game.js:738-771).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "KodoTagGameState.generated.h"

/** Game modes (start overlay selection). Maze = stock survival; God = buffed power fantasy;
 *  Bunker = kodos siege your BUILDINGS (not the runner) and your towers hit twice as hard. */
UENUM(BlueprintType)
enum class EKodoGameMode : uint8 { Maze, God, Bunker };

/** CC research flags/levels (game.js:764-771). */
USTRUCT(BlueprintType)
struct FKodoUpgrades
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Kodo") bool bStunUnlocked = false;
	UPROPERTY(BlueprintReadOnly, Category = "Kodo") bool bAoeUnlocked = false;
	UPROPERTY(BlueprintReadOnly, Category = "Kodo") bool bMultishotUnlocked = false;
	UPROPERTY(BlueprintReadOnly, Category = "Kodo") bool bAuraUnlocked = false;
	UPROPERTY(BlueprintReadOnly, Category = "Kodo") int32 MasonryLvl = 1; // max 3
	UPROPERTY(BlueprintReadOnly, Category = "Kodo") int32 AxeLvl = 1;     // max 3
	UPROPERTY(BlueprintReadOnly, Category = "Kodo") int32 GoldBonusLvl = 0; // max 3 — bonus-gold mining tech

	bool IsUnlocked(const FName Flag) const
	{
		if (Flag.IsNone()) { return true; }
		if (Flag == FName("stunUnlocked")) { return bStunUnlocked; }
		if (Flag == FName("aoeUnlocked")) { return bAoeUnlocked; }
		if (Flag == FName("multishotUnlocked")) { return bMultishotUnlocked; }
		if (Flag == FName("auraUnlocked")) { return bAuraUnlocked; }
		return false;
	}
};

UCLASS()
class KODOTAGV2_API AKodoTagGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	// Standard mode start: 1000 gold, 250 wood (game.js:738-739).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float Gold = 1000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float Wood = 250.f;
	UPROPERTY(BlueprintReadOnly, Category = "Kodo") int32 Score = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kodo") FKodoUpgrades Upgrades;

	// --- Food / supply upkeep ---
	/** Sum of FKodoStructurePreset::Food over all built structures (recomputed on each economy tick). */
	UPROPERTY(BlueprintReadOnly, Category = "Kodo") int32 SupplyUsed = 0;
	/** Supply ceiling; over-building past this throttles harvested-gold income. */
	UPROPERTY(BlueprintReadOnly, Category = "Kodo") int32 SupplyCap = 60;

	/** 1.0 while at/under the supply cap, 0.6 once over it (income penalty for over-building). */
	float UpkeepMult() const
	{
		return SupplyUsed <= SupplyCap ? 1.0f : 0.6f;
	}

	// --- Game mode + match state (start overlay drives these) ---

	/** Selected game mode. Set by ConfigureMode() at match start. */
	UPROPERTY(BlueprintReadOnly, Category = "Kodo") EKodoGameMode GameMode = EKodoGameMode::Maze;

	/** False until the start overlay begins the match; the wave controller gates spawns on this. */
	UPROPERTY(BlueprintReadOnly, Category = "Kodo") bool bMatchStarted = false;

	/** God-mode tower damage multiplier (Maze 1.0, God 2.0). */
	UPROPERTY(BlueprintReadOnly, Category = "Kodo") float TowerDamageMult = 1.f;

	/** God-mode kodo spawn-count multiplier (Maze 1.0, God 1.5). */
	UPROPERTY(BlueprintReadOnly, Category = "Kodo") float KodoSpawnMult = 1.f;

	/** Apply per-mode tuning. Maze: all mults 1. God: TowerDamageMult=2, KodoSpawnMult=1.5.
	 *  Bunker: TowerDamageMult=2 (towers +100% dmg), KodoSpawnMult=1 (kodos siege buildings). */
	void ConfigureMode(const EKodoGameMode Mode)
	{
		GameMode = Mode;
		if (Mode == EKodoGameMode::God)
		{
			TowerDamageMult = 2.f;
			KodoSpawnMult = 1.5f;
		}
		else if (Mode == EKodoGameMode::Bunker)
		{
			TowerDamageMult = 2.f;
			KodoSpawnMult = 1.f;
		}
		else
		{
			TowerDamageMult = 1.f;
			KodoSpawnMult = 1.f;
		}
	}

	bool CanAfford(const float GoldCost, const float WoodCost) const
	{
		return Gold >= GoldCost && Wood >= WoodCost;
	}

	void Spend(const float GoldCost, const float WoodCost)
	{
		Gold -= GoldCost;
		Wood -= WoodCost;
	}
};
