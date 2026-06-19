// Kodo Tag: Survivor — UE Migration, Phase 3.

#include "Actors/RunnerCharacter.h"
#include "Actors/KodoCharacter.h"
#include "Actors/KodoProjectile.h"
#include "Actors/KodoCastEffect.h"
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
	/** Base speed in prototype px/s: Blademaster 178 (+15%), others 155 (entities.js:19).
	 *  The +15% only applies when the Blademaster's speed passive (slot 2) is researched.
	 *  (Per-slot ability mana/cooldown tuning now lives in the FKodoAbility table — KodoAbility().) */
	float BaseSpeedPxFor(const EKodoHeroClass HeroClass, const bool bSpeedPassive)
	{
		return (HeroClass == EKodoHeroClass::Blademaster && bSpeedPassive) ? 178.f : 155.f;
	}
}

// =====================================================================================
// Per-hero basic-attack combat data (Pass 1). File-static table keyed by EKodoHeroClass.
// Ranges are in UU via KodoUnits::CellSizeUU (melee ~1.5 cells, ranged ~5.5 cells).
// =====================================================================================
const FKodoHeroCombatData& KodoHeroCombat(const EKodoHeroClass HeroClass)
{
	auto MakeMelee = [](float Dmg, float Interval) -> FKodoHeroCombatData
	{
		FKodoHeroCombatData D;
		D.bRanged = false;
		D.AttackRangeUU = 1.5f * KodoUnits::CellSizeUU;
		D.AttackDamage = Dmg;
		D.AttackInterval = Interval;
		D.MoveSpeedMult = 1.f;
		return D;
	};
	auto MakeRanged = [](float Dmg, float Interval) -> FKodoHeroCombatData
	{
		FKodoHeroCombatData D;
		D.bRanged = true;
		D.AttackRangeUU = 5.5f * KodoUnits::CellSizeUU;
		D.AttackDamage = Dmg;
		D.AttackInterval = Interval;
		D.MoveSpeedMult = 1.f;
		return D;
	};

	// Static so the returned reference stays valid; built once on first use.
	// Damage tuned so a basic attack VISIBLY chunks a Kodo (Speed 450 / Standard 500 / Blink 1500 /
	// Tank 5000 HP). The old ~20 dmg barely scratched them so attacking felt broken. Melee hits harder
	// (must close in); ranged a bit less (safe poke). War Altar (+8/lvl) + Claws (+10) scale further.
	static const FKodoHeroCombatData MountainKing = MakeMelee(70.f, 1.0f);
	static const FKodoHeroCombatData Blademaster  = MakeMelee(60.f, 0.85f);
	static const FKodoHeroCombatData Paladin      = MakeMelee(65.f, 1.1f);
	static const FKodoHeroCombatData Archmage     = MakeRanged(45.f, 1.2f);
	static const FKodoHeroCombatData FarSeer      = MakeRanged(48.f, 1.15f);
	static const FKodoHeroCombatData Dreadlord    = MakeRanged(52.f, 1.25f);

	switch (HeroClass)
	{
	case EKodoHeroClass::MountainKing: return MountainKing;
	case EKodoHeroClass::Blademaster:  return Blademaster;
	case EKodoHeroClass::Paladin:      return Paladin;
	case EKodoHeroClass::Archmage:     return Archmage;
	case EKodoHeroClass::FarSeer:      return FarSeer;
	case EKodoHeroClass::Dreadlord:    return Dreadlord;
	default:                           return MountainKing;
	}
}

// =====================================================================================
// Per-hero 4-slot ABILITY table (Pass 2). slot0 = primary active, slot1 = secondary active,
// slot2 = PASSIVE, slot3 = ULTIMATE. Unlock levels: slot0=1, slot1=2, slot2=3, slot3=6.
// Function-local-static storage so the returned reference stays valid (built once on first use).
// =====================================================================================
const FKodoAbility& KodoAbility(const EKodoHeroClass Class, const int32 Slot)
{
	auto Make = [](const TCHAR* Name, bool bPassive, float Mana, float Cd, int32 Lvl,
	               EKodoCastTarget Target, const TCHAR* Desc) -> FKodoAbility
	{
		FKodoAbility A;
		A.Name = Name;
		A.bPassive = bPassive;
		A.ManaCost = Mana;
		A.Cooldown = Cd;
		A.UnlockLevel = Lvl;
		A.Target = Target;
		A.Description = Desc;
		return A;
	};

	// [Class][Slot]. Slot2 is always the passive (bPassive=true, no mana/cooldown).
	// EKodoCastTarget marks how each active is aimed; the trailing string is the hover description.
	static const FKodoAbility Table[6][4] =
	{
		// MountainKing: Storm Bolt, Thunder Clap, Bash (passive), Avatar (ult)
		{
			Make(TEXT("Storm Bolt"),    false, 35.f, 8.f,  1, EKodoCastTarget::TargetKodo,   TEXT("Hurl a hammer at a target Kodo, dealing damage and stunning it.")),
			Make(TEXT("Thunder Clap"),  false, 40.f, 10.f, 2, EKodoCastTarget::Instant,      TEXT("Slam the ground, damaging and stunning nearby Kodos.")),
			Make(TEXT("Bash"),          true,  0.f,  0.f,  3, EKodoCastTarget::Instant,      TEXT("Passive: basic attacks may stun and deal bonus damage.")),
			Make(TEXT("Avatar"),        false, 80.f, 60.f, 6, EKodoCastTarget::Instant,      TEXT("Heal fully, become invulnerable, and gain bonus attack damage.")),
		},
		// Blademaster: Wind Walk, Mirror Image, Critical Strike (passive), Bladestorm (ult)
		{
			Make(TEXT("Wind Walk"),       false, 35.f, 12.f, 1, EKodoCastTarget::Instant,    TEXT("Turn invisible and move faster for a few seconds.")),
			Make(TEXT("Mirror Image"),    false, 40.f, 16.f, 2, EKodoCastTarget::Instant,    TEXT("Conjure illusions, sharply reducing incoming damage briefly.")),
			Make(TEXT("Critical Strike"), true,  0.f,  0.f,  3, EKodoCastTarget::Instant,    TEXT("Passive: basic attacks may strike for double damage.")),
			Make(TEXT("Bladestorm"),      false, 90.f, 50.f, 6, EKodoCastTarget::Instant,    TEXT("Spin in a deadly whirlwind, damaging nearby Kodos over time.")),
		},
		// Archmage: Blizzard, Summon Water Elemental, Brilliance Aura (passive), Mass Teleport (ult)
		{
			Make(TEXT("Blizzard"),               false, 45.f, 12.f, 1, EKodoCastTarget::TargetGround, TEXT("Call ice shards onto a target area, damaging Kodos in waves.")),
			Make(TEXT("Summon Water Elemental"), false, 50.f, 20.f, 2, EKodoCastTarget::Instant,      TEXT("Summon an elemental that harries nearby Kodos for a time.")),
			Make(TEXT("Brilliance Aura"),        true,  0.f,  0.f,  3, EKodoCastTarget::Instant,      TEXT("Passive: increases your mana regeneration.")),
			Make(TEXT("Mass Teleport"),          false, 60.f, 45.f, 6, EKodoCastTarget::Instant,      TEXT("Teleport instantly to your nearest Command Center.")),
		},
		// FarSeer: Chain Lightning, Feral Spirit, Far Sight (passive), Earthquake (ult)
		{
			Make(TEXT("Chain Lightning"), false, 45.f, 11.f, 1, EKodoCastTarget::TargetKodo,   TEXT("Lightning leaps from a target Kodo to several nearby Kodos.")),
			Make(TEXT("Feral Spirit"),    false, 50.f, 20.f, 2, EKodoCastTarget::Instant,      TEXT("Summon spirit wolves that harry nearby Kodos for a time.")),
			Make(TEXT("Far Sight"),       true,  0.f,  0.f,  3, EKodoCastTarget::Instant,      TEXT("Passive: increases your mana regeneration.")),
			Make(TEXT("Earthquake"),      false, 90.f, 50.f, 6, EKodoCastTarget::TargetGround, TEXT("Shake a target area, damaging Kodos and structures and stunning enemies.")),
		},
		// Paladin: Holy Light, Divine Shield, Devotion Aura (passive), Resurrection (ult)
		{
			Make(TEXT("Holy Light"),    false, 40.f, 9.f,  1, EKodoCastTarget::Instant, TEXT("Channel holy energy to heal yourself.")),
			Make(TEXT("Divine Shield"), false, 45.f, 20.f, 2, EKodoCastTarget::Instant, TEXT("Become invulnerable for a few seconds.")),
			Make(TEXT("Devotion Aura"), true,  0.f,  0.f,  3, EKodoCastTarget::Instant, TEXT("Passive: reduces all incoming damage.")),
			Make(TEXT("Resurrection"),  false, 80.f, 60.f, 6, EKodoCastTarget::Instant, TEXT("Heal to full, cleanse effects, and gain a brief combat blessing.")),
		},
		// Dreadlord: Carrion Swarm, Sleep, Vampiric Aura (passive), Inferno (ult)
		{
			Make(TEXT("Carrion Swarm"), false, 40.f, 10.f, 1, EKodoCastTarget::TargetGround, TEXT("Unleash a wave of bats toward a point, damaging Kodos in its path.")),
			Make(TEXT("Sleep"),         false, 35.f, 12.f, 2, EKodoCastTarget::TargetKodo,   TEXT("Put a target Kodo to sleep, disabling it for several seconds.")),
			Make(TEXT("Vampiric Aura"), true,  0.f,  0.f,  3, EKodoCastTarget::Instant,      TEXT("Passive: heal for a portion of your basic-attack damage.")),
			Make(TEXT("Inferno"),       false, 90.f, 50.f, 6, EKodoCastTarget::TargetGround, TEXT("Call a meteor onto a target area, dealing heavy damage and stunning.")),
		},
	};

	const int32 ClassIdx = FMath::Clamp(static_cast<int32>(Class), 0, 5);
	const int32 SlotIdx = FMath::Clamp(Slot, 0, 3);
	return Table[ClassIdx][SlotIdx];
}

