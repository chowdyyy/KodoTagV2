// Kodo Tag: Survivor — UE Migration, Phase 3.

#include "Actors/RunnerCharacter.h"
#include "Actors/KodoCharacter.h"
#include "Actors/KodoProjectile.h"
#include "Actors/KodoRepairBot.h"
#include "Grid/KodoGridSubsystem.h"
#include "Core/KodoTagGameState.h"
#include "Core/KodoTagGameMode.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Core/KodoTint.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

namespace
{
	/** Spell max cooldowns in seconds (entities.js:38-40). */
	float SpellMaxCooldownFor(const EKodoHeroClass HeroClass)
	{
		switch (HeroClass)
		{
		case EKodoHeroClass::MountainKing: return 12.f;
		case EKodoHeroClass::DeathKnight:  return 10.f;
		case EKodoHeroClass::Blademaster:  return 15.f;
		default:                           return 20.f; // Tinker
		}
	}

	/** Base speed in prototype px/s: Blademaster 178 (+15%), others 155 (entities.js:19).
	 *  The +15% only applies when the Blademaster's speed passive (slot 2) is researched. */
	float BaseSpeedPxFor(const EKodoHeroClass HeroClass, const bool bSpeedPassive)
	{
		return (HeroClass == EKodoHeroClass::Blademaster && bSpeedPassive) ? 178.f : 155.f;
	}

	/** Cooldown for the researched active (slot 3), per class, in seconds. */
	float Skill3CooldownFor(const EKodoHeroClass HeroClass)
	{
		switch (HeroClass)
		{
		case EKodoHeroClass::MountainKing: return 30.f; // Avatar
		case EKodoHeroClass::DeathKnight:  return 16.f; // Death Pact
		case EKodoHeroClass::Blademaster:  return 8.f;  // Blink
		default:                           return 12.f; // Tinker: Rocket Salvo
		}
	}

	/** Mana cost for slot 1 (signature) per class. */
	float Skill1ManaCostFor(const EKodoHeroClass HeroClass)
	{
		switch (HeroClass)
		{
		case EKodoHeroClass::MountainKing: return 40.f; // Thunder Clap
		case EKodoHeroClass::DeathKnight:  return 40.f; // Death Coil
		case EKodoHeroClass::Blademaster:  return 35.f; // Wind Walk
		default:                           return 40.f; // Deploy Bot
		}
	}

	/** Mana cost for slot 3 (researched active) per class. */
	float Skill3ManaCostFor(const EKodoHeroClass HeroClass)
	{
		switch (HeroClass)
		{
		case EKodoHeroClass::MountainKing: return 60.f; // Avatar
		case EKodoHeroClass::DeathKnight:  return 45.f; // Death Pact
		case EKodoHeroClass::Blademaster:  return 30.f; // Blink
		default:                           return 55.f; // Rocket Salvo
		}
	}
}

ARunnerCharacter::ARunnerCharacter()
{
	// Runner: 100 HP, 155 px/s base speed (entities.js:17-19). Blademaster's +15%
	// and item modifiers arrive with hero classes in Phase 3B/4.
	MaxHp = 100.f;
	Hp = 100.f;
	MoveSpeedUU = KodoUnits::RunnerSpeedUU;
}

void ARunnerCharacter::BeginPlay()
{
	Super::BeginPlay(); // base grabs the grid subsystem into Grid

	// Tint the hero body with the editor-configured Hero color (editor color config).
	if (BodyMesh && Grid)
	{
		KodoTint::Apply(BodyMesh, Grid->GetMapColor(EKodoMapColor::Hero));
	}
}

AKodoTagGameState* ARunnerCharacter::GetKodoGameState() const
{
	return GetWorld()->GetGameState<AKodoTagGameState>();
}

void ARunnerCharacter::Msg(const FString& Text, const FColor& Color) const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.5f, Color, Text);
	}
}

void ARunnerCharacter::CancelHarvest()
{
	PendingKind = EKodoHarvestKind::None;
	ActiveKind = EKodoHarvestKind::None;
	ReturnKind = EKodoHarvestKind::None;
}

