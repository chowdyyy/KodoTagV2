// Kodo Tag: Survivor — UE Migration, Phase 3.

#include "Camera/KodoPlayerController.h"
#include "Camera/KodoCameraPawn.h"
#include "Actors/RunnerCharacter.h"
#include "Actors/KodoCharacter.h"
#include "Actors/KodoCastEffect.h"
#include "EngineUtils.h"
#include "Grid/KodoGridSubsystem.h"
#include "Grid/KodoStructureManager.h"
#include "Grid/KodoMapBootstrapper.h"
#include "Data/KodoStructureData.h"
#include "Core/KodoTagUnits.h"
#include "Core/KodoTagGameState.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "UI/KodoHudWidget.h"

AKodoPlayerController::AKodoPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void AKodoPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	const auto MakeBoolAction = [this](const TCHAR* Name)
	{
		return NewObject<UInputAction>(this, Name); // bool by default
	};

	// --- Build actions ---
	PanAction = NewObject<UInputAction>(this, TEXT("IA_CameraPan"));
	PanAction->ValueType = EInputActionValueType::Axis2D;
	ZoomAction = NewObject<UInputAction>(this, TEXT("IA_CameraZoom"));
	ZoomAction->ValueType = EInputActionValueType::Axis1D;
	LockAction = MakeBoolAction(TEXT("IA_CameraLock"));
	MoveAction = MakeBoolAction(TEXT("IA_MoveCommand"));
	PlaceAction = MakeBoolAction(TEXT("IA_Place"));
	WallAction = MakeBoolAction(TEXT("IA_BlueprintWall"));
	CommandCenterAction = MakeBoolAction(TEXT("IA_BlueprintCC"));
	TowerAction = MakeBoolAction(TEXT("IA_BlueprintTower"));
	CastSpellAction = MakeBoolAction(TEXT("IA_CastSpell"));
	CastSkill2Action = MakeBoolAction(TEXT("IA_CastSkill2"));
	CastSkill3Action = MakeBoolAction(TEXT("IA_CastSkill3"));
	QuitAction = MakeBoolAction(TEXT("IA_QuitGame"));
	GunModeAction = MakeBoolAction(TEXT("IA_GunMode"));
	HeroClassActions.Reset();
	for (int32 i = 0; i < 4; ++i)
	{
		HeroClassActions.Add(MakeBoolAction(*FString::Printf(TEXT("IA_HeroClass%d"), i + 1)));
	}
	ResearchActions.Reset();
	for (int32 i = 0; i < 7; ++i)
	{
		ResearchActions.Add(MakeBoolAction(*FString::Printf(TEXT("IA_Research%d"), i + 1)));
	}

	// --- Build mapping context ---
	// World axes: X = east, Y = south. Bool keys land in X; swizzle moves to Y.
	DefaultContext = NewObject<UInputMappingContext>(this, TEXT("IMC_KodoDefault"));

	DefaultContext->MapKey(PanAction, EKeys::Right);
	FEnhancedActionKeyMapping& LeftMap = DefaultContext->MapKey(PanAction, EKeys::Left);
	LeftMap.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	FEnhancedActionKeyMapping& DownMap = DefaultContext->MapKey(PanAction, EKeys::Down);
	DownMap.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
	FEnhancedActionKeyMapping& UpMap = DefaultContext->MapKey(PanAction, EKeys::Up);
	UpMap.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
	UpMap.Modifiers.Add(NewObject<UInputModifierNegate>(this));

	DefaultContext->MapKey(ZoomAction, EKeys::MouseWheelAxis);
	DefaultContext->MapKey(LockAction, EKeys::SpaceBar);
	DefaultContext->MapKey(MoveAction, EKeys::RightMouseButton);
	DefaultContext->MapKey(PlaceAction, EKeys::LeftMouseButton);
	DefaultContext->MapKey(WallAction, EKeys::W);            // game.js:3074 hotkeys
	DefaultContext->MapKey(CommandCenterAction, EKeys::C);
	DefaultContext->MapKey(TowerAction, EKeys::T);
	DefaultContext->MapKey(CastSpellAction, EKeys::Q); // Q casts the primary active (slot 0)
	DefaultContext->MapKey(CastSkill2Action, EKeys::E); // E casts the secondary active (slot 1)
	DefaultContext->MapKey(CastSkill3Action, EKeys::R); // R casts the ultimate (slot 3)
	DefaultContext->MapKey(QuitAction, EKeys::Escape);  // Escape quits a packaged build
	DefaultContext->MapKey(GunModeAction, EKeys::G);    // G toggles the TEST aim/gun mode
	const FKey HeroKeys[4] = { EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero };
	for (int32 i = 0; i < 4; ++i)
	{
		DefaultContext->MapKey(HeroClassActions[i], HeroKeys[i]);
	}
	const FKey ResearchKeys[7] = { EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7 };
	for (int32 i = 0; i < 7; ++i)
	{
		DefaultContext->MapKey(ResearchActions[i], ResearchKeys[i]);
	}

	// --- Bind ---
	UEnhancedInputComponent* EIC = CastChecked<UEnhancedInputComponent>(InputComponent);
	EIC->BindAction(PanAction, ETriggerEvent::Triggered, this, &AKodoPlayerController::OnPan);
	EIC->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &AKodoPlayerController::OnZoom);
	EIC->BindAction(LockAction, ETriggerEvent::Started, this, &AKodoPlayerController::OnCameraLock);
	EIC->BindAction(MoveAction, ETriggerEvent::Started, this, &AKodoPlayerController::OnMoveCommand);
	EIC->BindAction(PlaceAction, ETriggerEvent::Started, this, &AKodoPlayerController::OnPlace);
	EIC->BindAction(WallAction, ETriggerEvent::Started, this, &AKodoPlayerController::OnSelectWall);
	EIC->BindAction(CommandCenterAction, ETriggerEvent::Started, this, &AKodoPlayerController::OnSelectCommandCenter);
	EIC->BindAction(TowerAction, ETriggerEvent::Started, this, &AKodoPlayerController::OnSelectBasicTower);
	EIC->BindAction(CastSpellAction, ETriggerEvent::Started, this, &AKodoPlayerController::OnCastSpell);
	EIC->BindAction(CastSkill2Action, ETriggerEvent::Started, this, &AKodoPlayerController::OnCastSkill2);
	EIC->BindAction(CastSkill3Action, ETriggerEvent::Started, this, &AKodoPlayerController::OnCastSkill3);
	EIC->BindAction(QuitAction, ETriggerEvent::Started, this, &AKodoPlayerController::OnQuitGame);
	EIC->BindAction(GunModeAction, ETriggerEvent::Started, this, &AKodoPlayerController::OnToggleGunMode);
	EIC->BindAction(HeroClassActions[0], ETriggerEvent::Started, this, &AKodoPlayerController::OnSetHeroClass1);
	EIC->BindAction(HeroClassActions[1], ETriggerEvent::Started, this, &AKodoPlayerController::OnSetHeroClass2);
	EIC->BindAction(HeroClassActions[2], ETriggerEvent::Started, this, &AKodoPlayerController::OnSetHeroClass3);
	EIC->BindAction(HeroClassActions[3], ETriggerEvent::Started, this, &AKodoPlayerController::OnSetHeroClass4);
	EIC->BindAction(ResearchActions[0], ETriggerEvent::Started, this, &AKodoPlayerController::OnResearch1);
	EIC->BindAction(ResearchActions[1], ETriggerEvent::Started, this, &AKodoPlayerController::OnResearch2);
	EIC->BindAction(ResearchActions[2], ETriggerEvent::Started, this, &AKodoPlayerController::OnResearch3);
	EIC->BindAction(ResearchActions[3], ETriggerEvent::Started, this, &AKodoPlayerController::OnResearch4);
	EIC->BindAction(ResearchActions[4], ETriggerEvent::Started, this, &AKodoPlayerController::OnResearch5);
	EIC->BindAction(ResearchActions[5], ETriggerEvent::Started, this, &AKodoPlayerController::OnResearch6);
	EIC->BindAction(ResearchActions[6], ETriggerEvent::Started, this, &AKodoPlayerController::OnResearch7);
}

void AKodoPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			InputSubsystem->AddMappingContext(DefaultContext, /*Priority*/ 0);
		}
	}

	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);

	// WC3-style HUD (Phase 5).
	HudWidget = CreateWidget<UKodoHudWidget>(this, UKodoHudWidget::StaticClass());
	if (HudWidget)
	{
		HudWidget->InitHud(this);
		HudWidget->AddToViewport(10);
	}
}

EKodoSelection AKodoPlayerController::GetSelectionKind() const
{
	// Self-validating: a sold/eaten structure deselects itself.
	if (SelectionKind == EKodoSelection::Cell)
	{
		const UKodoGridSubsystem* GridSub = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
		if (!GridSub || GridSub->GetCell(SelectedCell).Type == ECellType::Empty)
		{
			return EKodoSelection::None;
		}
	}
	return SelectionKind;
}

void AKodoPlayerController::ClearSelection()
{
	SelectionKind = EKodoSelection::None;
	SelectedCell = FIntPoint(-1, -1);
	SelectedKodo = nullptr;
}

void AKodoPlayerController::PanCameraToNormalized(const FVector2D& Normalized)
{
	if (AKodoCameraPawn* Cam = GetCameraPawn())
	{
		Cam->JumpFocusTo(FVector(Normalized.X * KodoUnits::MapExtentUU,
		                         Normalized.Y * KodoUnits::MapExtentUU, 0.f));
	}
}

AKodoCameraPawn* AKodoPlayerController::GetCameraPawn() const
{
	return Cast<AKodoCameraPawn>(GetPawn());
}

// =====================================================================================
// In-world map editor (spatial editor pass).
// =====================================================================================

AKodoMapBootstrapper* AKodoPlayerController::GetBootstrapper()
{
	if (!CachedBootstrapper.IsValid())
	{
		for (TActorIterator<AKodoMapBootstrapper> It(GetWorld()); It; ++It)
		{
			CachedBootstrapper = *It;
			break;
		}
	}
	return CachedBootstrapper.Get();
}

void AKodoPlayerController::EnterEditMode()
{
	bEditMode = true;
	SelectedBuildPreset = NAME_None; // no build ghost while painting
	PendingBuildPreset = NAME_None;
	ClearSelection();

	// Start in Pan mode (no tool) so scrolling/clicking around the map doesn't place anything
	// until the player deliberately picks a tool from the palette.
	ActiveEditTool = EEditTool::None;
	LastPaintedCell = FIntPoint(-1, -1);

	// Hide the hero — you sculpt the map, you don't control a unit while editing.
	if (ARunnerCharacter* Runner = GetRunner())
	{
		Runner->SetActorHiddenInGame(true);
		Runner->SetActorEnableCollision(false);
	}
	// Free the camera so you can scroll the whole map (no follow-lock on the hero).
	if (AKodoCameraPawn* Cam = GetCameraPawn())
	{
		Cam->SetLocked(false);
	}
	// The match stays unstarted (GameState->bMatchStarted is left false), so play is gated
	// until the player presses Play/Start.
}

void AKodoPlayerController::ExitEditMode()
{
	bEditMode = false;

	// Restore the hero for play.
	if (ARunnerCharacter* Runner = GetRunner())
	{
		Runner->SetActorHiddenInGame(false);
		Runner->SetActorEnableCollision(true);
	}
	if (AKodoCameraPawn* Cam = GetCameraPawn())
	{
		Cam->SetLocked(true);
	}
}

void AKodoPlayerController::SetEditTool(const EEditTool Tool, const int32 RampDir)
{
	ActiveEditTool = Tool;
	if (RampDir >= 0)
	{
		EditRampDir = FMath::Clamp(RampDir, 0, 3);
	}
}

void AKodoPlayerController::ApplyEditAt(const FIntPoint& Cell)
{
	UKodoGridSubsystem* GridSub = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
	if (!GridSub || !GridSub->IsInBounds(Cell))
	{
		return;
	}

	switch (ActiveEditTool)
	{
	case EEditTool::Ridge:
	{
		FGridCell C;
		C.Type = ECellType::Cliff;
		C.Hp = 999999.f;
		C.MaxHp = 999999.f;
		C.Level = 1;
		C.StructureId = FName(TEXT("cliff"));
		GridSub->SetCell(Cell, C);
		break;
	}
	case EEditTool::Tree:
	{
		FGridCell C;
		C.Type = ECellType::Tree;
		C.Hp = 120.f;
		C.MaxHp = 120.f;
		C.Level = 1;
		C.StructureId = FName(TEXT("tree"));
		GridSub->SetCell(Cell, C);
		break;
	}
	case EEditTool::Mine:
	{
		for (int32 Dx = 0; Dx < 2; ++Dx)
		{
			for (int32 Dy = 0; Dy < 2; ++Dy)
			{
				FGridCell C;
				C.Type = ECellType::Goldmine;
				C.Hp = 999999.f;
				C.MaxHp = 999999.f;
				C.Level = 1;
				C.StructureId = FName(TEXT("goldmine"));
				C.MasterCell = Cell;
				const FIntPoint MineCell(Cell.X + Dx, Cell.Y + Dy);
				GridSub->SetCell(MineCell, C);
				EditDirtyCells.Add(MineCell); // all 4 footprint cells
			}
		}
		break;
	}
	case EEditTool::Erase:
		EraseCell(Cell);
		EditDirtyCells.Add(Cell);
		return;
	case EEditTool::KodoSpawn:
		GridSub->SetKodoSpawnCell(Cell);
		break;
	case EEditTool::RunnerSpawn:
		GridSub->SetRunnerSpawnCell(Cell);
		break;
	case EEditTool::Merchant:
		GridSub->SetMerchantCell(Cell);
		break;
	case EEditTool::ElevRaise:
		GridSub->SetElevation(Cell, FMath::Min(GridSub->GetElevationLevel(Cell) + 1, 2));
		break;
	case EEditTool::ElevLower:
		GridSub->SetElevation(Cell, FMath::Max(GridSub->GetElevationLevel(Cell) - 1, 0));
		break;
	case EEditTool::Ramp:
		GridSub->SetRamp(Cell, EditRampDir);
		break;
	default:
		return;
	}

	// Mark the edited cell for an incremental per-cell visual update in PlayerTick (the cell +
	// its 8 neighbors are refreshed — far cheaper than a full-map rebuild).
	EditDirtyCells.Add(Cell);
}

void AKodoPlayerController::EraseCell(const FIntPoint& Cell)
{
	UKodoGridSubsystem* GridSub = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
	if (!GridSub || !GridSub->IsInBounds(Cell))
	{
		return;
	}
	GridSub->SetCell(Cell, FGridCell());
	GridSub->SetElevation(Cell, 0);
	GridSub->ClearRamp(Cell);
}

ARunnerCharacter* AKodoPlayerController::GetRunner()
{
	if (!CachedRunner.IsValid())
	{
		CachedRunner = Cast<ARunnerCharacter>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ARunnerCharacter::StaticClass()));
	}
	return CachedRunner.Get();
}

AKodoStructureManager* AKodoPlayerController::GetStructureManager()
{
	if (!CachedManager.IsValid())
	{
		CachedManager = Cast<AKodoStructureManager>(
			UGameplayStatics::GetActorOfClass(GetWorld(), AKodoStructureManager::StaticClass()));
	}
	return CachedManager.Get();
}

