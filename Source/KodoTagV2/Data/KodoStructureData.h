// Kodo Tag: Survivor — UE Migration, Phase 3.
// Static structure/tower registry — exact port of TOWER_PRESETS and
// getTowerStatsForLevel (js/config/towers.js). Kept as code (not a .uasset)
// so the project stays code-only buildable; the structs are DataTable-ready
// (FTableRowBase) if balance editing in-editor is wanted later.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "KodoStructureData.generated.h"

/** Merged stats for a structure at a given level (towers.js getTowerStatsForLevel). */
USTRUCT(BlueprintType)
struct FKodoStructureStats : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") FString DisplayName;
	/** Gold cost: base placement cost at L1, upgrade cost at L2/L3. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") int32 GoldCost = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") int32 WoodCost = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float MaxHp = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float RangeTiles = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float CooldownSeconds = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float Damage = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float SlowPercent = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float StunChance = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float StunDurationSeconds = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float SplashRadiusTiles = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") int32 MaxTargets = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") int32 Level = 1;
};

/** One preset (towers.js TOWER_PRESETS entry). */
USTRUCT(BlueprintType)
struct FKodoStructurePreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") int32 GoldCost = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") int32 WoodCost = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float MaxHp = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float RangeTiles = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float CooldownSeconds = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float Damage = 0.f;
	/** 'frost','stun','aoe','multishot','aura','mine_shaft','lumber_mill','command_center' or None. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") FName Special;
	/** CC research flag gating this preset ('stunUnlocked', ...), None if always available. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") FName RequiresUpgrade;
	/** CC, lumber_mill, mine_shaft occupy 2x2 (game.js:1926). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") bool bIs2x2 = false;
	/** Walls 2.5 s, everything else 4.0 s (game.js:1971/1996). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float ConstructionSeconds = 4.f;

	/** Food/supply upkeep this structure consumes once built (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") float Food = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kodo") TArray<FKodoStructureStats> Levels;
};

namespace KodoStructures
{
	/** All presets keyed by id ('wall','basic_tower','arrow',...,'command_center'). */
	KODOTAGV2_API const TMap<FName, FKodoStructurePreset>& Registry();

	KODOTAGV2_API const FKodoStructurePreset* Find(FName Id);

	/**
	 * Merged base+level stats — port of getTowerStatsForLevel (towers.js:202-242):
	 * level fields override base where non-zero; level clamps to highest defined.
	 */
	KODOTAGV2_API FKodoStructureStats GetStatsForLevel(FName Id, int32 Level);
}