bool ARunnerCharacter::PathToNeighborOf(const FIntPoint& TargetCell, const int32 SearchRadius)
{
	// Best walkable neighbor closest to the runner (game.js:946-963 pattern).
	if (!Grid)
	{
		return false;
	}
	const FIntPoint MyCell = GetGridCell();

	FIntPoint BestNeighbor;
	float BestDist = TNumericLimits<float>::Max();
	bool bFound = false;

	for (int32 Dx = -SearchRadius; Dx <= SearchRadius; ++Dx)
	{
		for (int32 Dy = -SearchRadius; Dy <= SearchRadius; ++Dy)
		{
			if (Dx == 0 && Dy == 0)
			{
				continue;
			}
			const FIntPoint Test(TargetCell.X + Dx, TargetCell.Y + Dy);
			if (Grid->IsInBounds(Test) && !Grid->IsCellBlockedForSize(Test, 1))
			{
				const float Dist = FVector2D(Test.X - MyCell.X, Test.Y - MyCell.Y).Size();
				if (Dist < BestDist)
				{
					BestDist = Dist;
					BestNeighbor = Test;
					bFound = true;
				}
			}
		}
	}
	if (!bFound)
	{
		return false;
	}

	TArray<FKodoPathStep> Steps;
	if (Grid->FindPath(MyCell, BestNeighbor, 1, Steps))
	{
		SetPath(Steps);
		return true;
	}
	return false;
}

void ARunnerCharacter::CommandHarvest(const FIntPoint& TargetCell, const EKodoHarvestKind Kind)
{
	CancelHarvest();
	if (Kind == EKodoHarvestKind::None)
	{
		return;
	}

	// Already adjacent? Start immediately (entities.js:148 adjacency is +/-1).
	const FIntPoint MyCell = GetGridCell();
	if (FMath::Abs(MyCell.X - TargetCell.X) <= 1 && FMath::Abs(MyCell.Y - TargetCell.Y) <= 1)
	{
		ActiveKind = Kind;
		ActiveCell = TargetCell;
		HarvestTickTimer = 0.f;
		Msg(Kind == EKodoHarvestKind::Tree ? TEXT("Chop-chop! Harvesting lumber wood...")
		                                   : TEXT("Clink-clank! Mining gold..."), FColor::Green);
		return;
	}

	if (PathToNeighborOf(TargetCell, 1))
	{
		PendingKind = Kind;
		PendingCell = TargetCell;
	}
	else
	{
		Msg(TEXT("Can't reach that resource!"), FColor::Red);
	}
}

void ARunnerCharacter::StartAutoDepositRun(const FIntPoint& HarvestCell, const EKodoHarvestKind Kind)
{
	// Backpack full: remember the harvest spot, path to the nearest CC (game.js:939-978).
	ActiveKind = EKodoHarvestKind::None;
	ReturnKind = Kind;
	ReturnCell = HarvestCell;

	FIntPoint CC;
	if (!Grid || !Grid->FindNearestCommandCenter(GetGridCell(), CC))
	{
		Msg(TEXT("Backpack FULL! No Command Center found!"), FColor::Orange);
		ReturnKind = EKodoHarvestKind::None;
		return;
	}

	if (PathToNeighborOf(CC, 2))
	{
		Msg(TEXT("Backpack full! Heading to Command Center..."), FColor::Yellow);
	}
	else
	{
		Msg(TEXT("Backpack FULL! No path to Command Center!"), FColor::Red);
		ReturnKind = EKodoHarvestKind::None;
	}
}

