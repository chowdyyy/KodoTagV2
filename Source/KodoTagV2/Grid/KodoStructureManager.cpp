// Kodo Tag: Survivor — UE Migration, Phase 3.

#include "Grid/KodoStructureManager.h"
#include "Grid/KodoGridSubsystem.h"
#include "Core/KodoTint.h"
#include "Data/KodoStructureData.h"
#include "Core/KodoTagUnits.h"
#include "Core/KodoTagGameState.h"
#include "Actors/KodoWaveController.h"
#include "Actors/RunnerCharacter.h"
#include "Actors/KodoCharacter.h"
#include "Actors/KodoProjectile.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

AKodoStructureManager::AKodoStructureManager()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	CubeMesh = Cube.Succeeded() ? Cube.Object : nullptr;
	CylinderMesh = Cylinder.Succeeded() ? Cylinder.Object : nullptr;
}

void AKodoStructureManager::BeginPlay()
{
	Super::BeginPlay();
	Grid = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
	if (Grid)
	{
		CellChangedHandle = Grid->OnCellChanged.AddUObject(this, &AKodoStructureManager::OnCellChanged);

		// Initial scan: structural cells written into the grid BEFORE we subscribed (e.g. the
		// bootstrapper's admin_tower, placed in its own BeginPlay which runs first) never fired
		// OnCellChanged for us. Render + register them once here so they get visuals/exclusions.
		for (int32 X = 0; X < Grid->GetCols(); ++X)
		{
			for (int32 Y = 0; Y < Grid->GetRows(); ++Y)
			{
				const FIntPoint Coord(X, Y);
				const FGridCell& Cell = Grid->GetCell(Coord);
				if (Cell.Type == ECellType::Wall || Cell.Type == ECellType::Tower)
				{
					OnCellChanged(Coord, Cell);
				}
			}
		}
	}
}

void AKodoStructureManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Grid)
	{
		Grid->OnCellChanged.Remove(CellChangedHandle);
	}
	Super::EndPlay(EndPlayReason);
}

AKodoTagGameState* AKodoStructureManager::GetKodoGameState() const
{
	return GetWorld()->GetGameState<AKodoTagGameState>();
}

void AKodoStructureManager::ShowMessage(const FString& Text, const FColor& Color) const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, Color, Text);
	}
}

void AKodoStructureManager::RecalcAllKodoPaths()
{
	if (!CachedWaveController.IsValid())
	{
		CachedWaveController = Cast<AKodoWaveController>(
			UGameplayStatics::GetActorOfClass(GetWorld(), AKodoWaveController::StaticClass()));
	}
	if (CachedWaveController.IsValid())
	{
		CachedWaveController->RecalcAllKodoPaths();
	}
}

bool AKodoStructureManager::CanBuildAt(const FIntPoint& Cell, const FName PresetId) const
{
	if (!Grid || !Grid->IsInBounds(Cell))
	{
		return false;
	}

	// Port of canBuildAt (game.js:1884-1901).
	if (Cell.X >= 1 && Cell.X <= 5 && Cell.Y >= 1 && Cell.Y <= 5)
	{
		return false; // reserved corner (game.js:1885)
	}

	if (const ARunnerCharacter* Runner = Cast<ARunnerCharacter>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ARunnerCharacter::StaticClass())))
	{
		if (Cell == Runner->GetGridCell())
		{
			return false; // can't build under your own feet
		}
	}

	const FIntPoint MerchantCell = Grid->GetMerchantCell(); // editor spawn config
	const FVector2D MerchantDelta(Cell.X - MerchantCell.X, Cell.Y - MerchantCell.Y);
	if (MerchantDelta.Size() < 2.5f)
	{
		return false; // merchant exclusion (game.js:1891)
	}
	const FVector2D PortalDelta(Cell.X - KodoUnits::KodoRetreatCell.X, Cell.Y - KodoUnits::KodoRetreatCell.Y);
	if (PortalDelta.Size() < 2.5f)
	{
		return false; // central portal exclusion (game.js:1892)
	}

	const ECellType Type = Grid->GetCell(Cell).Type;
	if (PresetId == FName("mine_shaft"))
	{
		return Type == ECellType::Goldmine; // must sit ON a goldmine
	}
	if (PresetId == FName("basic_tower"))
	{
		return Type == ECellType::Empty || Type == ECellType::Goldmine;
	}
	return Type == ECellType::Empty;
}

FIntPoint AKodoStructureManager::ComputeBuildOrigin(const FIntPoint& Cell, const FName PresetId) const
{
	const FKodoStructurePreset* Preset = KodoStructures::Find(PresetId);
	if (!Preset || !Preset->bIs2x2 || !Grid)
	{
		return Cell;
	}
	// Over a gold mine: align to the mine's master so a basic tower (and the mine shaft
	// it morphs into) sits exactly on the mine, even when the mine is off the tile lattice.
	if (Grid->GetCell(Cell).Type == ECellType::Goldmine)
	{
		return Grid->GetMasterCell(Cell);
	}
	// 2x2 buildings place with their top-left at the EXACT cursor cell (1-cell granularity)
	// instead of snapping to the coarse even 2x2 lattice. This lets the player set a Command
	// Center / mine shaft / lumber mill directly adjacent to 1x1 things (trees, walls) with
	// no forced gap. The 2x2 footprint + per-cell CanBuildAt validation are unchanged.
	return Cell;
}

