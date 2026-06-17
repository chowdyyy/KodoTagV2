// Kodo Tag: Survivor — UE Migration, Phase 3.

#include "Camera/KodoPlayerController.h"
#include "Camera/KodoCameraPawn.h"
#include "Actors/RunnerCharacter.h"
#include "Actors/KodoCharacter.h"
#include "EngineUtils.h"
#include "Grid/KodoGridSubsystem.h"
#include "Grid/KodoStructureManager.h"
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
	CastSkill3Action = MakeBoolAction(TEXT("IA_CastSkill3"));
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
	DefaultContext->MapKey(CastSpellAction, EKeys::Q); // README: Q casts the hero spell (slot 1)
	DefaultContext->MapKey(CastSkill3Action, EKeys::R); // R casts the researched active (slot 3)
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
	EIC->BindAction(CastSkill3Action, ETriggerEvent::Started, this, &AKodoPlayerController::OnCastSkill3);
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

void AKodoPlayerController::OnSelectWall(const FInputActionValue&) { SelectBlueprint(FName("wall")); }
void AKodoPlayerController::OnSelectCommandCenter(const FInputActionValue&) { SelectBlueprint(FName("command_center")); }
void AKodoPlayerController::OnSelectBasicTower(const FInputActionValue&) { SelectBlueprint(FName("basic_tower")); }

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
	if (ARunnerCharacter* Runner = GetRunner())
	{
		Runner->CastSkill(0);
	}
}

void AKodoPlayerController::OnCastSkill3(const FInputActionValue&)
{
	if (ARunnerCharacter* Runner = GetRunner())
	{
		Runner->CastSkill(2);
	}
}

void AKodoPlayerController::CastHeroSkill(const int32 Slot)
{
	if (ARunnerCharacter* Runner = GetRunner())
	{
		Runner->CastSkill(Slot);
	}
}

void AKodoPlayerController::OnSetHeroClass1(const FInputActionValue&)
{
	if (ARunnerCharacter* Runner = GetRunner()) { Runner->SetHeroClass(EKodoHeroClass::MountainKing); }
}
void AKodoPlayerController::OnSetHeroClass2(const FInputActionValue&)
{
	if (ARunnerCharacter* Runner = GetRunner()) { Runner->SetHeroClass(EKodoHeroClass::DeathKnight); }
}
void AKodoPlayerController::OnSetHeroClass3(const FInputActionValue&)
{
	if (ARunnerCharacter* Runner = GetRunner()) { Runner->SetHeroClass(EKodoHeroClass::Blademaster); }
}
void AKodoPlayerController::OnSetHeroClass4(const FInputActionValue&)
{
	if (ARunnerCharacter* Runner = GetRunner()) { Runner->SetHeroClass(EKodoHeroClass::Tinker); }
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
	if (Type != ECellType::Empty)
	{
		return;
	}

	Runner->CancelHarvest();
	TArray<FKodoPathStep> Steps;
	if (GridSub->FindPath(Runner->GetGridCell(), TargetCell, /*Size*/ 1, Steps))
	{
		Runner->SetPath(Steps);
	}
}

void AKodoPlayerController::UpdateGhost()
{
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

	if (!GhostActor)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GhostActor = GetWorld()->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (GhostActor)
		{
			UStaticMeshComponent* Comp = GhostActor->GetStaticMeshComponent();
			Comp->SetMobility(EComponentMobility::Movable);
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
			{
				Comp->SetStaticMesh(Cube);
				if (UMaterialInterface* BaseMat = Comp->GetMaterial(0))
				{
					GhostMID = UMaterialInstanceDynamic::Create(BaseMat, this);
					Comp->SetMaterial(0, GhostMID);
				}
			}
		}
	}
	if (!GhostActor)
	{
		return;
	}

	const FKodoStructurePreset* Preset = KodoStructures::Find(SelectedBuildPreset);
	const int32 Footprint = (Preset && Preset->bIs2x2) ? 2 : 1;

	// Resolve the real build origin so the ghost previews exactly where PlaceStructure
	// will land it (tile-lattice snap, or gold-mine snap when hovering a mine).
	if (Footprint == 2)
	{
		Cell = Manager->ComputeBuildOrigin(Cell, SelectedBuildPreset);
	}

	// Validity over the whole footprint (placeStructure's check, game.js:1926-1944).
	bool bValid = true;
	for (int32 Dx = 0; Dx < Footprint && bValid; ++Dx)
	{
		for (int32 Dy = 0; Dy < Footprint && bValid; ++Dy)
		{
			bValid = Manager->CanBuildAt(FIntPoint(Cell.X + Dx, Cell.Y + Dy), SelectedBuildPreset);
		}
	}

	FVector Center = GridSub->CellToWorldCenter(Cell);
	if (Footprint == 2)
	{
		Center += FVector(KodoUnits::CellSizeUU * 0.5f, KodoUnits::CellSizeUU * 0.5f, 0.f);
	}
	Center.Z += GridSub->GetElevationZAtWorld(Center); // sit the ghost on the raised/ramped ground
	Center.Z += 25.f;

	GhostActor->SetActorHiddenInGame(false);
	GhostActor->SetActorLocation(Center);
	GhostActor->SetActorScale3D(FVector(1.5f * Footprint, 1.5f * Footprint, 0.5f));

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

	GetRunner(); // keep cache warm for the HUD
	UpdateGhost();
	UpdateSelectionMarker();
	TickPendingBuild();
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