void ARunnerCharacter::TickHarvesting(const float DeltaSeconds)
{
	// Arrival at a pending harvest target (entities.js:143-160).
	if (PendingKind != EKodoHarvestKind::None && !HasPath())
	{
		const FIntPoint MyCell = GetGridCell();
		if (FMath::Abs(MyCell.X - PendingCell.X) <= 1 && FMath::Abs(MyCell.Y - PendingCell.Y) <= 1)
		{
			ActiveKind = PendingKind;
			ActiveCell = PendingCell;
			HarvestTickTimer = 0.f;
			Msg(PendingKind == EKodoHarvestKind::Tree ? TEXT("Chop-chop! Harvesting lumber wood...")
			                                          : TEXT("Clink-clank! Mining gold..."), FColor::Green);
		}
		PendingKind = EKodoHarvestKind::None;
	}

	if (ActiveKind == EKodoHarvestKind::None || !Grid)
	{
		return;
	}

	// Validity: still adjacent, node still exists (game.js:928-931 / 1016-1019).
	const FIntPoint MyCell = GetGridCell();
	const bool bAdjacent = FMath::Abs(MyCell.X - ActiveCell.X) <= 1 && FMath::Abs(MyCell.Y - ActiveCell.Y) <= 1;
	const ECellType NodeType = Grid->GetCell(ActiveCell).Type;
	const bool bNodeValid = (ActiveKind == EKodoHarvestKind::Tree && NodeType == ECellType::Tree) ||
		(ActiveKind == EKodoHarvestKind::Goldmine && NodeType == ECellType::Goldmine);

	if (!bAdjacent || !bNodeValid)
	{
		ActiveKind = EKodoHarvestKind::None;
		return;
	}

	SetPath(TArray<FKodoPathStep>()); // harvesting pins you in place (game.js:932/1020)

	HarvestTickTimer += DeltaSeconds;
	if (HarvestTickTimer < 0.5f) // 0.5 s tick (game.js:935/1023)
	{
		return;
	}
	HarvestTickTimer = 0.f;

	AKodoTagGameState* GS = GetKodoGameState();

	if (ActiveKind == EKodoHarvestKind::Tree)
	{
		// WOOD goes to the backpack (cap MaxCarry) and must be carried to a Command Center to
		// deposit. +2 wood/tick per axe level; trees are everlasting (game.js:986-994).
		if (HeldWood >= MaxCarry)
		{
			StartAutoDepositRun(ActiveCell, ActiveKind);
			return;
		}
		const int32 AxeLvl = GS ? GS->Upgrades.AxeLvl : 1;
		const int32 ChopVal = FMath::Min(2 * AxeLvl, MaxCarry - HeldWood);
		HeldWood += ChopVal;
	}
	else
	{
		// GOLD is credited DIRECTLY every tick — no backpack, no deposit trip (mining near the
		// base is enough). The upkeep throttle + bonus-gold tech apply right here.
		if (GS)
		{
			float Gain = 4.f * GS->UpkeepMult();
			const int32 BonusLvl = FMath::Clamp(GS->Upgrades.GoldBonusLvl, 0, 3);
			static const float BonusChance[4] = { 0.f, 0.33f, 0.50f, 0.66f };
			if (BonusLvl > 0 && FMath::FRand() < BonusChance[BonusLvl]) { Gain += Gain; }
			GS->Gold += Gain;
		}
	}
}

void ARunnerCharacter::TickDeposit()
{
	// Auto-deposit when near any CC cell (+/-2 box, game.js:1086-1115),
	// then auto-return to the saved harvest spot (game.js:1117-1148).
	// Only WOOD deposits here — gold is credited directly at the mine (no trip needed).
	if (HeldWood <= 0 || !Grid)
	{
		return;
	}
	if (!Grid->IsNearCommandCenter(GetGridCell(), 2))
	{
		return;
	}

	if (AKodoTagGameState* GS = GetKodoGameState())
	{
		GS->Wood += HeldWood;
	}
	Msg(FString::Printf(TEXT("Deposited +%dw!"), HeldWood), FColor::Green);
	HeldWood = 0;

	if (ReturnKind != EKodoHarvestKind::None)
	{
		if (PathToNeighborOf(ReturnCell, 1))
		{
			PendingKind = ReturnKind;
			PendingCell = ReturnCell;
			Msg(TEXT("Deposited! Returning to harvest..."), FColor::Cyan);
		}
		ReturnKind = EKodoHarvestKind::None;
	}
}

void ARunnerCharacter::SetHeroClass(const EKodoHeroClass NewClass)
{
	HeroClass = NewClass;
	SpellCooldown = 0.f;

	// Class tint on the blockout body (prototype hero palette).
	if (BodyMesh)
	{
		FLinearColor ClassColor(0.f, 0.94f, 1.f); // MK cyan
		switch (NewClass)
		{
		case EKodoHeroClass::DeathKnight: ClassColor = FLinearColor(0.64f, 0.75f, 0.55f); break; // runic green-gray
		case EKodoHeroClass::Blademaster: ClassColor = FLinearColor(0.82f, 0.53f, 0.44f); break; // d08770
		case EKodoHeroClass::Tinker:      ClassColor = FLinearColor(0.71f, 0.56f, 0.68f); break; // copper b48ead
		default: break;
		}
		// The editor-configured Hero color wins when available so the runner stays the
		// color the map author picked; otherwise fall back to the per-class tint.
		KodoTint::Apply(BodyMesh, Grid ? Grid->GetMapColor(EKodoMapColor::Hero) : ClassColor);
	}

	Msg(FString::Printf(TEXT("Hero class: %s"), *UEnum::GetDisplayValueAsText(NewClass).ToString()), FColor::Cyan);
}

