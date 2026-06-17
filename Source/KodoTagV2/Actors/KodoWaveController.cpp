// Kodo Tag: Survivor — UE Migration, Phase 2.

#include "Actors/KodoWaveController.h"
#include "Grid/KodoGridSubsystem.h"
#include "Core/KodoTagUnits.h"
#include "Core/KodoTagGameState.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

// Perf heartbeat category — grep "PERF" (or this category) in Saved/Logs/KodoTagV2.log.
DEFINE_LOG_CATEGORY_STATIC(LogKodoPerf, Display, All);

AKodoWaveController::AKodoWaveController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AKodoWaveController::BeginPlay()
{
	Super::BeginPlay();
	Grid = GetWorld()->GetSubsystem<UKodoGridSubsystem>();

	// WC3 model (kodo_balance_data.md §4): an initial build-grace, then continuous spawns.
	bInGrace = true;
	GraceRemaining = InitialGraceSeconds;
	SpawnCadenceTimer = GetCadenceSeconds();
	DifficultyTier = 0;
	ElapsedSinceGrace = 0.f;

	ShowMessage(FString::Printf(TEXT("BUILD PHASE: %.0fs before the Kodos come!"), GraceRemaining),
	            FColor::Cyan);
}

float AKodoWaveController::GetRr() const
{
	// kodo_balance_data.md §4: Easy 0.70 / Normal 1.00 / Hard 1.50 / Insane 2.45.
	switch (Difficulty)
	{
	case EKodoDifficulty::Easy:   return 0.70f;
	case EKodoDifficulty::Hard:   return 1.50f;
	case EKodoDifficulty::Insane: return 2.45f;
	default:                      return 1.00f; // Normal
	}
}

float AKodoWaveController::GetCadenceSeconds() const
{
	// kodo_balance_data.md §4: Easy/Normal 45s / Hard 20s / Insane 10s between cycles.
	switch (Difficulty)
	{
	case EKodoDifficulty::Hard:   return 20.f;
	case EKodoDifficulty::Insane: return 10.f;
	case EKodoDifficulty::Easy:   return 45.f;
	default:                      return 45.f; // Normal
	}
}

EKodoType AKodoWaveController::RollKodoType() const
{
	// WC3 escalation (kodo_balance_data.md §1/§4): no per-kodo HP/speed scaling — instead
	// tougher TYPES come online as the difficulty tier rises. Early tiers are mostly the
	// mid "Standard" kodo; Speed appears soon; Tank/Blink unlock at higher tiers, and their
	// share of the spawn pool grows with the tier.
	const float Roll = FMath::FRand();
	const int32 T = DifficultyTier;

	// Tank: heavy 2x2 wall-eaters, unlock at tier 3, share grows to ~30%.
	if (T >= 3 && Roll < FMath::Min(0.30f, 0.08f + 0.03f * T))
	{
		return EKodoType::Tank;
	}
	// Blink: special teleporters, unlock at tier 5, ~15% share.
	if (T >= 5 && FMath::FRand() < 0.15f)
	{
		return EKodoType::Blink;
	}
	// Speed: fast 1x1 runners that can slip 1-cell gaps, unlock at tier 1, ~25%.
	if (T >= 1 && FMath::FRand() < 0.25f)
	{
		return EKodoType::Speed;
	}
	return EKodoType::Standard; // mid kodo — the bulk of spawns
}

void AKodoWaveController::RecalcAllKodoPaths()
{
	for (const TWeakObjectPtr<AKodoCharacter>& Kodo : ActiveKodos)
	{
		if (Kodo.IsValid())
		{
			Kodo->ResetPathNow();
		}
	}
}

void AKodoWaveController::ShowMessage(const FString& Text, const FColor& Color, const float Duration,
                                      const int32 Key) const
{
	// Blockout-grade messaging; replaced by the UMG HUD in Phase 3.
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, Duration, Color, Text);
	}
}

void AKodoWaveController::SpawnTestKodos(const int32 Count)
{
	if (!Grid)
	{
		Grid = GetWorld() ? GetWorld()->GetSubsystem<UKodoGridSubsystem>() : nullptr;
	}
	if (!Grid)
	{
		return;
	}

	// Skip the build-grace so the spawned Kodos chase immediately.
	bInGrace = false;
	GraceRemaining = 0.f;

	// Queue the request (clamped to remaining headroom under the cap); Tick drip-spawns
	// them over several frames so a single click can never spike the frame.
	const int32 Headroom = FMath::Max(0, MaxActiveKodos - ActiveKodos.Num() - PendingTestSpawns);
	const int32 N = FMath::Clamp(Count, 0, Headroom);
	PendingTestSpawns += N;

	if (N < Count)
	{
		ShowMessage(FString::Printf(TEXT("TEST: queued %d Kodos (capped at %d alive)"), N, MaxActiveKodos),
		            FColor::Yellow, 3.f);
	}
	else
	{
		ShowMessage(FString::Printf(TEXT("TEST: queued %d Kodos"), N), FColor::Yellow, 3.f);
	}
}