void AKodoPlayerController::OnPan(const FInputActionValue& Value)
{
	if (AKodoCameraPawn* Cam = GetCameraPawn())
	{
		Cam->AddPanInput(Value.Get<FVector2D>());
	}
}

void AKodoPlayerController::OnZoom(const FInputActionValue& Value)
{
	if (AKodoCameraPawn* Cam = GetCameraPawn())
	{
		Cam->AddZoomInput(Value.Get<float>());
	}
}

void AKodoPlayerController::OnCameraLock(const FInputActionValue& /*Value*/)
{
	if (AKodoCameraPawn* Cam = GetCameraPawn())
	{
		Cam->LockToTarget();
	}
}

void AKodoPlayerController::SelectBlueprint(const FName PresetId)
{
	SelectedBuildPreset = PresetId;
	if (const FKodoStructurePreset* Preset = KodoStructures::Find(PresetId))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan,
				FString::Printf(TEXT("Blueprint: %s — left-click to place, right-click to cancel"), *Preset->DisplayName));
		}
	}
}

// Build hotkeys (W/C/T): select the blueprint to place AND open the build submenu with the matching
// card flashed, so pressing the key makes the command card visibly react like clicking the button.
// Card indices match the build-submenu layout in RebuildCommandCardIfNeeded (0=Wall, 1=Cmd Center, 2=Basic Spire).
void AKodoPlayerController::OnSelectWall(const FInputActionValue&)
{
	SelectBlueprint(FName("wall"));
	if (HudWidget) { HudWidget->OpenBuildSubmenuFlash(0); }
}
void AKodoPlayerController::OnSelectCommandCenter(const FInputActionValue&)
{
	SelectBlueprint(FName("command_center"));
	if (HudWidget) { HudWidget->OpenBuildSubmenuFlash(1); }
}
void AKodoPlayerController::OnSelectBasicTower(const FInputActionValue&)
{
	SelectBlueprint(FName("basic_tower"));
	if (HudWidget) { HudWidget->OpenBuildSubmenuFlash(2); }
}

void AKodoPlayerController::TryResearch(const EKodoResearch Type)
{
	// Research no longer requires the hero to be near the building — you can queue it from
	// anywhere as long as the right research building exists (the StructureManager checks that,
	// charges the cost, and runs a timed research that completes after a few seconds).
	if (AKodoStructureManager* Manager = GetStructureManager())
	{
		Manager->Research(Type);
	}
}

void AKodoPlayerController::OnCastSpell(const FInputActionValue&)
{
	CastHeroSkill(0); // Q -> primary active (slot 0); routes through targeting like the HUD
}

void AKodoPlayerController::OnCastSkill2(const FInputActionValue&)
{
	CastHeroSkill(1); // E -> secondary active (slot 1)
}

void AKodoPlayerController::OnCastSkill3(const FInputActionValue&)
{
	CastHeroSkill(3); // R -> ultimate (slot 3)
}

void AKodoPlayerController::OnQuitGame(const FInputActionValue&)
{
	// Escape quits a packaged/standalone build (no-op effect in the editor's PIE is fine).
	ConsoleCommand(TEXT("quit"));
}

void AKodoPlayerController::OnToggleGunMode(const FInputActionValue&)
{
	ToggleGunMode(); // G -> flip the TEST aim/gun mode
}

void AKodoPlayerController::ToggleGunMode()
{
	bGunMode = !bGunMode;
	GunFireTimer = 0.f; // start ready to fire on enable; cleared on disable
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, bGunMode ? FColor::Yellow : FColor::Silver,
			bGunMode ? TEXT("Gun mode ON — hold Left-Click to fire") : TEXT("Gun mode OFF"));
	}
}

void AKodoPlayerController::FirePistol(ARunnerCharacter* R)
{
	if (!R)
	{
		return;
	}

	// Coarse aim point under the cursor: deproject to a grid cell, then take that cell's world center
	// as the ground-plane target (reuses the same deprojection the rest of the controller uses).
	UKodoGridSubsystem* GridSub = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
	FIntPoint AimCell;
	if (!GridSub || !GridSub->DeprojectCursorToCell(this, AimCell))
	{
		return;
	}
	const FVector AimWorld = GridSub->CellToWorldCenter(AimCell);

	const FVector Origin = R->GetActorLocation();
	const FVector AimDir = (AimWorld - Origin).GetSafeNormal2D();
	if (AimDir.IsNearlyZero())
	{
		return; // cursor on top of the hero — nothing to aim at
	}

	// Face the hero toward where it shoots (yaw only).
	R->SetActorRotation(FRotator(0.f, AimDir.Rotation().Yaw, 0.f));

	const float RangeUU = 6.f * KodoUnits::CellSizeUU; // ~6-cell pistol range
	const float HitRadiusUU = 0.7f * KodoUnits::CellSizeUU; // perpendicular "near the ray" tolerance

	// Hitscan: first alive kodo whose 2D position lies near the ray, in front, within range. Pick the
	// closest by projection length along the aim direction.
	AKodoCharacter* HitKodo = nullptr;
	float BestProj = TNumericLimits<float>::Max();
	for (TActorIterator<AKodoCharacter> It(GetWorld()); It; ++It)
	{
		AKodoCharacter* Kodo = *It;
		if (!Kodo || Kodo->IsDying())
		{
			continue;
		}
		FVector ToKodo = Kodo->GetActorLocation() - Origin;
		ToKodo.Z = 0.f;
		const float Proj = FVector::DotProduct(ToKodo, AimDir); // along-ray distance (2D, AimDir is normalized)
		if (Proj <= 0.f || Proj > RangeUU)
		{
			continue; // behind the hero or beyond range
		}
		// Perpendicular distance from the kodo to the aim line.
		const FVector Closest = Origin + AimDir * Proj;
		const float Perp = FVector::Dist2D(Kodo->GetActorLocation(), Closest);
		if (Perp <= HitRadiusUU && Proj < BestProj)
		{
			BestProj = Proj;
			HitKodo = Kodo;
		}
	}

	const float PistolDamage = 60.f;

	// Muzzle flash at the hero (always), tiny yellow burst.
	if (AKodoCastEffect* Muzzle = GetWorld()->SpawnActor<AKodoCastEffect>(Origin, FRotator::ZeroRotator))
	{
		Muzzle->Init(Origin, FLinearColor(1.f, 0.9f, 0.3f), 0.4f * KodoUnits::CellSizeUU, 0.12f);
	}

	if (HitKodo)
	{
		HitKodo->ApplyDamageAmount(PistolDamage);
		// Impact burst at the kodo, small red/orange.
		const FVector ImpactLoc = HitKodo->GetActorLocation();
		if (AKodoCastEffect* Impact = GetWorld()->SpawnActor<AKodoCastEffect>(ImpactLoc, FRotator::ZeroRotator))
		{
			Impact->Init(ImpactLoc, FLinearColor(1.f, 0.35f, 0.1f), 0.6f * KodoUnits::CellSizeUU, 0.2f);
		}
	}
	else
	{
		// Miss: small burst at the aim endpoint so the shot direction still reads.
		const FVector EndLoc = Origin + AimDir * RangeUU;
		if (AKodoCastEffect* Trail = GetWorld()->SpawnActor<AKodoCastEffect>(EndLoc, FRotator::ZeroRotator))
		{
			Trail->Init(EndLoc, FLinearColor(1.f, 0.8f, 0.4f), 0.3f * KodoUnits::CellSizeUU, 0.12f);
		}
	}
}