int32 ARunnerCharacter::GetXpForNextLevel() const
{
	// Simple linear ramp: 100, 200, 300, ... XP per level.
	return HeroLevel * 100;
}

void ARunnerCharacter::GainXp(const int32 Amount)
{
	if (Amount <= 0 || bDead)
	{
		return;
	}
	HeroXp += Amount;

	// Consume thresholds, applying a level-up bonus each time, until under the cap.
	while (HeroLevel < MaxHeroLevel && HeroXp >= GetXpForNextLevel())
	{
		HeroXp -= GetXpForNextLevel();
		++HeroLevel;

		// Level-up bonus: +20 max HP and heal ~20% of that gain.
		MaxHp += 20.f;
		Hp = FMath::Min(MaxHp, Hp + 0.2f * 20.f);

		Msg(FString::Printf(TEXT("Level Up! (Lv %d)"), HeroLevel), FColor::Yellow);
	}

	// At max level, banked XP is clamped to the (now unreachable) threshold so the HUD
	// reads a full bar instead of an ever-growing number.
	if (HeroLevel >= MaxHeroLevel)
	{
		HeroXp = FMath::Min(HeroXp, GetXpForNextLevel());
	}
}

void ARunnerCharacter::ApplyDamageAmount(const float Amount)
{
	if (IsInvulnerable())
	{
		return; // entities.js:79-80
	}
	Super::ApplyDamageAmount(Amount);
}

void ARunnerCharacter::CastSpell()
{
	// Thin wrapper so the existing Q binding keeps casting the slot-1 signature spell.
	CastSkill(0);
}