// =====================================================================================
// Purchasable hero items (Pass 3). File-static table keyed by EKodoItem. Passives apply a
// permanent stat hook while held; consumables are spent on use for an instant effect.
// Function-local-static storage keeps the returned reference valid (built once on first use).
// =====================================================================================
const FKodoItemDef& KodoItemDef(const EKodoItem Item)
{
	auto Make = [](const TCHAR* Name, int32 Cost, bool bConsumable) -> FKodoItemDef
	{
		FKodoItemDef D;
		D.Name = Name;
		D.Cost = Cost;
		D.bConsumable = bConsumable;
		return D;
	};

	static const FKodoItemDef None             = Make(TEXT("(empty)"),          0,   false);
	static const FKodoItemDef BootsOfSpeed      = Make(TEXT("Boots of Speed"),   150, false);
	static const FKodoItemDef ClawsOfAttack     = Make(TEXT("Claws of Attack"),  250, false);
	static const FKodoItemDef RingOfProtection  = Make(TEXT("Ring of Protection"), 200, false);
	static const FKodoItemDef PotionOfHealing   = Make(TEXT("Potion of Healing"),  100, true);
	static const FKodoItemDef PotionOfMana      = Make(TEXT("Potion of Mana"),     100, true);
	static const FKodoItemDef TomeOfExperience  = Make(TEXT("Tome of Experience"), 200, true);

	switch (Item)
	{
	case EKodoItem::BootsOfSpeed:     return BootsOfSpeed;
	case EKodoItem::ClawsOfAttack:    return ClawsOfAttack;
	case EKodoItem::RingOfProtection: return RingOfProtection;
	case EKodoItem::PotionOfHealing:  return PotionOfHealing;
	case EKodoItem::PotionOfMana:     return PotionOfMana;
	case EKodoItem::TomeOfExperience: return TomeOfExperience;
	default:                          return None;
	}
}

namespace
{
	/** Per-hero cast-FX tint (mirrors the body-tint palette used in SetHeroClass). */
	FLinearColor CastFxColorFor(const EKodoHeroClass HeroClass)
	{
		switch (HeroClass)
		{
		case EKodoHeroClass::MountainKing: return FLinearColor(0.f, 0.94f, 1.f);    // cyan
		case EKodoHeroClass::Blademaster:  return FLinearColor(0.82f, 0.53f, 0.44f); // d08770
		case EKodoHeroClass::Archmage:     return FLinearColor(0.46f, 0.62f, 0.93f); // arcane blue
		case EKodoHeroClass::FarSeer:      return FLinearColor(0.42f, 0.72f, 0.45f); // wilds green
		case EKodoHeroClass::Paladin:      return FLinearColor(0.92f, 0.82f, 0.45f); // holy gold
		case EKodoHeroClass::Dreadlord:    return FLinearColor(0.55f, 0.30f, 0.62f); // shadow purple
		default:                           return FLinearColor(0.f, 0.94f, 1.f);
		}
	}
}