void AKodoPlayerController::CastHeroSkill(const int32 Slot)
{
	ARunnerCharacter* Runner = GetRunner();
	if (!Runner)
	{
		return;
	}

	// Casting (via hotkey OR the card button) selects the hero, so the bottom panel switches to the
	// hero's ability card — otherwise, with a building selected, you'd cast but never see the ability
	// or its cooldown counting down (the cooldown overlay only draws on the runner card).
	SelectionKind = EKodoSelection::Runner;
	SelectedCell = FIntPoint(-1, -1);
	SelectedKodo = nullptr;

	// Pre-check the cast gates first so a passive / locked / cooldown / no-mana ability shows a
	// message instead of silently entering (or staying in) targeting mode.
	FString Why;
	if (!Runner->CanCastSkill(Slot, Why))
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange, Why); }
		CancelPendingCast();
		return;
	}

	// Visual parity: flash the matching command-card button so a hotkey press reads like a click
	// (the button-click path also routes through here, so clicks flash identically). Fires for both
	// the instant and the targeted paths once the cast pre-check has passed.
	if (HudWidget)
	{
		HudWidget->FlashCardButton(Slot);
	}

	const EKodoCastTarget Type = Runner->GetAbilityTarget(Slot);
	if (Type == EKodoCastTarget::Instant)
	{
		// Self / instant: fire immediately at the hero's own location, no targeting step.
		Runner->CastSkillAt(Slot, Runner->GetActorLocation(), nullptr);
		CancelPendingCast();
		return;
	}

	// TargetKodo / TargetGround: arm click-to-target. The crosshair cursor + hint tell the player
	// the next left-click picks the target (right-click cancels — handled in OnMoveCommand).
	PendingCastSlot = Slot;
	PendingCastType = Type;
	CurrentMouseCursor = EMouseCursor::Crosshairs;
	if (GEngine)
	{
		const FString Hint = (Type == EKodoCastTarget::TargetKodo)
			? TEXT("Select a target (right-click to cancel)")
			: TEXT("Click a location (right-click to cancel)");
		GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Cyan, Hint);
	}
}

void AKodoPlayerController::CancelPendingCast()
{
	PendingCastSlot = -1;
	PendingCastType = EKodoCastTarget::Instant;
	CurrentMouseCursor = EMouseCursor::Default;
}

// Pass 1: the 4 hero-class hotkeys (7/8/9/0) map to the first four of the new 6-class set.
// The remaining two (Paladin, Dreadlord) get hotkeys when the start-overlay selection UI lands.
void AKodoPlayerController::OnSetHeroClass1(const FInputActionValue&)
{
	if (ARunnerCharacter* Runner = GetRunner()) { Runner->SetHeroClass(EKodoHeroClass::MountainKing); }
}
void AKodoPlayerController::OnSetHeroClass2(const FInputActionValue&)
{
	if (ARunnerCharacter* Runner = GetRunner()) { Runner->SetHeroClass(EKodoHeroClass::Blademaster); }
}
void AKodoPlayerController::OnSetHeroClass3(const FInputActionValue&)
{
	if (ARunnerCharacter* Runner = GetRunner()) { Runner->SetHeroClass(EKodoHeroClass::Archmage); }
}
void AKodoPlayerController::OnSetHeroClass4(const FInputActionValue&)
{
	if (ARunnerCharacter* Runner = GetRunner()) { Runner->SetHeroClass(EKodoHeroClass::FarSeer); }
}

void AKodoPlayerController::OnResearch1(const FInputActionValue&) { TryResearch(EKodoResearch::Stun); }
void AKodoPlayerController::OnResearch2(const FInputActionValue&) { TryResearch(EKodoResearch::Aoe); }
void AKodoPlayerController::OnResearch3(const FInputActionValue&) { TryResearch(EKodoResearch::Multishot); }
void AKodoPlayerController::OnResearch4(const FInputActionValue&) { TryResearch(EKodoResearch::Aura); }
void AKodoPlayerController::OnResearch5(const FInputActionValue&) { TryResearch(EKodoResearch::Masonry); }
void AKodoPlayerController::OnResearch6(const FInputActionValue&) { TryResearch(EKodoResearch::Axe); }
void AKodoPlayerController::OnResearch7(const FInputActionValue&) { TryResearch(EKodoResearch::GoldBonus); }

void AKodoPlayerController::OnPlace(const FInputActionValue& /*Value*/)
{
	// (Clicks over HUD panels never reach here — UMG consumes them.)
	UKodoGridSubsystem* GridSub = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
	FIntPoint Cell;
	if (!GridSub || !GridSub->DeprojectCursorToCell(this, Cell))
	{
		return;
	}

	// Targeted spell casting (Pass 5): when a target ability is armed, the next left-click picks
	// the target and casts — consuming the click (no select/build). Disabled while editing.
	if (!bEditMode && PendingCastSlot >= 0)
	{
		ARunnerCharacter* Runner = GetRunner();
		if (!Runner)
		{
			CancelPendingCast();
			return;
		}
		const FVector Loc = GridSub->CellToWorldCenter(Cell);
		if (PendingCastType == EKodoCastTarget::TargetKodo)
		{
			// Nearest active kodo to the clicked spot, within ~1.5 cells (same pick pattern as
			// selection). Miss -> keep targeting armed so the player can click again.
			AKodoCharacter* Best = nullptr;
			float BestDistSq = TNumericLimits<float>::Max();
			for (TActorIterator<AKodoCharacter> It(GetWorld()); It; ++It)
			{
				if (It->IsDying()) { continue; }
				const float D = FVector::DistSquared2D(It->GetActorLocation(), Loc);
				if (D < BestDistSq) { BestDistSq = D; Best = *It; }
			}
			if (Best && BestDistSq <= FMath::Square(KodoUnits::CellSizeUU * 1.5f))
			{
				Runner->CastSkillAt(PendingCastSlot, Loc, Best);
				CancelPendingCast();
			}
			else if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Orange, TEXT("No target there"));
			}
		}
		else // TargetGround
		{
			Runner->CastSkillAt(PendingCastSlot, Loc, nullptr);
			CancelPendingCast();
		}
		return; // consume the click — never also select/build while targeting
	}

	// TEST Gun Mode: left-click is "fire". Fire immediately on the click here (reliable — the
	// PlayerTick hold-to-repeat path depends on IsInputKeyDown, which can be flaky in GameAndUI
	// input mode), and suppress selection/build so clicking the ground doesn't also move/select.
	// (Comes after the pending-cast handling above so targeted casts still resolve; skipped in edit
	// mode so painting works.)
	if (bGunMode && !bEditMode)
	{
		if (ARunnerCharacter* GunRunner = GetRunner())
		{
			FirePistol(GunRunner);
			GunFireTimer = 1.0f; // start the 1-shot/sec cadence; holding repeats via PlayerTick
		}
		return;
	}

	// Editor paint mode: left-click paints the active tool instead of placing a blueprint
	// or making a selection. (Held-mouse drag painting is handled in PlayerTick.)
	if (bEditMode)
	{
		ApplyEditAt(Cell);
		// Seed the drag guard with the just-painted cell so the first PlayerTick drag frame
		// doesn't re-apply this same cell (a cumulative tool like raise/lower must not double-apply).
		LastPaintedCell = Cell;
		return;
	}

	// Blueprint active: build it. The hero must be next to the spot to START it; if you click
	// far away, the hero walks over, starts it on arrival (TickPendingBuild), then is free —
	// the building finishes construction on its own.
	if (!SelectedBuildPreset.IsNone())
	{
		AKodoStructureManager* Manager = GetStructureManager();
		ARunnerCharacter* Runner = GetRunner();
		if (Manager && Runner)
		{
			const FIntPoint Origin = Manager->ComputeBuildOrigin(Cell, SelectedBuildPreset);
			const FKodoStructurePreset* Preset = KodoStructures::Find(SelectedBuildPreset);
			const int32 Size = (Preset && Preset->bIs2x2) ? 2 : 1;
			const FIntPoint H = Runner->GetGridCell();
			const bool bAdjacent = H.X >= Origin.X - 1 && H.X <= Origin.X + Size &&
			                       H.Y >= Origin.Y - 1 && H.Y <= Origin.Y + Size;
			if (bAdjacent)
			{
				if (Manager->PlaceStructure(SelectedBuildPreset, Cell))
				{
					SelectedBuildPreset = NAME_None; // cleared after placement (game.js:2005)
					PendingBuildPreset = NAME_None;
				}
			}
			else
			{
				// Queue it and walk the hero to the build spot.
				TArray<FKodoPathStep> Steps;
				if (GridSub->FindPath(H, Origin, /*Size*/ 1, Steps))
				{
					Runner->CancelHarvest();
					Runner->SetPath(Steps);
					PendingBuildPreset = SelectedBuildPreset;
					PendingBuildCell = Cell;
					PendingBuildOrigin = Origin;
					SelectedBuildPreset = NAME_None; // committed to this spot; ghost hides
					if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Cyan, TEXT("Moving to build site...")); }
				}
				else if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Red, TEXT("Can't reach that build spot."));
				}
			}
		}
		return;
	}

	// Selection (README: left-click inspects Runner/structures/nodes).
	if (ARunnerCharacter* Runner = GetRunner())
	{
		const FIntPoint RunnerCell = Runner->GetGridCell();
		if (FMath::Abs(Cell.X - RunnerCell.X) <= 0 && FMath::Abs(Cell.Y - RunnerCell.Y) <= 0)
		{
			SelectionKind = EKodoSelection::Runner;
			SelectedCell = FIntPoint(-1, -1);
			SelectedKodo = nullptr;
			return;
		}
	}
	// Kodo pick: nearest active kodo to the click (inspect/highlight enemies).
	{
		AKodoCharacter* Best = nullptr;
		float BestDistSq = TNumericLimits<float>::Max();
		const FVector ClickWorld = GridSub->CellToWorldCenter(Cell);
		for (TActorIterator<AKodoCharacter> It(GetWorld()); It; ++It)
		{
			const float D = FVector::DistSquared2D(It->GetActorLocation(), ClickWorld);
			if (D < BestDistSq) { BestDistSq = D; Best = *It; }
		}
		if (Best && BestDistSq <= FMath::Square(KodoUnits::CellSizeUU * 1.2f))
		{
			SelectionKind = EKodoSelection::Kodo;
			SelectedKodo = Best;
			SelectedCell = FIntPoint(-1, -1);
			return;
		}
	}

	if (GridSub->GetCell(Cell).Type != ECellType::Empty)
	{
		SelectionKind = EKodoSelection::Cell;
		SelectedCell = Cell;
		SelectedKodo = nullptr;
		return;
	}
	ClearSelection();
}

