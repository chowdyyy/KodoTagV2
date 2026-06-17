// Kodo Tag: Survivor — UE Migration, Phase 1.

#include "Core/KodoTagGameMode.h"
#include "Core/KodoTagUnits.h"
#include "Core/KodoTagGameState.h"
#include "Grid/KodoMapBootstrapper.h"
#include "Grid/KodoStructureManager.h"
#include "Grid/KodoGridSubsystem.h"
#include "Camera/KodoCameraPawn.h"
#include "Camera/KodoPlayerController.h"
#include "UI/KodoHUD.h"
#include "Actors/RunnerCharacter.h"
#include "Actors/KodoWaveController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

AKodoTagGameMode::AKodoTagGameMode()
{
	// The possessed pawn is the CAMERA, not the hero — camera and Runner are
	// decoupled and re-coupled by Spacebar, like the prototype (TDD §3.2).
	DefaultPawnClass = AKodoCameraPawn::StaticClass();
	PlayerControllerClass = AKodoPlayerController::StaticClass();
	GameStateClass = AKodoTagGameState::StaticClass();
	HUDClass = AKodoHUD::StaticClass();

	// The match clock runs in Tick (AGameModeBase ticks once enabled).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AKodoTagGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();

	// 1. Build the map (grid data + blockout visuals).
	Bootstrapper = World->SpawnActor<AKodoMapBootstrapper>(FVector::ZeroVector, FRotator::ZeroRotator);

	// 2. Spawn the Runner at the editor-configured runner spawn cell (editor spawn config),
	// cell-centered (game.js:773). The bootstrapper above ran its BeginPlay synchronously,
	// so the grid already holds the map file's spawn values.
	if (UKodoGridSubsystem* Grid = World->GetSubsystem<UKodoGridSubsystem>())
	{
		const FIntPoint RunnerCell = Grid->GetRunnerSpawnCell();
		FVector SpawnLocation = Grid->CellToWorldCenter(RunnerCell);
		// capsule half-height; ground top is Z = 0, raised by the base's elevation level.
		SpawnLocation.Z = 88.f + Grid->GetElevationZ(RunnerCell);

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Runner = World->SpawnActor<ARunnerCharacter>(ARunnerCharacter::StaticClass(), SpawnLocation,
		                                             FRotator::ZeroRotator, Params);
	}

	// 3. Structure manager (Phase 3): placement, construction, income, research, visuals.
	World->SpawnActor<AKodoStructureManager>(FVector::ZeroVector, FRotator::ZeroRotator);

	// 4. Start the wave spawner (Phase 2) — Kodos pour from cell (80, 45).
	WaveController = World->SpawnActor<AKodoWaveController>(FVector::ZeroVector, FRotator::ZeroRotator);

	// Camera pawn acquires the Runner lazily in its Tick (no ordering dependency).
}

void AKodoTagGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AKodoTagGameState* GS = GetGameState<AKodoTagGameState>();
	if (!GS || !GS->bMatchStarted || bGameOver)
	{
		return;
	}

	MatchElapsedSeconds += DeltaSeconds;
	if (MatchElapsedSeconds >= MatchDurationSeconds)
	{
		bGameOver = true;
		bVictory = true;
		// Halt spawns: the wave controller gates on bMatchStarted.
		GS->bMatchStarted = false;
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Green,
				TEXT("YOU SURVIVED 30 MINUTES — VICTORY!"));
		}
	}
}

void AKodoTagGameMode::BeginMatch(const EKodoGameMode Mode, const EKodoDifficulty Difficulty)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 1. Configure GameState mode + mults, start the match.
	if (AKodoTagGameState* GS = GetGameState<AKodoTagGameState>())
	{
		GS->ConfigureMode(Mode);
		GS->bMatchStarted = true;
	}

	// 2. Reset the survival clock.
	MatchElapsedSeconds = 0.f;
	bGameOver = false;
	bVictory = false;

	// 3. Set the wave difficulty on the live wave controller.
	for (TActorIterator<AKodoWaveController> It(World); It; ++It)
	{
		It->SetDifficulty(Difficulty);
		break;
	}

	// 4. Apply per-mode hero buffs to the runner.
	for (TActorIterator<ARunnerCharacter> It(World); It; ++It)
	{
		It->ApplyModeBuffs(Mode);
		break;
	}
}

void AKodoTagGameMode::OnRunnerDied()
{
	if (bGameOver)
	{
		return;
	}
	bGameOver = true;
	bVictory = false;

	if (AKodoTagGameState* GS = GetGameState<AKodoTagGameState>())
	{
		GS->bMatchStarted = false; // halt spawns
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Red, TEXT("GAME OVER"));
	}
}