bool AKodoStructureManager::PlaceStructure(const FName PresetId, const FIntPoint& Cell)
{
	const FKodoStructurePreset* Preset = KodoStructures::Find(PresetId);
	AKodoTagGameState* GS = GetKodoGameState();
	if (!Preset || !GS || !Grid)
	{
		return false;
	}

	// Walls and the CC are free to place (game.js:1908-1910).
	const bool bIsFree = PresetId == FName("wall") || PresetId == FName("command_center");
	const float GoldCost = bIsFree ? 0.f : Preset->GoldCost;
	const float WoodCost = bIsFree ? 0.f : Preset->WoodCost;

	if (GS->Gold < GoldCost) { ShowMessage(TEXT("Not enough gold!"), FColor::Red); return false; }
	if (GS->Wood < WoodCost) { ShowMessage(TEXT("Not enough lumber wood!"), FColor::Red); return false; }

	if (!Preset->RequiresUpgrade.IsNone() && !GS->Upgrades.IsUnlocked(Preset->RequiresUpgrade))
	{
		ShowMessage(FString::Printf(TEXT("TOWER LOCKED! Requires %s research at the Upgrade Center."), *Preset->DisplayName),
		            FColor::Orange);
		return false;
	}

	// Footprint validation (game.js:1926-1944). 2x2 buildings place at the exact cursor
	// cell (ComputeBuildOrigin no longer snaps to the coarse lattice), so they can sit
	// tightly adjacent to 1x1 trees/walls; every footprint cell is still validated below.
	const int32 Footprint = Preset->bIs2x2 ? 2 : 1;
	const FIntPoint Origin = ComputeBuildOrigin(Cell, PresetId);
	for (int32 Dx = 0; Dx < Footprint; ++Dx)
	{
		for (int32 Dy = 0; Dy < Footprint; ++Dy)
		{
			const FIntPoint Test(Origin.X + Dx, Origin.Y + Dy);
			if (!Grid->IsInBounds(Test) || !CanBuildAt(Test, PresetId))
			{
				ShowMessage(Preset->bIs2x2 ? TEXT("Invalid 2x2 build location!") : TEXT("Invalid build location!"),
				            FColor::Red);
				return false;
			}
		}
	}

	GS->Spend(GoldCost, WoodCost);

	const FKodoStructureStats Stats = KodoStructures::GetStatsForLevel(PresetId, 1);

	// Masonry HP bonus + Mountain King's x1.20 (game.js:1950-1953).
	float HpMult = 1.f + (GS->Upgrades.MasonryLvl - 1) * 0.35f;
	if (const ARunnerCharacter* Runner = Cast<ARunnerCharacter>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ARunnerCharacter::StaticClass())))
	{
		if (Runner->GetHeroClass() == EKodoHeroClass::MountainKing && Runner->IsSkill2Unlocked())
		{
			HpMult *= 1.20f;
		}
	}
	const float FinalHp = FMath::RoundToFloat(Stats.MaxHp * HpMult);
	const float Duration = Preset->ConstructionSeconds;

	for (int32 Dx = 0; Dx < Footprint; ++Dx)
	{
		for (int32 Dy = 0; Dy < Footprint; ++Dy)
		{
			const FIntPoint Target(Origin.X + Dx, Origin.Y + Dy);
			FGridCell NewCell;
			NewCell.Type = (PresetId == FName("wall") || PresetId == FName("magic_wall")) ? ECellType::Wall : ECellType::Tower;
			NewCell.Hp = FinalHp;
			NewCell.MaxHp = FinalHp;
			NewCell.Level = 1;
			NewCell.StructureId = PresetId;
			NewCell.bUnderConstruction = true;
			NewCell.bWasGoldMine = Grid->GetCell(Target).Type == ECellType::Goldmine || Grid->GetCell(Target).bWasGoldMine;
			NewCell.MasterCell = Preset->bIs2x2 ? Origin : Target;
			Grid->SetCell(Target, NewCell);

			Constructions.Add({ Target, 0.f, Duration });
		}
	}

	// Structures invalidate every Kodo's current path (game.js:2003).
	RecalcAllKodoPaths();
	return true;
}

namespace
{
	// Cost (gold/wood) + research TIME (seconds) per upgrade.
	void ResearchInfo(const EKodoResearch Type, float& Gold, float& Wood, float& Seconds)
	{
		switch (Type)
		{
		case EKodoResearch::Stun:       Gold = 150.f; Wood =  80.f; Seconds = 20.f; break;
		case EKodoResearch::Aoe:        Gold = 200.f; Wood = 100.f; Seconds = 25.f; break;
		case EKodoResearch::Multishot:  Gold = 220.f; Wood = 120.f; Seconds = 25.f; break;
		case EKodoResearch::Aura:       Gold = 250.f; Wood = 150.f; Seconds = 30.f; break;
		case EKodoResearch::Masonry:    Gold = 120.f; Wood =  60.f; Seconds = 18.f; break;
		case EKodoResearch::Axe:        Gold =  80.f; Wood =  30.f; Seconds = 15.f; break;
		case EKodoResearch::GoldBonus:  Gold = 100.f; Wood =  40.f; Seconds = 18.f; break;
		case EKodoResearch::HeroSkill2: Gold = 120.f; Wood =  60.f; Seconds = 25.f; break;
		case EKodoResearch::HeroSkill3: Gold = 200.f; Wood = 100.f; Seconds = 35.f; break;
		case EKodoResearch::ManaRegen:  Gold = 200.f; Wood =   0.f; Seconds = 20.f; break;
		}
	}

	FString ResearchName(const EKodoResearch Type)
	{
		switch (Type)
		{
		case EKodoResearch::Stun:       return TEXT("Stun Powder");
		case EKodoResearch::Aoe:        return TEXT("Mortar Shells");
		case EKodoResearch::Multishot:  return TEXT("Multishot Bow");
		case EKodoResearch::Aura:       return TEXT("Decay Spores");
		case EKodoResearch::Masonry:    return TEXT("Masonry");
		case EKodoResearch::Axe:        return TEXT("Lumber Axes");
		case EKodoResearch::GoldBonus:  return TEXT("Gold Bonus");
		case EKodoResearch::HeroSkill2: return TEXT("Hero Skill 2");
		case EKodoResearch::ManaRegen:  return TEXT("Mana Regen");
		default:                        return TEXT("Hero Skill 3");
		}
	}
}

bool AKodoStructureManager::IsResearching(const EKodoResearch Type) const
{
	for (const FActiveResearch& R : ActiveResearches)
	{
		if (R.Type == Type) { return true; }
	}
	return false;
}

bool AKodoStructureManager::GetActiveResearch(FString& OutName, float& OutFrac, float& OutRemaining) const
{
	if (ActiveResearches.Num() == 0)
	{
		return false;
	}
	const FActiveResearch& R = ActiveResearches[0];
	OutName = ResearchName(R.Type);
	OutRemaining = R.TimeRemaining;
	OutFrac = R.TotalTime > 0.f ? (R.TotalTime - R.TimeRemaining) / R.TotalTime : 0.f; // elapsed fraction
	return true;
}

float AKodoStructureManager::GetResearchProgress(const EKodoResearch Type, float& OutRemaining) const
{
	for (const FActiveResearch& R : ActiveResearches)
	{
		if (R.Type == Type)
		{
			OutRemaining = R.TimeRemaining;
			return R.TotalTime > 0.f ? (R.TotalTime - R.TimeRemaining) / R.TotalTime : 0.f; // elapsed fraction 0..1
		}
	}
	return -1.f; // this type isn't currently researching
}

