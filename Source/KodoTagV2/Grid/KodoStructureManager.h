// Kodo Tag: Survivor — UE Migration, Phase 3.
// Owns player-built structures: placement validation (canBuildAt, game.js:1884),
// placement (placeStructure, game.js:1903), construction timers (game.js:1217),
// passive income (game.js:1232-1270), CC research (game.js:2520-2578), and
// blockout visuals driven by the grid's OnCellChanged event.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Grid/KodoGridTypes.h"
#include "KodoStructureManager.generated.h"

class UKodoGridSubsystem;
class AKodoTagGameState;
class AKodoWaveController;
class UStaticMeshComponent;
class UStaticMesh;

UENUM(BlueprintType)
enum class EKodoResearch : uint8
{
	Stun,      // 150g  80w (game.js:2528)
	Aoe,       // 200g 100w
	Multishot, // 220g 120w
	Aura,      // 250g 150w
	Masonry,   // 120g  60w, max 3, retroactive x1.35 HP
	Axe,       //  80g  30w, max 3
	GoldBonus, // 100g  40w, max 3, bonus-gold mining chance
	HeroSkill2,// 120g  60w, unlocks the hero's slot-2 passive (offensive gate: Upgrade Center)
	HeroSkill3,// 200g 100w, unlocks the hero's slot-3 active spell (offensive gate: Upgrade Center)
	ManaRegen  // 200g   0w, one-time: doubles the hero's mana regen (offensive gate: Upgrade Center)
};

UCLASS()
class KODOTAGV2_API AKodoStructureManager : public AActor
{
	GENERATED_BODY()

public:
	AKodoStructureManager();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Port of canBuildAt (game.js:1884-1901). PresetId affects goldmine rules. */
	UFUNCTION(BlueprintPure, Category = "KodoBuild")
	bool CanBuildAt(const FIntPoint& Cell, FName PresetId) const;

	/**
	 * Top-left cell a 2x2 building will actually occupy for a given cursor cell:
	 * snaps to the underlying gold mine when over one (keeps mine shafts buildable on
	 * off-lattice mines), otherwise the exact cursor cell (1-cell granularity, no lattice
	 * snap — 2x2 buildings can sit tight against trees/walls). 1x1 = cursor cell.
	 * Shared by PlaceStructure and the build ghost so the preview never lies.
	 */
	UFUNCTION(BlueprintPure, Category = "KodoBuild")
	FIntPoint ComputeBuildOrigin(const FIntPoint& Cell, FName PresetId) const;

	/** Port of placeStructure (game.js:1903-2007). @return true if placed. */
	UFUNCTION(BlueprintCallable, Category = "KodoBuild")
	bool PlaceStructure(FName PresetId, const FIntPoint& Cell);

	/** Port of researchCCUpgrade (game.js:2520-2578). @return true on success. */
	UFUNCTION(BlueprintCallable, Category = "KodoBuild")
	bool Research(EKodoResearch Type);

	/**
	 * Live progress of the first in-progress research (for the HUD loading bar).
	 * @return false if nothing is researching; otherwise fills the display name,
	 *         elapsed fraction (0..1), and seconds remaining for ActiveResearches[0].
	 */
	bool GetActiveResearch(FString& OutName, float& OutFrac, float& OutRemaining) const;

	/**
	 * Per-type research progress for the command-card loading bars. Searches ActiveResearches
	 * for Type; if found, sets OutRemaining and returns the elapsed fraction in 0..1. If that
	 * type is not currently researching, returns -1.f (and leaves OutRemaining untouched).
	 */
	UFUNCTION(BlueprintPure, Category = "KodoBuild")
	float GetResearchProgress(EKodoResearch Type, float& OutRemaining) const;

	/** Port of upgradeBasicTowerTo (game.js:2009-2129) with the morph cost table (game.js:2424-2433). */
	UFUNCTION(BlueprintCallable, Category = "KodoBuild")
	bool MorphBasicTower(const FIntPoint& Cell, FName TargetId);