void ARunnerCharacter::CastSkill(const int32 Slot)
{
	// Slot 0 = signature spell (UI slot 1); Slot 2 = researched active (UI slot 3).
	// Slot index 1 is the passive (slot 2 in the UI) and is not castable here.
	if (Slot != 0 && Slot != 2)
	{
		return;
	}

	const bool bIsResearchedActive = (Slot == 2);

	// Slot 3 requires its research unlock.
	if (bIsResearchedActive && !bSkill3Unlocked)
	{
		Msg(TEXT("Skill locked — research it at the Upgrade Center!"), FColor::Orange);
		return;
	}

	// Shared cooldown gate (both actives use SpellCooldown).
	if (SpellCooldown > 0.f)
	{
		Msg(TEXT("Spell is on cooldown!"), FColor::Orange);
		return;
	}

	// Mana gate.
	const float ManaCost = bIsResearchedActive ? Skill3ManaCostFor(HeroClass) : Skill1ManaCostFor(HeroClass);
	if (Mana < ManaCost)
	{
		Msg(TEXT("Not enough mana!"), FColor::Orange);
		return;
	}

	bool bSuccess = false;
	UWorld* World = GetWorld();

	// Hero-level ability scalar: each spell's damage/effect magnitude grows with level
	// (1 + 0.15*(level-1), ~2.35x at level 10).
	const float AbilityScale = GetAbilityScale();

	if (!bIsResearchedActive)
	{
		// ===== Slot 1: per-class SIGNATURE spell (Thunder Clap / Death Coil / Wind Walk / Deploy Bot). =====
		switch (HeroClass)
		{
		case EKodoHeroClass::MountainKing:
		{
			// Thunder Clap: 2 s stun (scaled by level), 3.0-tile radius (entities.js:657-675).
			const float ClapRangeUU = 3.f * KodoUnits::CellSizeUU;
			const float ClapStun = 2.f * AbilityScale; // longer stun as the hero levels
			for (TActorIterator<AKodoCharacter> It(World); It; ++It)
			{
				if (FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) <= ClapRangeUU)
				{
					It->ApplyStun(ClapStun);
				}
			}
			Msg(TEXT("Casted Thunder Clap!"), FColor::Cyan);
			bSuccess = true;
			break;
		}
		case EKodoHeroClass::DeathKnight:
		{
			// Death Coil: homing skull, 300 damage, nearest Kodo within 10 tiles
			// (entities.js:677-713). No cooldown/mana spent if nothing in range.
			AKodoCharacter* Closest = nullptr;
			float BestDist = TNumericLimits<float>::Max();
			for (TActorIterator<AKodoCharacter> It(World); It; ++It)
			{
				const float Dist = FVector::Dist2D(It->GetActorLocation(), GetActorLocation());
				if (Dist < BestDist)
				{
					BestDist = Dist;
					Closest = *It;
				}
			}
			if (!Closest || BestDist > 10.f * KodoUnits::CellSizeUU)
			{
				Msg(TEXT("No Kodos close enough for Death Coil!"), FColor::Orange);
				return;
			}

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			FVector SpawnLoc = GetActorLocation();
			SpawnLoc.Z = 150.f;
			if (AKodoProjectile* Coil = World->SpawnActor<AKodoProjectile>(AKodoProjectile::StaticClass(), SpawnLoc,
			                                                               FRotator::ZeroRotator, Params))
			{
				FKodoProjectileConfig Config;
				Config.Type = FName("death_coil");
				Config.Damage = 300.f * AbilityScale;           // entities.js:705, scaled by hero level
				Config.SpeedUU = 320.f * KodoUnits::PxToUU;     // entities.js:706
				Config.RadiusPx = 9.f;
				Config.Color = FLinearColor(0.19f, 0.82f, 0.35f); // #30d158 green
				Coil->Init(Closest, Config);
			}
			Msg(TEXT("Casted Death Coil!"), FColor::Green);
			bSuccess = true;
			break;
		}
		case EKodoHeroClass::Blademaster:
		{
			// Wind Walk: 4 s base, +50% speed, Kodos lose aggro (entities.js:715-721).
			WindWalkTimer = 4.f * AbilityScale;
			Msg(TEXT("Wind Walk activated!"), FColor::Cyan);
			bSuccess = true;
			break;
		}
		case EKodoHeroClass::Tinker:
		{
			// Deploy Bot at the Runner's cell (entities.js:723-731). The Tinker's +30% repair
			// passive only boosts the bot once slot 2 is researched (see below).
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AKodoRepairBot* Bot = World->SpawnActor<AKodoRepairBot>(AKodoRepairBot::StaticClass(),
			                                                            GetActorLocation(), FRotator::ZeroRotator, Params))
			{
				Bot->InitAtCell(GetGridCell());
				const float RepairMult = bSkill2Unlocked ? 1.30f : 1.0f;
				Bot->SetHealPerTick(20.f * RepairMult * AbilityScale);
			}
			Msg(TEXT("Repair Bot deployed!"), FColor::Green);
			bSuccess = true;
			break;
		}
		}

		if (bSuccess)
		{
			Mana -= ManaCost;
			SpellCooldown = SpellMaxCooldownFor(HeroClass);
		}
		return;
	}

	// ===== Slot 3: per-class RESEARCHED active spell. =====
	switch (HeroClass)
	{
	case EKodoHeroClass::MountainKing:
	{
		// Avatar: heal to full + a brief 4 s invulnerability window.
		Hp = MaxHp;
		ActivateInvulnerability(4.f);
		Msg(TEXT("AVATAR! Healed to full and invulnerable!"), FColor::Cyan);
		bSuccess = true;
		break;
	}
	case EKodoHeroClass::DeathKnight:
	{
		// Death Pact: instant self-heal for 40% of MaxHp.
		Hp = FMath::Min(MaxHp, Hp + 0.40f * MaxHp);
		Msg(TEXT("Death Pact — self-healed!"), FColor::Green);
		bSuccess = true;
		break;
	}
	case EKodoHeroClass::Blademaster:
	{
		// Blink: teleport ~4 cells along the hero's facing, clamped to a walkable in-bounds cell.
		if (Grid)
		{
			const FIntPoint MyCell = GetGridCell();
			FVector Facing = GetActorForwardVector().GetSafeNormal2D();
			if (Facing.IsNearlyZero())
			{
				Facing = FVector(1.f, 0.f, 0.f);
			}
			const int32 Dx = FMath::RoundToInt(Facing.X);
			const int32 Dy = FMath::RoundToInt(Facing.Y);
			// Search inward from 4 cells to 1 for the furthest walkable, in-bounds cell.
			FIntPoint Dest = MyCell;
			bool bFound = false;
			for (int32 Dist = 4; Dist >= 1 && !bFound; --Dist)
			{
				const FIntPoint Test(MyCell.X + Dx * Dist, MyCell.Y + Dy * Dist);
				if (Grid->IsInBounds(Test) && !Grid->IsCellBlockedForSize(Test, 1))
				{
					Dest = Test;
					bFound = true;
				}
			}
			if (bFound)
			{
				FVector NewLoc = Grid->CellToWorldCenter(Dest);
				NewLoc.Z = GetActorLocation().Z;
				SetActorLocation(NewLoc);
				SetPath(TArray<FKodoPathStep>()); // cancel any pathing after the teleport
				Msg(TEXT("Blink!"), FColor::Cyan);
				bSuccess = true;
			}
			else
			{
				Msg(TEXT("No clear cell to Blink to!"), FColor::Orange);
				return;
			}
		}
		break;
	}
	case EKodoHeroClass::Tinker:
	{
		// Rocket Salvo: AoE damage to all kodos within ~3 cells.
		const float SalvoRangeUU = 3.f * KodoUnits::CellSizeUU;
		const float SalvoDamage = 120.f * AbilityScale;
		for (TActorIterator<AKodoCharacter> It(World); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) <= SalvoRangeUU)
			{
				It->ApplyDamageAmount(SalvoDamage);
			}
		}
		Msg(TEXT("Rocket Salvo!"), FColor::Red);
		bSuccess = true;
		break;
	}
	}

	if (bSuccess)
	{
		Mana -= ManaCost;
		SpellCooldown = Skill3CooldownFor(HeroClass);
	}
}

void ARunnerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bDead)
	{
		return;
	}

	// Timers (entities.js:98-111).
	WindWalkTimer = FMath::Max(0.f, WindWalkTimer - DeltaSeconds);
	InvulnerableTimer = FMath::Max(0.f, InvulnerableTimer - DeltaSeconds);
	SpellCooldown = FMath::Max(0.f, SpellCooldown - DeltaSeconds);

	// Mana regen toward MaxMana.
	Mana = FMath::Min(MaxMana, Mana + ManaRegenPerSec * DeltaSeconds);

	// Speed: class base, +50% during Wind Walk (entities.js:58-69), plus the per-mode
	// multiplier (God 1.25) and Boots of Speed (+25% when owned). The Blademaster's +15%
	// base-speed passive (slot 2) only applies once researched (bSkill2Unlocked).
	float SpeedMult = 1.f;
	if (WindWalkTimer > 0.f)
	{
		SpeedMult += 0.5f;
	}
	const float BootsMult = bHasBoots ? 1.25f : 1.f;
	MoveSpeedUU = BaseSpeedPxFor(HeroClass, bSkill2Unlocked) * SpeedMult * ModeSpeedMult * BootsMult * KodoUnits::PxToUU;

	// Passive HP regen (entities.js:113-115). Base is 0.5 HP/s for everyone; the Mountain
	// King's faster 2.0 HP/s is a researched passive (slot 2) — only applies when unlocked.
	const float RegenPerSec = (HeroClass == EKodoHeroClass::MountainKing && bSkill2Unlocked) ? 2.0f : 0.5f;
	Hp = FMath::Min(MaxHp, Hp + RegenPerSec * DeltaSeconds);

	TickHarvesting(DeltaSeconds);
	TickDeposit();
}

void ARunnerCharacter::OnZeroHp()
{
	if (bDead)
	{
		return;
	}
	bDead = true;
	CancelHarvest();
	SetPath(TArray<FKodoPathStep>());

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Red,
		                                 TEXT("THE RUNNER HAS BEEN EATEN — GAME OVER (restart PIE)"));
	}

	// Notify the GameMode so it resolves the match as a defeat (stops spawns, sets bGameOver).
	if (AKodoTagGameMode* GM = GetWorld()->GetAuthGameMode<AKodoTagGameMode>())
	{
		GM->OnRunnerDied();
	}
}

void ARunnerCharacter::ApplyModeBuffs(const EKodoGameMode Mode)
{
	if (Mode == EKodoGameMode::God)
	{
		// God: double max HP, heal to full, +25% move speed.
		MaxHp *= 2.f;
		Hp = MaxHp;
		ModeSpeedMult = 1.25f;
	}
	else
	{
		// Maze: stock tuning.
		ModeSpeedMult = 1.f;
	}
}

bool ARunnerCharacter::BuyBoots()
{
	if (bHasBoots)
	{
		Msg(TEXT("You already own Boots of Speed!"), FColor::Orange);
		return false;
	}

	AKodoTagGameState* GS = GetWorld() ? GetWorld()->GetGameState<AKodoTagGameState>() : nullptr;
	if (!GS || GS->Gold < BootsCost)
	{
		Msg(TEXT("Not enough gold for Boots of Speed!"), FColor::Red);
		return false;
	}

	GS->Spend(BootsCost, 0);
	bHasBoots = true;
	Msg(TEXT("Bought Boots of Speed! (+25% move speed)"), FColor::Green);
	return true;
}