bool AKodoStructureManager::Research(const EKodoResearch Type)
{
	// START a timed research (port of researchCCUpgrade, game.js:2520-2578). The effect is applied
	// after the research time elapses (see Tick -> ApplyResearchEffect); the hero need not be near.
	AKodoTagGameState* GS = GetKodoGameState();
	if (!GS || !Grid)
	{
		return false;
	}

	// GATE by purpose: OFFENSIVE upgrades research at the Upgrade Center; ECONOMY/support at the CC.
	const bool bOffensive = (Type == EKodoResearch::Stun || Type == EKodoResearch::Aoe ||
	                         Type == EKodoResearch::Multishot || Type == EKodoResearch::Aura ||
	                         Type == EKodoResearch::HeroSkill2 || Type == EKodoResearch::HeroSkill3 ||
	                         Type == EKodoResearch::ManaRegen);
	{
		const FName RequiredId = bOffensive ? FName(TEXT("upgrade_center")) : FName(TEXT("command_center"));
		bool bHasBuilding = false;
		for (int32 X = 0; X < Grid->GetCols() && !bHasBuilding; ++X)
		{
			for (int32 Y = 0; Y < Grid->GetRows(); ++Y)
			{
				if (Grid->GetCell(FIntPoint(X, Y)).StructureId == RequiredId) { bHasBuilding = true; break; }
			}
		}
		if (!bHasBuilding)
		{
			ShowMessage(bOffensive ? TEXT("Build an Upgrade Center to research combat upgrades.")
			                       : TEXT("Build a Command Center to research economy upgrades."), FColor::Orange);
			return false;
		}
	}

	// Already at max / unlocked?
	const FKodoUpgrades& Up = GS->Upgrades;
	bool bDone = false;
	switch (Type)
	{
	case EKodoResearch::Stun:      bDone = Up.bStunUnlocked; break;
	case EKodoResearch::Aoe:       bDone = Up.bAoeUnlocked; break;
	case EKodoResearch::Multishot: bDone = Up.bMultishotUnlocked; break;
	case EKodoResearch::Aura:      bDone = Up.bAuraUnlocked; break;
	case EKodoResearch::Masonry:   bDone = Up.MasonryLvl >= 3; break;
	case EKodoResearch::Axe:       bDone = Up.AxeLvl >= 3; break;
	case EKodoResearch::GoldBonus: bDone = Up.GoldBonusLvl >= 3; break;
	case EKodoResearch::HeroSkill2:
	case EKodoResearch::HeroSkill3:
	{
		const ARunnerCharacter* Runner = Cast<ARunnerCharacter>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ARunnerCharacter::StaticClass()));
		if (!Runner) { return false; }
		bDone = (Type == EKodoResearch::HeroSkill2) ? Runner->IsSkill2Unlocked() : Runner->IsSkill3Unlocked();
		break;
	}
	case EKodoResearch::ManaRegen:
	{
		const ARunnerCharacter* Runner = Cast<ARunnerCharacter>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ARunnerCharacter::StaticClass()));
		if (!Runner) { return false; }
		bDone = Runner->IsManaRegenUpgraded();
		break;
	}
	}
	if (bDone) { ShowMessage(TEXT("Already researched."), FColor::Orange); return false; }
	if (IsResearching(Type)) { ShowMessage(TEXT("Already researching that."), FColor::Orange); return false; }

	float Gold = 0.f, Wood = 0.f, Seconds = 10.f;
	ResearchInfo(Type, Gold, Wood, Seconds);
	if (!GS->CanAfford(Gold, Wood))
	{
		ShowMessage(TEXT("Missing Gold or Lumber requirements!"), FColor::Red);
		return false;
	}

	// Charge up-front; the effect lands when the timer finishes.
	GS->Spend(Gold, Wood);
	FActiveResearch R; R.Type = Type; R.TimeRemaining = Seconds; R.TotalTime = Seconds;
	ActiveResearches.Add(R);
	ShowMessage(FString::Printf(TEXT("Researching %s... (%.0fs)"), *ResearchName(Type), Seconds), FColor::Cyan);
	return true;
}

void AKodoStructureManager::ApplyResearchEffect(const EKodoResearch Type)
{
	AKodoTagGameState* GS = GetKodoGameState();
	if (!GS || !Grid)
	{
		return;
	}
	FKodoUpgrades& Up = GS->Upgrades;

	switch (Type)
	{
	case EKodoResearch::Stun:      Up.bStunUnlocked = true; break;
	case EKodoResearch::Aoe:       Up.bAoeUnlocked = true; break;
	case EKodoResearch::Multishot: Up.bMultishotUnlocked = true; break;
	case EKodoResearch::Aura:      Up.bAuraUnlocked = true; break;
	case EKodoResearch::Axe:       Up.AxeLvl = FMath::Min(Up.AxeLvl + 1, 3); break;
	case EKodoResearch::GoldBonus: Up.GoldBonusLvl = FMath::Min(Up.GoldBonusLvl + 1, 3); break;
	case EKodoResearch::Masonry:
		Up.MasonryLvl = FMath::Min(Up.MasonryLvl + 1, 3);
		{
			// Retroactive x1.35 to everything except CCs (game.js:2551-2559).
			static const FName CommandCenterId(TEXT("command_center"));
			for (int32 X = 0; X < Grid->GetCols(); ++X)
			{
				for (int32 Y = 0; Y < Grid->GetRows(); ++Y)
				{
					const FIntPoint Coord(X, Y);
					FGridCell Cell = Grid->GetCell(Coord);
					if (Cell.Type != ECellType::Empty && Cell.StructureId != CommandCenterId)
					{
						Cell.MaxHp = FMath::RoundToFloat(Cell.MaxHp * 1.35f);
						Cell.Hp = FMath::RoundToFloat(Cell.Hp * 1.35f);
						Grid->SetCell(Coord, Cell);
					}
				}
			}
		}
		break;
	case EKodoResearch::HeroSkill2:
	case EKodoResearch::HeroSkill3:
		if (ARunnerCharacter* Runner = Cast<ARunnerCharacter>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ARunnerCharacter::StaticClass())))
		{
			if (Type == EKodoResearch::HeroSkill2) { Runner->UnlockSkill2(); } else { Runner->UnlockSkill3(); }
		}
		break;
	case EKodoResearch::ManaRegen:
		if (ARunnerCharacter* Runner = Cast<ARunnerCharacter>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ARunnerCharacter::StaticClass())))
		{
			Runner->UpgradeManaRegen();
		}
		break;
	}
	ShowMessage(FString::Printf(TEXT("%s complete!"), *ResearchName(Type)), FColor::Green);
}

void AKodoStructureManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Grid)
	{
		return;
	}

	// 0. Timed research: count each in-progress upgrade down; apply its effect on completion.
	for (int32 i = ActiveResearches.Num() - 1; i >= 0; --i)
	{
		ActiveResearches[i].TimeRemaining -= DeltaSeconds;
		if (ActiveResearches[i].TimeRemaining <= 0.f)
		{
			const EKodoResearch Done = ActiveResearches[i].Type;
			ActiveResearches.RemoveAt(i);
			ApplyResearchEffect(Done);
		}
	}

	// 1. Construction progress (game.js:1217-1230). Progress is local; the grid
	// cell only changes once, on completion.
	for (int32 i = Constructions.Num() - 1; i >= 0; --i)
	{
		FConstructionEntry& Entry = Constructions[i];
		FGridCell State = Grid->GetCell(Entry.Cell);

		// Structure was destroyed/replaced mid-construction.
		if ((State.Type != ECellType::Wall && State.Type != ECellType::Tower) || !State.bUnderConstruction)
		{
			Constructions.RemoveAt(i);
			continue;
		}

		Entry.Elapsed += DeltaSeconds;
		const float Alpha = FMath::Clamp(Entry.Elapsed / Entry.Duration, 0.f, 1.f);
		UpdateStructureVisual(Entry.Cell, State, Alpha);

		if (Entry.Elapsed >= Entry.Duration)
		{
			State.bUnderConstruction = false;
			Grid->SetCell(Entry.Cell, State);
			Constructions.RemoveAt(i);
		}
	}

	// 1b. Tower targeting & shooting (game.js:1272-1312).
	TickTowerCombat(DeltaSeconds);
	TickTowerFx(DeltaSeconds);

	// 1c. Death Knight passive: +2 HP/s to all structures except goldmines/trees
	// (game.js:1328-1342).
	const ARunnerCharacter* RegenRunner = Cast<ARunnerCharacter>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ARunnerCharacter::StaticClass()));
	if (RegenRunner && RegenRunner->GetHeroClass() == EKodoHeroClass::DeathKnight &&
	    RegenRunner->IsSkill2Unlocked())
	{
		DkRegenTimer += DeltaSeconds;
		if (DkRegenTimer >= 1.f)
		{
			DkRegenTimer = 0.f;
			static const FName GoldmineId(TEXT("goldmine"));
			static const FName TreeId(TEXT("tree"));
			for (int32 X = 0; X < Grid->GetCols(); ++X)
			{
				for (int32 Y = 0; Y < Grid->GetRows(); ++Y)
				{
					const FIntPoint Coord(X, Y);
					FGridCell Cell = Grid->GetCell(Coord);
					if (Cell.Type != ECellType::Empty && Cell.StructureId != GoldmineId &&
					    Cell.StructureId != TreeId && Cell.Hp < Cell.MaxHp)
					{
						Cell.Hp = FMath::Min(Cell.MaxHp, Cell.Hp + 2.f);
						Grid->SetCell(Coord, Cell);
					}
				}
			}
		}
	}

	// 2. Passive income, 1 s cadence (game.js:1232-1270).
	EconomyTick += DeltaSeconds;
	if (EconomyTick >= 1.f)
	{
		EconomyTick = 0.f;
		AKodoTagGameState* GS = GetKodoGameState();
		if (!GS)
		{
			return;
		}

		static const FName MineShaftId(TEXT("mine_shaft"));
		static const FName LumberMillId(TEXT("lumber_mill"));

		// Recompute supply upkeep fresh each tick by summing Food over every built
		// structure cell. This is the single source of truth for SupplyUsed, so it
		// stays correct whether a structure was placed, sold, or destroyed by Kodos
		// (the grid clears destroyed cells; we just re-sum). NOTE: 2x2 buildings sum
		// Food per occupied cell, matching the per-cell income model above.
		float SupplyAccum = 0.f;

		for (int32 X = 0; X < Grid->GetCols(); ++X)
		{
			for (int32 Y = 0; Y < Grid->GetRows(); ++Y)
			{
				const FGridCell& Cell = Grid->GetCell(FIntPoint(X, Y));

				// Upkeep: count Food for any wall/tower structure cell (built or building).
				if (Cell.Type == ECellType::Wall || Cell.Type == ECellType::Tower)
				{
					if (const FKodoStructurePreset* FoodPreset = KodoStructures::Find(Cell.StructureId))
					{
						SupplyAccum += FoodPreset->Food;
					}
				}

				if (Cell.Type != ECellType::Tower || Cell.bUnderConstruction)
				{
					continue;
				}

				// NOTE: the prototype pays PER CELL, so a 2x2 mine shaft yields
				// 4x the listed rate. Ported bug-for-bug (game.js:1237-1267).
				if (Cell.StructureId == MineShaftId)
				{
					GS->Gold += (Cell.Level == 1) ? 12.f : (Cell.Level == 2) ? 22.f : 40.f;
				}
				else if (Cell.StructureId == LumberMillId)
				{
					int32 AdjacentTrees = 0;
					for (int32 Dx = -1; Dx <= 1; ++Dx)
					{
						for (int32 Dy = -1; Dy <= 1; ++Dy)
						{
							const FIntPoint Test(X + Dx, Y + Dy);
							if (Grid->IsInBounds(Test) && Grid->GetCell(Test).Type == ECellType::Tree)
							{
								++AdjacentTrees;
							}
						}
					}
					if (AdjacentTrees > 0)
					{
						GS->Wood += AdjacentTrees * 3.f * GS->Upgrades.AxeLvl;
					}
				}
			}
		}

		// Commit the freshly-summed supply upkeep (single source of truth for SupplyUsed).
		GS->SupplyUsed = FMath::RoundToInt(SupplyAccum);
	}
}

