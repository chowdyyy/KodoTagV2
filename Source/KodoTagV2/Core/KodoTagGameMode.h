// Kodo Tag: Survivor — UE Migration, Phase 1.
// Wires the world together: camera pawn as the possessed pawn, custom
// controller, map bootstrapper + Runner spawned at BeginPlay.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/KodoTagGameState.h"      // EKodoGameMode
#include "Actors/KodoWaveController.h"  // EKodoDifficulty
#include "KodoTagGameMode.generated.h"

class AKodoMapBootstrapper;
class ARunnerCharacter;
class AKodoWaveController;

UCLASS()
class KODOTAGV2_API AKodoTagGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKodoTagGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// --- 30-minute survival match clock + win/lose ---

	/** Total survival time required to win (30 min). */
	float MatchDurationSeconds = 1800.f;
	/** Elapsed match time; only advances while the match is started and not over. */
	float MatchElapsedSeconds = 0.f;
	bool bGameOver = false;
	bool bVictory = false;

	/** Seconds left on the survival clock (clamped at 0). */
	UFUNCTION(BlueprintPure, Category = "Kodo")
	float GetMatchTimeRemaining() const { return FMath::Max(0.f, MatchDurationSeconds - MatchElapsedSeconds); }

	UFUNCTION(BlueprintPure, Category = "Kodo")
	bool IsGameOver() const { return bGameOver; }

	UFUNCTION(BlueprintPure, Category = "Kodo")
	bool IsVictory() const { return bVictory; }

	/** Called by the UI start overlay. Configures GameState mode + mults, sets bMatchStarted,
	 *  sets wave difficulty, applies hero mode buffs, and starts the clock. */
	void BeginMatch(EKodoGameMode Mode, EKodoDifficulty Difficulty);

	/** Called when the runner is eaten -> defeat. */
	void OnRunnerDied();

protected:
	UPROPERTY()
	TObjectPtr<AKodoMapBootstrapper> Bootstrapper;

	UPROPERTY()
	TObjectPtr<ARunnerCharacter> Runner;

	UPROPERTY()
	TObjectPtr<AKodoWaveController> WaveController;
};