void AKodoWaveController::ClearAllKodos()
{
	for (const TWeakObjectPtr<AKodoCharacter>& Kodo : ActiveKodos)
	{
		if (Kodo.IsValid())
		{
			Kodo->Destroy();
		}
	}
	ActiveKodos.Reset();
	ShowMessage(TEXT("TEST: cleared all Kodos"), FColor::Yellow, 2.f);
}

void AKodoWaveController::SpawnKodoActor(const EKodoType Type, const FVector& ExtraOffset)
{
	// Shared actor-spawn core: spawn one kodo of the given explicit type at the portal cell.
	// WC3 model: the kodo's own stats are fixed per type (no per-wave scaling).
	UWorld* World = GetWorld();
	if (!World || !Grid)
	{
		return;
	}

	FVector SpawnLocation = Grid->CellToWorldCenter(Grid->GetKodoSpawnCell()) + ExtraOffset; // editor spawn config
	// capsule half-height, raised by the elevation of the actual spawn cell (incl. offset).
	SpawnLocation.Z = 88.f + Grid->GetElevationZAtWorld(SpawnLocation);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AKodoCharacter* Kodo = World->SpawnActor<AKodoCharacter>(AKodoCharacter::StaticClass(), SpawnLocation,
	                                                         FRotator::ZeroRotator, Params);
	if (Kodo)
	{
		Kodo->InitKodo(Type, DifficultyTier, this);
		ActiveKodos.Add(Kodo);
	}
}

void AKodoWaveController::SpawnKodo(const FVector& ExtraOffset)
{
	// Auto-spawn path: pick a type from the current difficulty tier, then spawn it.
	SpawnKodoActor(RollKodoType(), ExtraOffset);
}

void AKodoWaveController::SpawnKodoOfType(const EKodoType Type)
{
	// Admin Tower spawn: skip the build-grace and spawn one kodo of the chosen type right now,
	// still honoring the alive cap. A little jitter keeps stacked admin spawns from overlapping.
	bInGrace = false;
	GraceRemaining = 0.f;
	if (ActiveKodos.Num() >= MaxActiveKodos)
	{
		ShowMessage(FString::Printf(TEXT("ADMIN: kodo cap reached (%d)"), MaxActiveKodos), FColor::Yellow, 2.f);
		return;
	}
	const FVector Jitter(
		FMath::FRandRange(-1.f, 1.f) * KodoUnits::CellSizeUU,
		FMath::FRandRange(-1.f, 1.f) * KodoUnits::CellSizeUU,
		0.f);
	SpawnKodoActor(Type, Jitter);
}

void AKodoWaveController::StepDifficultyUp()
{
	// Easy -> Normal -> Hard -> Insane, clamped at Insane.
	const uint8 Next = FMath::Min<uint8>(static_cast<uint8>(Difficulty) + 1,
	                                     static_cast<uint8>(EKodoDifficulty::Insane));
	Difficulty = static_cast<EKodoDifficulty>(Next);
	ShowMessage(FString::Printf(TEXT("ADMIN: difficulty raised to %d"), Next + 1), FColor::Orange, 2.f);
}

void AKodoWaveController::SpawnCycle()
{
	// kodo_balance_data.md §4: each cycle emits round(BaseSpawnCount * Rr) kodos.
	// God mode multiplies the count by GS->KodoSpawnMult (default 1 if no GameState).
	const AKodoTagGameState* GS = GetWorld() ? GetWorld()->GetGameState<AKodoTagGameState>() : nullptr;
	const float SpawnMult = GS ? GS->KodoSpawnMult : 1.f;
	const int32 Count = FMath::Max(1, FMath::RoundToInt(BaseSpawnCount * GetRr() * SpawnMult));
	for (int32 i = 0; i < Count; ++i)
	{
		if (ActiveKodos.Num() >= MaxActiveKodos)
		{
			break; // perf cap (WC3 cap is 9999; ours stays at MaxActiveKodos)
		}
		// Fan the cycle out around the portal so they don't all stack on one cell.
		const FVector Jitter(
			FMath::FRandRange(-1.f, 1.f) * KodoUnits::CellSizeUU * 2.f,
			FMath::FRandRange(-1.f, 1.f) * KodoUnits::CellSizeUU * 2.f,
			0.f);
		SpawnKodo(Jitter);
	}
}

