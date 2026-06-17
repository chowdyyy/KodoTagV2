// Kodo Tag: Survivor — UE Migration, Phase 2.
// Continuous Kodo spawner + global enrage timer. ADOPTS THE ORIGINAL WC3 MODEL
// (kodo_balance_data.md §4): there is NO discrete prep->wave->prep loop. After an
// initial build-grace the spawner runs forever, emitting round(BaseSpawnCount*Rr)
// kodos every SpawnCadenceSeconds, with a difficulty "tier" rising over time that
// selects progressively tougher kodo types. The global blocked->enrage logic
// (game.js:1158-1170) is unchanged.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Actors/KodoCharacter.h"
#include "KodoWaveController.generated.h"

class UKodoGridSubsystem;

/** WC3 difficulty selector (kodo_balance_data.md §4): drives Rr + spawn cadence. */
UENUM(BlueprintType)
enum class EKodoDifficulty : uint8
{
	Easy,    // Rr 0.70, cadence 45s
	Normal,  // Rr 1.00, cadence 45s
	Hard,    // Rr 1.50, cadence 20s
	Insane   // Rr 2.45, cadence 10s
};

UCLASS()
class KODOTAGV2_API AKodoWaveController : public AActor
{
	GENERATED_BODY()

public:
	AKodoWaveController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Global enrage: ANY Kodo base-blocked for >= 3 s enrages ALL Kodos (game.js:1158-1170). */
	bool IsEnrageActive() const { return BlockedTimer >= EnrageThresholdSeconds; }

	// --- HUD compatibility (UI/KodoHudWidget.cpp) -------------------------------
	// The HUD still calls the old wave getters; we keep the signatures and repurpose
	// the meaning for the continuous model (see GetMeaning comments).

	/** Repurposed: the current escalation TIER (was wave number). */
	UFUNCTION(BlueprintPure, Category = "KodoWaves")
	int32 GetCurrentWave() const { return DifficultyTier; }

	/** Repurposed: the tier cap (was the 10-wave cap). */
	UFUNCTION(BlueprintPure, Category = "KodoWaves")
	int32 GetMaxWaves() const { return MaxDifficultyTier; }

	/** Repurposed: true during the initial build-grace (was the prep phase). */
	UFUNCTION(BlueprintPure, Category = "KodoWaves") bool IsPrepPhase() const { return bInGrace; }

	/** Repurposed: seconds left in the build-grace (was prep remaining). */
	UFUNCTION(BlueprintPure, Category = "KodoWaves") float GetPrepRemaining() const { return GraceRemaining; }

	/** Repurposed: seconds until the next spawn cycle (was wave time remaining). */
	UFUNCTION(BlueprintPure, Category = "KodoWaves") float GetWaveTimeRemaining() const { return SpawnCadenceTimer; }

	UFUNCTION(BlueprintPure, Category = "KodoWaves")
	int32 GetAliveKodoCount() const { return ActiveKodos.Num(); }

	/** Structures changed: every Kodo drops its path and recalcs next tick (game.js:2201-2206). */
	void RecalcAllKodoPaths();

	/** Debug/testing helper: immediately spawn Count Kodos (skips the build-grace). */
	UFUNCTION(BlueprintCallable, Category = "KodoWaves|Debug")
	void SpawnTestKodos(int32 Count);

	/** Debug/testing helper: destroy every active Kodo. */
	UFUNCTION(BlueprintCallable, Category = "KodoWaves|Debug")
	void ClearAllKodos();

	/** Set the spawn difficulty (called by the GameMode at match start). */
	void SetDifficulty(const EKodoDifficulty D) { Difficulty = D; }

	// --- Admin Tower control panel API (UI/KodoHudWidget admin card) ---------------

	/** Spawn ONE kodo of the given type at the portal immediately (skips the build-grace,
	 *  still respects MaxActiveKodos). Used by the Admin Tower "Spawn <Type>" buttons. */
	UFUNCTION(BlueprintCallable, Category = "KodoWaves|Admin")
	void SpawnKodoOfType(EKodoType Type);

	/** Enable/disable the automatic spawner (the Tick gate checks this in addition to the
	 *  match-start gate). Admin "Stop/Resume Spawning". */
	UFUNCTION(BlueprintCallable, Category = "KodoWaves|Admin")
	void SetSpawningEnabled(const bool b) { bSpawningEnabled = b; }

	UFUNCTION(BlueprintPure, Category = "KodoWaves|Admin")
	bool IsSpawningEnabled() const { return bSpawningEnabled; }