ARunnerCharacter::ARunnerCharacter()
{
	// Runner base survivability (combat viability): HeroBaseMaxHp = 250 so the hero can trade blows
	// with kodos instead of dying instantly. 155 px/s base speed (entities.js:17-19). The Blademaster's
	// +15% and item modifiers arrive with hero classes in Phase 3B/4; War-Altar MaxHealth adds on top.
	MaxHp = HeroBaseMaxHp;
	Hp = HeroBaseMaxHp;
	MoveSpeedUU = KodoUnits::RunnerSpeedUU;

	// Seed basic-attack combat data for the default class (refreshed by SetHeroClass).
	CombatData = KodoHeroCombat(HeroClass);
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

void ARunnerCharacter::CancelAttack()
{
	AttackTargetKodo = nullptr;
	AttackTargetCell = FIntPoint(-1, -1);
	AttackTickTimer = 0.f;
}

void ARunnerCharacter::OrderAttack(AKodoCharacter* TargetKodo)
{
	if (!TargetKodo || bDead)
	{
		return;
	}
	// Issuing an attack cancels harvest and clears the other (building) target — only one active.
	CancelHarvest();
	AttackTargetCell = FIntPoint(-1, -1);
	AttackTargetKodo = TargetKodo;
	AttackTickTimer = 0.f;
	Msg(TEXT("Attacking kodo!"), FColor::Red);
}

void ARunnerCharacter::OrderAttackCell(const FIntPoint& Cell)
{
	if (bDead || !Grid || !Grid->IsInBounds(Cell))
	{
		return;
	}
	// Only player buildings (wall/tower/CC cells) are valid attack-to-destroy targets. The
	// indestructible admin_tower is excluded (chewing it would never resolve).
	const FGridCell& State = Grid->GetCell(Cell);
	const bool bBuilding = (State.Type == ECellType::Wall || State.Type == ECellType::Tower)
		&& State.StructureId != FName(TEXT("admin_tower"));
	if (!bBuilding)
	{
		return;
	}
	// Issuing an attack cancels harvest and clears the other (kodo) target — only one active.
	CancelHarvest();
	AttackTargetKodo = nullptr;
	AttackTargetCell = Cell;
	AttackTickTimer = 0.f;
	Msg(TEXT("Demolishing building!"), FColor::Orange);
}

void ARunnerCharacter::AttackNearestKodo()
{
	if (bDead)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Nearest alive (non-dying) kodo within a generous range (~12 cells), mirroring the Pass 2
	// FindNearestKodo helper used by the spell casts.
	const FVector MyLoc = GetActorLocation();
	const float MaxRangeUU = 12.f * KodoUnits::CellSizeUU;
	AKodoCharacter* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<AKodoCharacter> It(World); It; ++It)
	{
		if (It->IsDying()) { continue; }
		const float Dist = FVector::Dist2D(It->GetActorLocation(), MyLoc);
		if (Dist < BestDist && Dist <= MaxRangeUU)
		{
			BestDist = Dist;
			Best = *It;
		}
	}
	if (Best)
	{
		OrderAttack(Best);
	}
	else
	{
		Msg(TEXT("No kodo in range."), FColor::Yellow);
	}
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

void ARunnerCharacter::TickAttack(const float DeltaSeconds)
{
	// Resolve the active target's world location + a cell to path toward. Clear stale targets.
	const bool bHasKodo = AttackTargetKodo.IsValid();
	const bool bHasCell = AttackTargetCell.X >= 0 && AttackTargetCell.Y >= 0;
	if (!bHasKodo && !bHasCell)
	{
		return;
	}

	FVector TargetLoc;
	FIntPoint TargetCell;

	if (bHasKodo)
	{
		AKodoCharacter* Kodo = AttackTargetKodo.Get();
		// The kodo died / is dying: order complete.
		if (!Kodo || Kodo->IsDying() || Kodo->GetHp() <= 0.f)
		{
			CancelAttack();
			return;
		}
		TargetLoc = Kodo->GetActorLocation();
		TargetCell = Kodo->GetGridCell();
	}
	else
	{
		// Building target: verify it's still a player building (wall/tower/CC) at that cell.
		if (!Grid)
		{
			CancelAttack();
			return;
		}
		const FGridCell& State = Grid->GetCell(AttackTargetCell);
		const bool bBuilding = (State.Type == ECellType::Wall || State.Type == ECellType::Tower);
		if (!bBuilding)
		{
			CancelAttack(); // destroyed or already cleared
			return;
		}
		TargetLoc = Grid->CellToWorldCenter(AttackTargetCell);
		TargetCell = AttackTargetCell;
	}

	const float DistUU = FVector::Dist2D(GetActorLocation(), TargetLoc);

	if (DistUU > CombatData.AttackRangeUU)
	{
		// Out of range: close the gap. Re-path toward a walkable neighbor of the target cell so we
		// don't try to stand inside a blocked building/kodo cell (reuses the existing path stepper).
		AttackTickTimer = 0.f; // reset the swing timer while repositioning
		if (!HasPath())
		{
			PathToNeighborOf(TargetCell, 1);
		}
		return;
	}

	// In range: stop moving and swing on the AttackInterval cadence. War-Altar AttackSpeed shortens
	// the interval by 12% per level, capped at -60% (min 0.4x).
	SetPath(TArray<FKodoPathStep>());
	const float AtkSpdFactor = FMath::Max(0.4f, 1.f - 0.12f * static_cast<float>(HeroStatLevels[static_cast<int32>(EKodoHeroStat::AttackSpeed)]));
	const float EffectiveInterval = CombatData.AttackInterval * AtkSpdFactor;
	AttackTickTimer += DeltaSeconds;
	if (AttackTickTimer < EffectiveInterval)
	{
		return;
	}
	AttackTickTimer = 0.f;

	// Face the target for readability.
	const FVector Delta = TargetLoc - GetActorLocation();
	if (!Delta.IsNearlyZero())
	{
		const float YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		SetActorRotation(FRotator(0.f, YawDeg, 0.f));
	}

	if (bHasKodo)
	{
		if (AKodoCharacter* Kodo = AttackTargetKodo.Get())
		{
			// Base hit + War-Altar Damage upgrade (+8/level), times the temporary Avatar/Resurrection
			// attack buff (AttackBuffMult == 1 when none).
			const float DamageBonus = 8.f * static_cast<float>(HeroStatLevels[static_cast<int32>(EKodoHeroStat::Damage)]);
			float Damage = (CombatData.AttackDamage + DamageBonus) * AttackBuffMult;
				if (HasItem(EKodoItem::ClawsOfAttack)) { Damage += 10.f; } // Claws of Attack (passive item)
			const bool bPassiveUnlocked = IsAbilityUnlocked(2);

			// PASSIVE (slot 2): per-class on-hit effects, gated by level.
			if (bPassiveUnlocked)
			{
				if (HeroClass == EKodoHeroClass::MountainKing)
				{
					// Bash: ~25% chance to stun ~1 s + bonus damage.
					if (FMath::FRand() < 0.25f)
					{
						Damage += 15.f * GetAbilityScale();
						Kodo->ApplyStun(1.f);
					}
				}
				else if (HeroClass == EKodoHeroClass::Blademaster)
				{
					// Critical Strike: ~30% chance to deal 2x damage.
					if (FMath::FRand() < 0.30f)
					{
						Damage *= 2.f;
					}
				}
			}

			Kodo->ApplyDamageAmount(Damage);

			// Hit feedback: a small warm spark at the kodo so a landing melee swing reads on screen.
			const FVector KodoLoc = Kodo->GetActorLocation();
			if (AKodoCastEffect* Spark = GetWorld()->SpawnActor<AKodoCastEffect>(KodoLoc, FRotator::ZeroRotator))
			{
				Spark->Init(KodoLoc, FLinearColor(1.f, 0.85f, 0.2f), 0.5f * KodoUnits::CellSizeUU, 0.15f);
			}

			// PASSIVE (slot 2): Dreadlord Vampiric Aura — heal 40% of basic-attack damage dealt.
			if (bPassiveUnlocked && HeroClass == EKodoHeroClass::Dreadlord)
			{
				Hp = FMath::Min(MaxHp, Hp + 0.40f * Damage);
			}

			if (Kodo->GetHp() <= 0.f || Kodo->IsDying())
			{
				CancelAttack();
			}
		}
	}
	else if (Grid)
	{
		// Reuse the grid's existing structural-damage path: DamageCell reduces the cell Hp and, on
		// reaching 0, clears the whole footprint (ClearStructureFootprint) and fires OnCellChanged so
		// the StructureManager removes the visual — the same flow Kodos use to chew walls.
		const float WallDmg = CombatData.AttackDamage + 8.f * static_cast<float>(HeroStatLevels[static_cast<int32>(EKodoHeroStat::Damage)]);

		// Hit feedback: a small warm spark at the building cell so demolishing your own wall/tower reads.
		const FVector CellLoc = Grid->CellToWorldCenter(AttackTargetCell);
		if (AKodoCastEffect* Spark = GetWorld()->SpawnActor<AKodoCastEffect>(CellLoc, FRotator::ZeroRotator))
		{
			Spark->Init(CellLoc, FLinearColor(1.f, 0.85f, 0.2f), 0.5f * KodoUnits::CellSizeUU, 0.15f);
		}

		const bool bDestroyed = Grid->DamageCell(AttackTargetCell, WallDmg);
		if (bDestroyed)
		{
			Msg(TEXT("Building demolished!"), FColor::Orange);
			CancelAttack();
		}
	}
}

void ARunnerCharacter::SetHeroClass(const EKodoHeroClass NewClass)
{
	HeroClass = NewClass;
	SpellCooldown = 0.f;

	// Pass 2: clear per-slot cooldowns and any active buff/channel timers so the new class starts clean.
	for (int32 i = 0; i < 4; ++i) { SkillCooldowns[i] = 0.f; }
	AttackBuffTimer = 0.f; AttackBuffMult = 1.f;
	DamageReductionTimer = 0.f; DamageReductionMult = 1.f;
	BladestormTimer = 0.f; BlizzardTimer = 0.f; SummonTimer = 0.f;

	// Apply this hero's basic-attack combat data (Pass 1): range/damage/interval/speed mult.
	CombatData = KodoHeroCombat(NewClass);
	// Switching class drops any in-progress attack order (stats no longer match the target setup).
	CancelAttack();

	// Class tint on the blockout body (prototype hero palette).
	if (BodyMesh)
	{
		FLinearColor ClassColor(0.f, 0.94f, 1.f); // MK cyan
		switch (NewClass)
		{
		case EKodoHeroClass::MountainKing: ClassColor = FLinearColor(0.f, 0.94f, 1.f);    break; // cyan
		case EKodoHeroClass::Blademaster:  ClassColor = FLinearColor(0.82f, 0.53f, 0.44f); break; // d08770
		case EKodoHeroClass::Archmage:     ClassColor = FLinearColor(0.46f, 0.62f, 0.93f); break; // arcane blue
		case EKodoHeroClass::FarSeer:      ClassColor = FLinearColor(0.42f, 0.72f, 0.45f); break; // wilds green
		case EKodoHeroClass::Paladin:      ClassColor = FLinearColor(0.92f, 0.82f, 0.45f); break; // holy gold
		case EKodoHeroClass::Dreadlord:    ClassColor = FLinearColor(0.55f, 0.30f, 0.62f); break; // shadow purple
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

	float Final = Amount;

	// PASSIVE (slot 2): Paladin Devotion Aura reduces incoming damage by ~25% while unlocked.
	if (HeroClass == EKodoHeroClass::Paladin && IsAbilityUnlocked(2))
	{
		Final *= 0.75f;
	}

	// Blademaster Mirror Image buff: timed ~50% incoming-damage cut (decoy stand-in).
	if (DamageReductionTimer > 0.f)
	{
		Final *= DamageReductionMult;
	}

	// Ring of Protection (passive item): -15% incoming damage while held (stacks multiplicatively).
	if (HasItem(EKodoItem::RingOfProtection))
	{
		Final *= 0.85f;
	}

	// War-Altar Armor upgrade: -10% incoming damage per level, capped at 50% (stacks multiplicatively
	// with Devotion / Mirror Image / Ring of Protection).
	const int32 ArmorLvl = HeroStatLevels[static_cast<int32>(EKodoHeroStat::Armor)];
	if (ArmorLvl > 0)
	{
		Final *= (1.f - 0.10f * static_cast<float>(FMath::Min(ArmorLvl, 5)));
	}

	Super::ApplyDamageAmount(Final);
}

void ARunnerCharacter::CastSpell()
{
	// Thin wrapper so the existing Q binding keeps casting the primary active (slot 0).
	CastSkill(0);
}

bool ARunnerCharacter::CanCastSkill(const int32 Slot, FString& OutReason) const
{
	// Mirrors the EXACT gate order in CastSkillAt: passive -> locked -> cooldown -> mana.
	if (Slot < 0 || Slot > 3)
	{
		OutReason = TEXT("Invalid ability.");
		return false;
	}
	const FKodoAbility& A = KodoAbility(HeroClass, Slot);
	if (A.bPassive)
	{
		OutReason = TEXT("That ability is passive.");
		return false;
	}
	if (!IsAbilityUnlocked(Slot))
	{
		OutReason = FString::Printf(TEXT("%s unlocks at level %d"), *A.Name, A.UnlockLevel);
		return false;
	}
	if (SkillCooldowns[Slot] > 0.f)
	{
		OutReason = TEXT("On cooldown!");
		return false;
	}
	if (Mana < A.ManaCost)
	{
		OutReason = TEXT("Not enough mana!");
		return false;
	}
	OutReason.Reset();
	return true;
}

void ARunnerCharacter::CastSkill(const int32 Slot)
{
	// Backward-compatible / instant default: cast at the hero's own location, no unit target.
	CastSkillAt(Slot, GetActorLocation(), nullptr);
}

void ARunnerCharacter::CastSkillAt(const int32 Slot, const FVector& TargetLocation, AKodoCharacter* TargetKodo)
{
	// Pass 2: full 4-slot WC3 kits. slot0/slot1 actives, slot2 passive, slot3 ultimate.
	// Gating is the same order as CanCastSkill; we re-run it here so direct callers stay safe.
	if (Slot < 0 || Slot > 3)
	{
		return;
	}

	const FKodoAbility& A = KodoAbility(HeroClass, Slot);

	FString Why;
	if (!CanCastSkill(Slot, Why))
	{
		Msg(Why, FColor::Orange);
		return;
	}

	bool bSuccess = false;
	UWorld* World = GetWorld();

	// Hero-level ability scalar: each effect's magnitude grows with level
	// (1 + 0.15*(level-1), ~2.35x at level 10).
	const float AbilityScale = GetAbilityScale();
	const FVector MyLoc = GetActorLocation();

	// Small helper: nearest non-dying kodo to a world point, optionally within a max range.
	auto FindNearestKodo = [World](const FVector& From, float MaxRangeUU, AKodoCharacter* Exclude) -> AKodoCharacter*
	{
		AKodoCharacter* Best = nullptr;
		float BestDist = TNumericLimits<float>::Max();
		for (TActorIterator<AKodoCharacter> It(World); It; ++It)
		{
			if (It->IsDying() || *It == Exclude) { continue; }
			const float Dist = FVector::Dist2D(It->GetActorLocation(), From);
			if (Dist < BestDist && (MaxRangeUU <= 0.f || Dist <= MaxRangeUU))
			{
				BestDist = Dist;
				Best = *It;
			}
		}
		return Best;
	};

	switch (HeroClass)
	{
	// =================================================================================
	case EKodoHeroClass::MountainKing:
		switch (Slot)
		{
		case 0:
		{
			// Storm Bolt: the clicked kodo (TargetKodo) takes heavy damage + ~2 s stun; with no
			// explicit target, fall back to the nearest kodo within ~8 cells.
			AKodoCharacter* Target = TargetKodo ? TargetKodo
			                                    : FindNearestKodo(MyLoc, 8.f * KodoUnits::CellSizeUU, nullptr);
			if (!Target)
			{
				Msg(TEXT("No Kodo in range!"), FColor::Orange);
				return;
			}
			Target->ApplyDamageAmount(180.f * AbilityScale);
			Target->ApplyStun(2.f);
			// Spawn the existing hero-bolt projectile visual toward the target (cosmetic).
			if (World)
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				FVector SpawnLoc = MyLoc; SpawnLoc.Z = 150.f;
				if (AKodoProjectile* Bolt = World->SpawnActor<AKodoProjectile>(AKodoProjectile::StaticClass(),
				                                                               SpawnLoc, FRotator::ZeroRotator, Params))
				{
					FKodoProjectileConfig Config;
					Config.Type = FName("hero_bolt");
					Config.Damage = 0.f; // damage already applied directly above
					Config.SpeedUU = 360.f * KodoUnits::PxToUU;
					Config.RadiusPx = 9.f;
					Config.Color = FLinearColor(0.30f, 0.65f, 1.0f);
					Bolt->Init(Target, Config);
				}
			}
			Msg(TEXT("Storm Bolt!"), FColor::Cyan);
			bSuccess = true;
			break;
		}
		case 1:
		{
			// Thunder Clap: AoE ~3 cells, damage + ~1.5 s stun to all.
			const float RangeUU = 3.f * KodoUnits::CellSizeUU;
			for (TActorIterator<AKodoCharacter> It(World); It; ++It)
			{
				if (It->IsDying()) { continue; }
				if (FVector::Dist2D(It->GetActorLocation(), MyLoc) <= RangeUU)
				{
					It->ApplyDamageAmount(70.f * AbilityScale);
					It->ApplyStun(1.5f);
				}
			}
			Msg(TEXT("Thunder Clap!"), FColor::Cyan);
			bSuccess = true;
			break;
		}
		case 3:
		{
			// Avatar (ult): full heal + ~5 s invulnerability + a temporary basic-attack damage buff.
			Hp = MaxHp;
			ActivateInvulnerability(5.f);
			AttackBuffTimer = 5.f;
			AttackBuffMult = 1.6f;
			Msg(TEXT("AVATAR! Invulnerable + empowered!"), FColor::Cyan);
			bSuccess = true;
			break;
		}
		default: break; // slot 2 (passive) handled before the switch
		}
		break;

	// =================================================================================
	case EKodoHeroClass::Blademaster:
		switch (Slot)
		{
		case 0:
		{
			// Wind Walk: ~5 s, +50% speed (Tick), kodos lose aggro (entities.js:715-721).
			WindWalkTimer = 5.f;
			Msg(TEXT("Wind Walk!"), FColor::Cyan);
			bSuccess = true;
			break;
		}
		case 1:
		{
			// Mirror Image (SIMPLIFIED): no real decoy actors — grant a ~6 s 50% incoming-damage
			// cut (applied in ApplyDamageAmount) representing the images soaking hits.
			DamageReductionTimer = 6.f;
			DamageReductionMult = 0.5f;
			Msg(TEXT("Mirror Image! (decoys -> 50% damage taken)"), FColor::Cyan);
			bSuccess = true;
			break;
		}
		case 3:
		{
			// Bladestorm (ult): ~4 s channel; a periodic AoE around the hero damages kodos (Tick).
			BladestormTimer = 4.f;
			BladestormTickTimer = 0.f;
			Msg(TEXT("BLADESTORM!"), FColor::Red);
			bSuccess = true;
			break;
		}
		default: break;
		}
		break;

	// =================================================================================
	case EKodoHeroClass::Archmage:
		switch (Slot)
		{
		case 0:
		{
			// Blizzard: AoE waves centered on the TARGETED ground spot over ~3 s (BlizzardTimer in
			// Tick). GroundCastLoc records the click so the Tick waves stay put as the hero moves.
			BlizzardTimer = 3.f;
			BlizzardTickTimer = 0.f;
			GroundCastLoc = TargetLocation;
			BlizzardCenter = TargetLocation;
			Msg(TEXT("Blizzard!"), FColor::Cyan);
			bSuccess = true;
			break;
		}
		case 1:
		{
			// Summon Water Elemental (SIMPLIFIED): no real pet actor — a ~10 s periodic AoE near the
			// hero (SummonTimer in Tick) represents the elemental fighting alongside.
			SummonTimer = 10.f;
			SummonTickTimer = 0.f;
			SummonDamage = 25.f * AbilityScale;
			Msg(TEXT("Water Elemental summoned! (aura stand-in)"), FColor::Cyan);
			bSuccess = true;
			break;
		}
		case 3:
		{
			// Mass Teleport (ult): blink to the nearest owned Command Center (reuses Blink-style move).
			if (Grid)
			{
				FIntPoint CC;
				if (Grid->FindNearestCommandCenter(GetGridCell(), CC))
				{
					FVector NewLoc = Grid->CellToWorldCenter(CC);
					NewLoc.Z = MyLoc.Z;
					SetActorLocation(NewLoc);
					SetPath(TArray<FKodoPathStep>());
					Msg(TEXT("Mass Teleport — recalled to base!"), FColor::Cyan);
					bSuccess = true;
				}
				else
				{
					Msg(TEXT("No Command Center to teleport to!"), FColor::Orange);
					return;
				}
			}
			break;
		}
		default: break;
		}
		break;

	// =================================================================================
	case EKodoHeroClass::FarSeer:
		switch (Slot)
		{
		case 0:
		{
			// Chain Lightning: the clicked kodo (TargetKodo) is the first link, then bounce to up to
			// 4 more (~20% falloff each); with no explicit target, start on the nearest within ~9 cells.
			AKodoCharacter* Current = TargetKodo ? TargetKodo
			                                     : FindNearestKodo(MyLoc, 9.f * KodoUnits::CellSizeUU, nullptr);
			if (!Current)
			{
				Msg(TEXT("No Kodo in range!"), FColor::Orange);
				return;
			}
			float Dmg = 110.f * AbilityScale;
			TSet<AKodoCharacter*> Hit;
			for (int32 Bounce = 0; Bounce < 5 && Current; ++Bounce)
			{
				Current->ApplyDamageAmount(Dmg);
				Hit.Add(Current);
				Dmg *= 0.8f; // 20% falloff
				// Next nearest not yet hit, within ~3 cells of the current link.
				const FVector From = Current->GetActorLocation();
				AKodoCharacter* Next = nullptr;
				float BestDist = TNumericLimits<float>::Max();
				for (TActorIterator<AKodoCharacter> It(World); It; ++It)
				{
					if (It->IsDying() || Hit.Contains(*It)) { continue; }
					const float D = FVector::Dist2D(It->GetActorLocation(), From);
					if (D < BestDist && D <= 3.f * KodoUnits::CellSizeUU) { BestDist = D; Next = *It; }
				}
				Current = Next;
			}
			Msg(TEXT("Chain Lightning!"), FColor::Cyan);
			bSuccess = true;
			break;
		}
		case 1:
		{
			// Feral Spirit (SIMPLIFIED): like Water Elemental — a ~10 s periodic AoE near the hero.
			SummonTimer = 10.f;
			SummonTickTimer = 0.f;
			SummonDamage = 28.f * AbilityScale;
			Msg(TEXT("Feral Spirits summoned! (aura stand-in)"), FColor::Cyan);
			bSuccess = true;
			break;
		}
		case 3:
		{
			// Earthquake (ult): AoE ~4 cells centered on the TARGETED ground spot — heavy damage +
			// stun to kodos, and damage to player buildings in range via Grid->DamageCell.
			const float RangeUU = 4.f * KodoUnits::CellSizeUU;
			for (TActorIterator<AKodoCharacter> It(World); It; ++It)
			{
				if (It->IsDying()) { continue; }
				if (FVector::Dist2D(It->GetActorLocation(), TargetLocation) <= RangeUU)
				{
					It->ApplyDamageAmount(120.f * AbilityScale);
					It->ApplyStun(2.f);
				}
			}
			if (Grid)
			{
				const FIntPoint CenterCell = Grid->WorldToCell(TargetLocation);
				const int32 R = 4;
				for (int32 Dx = -R; Dx <= R; ++Dx)
				{
					for (int32 Dy = -R; Dy <= R; ++Dy)
					{
						const FIntPoint C(CenterCell.X + Dx, CenterCell.Y + Dy);
						if (!Grid->IsInBounds(C)) { continue; }
						if (FVector2D(Dx, Dy).Size() > static_cast<float>(R)) { continue; }
						const ECellType T = Grid->GetCell(C).Type;
						if (T == ECellType::Wall || T == ECellType::Tower)
						{
							Grid->DamageCell(C, 60.f * AbilityScale);
						}
					}
				}
			}
			Msg(TEXT("EARTHQUAKE!"), FColor::Red);
			bSuccess = true;
			break;
		}
		default: break;
		}
		break;

	// =================================================================================
	case EKodoHeroClass::Paladin:
		switch (Slot)
		{
		case 0:
		{
			// Holy Light: large flat self-heal scaled by level (always succeeds, no target needed).
			Hp = FMath::Min(MaxHp, Hp + 120.f * AbilityScale);
			Msg(TEXT("Holy Light — healed!"), FColor::Yellow);
			bSuccess = true;
			break;
		}
		case 1:
		{
			// Divine Shield: ~4 s invulnerability.
			ActivateInvulnerability(4.f);
			Msg(TEXT("Divine Shield!"), FColor::Yellow);
			bSuccess = true;
			break;
		}
		case 3:
		{
			// Resurrection (ult): full heal + clear debuffs/buff timers + a strong temporary buff
			// (~3 s invulnerability + attack buff).
			Hp = MaxHp;
			DamageReductionTimer = 0.f;
			DamageReductionMult = 1.f;
			ActivateInvulnerability(3.f);
			AttackBuffTimer = 5.f;
			AttackBuffMult = 1.5f;
			Msg(TEXT("RESURRECTION! Restored and empowered!"), FColor::Yellow);
			bSuccess = true;
			break;
		}
		default: break;
		}
		break;

	// =================================================================================
	case EKodoHeroClass::Dreadlord:
		switch (Slot)
		{
		case 0:
		{
			// Carrion Swarm: forward line/cone — damage kodos within ~5 cells. The cone aims at the
			// TARGETED ground spot (direction = target - hero), falling back to the hero's facing.
			FVector Facing = (TargetLocation - MyLoc).GetSafeNormal2D();
			if (Facing.IsNearlyZero()) { Facing = GetActorForwardVector().GetSafeNormal2D(); }
			if (Facing.IsNearlyZero()) { Facing = FVector(1.f, 0.f, 0.f); }
			const float RangeUU = 5.f * KodoUnits::CellSizeUU;
			for (TActorIterator<AKodoCharacter> It(World); It; ++It)
			{
				if (It->IsDying()) { continue; }
				const FVector ToK = (It->GetActorLocation() - MyLoc);
				const float Dist = ToK.Size2D();
				if (Dist > RangeUU || Dist < 1.f) { continue; }
				// Cone: within ~60 deg of facing (dot > 0.5).
				if (FVector::DotProduct(Facing, ToK.GetSafeNormal2D()) > 0.5f)
				{
					It->ApplyDamageAmount(90.f * AbilityScale);
				}
			}
			Msg(TEXT("Carrion Swarm!"), FColor::Magenta);
			bSuccess = true;
			break;
		}
		case 1:
		{
			// Sleep: long ~5 s disable on the clicked kodo (TargetKodo); with no explicit target,
			// fall back to the nearest single kodo within ~8 cells (stun stand-in).
			AKodoCharacter* Target = TargetKodo ? TargetKodo
			                                    : FindNearestKodo(MyLoc, 8.f * KodoUnits::CellSizeUU, nullptr);
			if (!Target)
			{
				Msg(TEXT("No Kodo in range!"), FColor::Orange);
				return;
			}
			Target->ApplyStun(5.f);
			Msg(TEXT("Sleep!"), FColor::Magenta);
			bSuccess = true;
			break;
		}
		case 3:
		{
			// Inferno (ult): big AoE ~3 cells on the TARGETED ground spot — heavy damage + ~2 s stun,
			// plus a brief lingering damage zone via the Summon-style periodic-AoE timer near the hero.
			const float RangeUU = 3.f * KodoUnits::CellSizeUU;
			for (TActorIterator<AKodoCharacter> It(World); It; ++It)
			{
				if (It->IsDying()) { continue; }
				if (FVector::Dist2D(It->GetActorLocation(), TargetLocation) <= RangeUU)
				{
					It->ApplyDamageAmount(150.f * AbilityScale);
					It->ApplyStun(2.f);
				}
			}
			// Lingering flames: a short periodic AoE zone reusing the Summon timer path.
			SummonTimer = 4.f;
			SummonTickTimer = 0.f;
			SummonDamage = 30.f * AbilityScale;
			Msg(TEXT("INFERNO!"), FColor::Red);
			bSuccess = true;
			break;
		}
		default: break;
		}
		break;

	default:
		break;
	}

	if (bSuccess)
	{
		Mana -= A.ManaCost;
		SkillCooldowns[Slot] = A.Cooldown;

		// Cast feedback: a quick expanding shockwave disc, tinted per hero class. Ground/target
		// spells burst at the target spot; self/instant spells burst at the hero. Additional to
		// any projectile (Storm Bolt etc.) — purely cosmetic, self-destroys.
		if (World)
		{
			const bool bAtTarget = (A.Target == EKodoCastTarget::TargetGround || A.Target == EKodoCastTarget::TargetKodo);
			const FVector FxLoc = bAtTarget ? TargetLocation : MyLoc;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AKodoCastEffect* Fx = World->SpawnActor<AKodoCastEffect>(AKodoCastEffect::StaticClass(),
			                                                             FxLoc, FRotator::ZeroRotator, Params))
			{
				Fx->Init(FxLoc, CastFxColorFor(HeroClass), 2.5f * KodoUnits::CellSizeUU, 0.4f);
			}
		}
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

	// Per-ability-slot cooldowns (Pass 2).
	for (int32 i = 0; i < 4; ++i)
	{
		SkillCooldowns[i] = FMath::Max(0.f, SkillCooldowns[i] - DeltaSeconds);
	}

	// Pass 2 buff timers (basic-attack buff + Mirror Image damage-reduction).
	AttackBuffTimer = FMath::Max(0.f, AttackBuffTimer - DeltaSeconds);
	if (AttackBuffTimer <= 0.f) { AttackBuffMult = 1.f; }
	DamageReductionTimer = FMath::Max(0.f, DamageReductionTimer - DeltaSeconds);
	if (DamageReductionTimer <= 0.f) { DamageReductionMult = 1.f; }

	// Pass 2 channels: Bladestorm (Blademaster ult) — periodic AoE around the hero ~every 0.5 s.
	if (BladestormTimer > 0.f)
	{
		BladestormTimer = FMath::Max(0.f, BladestormTimer - DeltaSeconds);
		BladestormTickTimer += DeltaSeconds;
		if (BladestormTickTimer >= 0.5f)
		{
			BladestormTickTimer = 0.f;
			const float RangeUU = 2.5f * KodoUnits::CellSizeUU;
			const float Dmg = 50.f * GetAbilityScale();
			const FVector Loc = GetActorLocation();
			for (TActorIterator<AKodoCharacter> It(GetWorld()); It; ++It)
			{
				if (It->IsDying()) { continue; }
				if (FVector::Dist2D(It->GetActorLocation(), Loc) <= RangeUU)
				{
					It->ApplyDamageAmount(Dmg);
				}
			}
		}
	}

	// Pass 2 channel: Blizzard (Archmage primary) — damage waves on the cast spot ~every 0.6 s.
	if (BlizzardTimer > 0.f)
	{
		BlizzardTimer = FMath::Max(0.f, BlizzardTimer - DeltaSeconds);
		BlizzardTickTimer += DeltaSeconds;
		if (BlizzardTickTimer >= 0.6f)
		{
			BlizzardTickTimer = 0.f;
			const float RangeUU = 3.f * KodoUnits::CellSizeUU;
			const float Dmg = 45.f * GetAbilityScale();
			// Waves land on the targeted ground spot (GroundCastLoc), not the hero's live location.
			for (TActorIterator<AKodoCharacter> It(GetWorld()); It; ++It)
			{
				if (It->IsDying()) { continue; }
				if (FVector::Dist2D(It->GetActorLocation(), GroundCastLoc) <= RangeUU)
				{
					It->ApplyDamageAmount(Dmg);
				}
			}
		}
	}

	// Pass 2 "summon" stand-in (Water Elemental / Feral Spirit / Inferno flames) — periodic AoE
	// near the hero ~every 1.0 s for the duration. SIMPLIFIED: no real pet actor is spawned.
	if (SummonTimer > 0.f)
	{
		SummonTimer = FMath::Max(0.f, SummonTimer - DeltaSeconds);
		SummonTickTimer += DeltaSeconds;
		if (SummonTickTimer >= 1.0f)
		{
			SummonTickTimer = 0.f;
			const float RangeUU = 2.5f * KodoUnits::CellSizeUU;
			const FVector Loc = GetActorLocation();
			for (TActorIterator<AKodoCharacter> It(GetWorld()); It; ++It)
			{
				if (It->IsDying()) { continue; }
				if (FVector::Dist2D(It->GetActorLocation(), Loc) <= RangeUU)
				{
					It->ApplyDamageAmount(SummonDamage);
				}
			}
		}
	}

	// Mana regen toward MaxMana. PASSIVE (slot 2): Archmage Brilliance Aura and FarSeer Far Sight
	// boost regen while their passive is unlocked (level-gated, independent of research flags).
	float ManaRegen = ManaRegenPerSec;
	if (IsAbilityUnlocked(2))
	{
		if (HeroClass == EKodoHeroClass::Archmage) { ManaRegen += 6.f; }      // Brilliance Aura
		else if (HeroClass == EKodoHeroClass::FarSeer) { ManaRegen += 3.f; }  // Far Sight (+regen)
	}
	// War-Altar Mana Regen upgrade: +2 mana/s per level.
	ManaRegen += 2.f * static_cast<float>(HeroStatLevels[static_cast<int32>(EKodoHeroStat::ManaRegen)]);
	Mana = FMath::Min(MaxMana, Mana + ManaRegen * DeltaSeconds);

	// Speed: class base, +50% during Wind Walk (entities.js:58-69), plus the per-mode
	// multiplier (God 1.25) and Boots of Speed (+25% when owned). The Blademaster's +15%
	// base-speed passive (slot 2) only applies once researched (bSkill2Unlocked).
	float SpeedMult = 1.f;
	if (WindWalkTimer > 0.f)
	{
		SpeedMult += 0.5f;
	}
	const float BootsMult = HasItem(EKodoItem::BootsOfSpeed) ? 1.25f : 1.f; // Boots of Speed (inventory)
	// CombatData.MoveSpeedMult lets heroes differ in base move speed (all 1.0 in Pass 1).
	MoveSpeedUU = BaseSpeedPxFor(HeroClass, bSkill2Unlocked) * CombatData.MoveSpeedMult
		* SpeedMult * ModeSpeedMult * BootsMult * KodoUnits::PxToUU;

	// Passive HP regen (entities.js:113-115). Base is 0.5 HP/s for everyone; the Mountain
	// King's faster 2.0 HP/s is a researched passive (slot 2) — only applies when unlocked.
	const float RegenPerSec = (HeroClass == EKodoHeroClass::MountainKing && bSkill2Unlocked) ? 2.0f : 0.5f;
	Hp = FMath::Min(MaxHp, Hp + RegenPerSec * DeltaSeconds);

	TickHarvesting(DeltaSeconds);
	TickDeposit();
	TickAttack(DeltaSeconds);
	TickStatUpgrades(DeltaSeconds);
}

void ARunnerCharacter::OnZeroHp()
{
	if (bDead)
	{
		return;
	}
	bDead = true;
	CancelHarvest();
	CancelAttack();
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

bool ARunnerCharacter::BuyItem(const EKodoItem Item)
{
	if (Item == EKodoItem::None)
	{
		return false;
	}

	const FKodoItemDef& Def = KodoItemDef(Item);

	// Inventory full?
	if (Inventory.Num() >= GetInventorySize())
	{
		Msg(TEXT("Inventory is full!"), FColor::Orange);
		return false;
	}

	// Non-consumable passives can only be owned once.
	if (!Def.bConsumable && HasItem(Item))
	{
		Msg(FString::Printf(TEXT("You already own %s!"), *Def.Name), FColor::Orange);
		return false;
	}

	AKodoTagGameState* GS = GetWorld() ? GetWorld()->GetGameState<AKodoTagGameState>() : nullptr;
	if (!GS || GS->Gold < static_cast<float>(Def.Cost))
	{
		Msg(FString::Printf(TEXT("Not enough gold for %s!"), *Def.Name), FColor::Red);
		return false;
	}

	GS->Spend(static_cast<float>(Def.Cost), 0.f);
	Inventory.Add(Item);
	Msg(FString::Printf(TEXT("Bought %s!"), *Def.Name), FColor::Green);
	return true;
}

void ARunnerCharacter::UseInventorySlot(const int32 Slot)
{
	if (Slot < 0 || Slot >= Inventory.Num())
	{
		return;
	}

	const EKodoItem Item = Inventory[Slot];
	const FKodoItemDef& Def = KodoItemDef(Item);

	if (!Def.bConsumable)
	{
		Msg(TEXT("Passive item — always active."), FColor::Orange);
		return;
	}

	switch (Item)
	{
	case EKodoItem::PotionOfHealing:
		Hp = FMath::Min(MaxHp, Hp + 0.5f * MaxHp);
		Msg(TEXT("Potion of Healing — restored 50% HP!"), FColor::Green);
		break;
	case EKodoItem::PotionOfMana:
		Mana = FMath::Min(MaxMana, Mana + 0.5f * MaxMana);
		Msg(TEXT("Potion of Mana — restored 50% mana!"), FColor::Cyan);
		break;
	case EKodoItem::TomeOfExperience:
		// Grant enough XP for roughly one level at the current threshold.
		GainXp(GetXpForNextLevel());
		Msg(TEXT("Tome of Experience — gained a level's XP!"), FColor::Yellow);
		break;
	default:
		// Unknown consumable: nothing to apply, leave it in place.
		return;
	}

	Inventory.RemoveAt(Slot);
}

bool ARunnerCharacter::BuyBoots()
{
	// Legacy entry routed through the unified item-purchase path.
	return BuyItem(EKodoItem::BootsOfSpeed);
}

namespace
{
	/** Player-facing label for a hero-upgrade stat (shared by queue + completion messages). */
	const TCHAR* HeroStatLabel(const EKodoHeroStat Stat)
	{
		switch (Stat)
		{
		case EKodoHeroStat::Damage:      return TEXT("Damage");
		case EKodoHeroStat::Armor:       return TEXT("Armor");
		case EKodoHeroStat::AttackSpeed: return TEXT("Attack Speed");
		case EKodoHeroStat::ManaRegen:   return TEXT("Mana Regen");
		case EKodoHeroStat::MaxHealth:   return TEXT("Max Health");
		default:                         return TEXT("Stat");
		}
	}
}

int32 ARunnerCharacter::GetQueuedStatCount(const EKodoHeroStat Stat) const
{
	int32 Count = 0;
	for (const FHeroStatUpgrade& U : PendingStatUpgrades)
	{
		if (U.Stat == Stat) { ++Count; }
	}
	return Count;
}

bool ARunnerCharacter::GetStatUpgradeProgress(const EKodoHeroStat Stat, float& OutFrac, float& OutRemaining) const
{
	// The currently-researching entry (front) shows live elapsed fill; any other queued entry of this
	// stat shows as "queued" (0% fill, full duration remaining). Return false when none queued.
	if (PendingStatUpgrades.Num() > 0 && PendingStatUpgrades[0].Stat == Stat)
	{
		const FHeroStatUpgrade& Front = PendingStatUpgrades[0];
		OutFrac = (Front.TotalTime > 0.f) ? (Front.TotalTime - Front.TimeRemaining) / Front.TotalTime : 0.f;
		OutFrac = FMath::Clamp(OutFrac, 0.f, 1.f);
		OutRemaining = Front.TimeRemaining;
		return true;
	}
	for (const FHeroStatUpgrade& U : PendingStatUpgrades)
	{
		if (U.Stat == Stat)
		{
			OutFrac = 0.f;          // queued but not yet started
			OutRemaining = U.TotalTime;
			return true;
		}
	}
	return false;
}

bool ARunnerCharacter::UpgradeHeroStat(const EKodoHeroStat Stat)
{
	const int32 Idx = static_cast<int32>(Stat);
	if (Idx < 0 || Idx >= 5)
	{
		return false;
	}

	// Prospective level = applied level + already-queued upgrades of this stat, so repeated queueing
	// raises the effective target (and the cost) each time.
	const int32 QueuedCount = GetQueuedStatCount(Stat);
	const int32 TargetLevel = HeroStatLevels[Idx] + QueuedCount;

	// Cap each stat at GetHeroStatMaxLevel() (5), counting queued upgrades.
	if (TargetLevel >= GetHeroStatMaxLevel())
	{
		Msg(TEXT("That hero upgrade is already maxed!"), FColor::Orange);
		return false;
	}

	// Gold-only cost via the GameState, mirroring the merchant/research purchases. Cost reflects the
	// prospective level being bought (matches GetHeroStatCost which also counts queued).
	const int32 Cost = 100 + 75 * TargetLevel;
	AKodoTagGameState* GS = GetKodoGameState();
	if (!GS || GS->Gold < static_cast<float>(Cost))
	{
		Msg(TEXT("Not enough gold for that hero upgrade!"), FColor::Red);
		return false;
	}
	GS->Spend(static_cast<float>(Cost), 0.f);

	// Research duration scales a little with the target level: 6 + 3*level, clamped ~6..20 s.
	const float Duration = FMath::Clamp(6.f + 3.f * static_cast<float>(TargetLevel), 6.f, 20.f);
	PendingStatUpgrades.Add(FHeroStatUpgrade{ Stat, Duration, Duration });

	Msg(FString::Printf(TEXT("Researching %s..."), HeroStatLabel(Stat)), FColor::Green);
	return true;
}

void ARunnerCharacter::TickStatUpgrades(const float DeltaSeconds)
{
	// Sequential build-queue style: only the FRONT entry counts down. Process at most a completion per
	// frame (a while-loop guards the unlikely ~0 duration case without consuming the whole queue in one tick).
	while (PendingStatUpgrades.Num() > 0)
	{
		FHeroStatUpgrade& Front = PendingStatUpgrades[0];
		Front.TimeRemaining -= DeltaSeconds;
		if (Front.TimeRemaining > 0.f)
		{
			break; // still researching the front item
		}

		// Front item complete: apply the stat. Damage / Armor / AttackSpeed / ManaRegen are read live
		// where they matter (TickAttack / ApplyDamageAmount / Tick), so they need no extra bookkeeping.
		// MaxHealth grows the live MaxHp by +75 and heals by the same delta (as the instant path did).
		const EKodoHeroStat Stat = Front.Stat;
		const int32 Idx = static_cast<int32>(Stat);
		if (Idx >= 0 && Idx < 5)
		{
			++HeroStatLevels[Idx];
			const int32 NewLevel = HeroStatLevels[Idx];

			if (Stat == EKodoHeroStat::MaxHealth)
			{
				MaxHp += 75.f;
				Hp = FMath::Min(MaxHp, Hp + 75.f); // heal by the gained delta, clamped to MaxHp
			}

			Msg(FString::Printf(TEXT("%s upgraded to Lv %d"), HeroStatLabel(Stat), NewLevel), FColor::Green);
		}

		PendingStatUpgrades.RemoveAt(0);
		// Loop again only if the next front item already has TimeRemaining <= 0 (won't normally happen).
	}
}