void AKodoPlayerController::OnMoveCommand(const FInputActionValue& /*Value*/)
{
	// Targeted casting (Pass 5): right-click cancels an armed target ability instead of issuing
	// a move/erase order. Takes priority over everything else below.
	if (PendingCastSlot >= 0)
	{
		CancelPendingCast();
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Silver, TEXT("Cast cancelled")); }
		return;
	}

	// In the editor, right-click ERASES the cell under the cursor (intuitive remove), instead of
	// issuing a move/harvest order. Right-click-drag erase is handled in PlayerTick.
	if (bEditMode)
	{
		UKodoGridSubsystem* EditGrid = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
		FIntPoint EraseCellPt;
		if (EditGrid && EditGrid->DeprojectCursorToCell(this, EraseCellPt))
		{
			EraseCell(EraseCellPt);
			EditDirtyCells.Add(EraseCellPt);
			LastPaintedCell = EraseCellPt; // seed the drag guard (mirrors the LMB paint seed)
		}
		return;
	}

	// A new right-click order cancels any queued walk-to-build.
	PendingBuildPreset = NAME_None;

	// Right-click priority: cancel blueprint > harvest > move (README controls).
	if (!SelectedBuildPreset.IsNone())
	{
		SelectedBuildPreset = NAME_None;
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Silver, TEXT("Blueprint cancelled."));
		}
		return;
	}

	UKodoGridSubsystem* GridSub = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
	if (!GridSub)
	{
		return;
	}
	FIntPoint TargetCell;
	if (!GridSub->DeprojectCursorToCell(this, TargetCell))
	{
		return;
	}
	ARunnerCharacter* Runner = GetRunner();
	if (!Runner)
	{
		return;
	}

	// Attack order: right-click on a kodo orders a basic attack on it (nearest active kodo to the
	// click, within ~1.2 cells — same pick test as left-click selection). Takes priority over the
	// cell-type routing below so clicking a kodo standing on grass attacks instead of moving.
	{
		AKodoCharacter* Best = nullptr;
		float BestDistSq = TNumericLimits<float>::Max();
		const FVector ClickWorld = GridSub->CellToWorldCenter(TargetCell);
		for (TActorIterator<AKodoCharacter> It(GetWorld()); It; ++It)
		{
			if (It->IsDying()) { continue; }
			const float D = FVector::DistSquared2D(It->GetActorLocation(), ClickWorld);
			if (D < BestDistSq) { BestDistSq = D; Best = *It; }
		}
		if (Best && BestDistSq <= FMath::Square(KodoUnits::CellSizeUU * 1.2f))
		{
			Runner->OrderAttack(Best);
			return;
		}
	}

	const ECellType Type = GridSub->GetCell(TargetCell).Type;
	if (Type == ECellType::Tree)
	{
		Runner->CommandHarvest(TargetCell, EKodoHarvestKind::Tree);
		return;
	}
	if (Type == ECellType::Goldmine)
	{
		Runner->CommandHarvest(TargetCell, EKodoHarvestKind::Goldmine);
		return;
	}
	// Own building (wall/tower/CC cell): attack-to-destroy. Walls/towers are ECellType::Wall/Tower;
	// the command center occupies Tower-type cells. Natural blockers (cliff/merchant) are skipped.
	if (Type == ECellType::Wall || Type == ECellType::Tower)
	{
		Runner->OrderAttackCell(TargetCell);
		return;
	}
	if (Type != ECellType::Empty)
	{
		return;
	}

	// Empty ground: plain move order. Cancel any harvest AND any attack order first.
	Runner->CancelHarvest();
	Runner->CancelAttack();
	TArray<FKodoPathStep> Steps;
	if (GridSub->FindPath(Runner->GetGridCell(), TargetCell, /*Size*/ 1, Steps))
	{
		Runner->SetPath(Steps);
	}
}