bool AKodoStructureManager::MorphBasicTower(const FIntPoint& Cell, const FName TargetId)
{
	AKodoTagGameState* GS = GetKodoGameState();
	if (!GS || !Grid)
	{
		return false;
	}
	// Resolve to the master so a click on any of the tower's four cells morphs the whole building.
	const FIntPoint Master = Grid->GetMasterCell(Cell);
	FGridCell Current = Grid->GetCell(Master);
	if (Current.StructureId != FName("basic_tower"))
	{
		return false;
	}

	// Morph cost/requirement table (game.js:2424-2433).
	struct FMorphRow { FName Id; float Gold; float Wood; FName Requires; };
	static const FMorphRow Morphs[] = {
		{ "arrow", 10.f, 0.f, NAME_None },
		{ "frost", 25.f, 0.f, NAME_None },
		{ "stun", 40.f, 0.f, "stunUnlocked" },
		{ "aoe", 55.f, 0.f, "aoeUnlocked" },
		{ "multishot", 60.f, 0.f, "multishotUnlocked" },
		{ "aura", 80.f, 0.f, "auraUnlocked" },
		{ "lumber_mill", 60.f, 20.f, NAME_None },
		{ "mine_shaft", 80.f, 50.f, NAME_None },
	};
	const FMorphRow* Row = nullptr;
	for (const FMorphRow& Candidate : Morphs)
	{
		if (Candidate.Id == TargetId)
		{
			Row = &Candidate;
			break;
		}
	}
	if (!Row)
	{
		return false;
	}

	if (!Row->Requires.IsNone() && !GS->Upgrades.IsUnlocked(Row->Requires))
	{
		ShowMessage(TEXT("Requires Upgrade Center research!"), FColor::Orange);
		return false;
	}
	if (TargetId == FName("mine_shaft") && !Current.bWasGoldMine)
	{
		ShowMessage(TEXT("Mine Shafts need a gold mine underneath!"), FColor::Orange);
		return false;
	}
	if (!GS->CanAfford(Row->Gold, Row->Wood))
	{
		ShowMessage(TEXT("Not enough resources!"), FColor::Red);
		return false;
	}

	// Morph replaces the basic tower in place, reusing its own footprint (combat towers
	// are now 1x1 so they form the maze; the loop below still honors bIs2x2 if it changes).
	const FKodoStructurePreset* TargetPreset = KodoStructures::Find(TargetId);
	const bool bIs2x2 = TargetPreset && TargetPreset->bIs2x2;

	GS->Spend(Row->Gold, Row->Wood);

	const FKodoStructureStats Stats = KodoStructures::GetStatsForLevel(TargetId, 1);
	float HpMult = 1.f + (GS->Upgrades.MasonryLvl - 1) * 0.35f;
	if (const ARunnerCharacter* Runner = Cast<ARunnerCharacter>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ARunnerCharacter::StaticClass())))
	{
		if (Runner->GetHeroClass() == EKodoHeroClass::MountainKing && Runner->IsSkill2Unlocked())
		{
			HpMult *= 1.20f;
		}
	}
	const float FinalHp = FMath::RoundToFloat(Stats.MaxHp * HpMult);

	const int32 Footprint = bIs2x2 ? 2 : 1;
	for (int32 Dx = 0; Dx < Footprint; ++Dx)
	{
		for (int32 Dy = 0; Dy < Footprint; ++Dy)
		{
			const FIntPoint Target(Master.X + Dx, Master.Y + Dy);
			if (!Grid->IsInBounds(Target))
			{
				continue; // 2x2 replacements are bounds-checked per cell (game.js:2088)
			}
			FGridCell NewCell;
			NewCell.Type = ECellType::Tower;
			NewCell.StructureId = TargetId;
			NewCell.Level = 1;
			NewCell.Hp = FinalHp;
			NewCell.MaxHp = FinalHp;
			NewCell.bUnderConstruction = true;
			NewCell.bWasGoldMine = Grid->GetCell(Target).Type == ECellType::Goldmine ||
				Grid->GetCell(Target).bWasGoldMine;
			NewCell.MasterCell = bIs2x2 ? Master : Target;
			Grid->SetCell(Target, NewCell);
			Constructions.Add({ Target, 0.f, 3.f }); // morphs build in 3.0 s (game.js:2066/2100/2123)
		}
	}
	ShowMessage(FString::Printf(TEXT("Upgrading to %s!"), *Stats.DisplayName), FColor::Green);
	return true;
}

bool AKodoStructureManager::UpgradeStructureTier(const FIntPoint& Cell)
{
	// Port of upgradeStructure (game.js:2131-2166): instant, HP ratio preserved.
	AKodoTagGameState* GS = GetKodoGameState();
	if (!GS || !Grid)
	{
		return false;
	}
	const FIntPoint Master = Grid->GetMasterCell(Cell);
	FGridCell Current = Grid->GetCell(Master);
	if (Current.Type == ECellType::Empty)
	{
		return false;
	}
	const FKodoStructurePreset* Preset = KodoStructures::Find(Current.StructureId);
	if (!Preset || Current.Level >= 3 || Current.Level >= Preset->Levels.Num())
	{
		ShowMessage(TEXT("Structure at maximum tier!"), FColor::Orange);
		return false;
	}

	const int32 NextLevel = Current.Level + 1;
	const FKodoStructureStats NextStats = KodoStructures::GetStatsForLevel(Current.StructureId, NextLevel);
	// Walls upgrade free in the prototype despite listed costs (game.js:2143) — ported bug-for-bug.
	const float Cost = Current.StructureId == FName("wall") ? 0.f : NextStats.GoldCost;
	if (GS->Gold < Cost)
	{
		ShowMessage(TEXT("Not enough gold!"), FColor::Red);
		return false;
	}
	GS->Spend(Cost, 0.f);

	float HpMult = 1.f + (GS->Upgrades.MasonryLvl - 1) * 0.35f;
	if (const ARunnerCharacter* Runner = Cast<ARunnerCharacter>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ARunnerCharacter::StaticClass())))
	{
		if (Runner->GetHeroClass() == EKodoHeroClass::MountainKing && Runner->IsSkill2Unlocked())
		{
			HpMult *= 1.20f;
		}
	}

	const float HpRatio = Current.MaxHp > 0.f ? Current.Hp / Current.MaxHp : 1.f;
	const float NewMaxHp = FMath::RoundToFloat(NextStats.MaxHp * HpMult);
	const float NewHp = FMath::RoundToFloat(NewMaxHp * HpRatio);

	// Bump every footprint cell so a 2x2 building's tier/HP stay in lockstep.
	for (int32 Dx = 0; Dx <= 1; ++Dx)
	{
		for (int32 Dy = 0; Dy <= 1; ++Dy)
		{
			const FIntPoint T(Master.X + Dx, Master.Y + Dy);
			if (!Grid->IsInBounds(T))
			{
				continue;
			}
			FGridCell C = Grid->GetCell(T);
			if (C.Type == ECellType::Empty || (T != Master && C.MasterCell != Master))
			{
				continue;
			}
			C.Level = NextLevel;
			C.MaxHp = NewMaxHp;
			C.Hp = NewHp;
			Grid->SetCell(T, C);
		}
	}

	ShowMessage(FString::Printf(TEXT("Upgraded to %s!"), *NextStats.DisplayName), FColor::Green);
	return true;
}

bool AKodoStructureManager::SellStructure(const FIntPoint& Cell)
{
	// Port of sellStructure (game.js:2168-2183). Resolve to the master so selling any
	// cell of a 2x2 building refunds once and clears the whole footprint.
	AKodoTagGameState* GS = GetKodoGameState();
	if (!GS || !Grid)
	{
		return false;
	}
	const FIntPoint Master = Grid->GetMasterCell(Cell);
	const FGridCell Current = Grid->GetCell(Master);
	if (Current.Type != ECellType::Wall && Current.Type != ECellType::Tower)
	{
		return false;
	}
	const FKodoStructurePreset* Preset = KodoStructures::Find(Current.StructureId);
	if (!Preset)
	{
		return false;
	}

	float TotalCost = Preset->GoldCost;
	for (int32 Level = 2; Level <= Current.Level; ++Level)
	{
		TotalCost += KodoStructures::GetStatsForLevel(Current.StructureId, Level).GoldCost;
	}
	const float Refund = FMath::FloorToFloat(TotalCost * 0.6f);
	GS->Gold += Refund;

	Grid->ClearStructure(Master);
	RecalcAllKodoPaths();
	ShowMessage(FString::Printf(TEXT("Refunded +%.0fg"), Refund), FColor::Yellow);
	return true;
}