void AKodoWaveController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// --- Perf heartbeat: one line every ~2s (averaged), even after game over so a
	// frame-rate collapse is visible in the log. Grep "PERF" in Saved/Logs/KodoTagV2.log.
	PerfLogAccum += DeltaSeconds;
	++PerfFrameCount;
	if (PerfLogAccum >= 2.f)
	{
		const float AvgMs = (PerfFrameCount > 0) ? (PerfLogAccum / PerfFrameCount) * 1000.f : 0.f;
		const float Fps = (PerfLogAccum > 0.f) ? PerfFrameCount / PerfLogAccum : 0.f;
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		UE_LOG(LogKodoPerf, Display,
			TEXT("[PERF] t=%.0fs fps=%.1f frame=%.1fms kodos=%d pending=%d tier=%d enrage=%d"),
			Now, Fps, AvgMs, ActiveKodos.Num(), PendingTestSpawns, DifficultyTier, IsEnrageActive() ? 1 : 0);
		if (Fps < 20.f)
		{
			UE_LOG(LogKodoPerf, Warning, TEXT("[PERF] LOW FPS %.1f at %d kodos"), Fps, ActiveKodos.Num());
		}
		PerfLogAccum = 0.f;
		PerfFrameCount = 0;
	}

	// Match-start gate: no grace countdown, no spawns, no enrage until the start overlay
	// begins the match (GS->bMatchStarted). The perf heartbeat above still runs regardless.
	const AKodoTagGameState* GS = GetWorld() ? GetWorld()->GetGameState<AKodoTagGameState>() : nullptr;
	if (!GS || !GS->bMatchStarted)
	{
		return;
	}

	ActiveKodos.RemoveAll([](const TWeakObjectPtr<AKodoCharacter>& K) { return !K.IsValid(); });

	// Drain throttled test spawns: a few per frame, never past the cap.
	if (PendingTestSpawns > 0)
	{
		int32 Budget = FMath::Min(TestSpawnsPerFrame, PendingTestSpawns);
		while (Budget-- > 0 && ActiveKodos.Num() < MaxActiveKodos)
		{
			const FVector Jitter(
				FMath::FRandRange(-1.f, 1.f) * KodoUnits::CellSizeUU * 3.f,
				FMath::FRandRange(-1.f, 1.f) * KodoUnits::CellSizeUU * 3.f,
				0.f);
			SpawnKodo(Jitter);
			--PendingTestSpawns;
		}
	}

	// --- Continuous spawn model (kodo_balance_data.md §4): no discrete waves. ---
	if (bInGrace)
	{
		GraceRemaining = FMath::Max(0.f, GraceRemaining - DeltaSeconds);
		if (GraceRemaining <= 0.f)
		{
			bInGrace = false;
			SpawnCadenceTimer = GetCadenceSeconds();
			ShowMessage(TEXT("THE KODOS ARE COMING!"), FColor::Red);
		}
	}
	else
	{
		// Difficulty tier rises with elapsed spawn-active time → tougher kodo TYPES.
		ElapsedSinceGrace += DeltaSeconds;
		const int32 NewTier = FMath::Clamp(
			FMath::FloorToInt(ElapsedSinceGrace / FMath::Max(1.f, TierStepSeconds)),
			0, MaxDifficultyTier);
		if (NewTier != DifficultyTier)
		{
			DifficultyTier = NewTier;
			ShowMessage(FString::Printf(TEXT("Kodos grow stronger… (tier %d)"), DifficultyTier),
			            FColor::Orange, 3.f);
		}

		// Spawn a cycle every cadence interval (round(BaseSpawnCount * Rr) kodos).
		// The admin toggle (bSpawningEnabled) pauses the AUTO spawner only; admin manual
		// spawns and the tier clock keep running so resuming picks up where it left off.
		SpawnCadenceTimer -= DeltaSeconds;
		if (SpawnCadenceTimer <= 0.f)
		{
			if (bSpawningEnabled && ActiveKodos.Num() < MaxActiveKodos)
			{
				SpawnCycle();
			}
			SpawnCadenceTimer = GetCadenceSeconds();
		}
	}

	// Global blocked timer -> enrage (game.js:1158-1170).
	bool bAnyBlocked = false;
	for (const TWeakObjectPtr<AKodoCharacter>& Kodo : ActiveKodos)
	{
		if (Kodo.IsValid() && Kodo->IsBaseBlocked())
		{
			bAnyBlocked = true;
			break;
		}
	}

	const bool bWasEnraged = IsEnrageActive();
	BlockedTimer = bAnyBlocked ? BlockedTimer + DeltaSeconds : 0.f;
	if (IsEnrageActive() && !bWasEnraged)
	{
		ShowMessage(TEXT("KODOS ENRAGED! RUN FOR YOUR LIFE!"), FColor::Red, 5.f);
	}
}