	/** Port of upgradeStructure (game.js:2131-2166): next tier, instant, HP ratio preserved, walls free. */
	UFUNCTION(BlueprintCallable, Category = "KodoBuild")
	bool UpgradeStructureTier(const FIntPoint& Cell);

	/** Port of sellStructure (game.js:2168-2183): refund 60% of cumulative gold cost. 1x1 structures only. */
	UFUNCTION(BlueprintCallable, Category = "KodoBuild")
	bool SellStructure(const FIntPoint& Cell);

protected:
	void OnCellChanged(const FIntPoint& Cell, const FGridCell& NewState);
	void UpdateStructureVisual(const FIntPoint& Cell, const FGridCell& State, float ConstructionAlpha);
	void RemoveStructureVisual(const FIntPoint& Cell);
	void RecalcAllKodoPaths();
	void ShowMessage(const FString& Text, const FColor& Color) const;

	AKodoTagGameState* GetKodoGameState() const;

	/** Under-construction bookkeeping (progress lives here, not in the grid, to avoid per-frame cell broadcasts). */
	struct FConstructionEntry
	{
		FIntPoint Cell;
		float Elapsed = 0.f;
		float Duration = 4.f;
	};
	TArray<FConstructionEntry> Constructions;

	/** In-progress timed research: each takes a few seconds, then its effect is applied. */
	struct FActiveResearch
	{
		EKodoResearch Type = EKodoResearch::Stun;
		float TimeRemaining = 0.f;
		float TotalTime = 0.f;
	};
	TArray<FActiveResearch> ActiveResearches;

	/** Apply a finished research's effect (set flag / bump level / unlock hero skill). */
	void ApplyResearchEffect(EKodoResearch Type);
	/** True if the research is already queued/in progress. */
	bool IsResearching(EKodoResearch Type) const;

	float EconomyTick = 0.f;
	float DkRegenTimer = 0.f;

	// --- Tower combat (Phase 4, game.js:1272-1312) ---
	void TickTowerCombat(float DeltaSeconds);
	void FireProjectile(const FVector& From, class AKodoCharacter* Target, const struct FKodoStructureStats& Stats);

	/** Per-tower cooldowns, kept locally so firing doesn't spam grid broadcasts. */
	TMap<FIntPoint, float> TowerCooldowns;

	/**
	 * Master cells of finished, shooting towers (excludes walls + CC/mill/shaft).
	 * Maintained incrementally from OnCellChanged so TickTowerCombat iterates only
	 * live weapons instead of rescanning all 160x160 cells every frame.
	 */
	TSet<FIntPoint> ActiveTowerCells;

	// --- Phase 5 visual hookups ---

	/** Rotating "head" barrels for shooting towers; aimed at the last target, recoils on fire. */
	TMap<FIntPoint, TObjectPtr<UStaticMeshComponent>> TowerHeads;

	struct FTowerFireFx
	{
		float RecoilTimer = 0.f;
		FVector FireDirection = FVector::ForwardVector;
	};
	TMap<FIntPoint, FTowerFireFx> TowerFx;

	void TickTowerFx(float DeltaSeconds);
	/** Non-static: wall + command_center tints read the grid's editor color table (editor color config). */
	FLinearColor TintForStructure(FName StructureId) const;

	/** World center of a structure: the master cell, offset to the footprint middle for 2x2 buildings. */
	FVector BuildingCenter(const FIntPoint& MasterCell, FName StructureId) const;

	TMap<FIntPoint, TObjectPtr<UStaticMeshComponent>> CellVisuals;

	UPROPERTY() TObjectPtr<UKodoGridSubsystem> Grid;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;

	TWeakObjectPtr<AKodoWaveController> CachedWaveController;
	FDelegateHandle CellChangedHandle;
};