	/** Raise difficulty one step: Easy -> Normal -> Hard -> Insane (clamped). Admin "Difficulty +". */
	UFUNCTION(BlueprintCallable, Category = "KodoWaves|Admin")
	void StepDifficultyUp();

	/** Current spawn difficulty (for the admin card label). */
	UFUNCTION(BlueprintPure, Category = "KodoWaves|Admin")
	EKodoDifficulty GetDifficulty() const { return Difficulty; }

protected:
	/** Spawn one kodo at the portal. Type is chosen from the current DifficultyTier. */
	void SpawnKodo(const FVector& ExtraOffset = FVector::ZeroVector);
	/** Shared actor-spawn core: spawns one kodo of an explicit type at the portal (+offset). */
	void SpawnKodoActor(EKodoType Type, const FVector& ExtraOffset);
	/** Emit a whole spawn cycle: round(BaseSpawnCount * Rr) kodos (kodo_balance_data.md §4). */
	void SpawnCycle();
	/** Resolve Rr for the current Difficulty (kodo_balance_data.md §4). */
	float GetRr() const;
	/** Resolve spawn cadence (s) for the current Difficulty (kodo_balance_data.md §4). */
	float GetCadenceSeconds() const;
	/** Pick a kodo TYPE for the current tier — tougher types unlock as the tier rises. */
	EKodoType RollKodoType() const;
	void ShowMessage(const FString& Text, const FColor& Color, float Duration = 4.f, int32 Key = -1) const;

	// --- Tuning (WC3 model, kodo_balance_data.md §4) ----------------------------

	/** Difficulty selector. Default Normal (Rr 1.0, cadence 45s). */
	UPROPERTY(EditAnywhere, Category = "KodoWaves") EKodoDifficulty Difficulty = EKodoDifficulty::Normal;

	/** Initial build-grace before any spawns (WC3 TriggerSleepAction(60.) guard). */
	UPROPERTY(EditAnywhere, Category = "KodoWaves") float InitialGraceSeconds = 60.f;

	/** Base kodos per cycle before the Rr multiplier (WC3 O = 4). */
	UPROPERTY(EditAnywhere, Category = "KodoWaves") int32 BaseSpawnCount = 4;

	/** Game seconds between tier bumps; tier selects tougher kodo TYPES (no per-kodo scaling). */
	UPROPERTY(EditAnywhere, Category = "KodoWaves") float TierStepSeconds = 90.f;

	/** Escalation tier cap (surfaced via GetMaxWaves for the HUD). */
	UPROPERTY(EditAnywhere, Category = "KodoWaves") int32 MaxDifficultyTier = 10;

	UPROPERTY(EditAnywhere, Category = "KodoWaves") float EnrageThresholdSeconds = 3.f;

	/** Hard cap on simultaneously-alive Kodos (perf safety valve; WC3 cap is 9999). */
	UPROPERTY(EditAnywhere, Category = "KodoWaves") int32 MaxActiveKodos = 200;
	/** Large test spawns are drained over several frames at this rate (avoids a 1-frame spike). */
	UPROPERTY(EditAnywhere, Category = "KodoWaves") int32 TestSpawnsPerFrame = 6;

	// --- Spawn state ------------------------------------------------------------

	/** Escalation tier (0..MaxDifficultyTier). Rises with elapsed time; picks tougher types. */
	int32 DifficultyTier = 0;

	/** True during the initial build-grace (HUD's "prep"). */
	bool bInGrace = true;
	float GraceRemaining = 0.f;

	/** Admin toggle: when false, the auto-spawn cadence is paused (manual admin spawns still work). */
	bool bSpawningEnabled = true;

	/** Counts down to the next spawn cycle (HUD's "wave time remaining"). */
	float SpawnCadenceTimer = 0.f;

	/** Elapsed spawn-active time, used to drive the difficulty tier. */
	float ElapsedSinceGrace = 0.f;

	/** Global blocked timer feeding enrage. */
	float BlockedTimer = 0.f;

	/** Test Kodos still waiting to be drip-spawned (throttled in Tick). */
	int32 PendingTestSpawns = 0;

	// Perf heartbeat accumulators (logged via LogKodoPerf every ~2s).
	float PerfLogAccum = 0.f;
	int32 PerfFrameCount = 0;

	UPROPERTY()
	TArray<TWeakObjectPtr<AKodoCharacter>> ActiveKodos;

	UPROPERTY()
	TObjectPtr<UKodoGridSubsystem> Grid;
};