void AKodoStructureManager::TickTowerCombat(const float DeltaSeconds)
{
	// Port of the tower shooting loop (game.js:1272-1312). Shooting towers are
	// type Tower, finished, and not CC/mine shaft/lumber mill.
	static const FName CommandCenterId(TEXT("command_center"));
	static const FName MineShaftId(TEXT("mine_shaft"));
	static const FName LumberMillId(TEXT("lumber_mill"));
	static const FName UpgradeCenterId(TEXT("upgrade_center"));
	static const FName AdminTowerId(TEXT("admin_tower"));
	static const FName AuraSpecial(TEXT("aura"));
	static const FName MultishotSpecial(TEXT("multishot"));
	static const FName StunSpecial(TEXT("stun"));

	// Nothing can shoot -> skip the Kodo gather and the whole pass entirely.
	if (ActiveTowerCells.Num() == 0)
	{
		return;
	}

	// God mode buffs tower output: resolve the multiplier once per tick (default 1).
	const AKodoTagGameState* GS = GetWorld() ? GetWorld()->GetGameState<AKodoTagGameState>() : nullptr;
	const float DamageMult = GS ? GS->TowerDamageMult : 1.f;

	// Gather living Kodos once per frame.
	TArray<AKodoCharacter*> Kodos;
	for (TActorIterator<AKodoCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsDying())
		{
			Kodos.Add(*It);
		}
	}

	// Iterate only live weapons (maintained incrementally in OnCellChanged) instead of
	// rescanning all 160x160 cells every frame.
	for (auto It = ActiveTowerCells.CreateIterator(); It; ++It)
	{
		{
			const FIntPoint Coord = *It;
			const FGridCell& Cell = Grid->GetCell(Coord);

			// Defensive: the set is incrementally maintained, but drop anything that is no
			// longer a finished weapon at its own master cell (e.g. a mid-frame edge case).
			if (Cell.Type != ECellType::Tower || Cell.bUnderConstruction || Cell.MasterCell != Coord ||
			    Cell.StructureId == CommandCenterId || Cell.StructureId == MineShaftId ||
			    Cell.StructureId == LumberMillId || Cell.StructureId == UpgradeCenterId ||
			    Cell.StructureId == AdminTowerId)
			{
				TowerCooldowns.Remove(Coord);
				It.RemoveCurrent();
				continue;
			}

			float& Cooldown = TowerCooldowns.FindOrAdd(Coord);
			if (Cooldown > 0.f)
			{
				Cooldown -= DeltaSeconds;
				continue;
			}
			if (Kodos.Num() == 0)
			{
				continue;
			}

			FKodoStructureStats Stats = KodoStructures::GetStatsForLevel(Cell.StructureId, Cell.Level);
			Stats.Damage *= DamageMult; // God mode: apply to aura pulse + projectiles alike
			const FKodoStructurePreset* Preset = KodoStructures::Find(Cell.StructureId);
			const FName Special = Preset ? Preset->Special : NAME_None;

			const FVector TowerCenter = BuildingCenter(Coord, Cell.StructureId);
			const float RangeUU = Stats.RangeTiles * KodoUnits::CellSizeUU;

			TArray<AKodoCharacter*> Targets;
			for (AKodoCharacter* Kodo : Kodos)
			{
				if (IsValid(Kodo) && FVector::Dist2D(Kodo->GetActorLocation(), TowerCenter) <= RangeUU)
				{
					Targets.Add(Kodo);
				}
			}
			if (Targets.Num() == 0)
			{
				continue;
			}

			if (Special == AuraSpecial)
			{
				// Direct pulse: ALL Kodos in range, no projectile (game.js:1287-1293).
				for (AKodoCharacter* Target : Targets)
				{
					Target->ApplyDamageAmount(Stats.Damage);
				}
				DrawDebugCircle(GetWorld(), TowerCenter + FVector(0, 0, 30.f), RangeUU, 32,
				                FColor::Purple, false, 0.3f, 0, 6.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
				Cooldown = Stats.CooldownSeconds;
			}
			else
			{
				Targets.Sort([&TowerCenter](const AKodoCharacter& A, const AKodoCharacter& B)
				{
					return FVector::Dist2D(A.GetActorLocation(), TowerCenter) <
						FVector::Dist2D(B.GetActorLocation(), TowerCenter);
				});

				// Phase 5: aim the tower head and kick off recoil.
				{
					const FVector Dir = (Targets[0]->GetActorLocation() - TowerCenter).GetSafeNormal2D();
					FTowerFireFx& Fx = TowerFx.FindOrAdd(Coord);
					Fx.RecoilTimer = 0.18f;
					Fx.FireDirection = Dir;
					if (const TObjectPtr<UStaticMeshComponent>* Head = TowerHeads.Find(Coord); Head && Head->Get())
					{
						const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
						Head->Get()->SetWorldRotation(FRotator(0.f, Yaw, 0.f));
					}
				}

				if (Special == MultishotSpecial)
				{
					// Up to maxTargets simultaneous arrows (game.js:1294-1301).
					const int32 MaxTargets = Stats.MaxTargets > 0 ? Stats.MaxTargets : 3;
					for (int32 i = 0; i < FMath::Min(MaxTargets, Targets.Num()); ++i)
					{
						FireProjectile(TowerCenter, Targets[i], Stats);
					}
				}
				else
				{
					FireProjectile(TowerCenter, Targets[0], Stats);

					// Stun tower: 3 jagged lightning arcs tower -> target (game.js:1455-1461).
					if (Special == StunSpecial)
					{
						const FVector From = TowerCenter + FVector(0, 0, 300.f);
						const FVector To = Targets[0]->GetActorLocation();
						for (int32 Arc = 0; Arc < 3; ++Arc)
						{
							FVector Prev = From;
							constexpr int32 Segments = 5;
							for (int32 Seg = 1; Seg <= Segments; ++Seg)
							{
								FVector Next = FMath::Lerp(From, To, static_cast<float>(Seg) / Segments);
								if (Seg < Segments)
								{
									Next += FVector(FMath::FRandRange(-60.f, 60.f),
									                FMath::FRandRange(-60.f, 60.f),
									                FMath::FRandRange(-40.f, 40.f));
								}
								DrawDebugLine(GetWorld(), Prev, Next, FColor::Yellow, false, 0.25f, 0, 3.f);
								Prev = Next;
							}
						}
					}
				}
				Cooldown = Stats.CooldownSeconds;
			}
		}
	}
}