void AKodoPlayerController::UpdateGhost()
{
	// Lazily build the shared ghost actor once; reused by both the build ghost and the editor preview.
	auto EnsureGhost = [this]() -> bool
	{
		// Cache the engine shapes the ghost switches between (cube/cone/cylinder) so BOTH the build
		// ghost and the editor preview can rely on them being loaded.
		if (!GhostCubeMesh)     { GhostCubeMesh     = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")); }
		if (!GhostConeMesh)     { GhostConeMesh     = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone")); }
		if (!GhostCylinderMesh) { GhostCylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")); }
		if (GhostActor)
		{
			return true;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GhostActor = GetWorld()->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (GhostActor)
		{
			UStaticMeshComponent* Comp = GhostActor->GetStaticMeshComponent();
			Comp->SetMobility(EComponentMobility::Movable);
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (GhostCubeMesh)
			{
				Comp->SetStaticMesh(GhostCubeMesh);
				if (UMaterialInterface* BaseMat = Comp->GetMaterial(0))
				{
					GhostMID = UMaterialInstanceDynamic::Create(BaseMat, this);
					Comp->SetMaterial(0, GhostMID);
				}
			}
		}
		return GhostActor != nullptr;
	};

	// Editor mode: preview the ACTUAL placed item under the cursor — mesh + scale + rotation +
	// tint mirror what AKodoMapBootstrapper::BuildVisuals renders for each tool (no build ghost).
	if (bEditMode)
	{
		// Lazily cache the two engine shapes the preview switches between (cube / cone).
		auto EnsureGhostMeshes = [this]()
		{
			if (!GhostCubeMesh)
			{
				GhostCubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			}
			if (!GhostConeMesh)
			{
				GhostConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
			}
		};

		// Per-tool look (matched to BuildVisuals). Engine Cube/Cone meshes are 100 UU, so a scale
		// of (Size/100) gives Size UU. EngineCube spans -50..+50 in Z about its origin, so a block
		// of height H centered at Z = H/2 sits exactly from the ground up (as the cliff wall does).
		const float CellUU = KodoUnits::CellSizeUU;             // 150
		const float StepUU = KodoUnits::ElevationLevelStepUU;   // 130 (one elevation step)

		bool bShowPreview = true;
		bool bUseCone = false;
		int32 ToolFootprint = 1;                 // cells across (2 => 2x2)
		FLinearColor ToolColor = FLinearColor::White;
		FVector ToolScale(CellUU / 100.f, CellUU / 100.f, 0.5f); // default: 1 flat cell-wide cube
		FRotator ToolRot = FRotator::ZeroRotator;
		float ZLift = 25.f;                      // extra Z so the preview reads above the terrain
		bool bGroundCentered = false;            // true => center so the block's base sits on the ground

		switch (ActiveEditTool)
		{
		case EEditTool::Ridge:
			// Tall brown cliff wall (BuildVisuals: cube, full cell wide, ~one step tall + lip).
			ToolColor = FLinearColor(0.5f, 0.33f, 0.18f);
			ToolScale = FVector(CellUU / 100.f, CellUU / 100.f, StepUU / 100.f);
			bGroundCentered = true;
			break;
		case EEditTool::Tree:
			// Green cone, ~0.85 cell wide, ~1.6 m tall (TreeInstances scale).
			ToolColor = FLinearColor(0.15f, 0.6f, 0.2f);
			bUseCone = true;
			ToolScale = FVector(0.85f, 0.85f, 1.6f);
			bGroundCentered = true;
			break;
		case EEditTool::Mine:
			// Gold low/wide cube spanning the 2x2 footprint (MineInstances are low blocks).
			ToolColor = FLinearColor(0.9f, 0.75f, 0.2f);
			ToolFootprint = 2;
			ToolScale = FVector(2.f * CellUU / 100.f, 2.f * CellUU / 100.f, 0.8f);
			bGroundCentered = true;
			break;
		case EEditTool::ElevRaise:
		case EEditTool::ElevLower:
			// Cyan flat platform step, 1 cell, ~one step tall, sitting on the current ground.
			ToolColor = FLinearColor(0.3f, 0.8f, 0.9f);
			ToolScale = FVector(CellUU / 100.f, CellUU / 100.f, StepUU / 100.f);
			bGroundCentered = true;
			break;
		case EEditTool::Ramp:
		{
			// Orange cube pitched down toward the ascent dir (BuildVisuals' ramp yaw/pitch idea).
			ToolColor = FLinearColor(0.95f, 0.6f, 0.15f);
			const int32 Dir = FMath::Clamp(EditRampDir, 0, 3);
			const float YawDeg = (Dir == 0) ? 0.f : (Dir == 1) ? 180.f : (Dir == 2) ? 90.f : 270.f;
			ToolRot = FRotator(20.f, YawDeg, 0.f); // mild downward slope wedge
			ToolScale = FVector(CellUU / 100.f, CellUU / 100.f, 0.40f);
			break;
		}
		case EEditTool::KodoSpawn:
			ToolColor = FLinearColor(1.f, 0.25f, 0.2f); // red
			ToolScale = FVector(CellUU / 100.f, CellUU / 100.f, 0.2f);
			break;
		case EEditTool::RunnerSpawn:
			ToolColor = FLinearColor(0.2f, 0.6f, 1.f); // blue
			ToolScale = FVector(CellUU / 100.f, CellUU / 100.f, 0.2f);
			break;
		case EEditTool::Merchant:
			ToolColor = FLinearColor(0.6f, 0.2f, 0.8f); // purple, 2x2
			ToolFootprint = 2;
			ToolScale = FVector(2.f * CellUU / 100.f, 2.f * CellUU / 100.f, 0.2f);
			break;
		case EEditTool::Erase:
			ToolColor = FLinearColor(0.9f, 0.2f, 0.2f); // translucent red, 1 cell
			ToolScale = FVector(CellUU / 100.f, CellUU / 100.f, 0.2f);
			break;
		case EEditTool::None:
		default:
			bShowPreview = false;
			break;
		}

		UKodoGridSubsystem* EditGrid = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
		FIntPoint Cell;
		if (!bShowPreview || !EditGrid || !EditGrid->DeprojectCursorToCell(this, Cell))
		{
			if (GhostActor) { GhostActor->SetActorHiddenInGame(true); }
			return;
		}
		if (!EnsureGhost() || !GhostActor) { return; }
		EnsureGhostMeshes();

		FVector Center = EditGrid->CellToWorldCenter(Cell);
		if (ToolFootprint == 2)
		{
			// Center the preview over the 2x2 footprint, like the build ghost does.
			Center += FVector(CellUU * 0.5f, CellUU * 0.5f, 0.f);
		}
		Center.Z += EditGrid->GetElevationZAtWorld(Center); // sit on the raised/ramped ground

		if (bGroundCentered)
		{
			// Center the body so its base rests on the ground: a cube of world height
			// (ToolScale.Z * 100) is centered at half that height (engine cube spans +/-50).
			Center.Z += ToolScale.Z * 100.f * 0.5f;
		}
		else
		{
			Center.Z += ZLift; // flat markers just read above the terrain
		}

		// Swap the mesh to match the placed item (cone for trees, cube for everything else).
		if (UStaticMeshComponent* Comp = GhostActor->GetStaticMeshComponent())
		{
			UStaticMesh* Wanted = bUseCone ? GhostConeMesh : GhostCubeMesh;
			if (Wanted && Comp->GetStaticMesh() != Wanted)
			{
				Comp->SetStaticMesh(Wanted);
			}
		}

		GhostActor->SetActorHiddenInGame(false);
		GhostActor->SetActorLocation(Center);
		GhostActor->SetActorRotation(ToolRot);
		GhostActor->SetActorScale3D(ToolScale);
		if (GhostMID)
		{
			GhostMID->SetVectorParameterValue(FName("Color"), ToolColor);
		}
		return;
	}

	UKodoGridSubsystem* GridSub = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
	AKodoStructureManager* Manager = GetStructureManager();

	if (SelectedBuildPreset.IsNone() || !GridSub || !Manager)
	{
		if (GhostActor)
		{
			GhostActor->SetActorHiddenInGame(true);
		}
		return;
	}

	FIntPoint Cell;
	if (!GridSub->DeprojectCursorToCell(this, Cell))
	{
		if (GhostActor)
		{
			GhostActor->SetActorHiddenInGame(true);
		}
		return;
	}

	if (!EnsureGhost() || !GhostActor)
	{
		return;
	}

	const FKodoStructurePreset* Preset = KodoStructures::Find(SelectedBuildPreset);
	const int32 Footprint = Preset ? FMath::Max(1, Preset->FootprintSize) : 1; // 1 / 2 / 4 — match the real build

	// Resolve the real build origin so the ghost previews exactly where PlaceStructure will land it
	// (exact cursor cell, or gold-mine master snap when hovering a mine). Same call PlaceStructure uses.
	if (Footprint > 1)
	{
		Cell = Manager->ComputeBuildOrigin(Cell, SelectedBuildPreset);
	}

	// Validity over the WHOLE footprint (placeStructure's check) — so a 4x4 needs 16 free cells.
	bool bValid = true;
	for (int32 Dx = 0; Dx < Footprint && bValid; ++Dx)
	{
		for (int32 Dy = 0; Dy < Footprint && bValid; ++Dy)
		{
			bValid = Manager->CanBuildAt(FIntPoint(Cell.X + Dx, Cell.Y + Dy), SelectedBuildPreset);
		}
	}

	// Classify the structure exactly like AKodoStructureManager::UpdateStructureVisual so the ghost's
	// SHAPE + SIZE match what actually gets built: shooting towers = one cylinder; walls/economy = cube.
	auto IdIs = [&](const TCHAR* S) { return SelectedBuildPreset == FName(S); };
	const bool bWall = IdIs(TEXT("wall")) || IdIs(TEXT("magic_wall"));
	const bool bEconomy = IdIs(TEXT("command_center")) || IdIs(TEXT("lumber_mill")) || IdIs(TEXT("mine_shaft")) ||
	                      IdIs(TEXT("upgrade_center")) || IdIs(TEXT("admin_tower")) || IdIs(TEXT("war_altar"));
	const bool bTower = !bWall && !bEconomy;

	// Center over the footprint middle (matches BuildingCenter's (Footprint-1)*0.5 nudge).
	FVector Center = GridSub->CellToWorldCenter(Cell);
	const float Nudge = (Footprint - 1) * 0.5f * KodoUnits::CellSizeUU;
	Center += FVector(Nudge, Nudge, 0.f);
	Center.Z += GridSub->GetElevationZAtWorld(Center); // sit the ghost on the raised/ramped ground

	// Mesh + scale mirror UpdateStructureVisual (engine shapes are 100 UU; scale = SizeUU/100).
	UStaticMesh* WantMesh = bTower ? GhostCylinderMesh : GhostCubeMesh;
	if (!WantMesh) { WantMesh = GhostCubeMesh; }
	FVector GhostScale;
	if (bTower)
	{
		// Towers: one cylinder filling the 2x2 footprint, ~3.4 tall (UpdateStructureVisual uses 2.85 x 3.4).
		GhostScale = FVector(2.85f, 2.85f, 3.4f);
	}
	else
	{
		// Walls + economy: a block spanning the footprint; height matches the real per-cell cubes
		// (wall ~1.6, admin tower ~4.0, other economy ~2.4).
		const float HeightZ = bWall ? 1.6f : (IdIs(TEXT("admin_tower")) ? 4.0f : 2.4f);
		GhostScale = FVector(Footprint * 1.45f, Footprint * 1.45f, HeightZ);
	}
	if (UStaticMeshComponent* Comp = GhostActor->GetStaticMeshComponent())
	{
		if (WantMesh && Comp->GetStaticMesh() != WantMesh)
		{
			Comp->SetStaticMesh(WantMesh);
		}
	}
	Center.Z += GhostScale.Z * 50.f; // engine shapes are 100 UU tall, centered → lift so the base sits on ground

	GhostActor->SetActorHiddenInGame(false);
	GhostActor->SetActorLocation(Center);
	GhostActor->SetActorRotation(FRotator::ZeroRotator);
	GhostActor->SetActorScale3D(GhostScale);

	if (GhostMID)
	{
		// BasicShapeMaterial exposes a 'Color' vector parameter.
		GhostMID->SetVectorParameterValue(FName("Color"),
		                                  bValid ? FLinearColor(0.2f, 0.9f, 0.35f) : FLinearColor(0.95f, 0.2f, 0.15f));
	}
}

void AKodoPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	float MouseX = 0.f, MouseY = 0.f;
	if (GetMousePosition(MouseX, MouseY))
	{
		int32 ViewX = 0, ViewY = 0;
		GetViewportSize(ViewX, ViewY);
		if (ViewX > 0 && ViewY > 0)
		{
			// Mouse edge scrolling (TDD §6.2), standard RTS mapping. With the
			// camera yaw pinned (north = screen-up), screen direction ==
			// world direction == minimap direction. Minimap pans live in the widget.
			FVector2D Dir = FVector2D::ZeroVector;
			if (MouseX <= EdgeScrollMarginPx)              Dir.X = -1.f;
			else if (MouseX >= ViewX - EdgeScrollMarginPx) Dir.X = 1.f;
			if (MouseY <= EdgeScrollMarginPx)              Dir.Y = -1.f;
			else if (MouseY >= ViewY - EdgeScrollMarginPx) Dir.Y = 1.f;

			if (!Dir.IsNearlyZero())
			{
				if (AKodoCameraPawn* Cam = GetCameraPawn())
				{
					Cam->AddPanInput(Dir);
				}
			}
		}
	}

	// Editor click-drag painting: while the left button is held in edit mode, keep painting
	// the cell under the cursor for the terrain/elevation/ramp tools (spawns apply on single
	// click only, handled in OnPlace). LastPaintedCell guards against repainting the same cell.
	if (bEditMode)
	{
		const bool bLeftHeld = IsInputKeyDown(EKeys::LeftMouseButton);
		const bool bDragTool = ActiveEditTool == EEditTool::Ridge || ActiveEditTool == EEditTool::Tree ||
		                       ActiveEditTool == EEditTool::Mine || ActiveEditTool == EEditTool::Erase ||
		                       ActiveEditTool == EEditTool::ElevRaise || ActiveEditTool == EEditTool::ElevLower ||
		                       ActiveEditTool == EEditTool::Ramp;
		// Right-button held erases (right-click-drag erase), mirroring the LMB drag-paint.
		const bool bRightHeld = IsInputKeyDown(EKeys::RightMouseButton);
		if (bLeftHeld && bDragTool)
		{
			UKodoGridSubsystem* GridSub = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
			FIntPoint Cell;
			if (GridSub && GridSub->DeprojectCursorToCell(this, Cell) && Cell != LastPaintedCell)
			{
				LastPaintedCell = Cell;
				ApplyEditAt(Cell);
			}
		}
		else if (bRightHeld)
		{
			UKodoGridSubsystem* GridSub = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
			FIntPoint Cell;
			if (GridSub && GridSub->DeprojectCursorToCell(this, Cell) && Cell != LastPaintedCell)
			{
				LastPaintedCell = Cell;
				EraseCell(Cell);
				EditDirtyCells.Add(Cell);
			}
		}
		else
		{
			// Neither button held: the next click starts a fresh single-cell paint (Part A — so one
			// click paints exactly one cell, with no leftover guard from a previous drag).
			LastPaintedCell = FIntPoint(-1, -1);
		}

		// Incremental per-cell visual update: only the cells edited this frame (plus their 8
		// neighbors, whose ridge-wall heights & ramp slopes depend on them) are refreshed — no
		// full-map rebuild, so dragging across many cells stays fast.
		if (EditDirtyCells.Num() > 0)
		{
			if (AKodoMapBootstrapper* B = GetBootstrapper())
			{
				if (UKodoGridSubsystem* G = GetWorld()->GetSubsystem<UKodoGridSubsystem>())
				{
					for (const FIntPoint& C : EditDirtyCells)
					{
						B->UpdateCellRegion(*G, C);
					}
				}
			}
			EditDirtyCells.Empty();
		}
	}

	// TEST Gun Mode: hold-to-fire toward the cursor (1 shot/sec, ~6-cell hitscan). Disabled while editing.
	if (bGunMode && !bEditMode)
	{
		GunFireTimer = FMath::Max(0.f, GunFireTimer - DeltaTime);
		if (ARunnerCharacter* GunRunner = GetRunner())
		{
			if (IsInputKeyDown(EKeys::LeftMouseButton) && GunFireTimer <= 0.f)
			{
				FirePistol(GunRunner);
				GunFireTimer = 1.0f; // 1 shot per second
			}
		}
	}

	GetRunner(); // keep cache warm for the HUD
	UpdateGhost();
	UpdateSelectionMarker();
	UpdateHoverCursor();
	TickPendingBuild();
}

void AKodoPlayerController::UpdateHoverCursor()
{
	// Edit mode, gun mode, and armed targeting each manage their own cursor (editor chrome / crosshair),
	// so leave it alone in those states.
	if (bEditMode || bGunMode || PendingCastSlot >= 0)
	{
		return;
	}

	UKodoGridSubsystem* GridSub = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
	FIntPoint Cell;
	if (!GridSub || !GridSub->DeprojectCursorToCell(this, Cell))
	{
		return; // cursor off the grid (e.g. over a HUD panel) — keep the current cursor
	}
	const FVector CursorWorld = GridSub->CellToWorldCenter(Cell);
	const float NearRadiusSq = FMath::Square(KodoUnits::CellSizeUU); // ~1 cell

	// Over a live kodo (within ~1 cell of the cursor world point) -> attack crosshair. Bounded search
	// reusing the nearest-kodo pattern used elsewhere (FirePistol / OnPlace).
	for (TActorIterator<AKodoCharacter> It(GetWorld()); It; ++It)
	{
		AKodoCharacter* Kodo = *It;
		if (!Kodo || Kodo->IsDying()) { continue; }
		if (FVector::DistSquared2D(Kodo->GetActorLocation(), CursorWorld) <= NearRadiusSq)
		{
			CurrentMouseCursor = EMouseCursor::Crosshairs;
			return;
		}
	}

	// Over the hero (within ~1 cell) -> hand (interact/select).
	if (ARunnerCharacter* Runner = GetRunner())
	{
		if (FVector::DistSquared2D(Runner->GetActorLocation(), CursorWorld) <= NearRadiusSq)
		{
			CurrentMouseCursor = EMouseCursor::Hand;
			return;
		}
	}

	// Over an own building (wall/tower cell) -> hand. Natural blockers (cliff/tree/mine/merchant) read as ground.
	const ECellType Type = GridSub->GetCell(Cell).Type;
	if (Type == ECellType::Wall || Type == ECellType::Tower)
	{
		CurrentMouseCursor = EMouseCursor::Hand;
		return;
	}

	// Empty ground / tree / mine / cliff: default arrow.
	CurrentMouseCursor = EMouseCursor::Default;
}

void AKodoPlayerController::TickPendingBuild()
{
	if (PendingBuildPreset.IsNone())
	{
		return;
	}
	AKodoStructureManager* Manager = GetStructureManager();
	ARunnerCharacter* Runner = GetRunner();
	if (!Manager || !Runner)
	{
		PendingBuildPreset = NAME_None;
		return;
	}

	const FKodoStructurePreset* Preset = KodoStructures::Find(PendingBuildPreset);
	const int32 Size = (Preset && Preset->bIs2x2) ? 2 : 1;
	const FIntPoint H = Runner->GetGridCell();
	const bool bAdjacent = H.X >= PendingBuildOrigin.X - 1 && H.X <= PendingBuildOrigin.X + Size &&
	                       H.Y >= PendingBuildOrigin.Y - 1 && H.Y <= PendingBuildOrigin.Y + Size;
	if (bAdjacent)
	{
		// Start construction; it finishes on its own, so the hero is free to leave.
		Manager->PlaceStructure(PendingBuildPreset, PendingBuildCell);
		Runner->SetPath(TArray<FKodoPathStep>()); // halt at the build site
		PendingBuildPreset = NAME_None;
	}
	else if (!Runner->HasPath())
	{
		// Walked as far as it could but couldn't reach the spot — abandon the order.
		PendingBuildPreset = NAME_None;
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Red, TEXT("Couldn't reach the build spot.")); }
	}
}

void AKodoPlayerController::UpdateSelectionMarker()
{
	UKodoGridSubsystem* GridSub = GetWorld()->GetSubsystem<UKodoGridSubsystem>();

	// Resolve the selected entity's ground position + footprint + ring color.
	FVector Loc = FVector::ZeroVector;
	float DiameterCells = 0.f;
	FLinearColor Color = FLinearColor::White;
	bool bShow = false;

	if (SelectionKind == EKodoSelection::Runner)
	{
		if (const ARunnerCharacter* Runner = GetRunner())
		{
			Loc = Runner->GetActorLocation();
			DiameterCells = 1.5f;
			Color = FLinearColor(0.15f, 0.95f, 0.30f); // green = hero
			bShow = true;
		}
	}
	else if (SelectionKind == EKodoSelection::Kodo)
	{
		if (AKodoCharacter* K = SelectedKodo.Get())
		{
			Loc = K->GetActorLocation();
			DiameterCells = (K->GetKodoType() != EKodoType::Speed) ? 2.7f : 1.5f;
			Color = FLinearColor(1.0f, 0.25f, 0.20f); // red = kodo
			bShow = true;
		}
		else { SelectionKind = EKodoSelection::None; } // selected kodo died
	}
	else if (SelectionKind == EKodoSelection::Cell && GridSub)
	{
		// Center on the structure's MASTER cell (top-left of a 2x2), not the clicked cell,
		// so the ring lines up with the building's visual no matter which cell was clicked.
		const FIntPoint Master = GridSub->GetMasterCell(SelectedCell);
		const FGridCell& Cell = GridSub->GetCell(Master);
		if (Cell.Type != ECellType::Empty)
		{
			const FKodoStructurePreset* Preset = KodoStructures::Find(Cell.StructureId);
			const bool b2x2 = Preset && Preset->bIs2x2;
			FVector C = GridSub->CellToWorldCenter(Master);
			if (b2x2) { C += FVector(KodoUnits::CellSizeUU * 0.5f, KodoUnits::CellSizeUU * 0.5f, 0.f); }
			Loc = C;
			DiameterCells = b2x2 ? 2.8f : 1.6f;
			Color = FLinearColor(0.20f, 0.60f, 1.0f); // blue = building
			bShow = true;
		}
		else { SelectionKind = EKodoSelection::None; } // structure gone
	}

	if (!bShow)
	{
		if (SelMarkerActor) { SelMarkerActor->SetActorHiddenInGame(true); }
		return;
	}

	// Lazy-spawn a flat disc; the selected body covers the centre so the rim reads as a ring.
	if (!SelMarkerActor)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SelMarkerActor = GetWorld()->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (SelMarkerActor)
		{
			UStaticMeshComponent* Comp = SelMarkerActor->GetStaticMeshComponent();
			Comp->SetMobility(EComponentMobility::Movable);
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
			{
				Comp->SetStaticMesh(Cyl);
				if (UMaterialInterface* BaseMat = Comp->GetMaterial(0))
				{
					SelMarkerMID = UMaterialInstanceDynamic::Create(BaseMat, this);
					Comp->SetMaterial(0, SelMarkerMID);
				}
			}
		}
	}
	if (!SelMarkerActor) { return; }

	const float GroundZ = GridSub ? GridSub->GetElevationZAtWorld(Loc) : 0.f;
	const float Diameter = DiameterCells * KodoUnits::CellSizeUU;
	SelMarkerActor->SetActorHiddenInGame(false);
	SelMarkerActor->SetActorLocation(FVector(Loc.X, Loc.Y, GroundZ + 6.f));
	// Engine cylinder is 100 UU diameter / 100 tall -> scale to a flat disc.
	SelMarkerActor->SetActorScale3D(FVector(Diameter / 100.f, Diameter / 100.f, 0.06f));
	if (SelMarkerMID)
	{
		SelMarkerMID->SetVectorParameterValue(FName("Color"), Color);
		SelMarkerMID->SetVectorParameterValue(FName("BaseColor"), Color);
	}
}