void AKodoStructureManager::FireProjectile(const FVector& From, AKodoCharacter* Target,
                                           const FKodoStructureStats& Stats)
{
	// Port of fireProjectile (game.js:1421-1452): type-specific speed/size/color.
	const FKodoStructurePreset* Preset = KodoStructures::Find(Stats.Id);
	const FName Special = Preset ? Preset->Special : NAME_None;

	FKodoProjectileConfig Config;
	Config.Type = Special;
	Config.Damage = Stats.Damage;
	Config.SlowPercent = Stats.SlowPercent;
	Config.StunChance = Stats.StunChance;
	Config.StunDurationSeconds = Stats.StunDurationSeconds;
	Config.SplashRadiusTiles = Stats.SplashRadiusTiles;
	Config.SpeedUU = 400.f * KodoUnits::PxToUU; // arrow default
	Config.RadiusPx = 4.f;
	Config.Color = FLinearColor(1.f, 0.84f, 0.04f); // #ffd60a

	if (Special == FName("frost"))
	{
		Config.SpeedUU = 300.f * KodoUnits::PxToUU;
		Config.RadiusPx = 6.f;
		Config.Color = FLinearColor(0.f, 0.94f, 1.f); // #00f0ff
	}
	else if (Special == FName("stun"))
	{
		Config.SpeedUU = 260.f * KodoUnits::PxToUU;
		Config.RadiusPx = 8.f;
		Config.Color = FLinearColor(1.f, 0.62f, 0.04f); // #ff9f0a
	}
	else if (Special == FName("aoe"))
	{
		Config.SpeedUU = 220.f * KodoUnits::PxToUU;
		Config.RadiusPx = 7.f;
		Config.Color = FLinearColor(1.f, 0.23f, 0.19f); // #ff3b30
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector SpawnLocation = From + FVector(0.f, 0.f, 280.f); // from the tower head
	if (AKodoProjectile* Projectile = GetWorld()->SpawnActor<AKodoProjectile>(
		AKodoProjectile::StaticClass(), SpawnLocation, FRotator::ZeroRotator, Params))
	{
		Projectile->Init(Target, Config);
	}
}

void AKodoStructureManager::OnCellChanged(const FIntPoint& Cell, const FGridCell& NewState)
{
	// Keep the active-tower set in sync so combat never rescans the whole grid.
	// A "weapon" is a finished, non-economy tower seen at its own master cell.
	static const FName CommandCenterId(TEXT("command_center"));
	static const FName MineShaftId(TEXT("mine_shaft"));
	static const FName LumberMillId(TEXT("lumber_mill"));
	static const FName UpgradeCenterId(TEXT("upgrade_center"));
	static const FName AdminTowerId(TEXT("admin_tower"));
	const bool bActiveTower = NewState.Type == ECellType::Tower && !NewState.bUnderConstruction &&
		NewState.MasterCell == Cell && NewState.StructureId != CommandCenterId &&
		NewState.StructureId != MineShaftId && NewState.StructureId != LumberMillId &&
		NewState.StructureId != UpgradeCenterId && NewState.StructureId != AdminTowerId;
	if (bActiveTower)
	{
		ActiveTowerCells.Add(Cell);
	}
	else
	{
		ActiveTowerCells.Remove(Cell);
		TowerCooldowns.Remove(Cell);
	}

	const bool bStructural = NewState.Type == ECellType::Wall || NewState.Type == ECellType::Tower;
	if (!bStructural)
	{
		RemoveStructureVisual(Cell);
		return;
	}
	UpdateStructureVisual(Cell, NewState, NewState.bUnderConstruction ? 0.05f : 1.f);
}

void AKodoStructureManager::TickTowerFx(const float DeltaSeconds)
{
	// Recoil: head kicks back along -fire direction and eases home.
	for (auto It = TowerFx.CreateIterator(); It; ++It)
	{
		FTowerFireFx& Fx = It.Value();
		if (Fx.RecoilTimer <= 0.f)
		{
			continue;
		}
		Fx.RecoilTimer = FMath::Max(0.f, Fx.RecoilTimer - DeltaSeconds);

		const TObjectPtr<UStaticMeshComponent>* Head = TowerHeads.Find(It.Key());
		if (!Head || !Head->Get() || !Grid)
		{
			continue;
		}
		const float Alpha = Fx.RecoilTimer / 0.18f; // 1 -> 0
		FVector HeadLocation = BuildingCenter(It.Key(), Grid->GetCell(It.Key()).StructureId);
		HeadLocation.Z += 330.f;
		HeadLocation -= Fx.FireDirection * 35.f * Alpha;
		Head->Get()->SetWorldLocation(HeadLocation);
	}
}

FLinearColor AKodoStructureManager::TintForStructure(const FName StructureId) const
{
	// Wall + command center read the grid's editor color table (editor color config),
	// defaulted to the prototype palette below; the rest stay fixed prototype colors.
	if (StructureId == FName("wall"))
	{
		return Grid ? Grid->GetMapColor(EKodoMapColor::Wall) : FLinearColor(0.37f, 0.25f, 0.12f);
	}
	if (StructureId == FName("command_center"))
	{
		return Grid ? Grid->GetMapColor(EKodoMapColor::CommandCenter) : FLinearColor(0.92f, 0.9f, 0.85f);
	}
	// Magical wall: a bluish wall so it reads differently from the 15-HP brown wall.
	if (StructureId == FName("magic_wall"))     { return FLinearColor(0.35f, 0.55f, 0.95f); }
	// Upgrade Center: distinct teal tech building.
	if (StructureId == FName("upgrade_center")) { return FLinearColor(0.1f, 0.65f, 0.7f); }
	// Admin Tower: dark charcoal-red so it reads as the distinct control panel in the corner.
	if (StructureId == FName("admin_tower"))    { return FLinearColor(0.28f, 0.05f, 0.06f); }
	// Prototype palette (towers.js icons / projectile colors).
	if (StructureId == FName("arrow"))          { return FLinearColor(1.f, 0.84f, 0.04f); }
	if (StructureId == FName("frost"))          { return FLinearColor(0.f, 0.94f, 1.f); }
	if (StructureId == FName("stun"))           { return FLinearColor(1.f, 0.62f, 0.04f); }
	if (StructureId == FName("aoe"))            { return FLinearColor(1.f, 0.23f, 0.19f); }
	if (StructureId == FName("multishot"))      { return FLinearColor(1.f, 0.9f, 0.55f); }
	if (StructureId == FName("aura"))           { return FLinearColor(0.69f, 0.15f, 1.f); }
	if (StructureId == FName("lumber_mill"))    { return FLinearColor(0.45f, 0.3f, 0.1f); }
	if (StructureId == FName("mine_shaft"))     { return FLinearColor(0.9f, 0.75f, 0.2f); }
	return FLinearColor(0.7f, 0.7f, 0.75f); // basic_tower silver
}

FVector AKodoStructureManager::BuildingCenter(const FIntPoint& MasterCell, const FName StructureId) const
{
	FVector Center = Grid ? Grid->CellToWorldCenter(MasterCell) : FVector::ZeroVector;
	// 2x2 buildings have their master at the top-left, so nudge to the footprint middle.
	if (const FKodoStructurePreset* Preset = KodoStructures::Find(StructureId); Preset && Preset->bIs2x2)
	{
		Center.X += KodoUnits::CellSizeUU * 0.5f;
		Center.Y += KodoUnits::CellSizeUU * 0.5f;
	}
	// Sit on the raised/ramped ground (continuous height: angled on ramps, stepped on plateaus).
	if (Grid)
	{
		Center.Z += Grid->GetElevationZAtWorld(Center);
	}
	return Center;
}

void AKodoStructureManager::UpdateStructureVisual(const FIntPoint& Cell, const FGridCell& State,
                                                  const float ConstructionAlpha)
{
	// Blockout shapes: walls = squat cubes (rendered per cell -> a 2x2 wall reads as
	// "4 blocks", per creator feedback); CC/mills/shaft = tall cubes per cell; shooting
	// towers = ONE centered cylinder on the master cell (the other 3 cells carry no body).
	const bool bIsWall = State.Type == ECellType::Wall;
	const bool bIsEconomyBuilding = State.StructureId == FName("command_center") ||
		State.StructureId == FName("lumber_mill") || State.StructureId == FName("mine_shaft") ||
		State.StructureId == FName("upgrade_center") || State.StructureId == FName("admin_tower");
	const bool bIsShootingTower = !bIsWall && !bIsEconomyBuilding;

	const FKodoStructurePreset* Preset = KodoStructures::Find(State.StructureId);
	const bool b2x2 = Preset && Preset->bIs2x2;
	const bool bIsMaster = State.MasterCell == Cell;

	// A shooting tower draws a single body on its master cell, so the three other
	// footprint cells get no visual of their own.
	if (bIsShootingTower && b2x2 && !bIsMaster)
	{
		RemoveStructureVisual(Cell);
		return;
	}

	TObjectPtr<UStaticMeshComponent>* Existing = CellVisuals.Find(Cell);
	UStaticMeshComponent* Comp = Existing ? Existing->Get() : nullptr;

	if (!Comp)
	{
		Comp = NewObject<UStaticMeshComponent>(this);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->RegisterComponent();
		Comp->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
		CellVisuals.Add(Cell, Comp);
	}

	// No structure shadows at all: with Virtual Shadow Maps off and many units on
	// screen, dropping every structure's dynamic shadow is a clear perf win and keeps
	// the clean top-down read the creator asked for.
	Comp->SetCastShadow(false);

	UStaticMesh* Mesh = bIsShootingTower ? CylinderMesh.Get() : CubeMesh.Get();
	if (Comp->GetStaticMesh() != Mesh)
	{
		Comp->SetStaticMesh(Mesh);
	}
	// Tint via the shared helper (the mesh's default material doesn't expose a settable color).
	KodoTint::Apply(Comp, TintForStructure(State.StructureId));

	// Shooting towers get a rotating head barrel that tracks targets (Phase 5).
	if (bIsShootingTower && !State.bUnderConstruction && !TowerHeads.Contains(Cell) && Grid)
	{
		UStaticMeshComponent* Head = NewObject<UStaticMeshComponent>(this);
		Head->SetMobility(EComponentMobility::Movable);
		Head->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Head->RegisterComponent();
		Head->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
		Head->SetStaticMesh(CubeMesh.Get());
		KodoTint::Apply(Head, TintForStructure(State.StructureId) * 1.2f);
		FVector HeadLocation = BuildingCenter(Cell, State.StructureId);
		HeadLocation.Z += 330.f;
		Head->SetWorldLocation(HeadLocation);
		Head->SetWorldScale3D(FVector(1.4f, 0.45f, 0.4f)); // barrel pointing +X
		TowerHeads.Add(Cell, Head);
	}
	else if ((!bIsShootingTower || State.bUnderConstruction) && TowerHeads.Contains(Cell))
	{
		if (UStaticMeshComponent* OldHead = TowerHeads[Cell].Get())
		{
			OldHead->DestroyComponent();
		}
		TowerHeads.Remove(Cell);
	}

	const float GrowAlpha = FMath::Lerp(0.25f, 1.f, ConstructionAlpha); // rises out of the ground while building
	FVector Scale;
	FVector Location;
	if (bIsShootingTower)
	{
		// One fat cylinder filling the 2x2 footprint, centered over the whole building.
		const float Width = b2x2 ? 2.0f : 1.1f;
		Scale = FVector(Width, Width, 3.4f * GrowAlpha);
		Location = BuildingCenter(Cell, State.StructureId);
	}
	else if (bIsWall)
	{
		Scale = FVector(1.45f, 1.45f, 1.6f * GrowAlpha);
		Location = Grid ? Grid->CellToWorldCenter(Cell) : FVector::ZeroVector;
	}
	else // economy building (CC / lumber mill / mine shaft / admin tower): tall cube, one per cell
	{
		// Admin Tower stands noticeably taller so the corner control panel reads at a glance.
		const float Height = State.StructureId == FName("admin_tower") ? 4.0f : 2.4f;
		Scale = FVector(1.45f, 1.45f, Height * GrowAlpha);
		Location = Grid ? Grid->CellToWorldCenter(Cell) : FVector::ZeroVector;
	}
	// Per-cell bodies (walls/economy) sit on the raised/ramped ground (towers came via
	// BuildingCenter, which already added it). Add here without the 2x2 nudge.
	if (Grid && !bIsShootingTower)
	{
		Location.Z += Grid->GetElevationZAtWorld(Location);
	}
	Location.Z += Scale.Z * 50.f; // engine shapes are 100 UU tall, centered

	Comp->SetWorldLocation(Location);
	Comp->SetWorldScale3D(Scale);
}

void AKodoStructureManager::RemoveStructureVisual(const FIntPoint& Cell)
{
	if (TObjectPtr<UStaticMeshComponent>* Existing = CellVisuals.Find(Cell))
	{
		if (UStaticMeshComponent* Comp = Existing->Get())
		{
			Comp->DestroyComponent();
		}
		CellVisuals.Remove(Cell);
	}
	if (TObjectPtr<UStaticMeshComponent>* Head = TowerHeads.Find(Cell))
	{
		if (UStaticMeshComponent* HeadComp = Head->Get())
		{
			HeadComp->DestroyComponent();
		}
		TowerHeads.Remove(Cell);
	}
	TowerFx.Remove(Cell);
}
