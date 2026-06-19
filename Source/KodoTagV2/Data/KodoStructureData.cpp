// Kodo Tag: Survivor — UE Migration, Phase 3.
// Data port of js/config/towers.js — every number verified against source.

#include "Data/KodoStructureData.h"

namespace
{
	FKodoStructureStats MakeLevel(const TCHAR* Name, const int32 Gold, const float MaxHp = 0.f,
	                              const float Cooldown = 0.f, const float Damage = 0.f)
	{
		FKodoStructureStats L;
		L.DisplayName = Name;
		L.GoldCost = Gold;
		L.MaxHp = MaxHp;
		L.CooldownSeconds = Cooldown;
		L.Damage = Damage;
		return L;
	}

	TMap<FName, FKodoStructurePreset> BuildRegistry()
	{
		TMap<FName, FKodoStructurePreset> R;

		// --- wall (WC3 "Spiked Box": 50g / 15hp — kodo_balance_data.md §2/§5) ---
		{
			FKodoStructurePreset P;
			P.Id = "wall"; P.DisplayName = TEXT("Spiked Box");
			// WC3 walls cost 50g and are fragile (15hp). Now a 2x2 barrier snapped to the even
			// 2x2 lattice (see ComputeBuildOrigin) so a row of walls tiles flush edge-to-edge with
			// no 1-cell jog a 2x2 Kodo could slip through. Upgrade tiers preserved structurally,
			// tuned toward the WC3 "Magical Wall" (30hp) tier.
			P.GoldCost = 50; P.MaxHp = 15.f; P.ConstructionSeconds = 2.5f; P.bIs2x2 = true; P.FootprintSize = 2; P.Food = 1.f;
			P.Levels.Add(MakeLevel(TEXT("Spiked Box"), 50, 15.f));
			P.Levels.Add(MakeLevel(TEXT("Magical Wall"), 25, 30.f));
			P.Levels.Add(MakeLevel(TEXT("Reinforced Wall"), 50, 60.f));
			R.Add(P.Id, P);
		}
		// --- magic_wall (WC3 "Magical Wall": 5g / 30hp — sturdier 1x1 wall tier) ---
		{
			FKodoStructurePreset P;
			P.Id = "magic_wall"; P.DisplayName = TEXT("Magical Wall");
			P.GoldCost = 5; P.WoodCost = 0; P.MaxHp = 30.f; P.ConstructionSeconds = 2.5f; P.bIs2x2 = true; P.FootprintSize = 2; P.Food = 1.f;
			P.Levels.Add(MakeLevel(TEXT("Magical Wall"), 5, 30.f));
			R.Add(P.Id, P);
		}
		// --- basic_tower (WC3 "Basic Tower": 75g+30l / 40hp / 47dmg / 1.1cd — §2/§5) ---
		{
			FKodoStructurePreset P;
			P.Id = "basic_tower"; P.DisplayName = TEXT("Basic Tower");
			P.GoldCost = 75; P.WoodCost = 30; P.MaxHp = 40.f; P.RangeTiles = 5.5f; P.CooldownSeconds = 1.1f; P.Damage = 47.f; P.bIs2x2 = true; P.FootprintSize = 2; P.Food = 2.f;
			P.Levels.Add(MakeLevel(TEXT("Basic Tower Lvl 1"), 75, 40.f, 1.1f, 47.f));      // WC3 o008
			P.Levels.Add(MakeLevel(TEXT("Basic Tower Lvl 2"), 125, 50.f, 1.1f, 76.f));     // WC3 o025
			R.Add(P.Id, P);
		}
		// --- arrow (WC3 rapid basic o00A: 75g+30l / fast cd / low dmg — §2/§5) ---
		{
			FKodoStructurePreset P;
			P.Id = "arrow"; P.DisplayName = TEXT("Rapid Tower");
			// WC3 o00A: 6 dmg @ 0.33 cd (rapid-fire). Upgrade o027: 9 dmg.
			P.GoldCost = 75; P.WoodCost = 30; P.MaxHp = 40.f; P.RangeTiles = 5.5f; P.CooldownSeconds = 0.33f; P.Damage = 6.f; P.bIs2x2 = true; P.FootprintSize = 2; P.Food = 2.f;
			P.Levels.Add(MakeLevel(TEXT("Rapid Tower Lvl 1"), 75, 40.f, 0.33f, 6.f));
			P.Levels.Add(MakeLevel(TEXT("Rapid Tower Lvl 2"), 125, 50.f, 0.33f, 9.f));
			R.Add(P.Id, P);
		}
		// --- frost (towers.js:57-73) ---
		{
			FKodoStructurePreset P;
			P.Id = "frost"; P.DisplayName = TEXT("Frost Tower");
			P.GoldCost = 45; P.MaxHp = 200.f; P.RangeTiles = 5.0f; P.CooldownSeconds = 1.2f; P.Damage = 12.f;
			P.Special = "frost"; P.bIs2x2 = true; P.FootprintSize = 2; P.Food = 2.f;
			// WC3 Slow/Poison tower (§2): movement-slow tiers 30% / 50% / 70% + a poison DoT
			// (3 / 5 / 10 dmg/s). We only model MOVE-slow here; attack-speed slow is NOT
			// supported (deferred). The poison DoT is approximated by the tower's per-hit Damage.
			FKodoStructureStats L1 = MakeLevel(TEXT("Slow Tower Lvl 1"), 0, 0.f, 0.f, 3.f);  L1.SlowPercent = 0.30f;
			FKodoStructureStats L2 = MakeLevel(TEXT("Slow Tower Lvl 2"), 45, 0.f, 0.f, 5.f);  L2.SlowPercent = 0.50f;
			FKodoStructureStats L3 = MakeLevel(TEXT("Slow Tower Lvl 3"), 80, 0.f, 0.f, 10.f); L3.SlowPercent = 0.70f;
			P.Levels = { L1, L2, L3 };
			R.Add(P.Id, P);
		}
		// --- stun (towers.js:74-91) ---
		{
			FKodoStructurePreset P;
			P.Id = "stun"; P.DisplayName = TEXT("Stun Tower");
			P.GoldCost = 60; P.MaxHp = 220.f; P.RangeTiles = 6.0f; P.CooldownSeconds = 1.8f; P.Damage = 40.f;
			P.Special = "stun"; P.RequiresUpgrade = "stunUnlocked"; P.bIs2x2 = true; P.FootprintSize = 2; P.Food = 2.f;
			// WC3 Stun (bash) tower (§2): base 8% chance; upgrades 16% / named variants 30-50%.
			FKodoStructureStats L1 = MakeLevel(TEXT("Stun Tower Lvl 1"), 0, 0.f, 0.f, 40.f);
			L1.StunChance = 0.08f; L1.StunDurationSeconds = 1.5f;
			FKodoStructureStats L2 = MakeLevel(TEXT("Thunder Bolt Tower Lvl 2"), 60, 0.f, 0.f, 110.f);
			L2.StunChance = 0.16f; L2.StunDurationSeconds = 1.8f;
			FKodoStructureStats L3 = MakeLevel(TEXT("Titan Slammer Lvl 3"), 110, 0.f, 0.f, 320.f);
			L3.StunChance = 0.30f; L3.StunDurationSeconds = 2.2f;
			P.Levels = { L1, L2, L3 };
			R.Add(P.Id, P);
		}
		// --- aoe (towers.js:92-109) ---
		{
			FKodoStructurePreset P;
			P.Id = "aoe"; P.DisplayName = TEXT("Flame/AoE Tower");
			// WC3 Flame/AoE tower (§2/§5): 400g + 150l, 150 dmg + splash/burn. Much pricier
			// than the prototype's 75g cannon. Upgrade tiers preserved structurally.
			P.GoldCost = 400; P.WoodCost = 150; P.MaxHp = 50.f; P.RangeTiles = 4.5f; P.CooldownSeconds = 6.0f; P.Damage = 150.f;
			P.Special = "aoe"; P.RequiresUpgrade = "aoeUnlocked"; P.bIs2x2 = true; P.FootprintSize = 2; P.Food = 2.f;
			FKodoStructureStats L1 = MakeLevel(TEXT("Flame Tower Lvl 1"), 400, 50.f, 6.0f, 150.f); L1.SplashRadiusTiles = 2.0f;
			FKodoStructureStats L2 = MakeLevel(TEXT("Inferno Tower Lvl 2"), 200, 50.f, 5.0f, 220.f); L2.SplashRadiusTiles = 2.3f;
			FKodoStructureStats L3 = MakeLevel(TEXT("Cataclysm Engine Lvl 3"), 300, 50.f, 5.0f, 375.f); L3.SplashRadiusTiles = 2.7f;
			P.Levels = { L1, L2, L3 };
			R.Add(P.Id, P);
		}
		// --- multishot (towers.js:110-127) ---
		{
			FKodoStructurePreset P;
			P.Id = "multishot"; P.DisplayName = TEXT("Multishot Tower");
			P.GoldCost = 80; P.MaxHp = 200.f; P.RangeTiles = 5.5f; P.CooldownSeconds = 0.9f; P.Damage = 18.f;
			P.Special = "multishot"; P.RequiresUpgrade = "multishotUnlocked"; P.bIs2x2 = true; P.FootprintSize = 2; P.Food = 2.f;
			// WC3 bounce (§2): attacks hit up to 3 targets, each bounce -40%/-60% falloff.
			// L1 set to 3 targets to match WC3; falloff handled by the bounce special at runtime.
			FKodoStructureStats L1 = MakeLevel(TEXT("Multishot Tower Lvl 1"), 0, 0.f, 0.f, 18.f); L1.MaxTargets = 3;
			FKodoStructureStats L2 = MakeLevel(TEXT("Spreadshot Nest Lvl 2"), 70, 0.f, 0.f, 48.f); L2.MaxTargets = 4;
			FKodoStructureStats L3 = MakeLevel(TEXT("Hurricane Bow Lvl 3"), 120, 0.f, 0.f, 125.f); L3.MaxTargets = 5;
			P.Levels = { L1, L2, L3 };
			R.Add(P.Id, P);
		}
		// --- aura (towers.js:128-145) ---
		{
			FKodoStructurePreset P;
			P.Id = "aura"; P.DisplayName = TEXT("Aura Tower");
			P.GoldCost = 100; P.MaxHp = 280.f; P.RangeTiles = 3.5f; P.CooldownSeconds = 0.8f; P.Damage = 10.f;
			// WC3 aura (§2) is a +15% atk-speed/damage BUFF to friendly units, not a damage
			// tower. We leave our functional damaging-aura tower as-is to avoid breaking the
			// aura special; the WC3 buff semantics are noted as a future divergence.
			P.Special = "aura"; P.RequiresUpgrade = "auraUnlocked"; P.bIs2x2 = true; P.FootprintSize = 2; P.Food = 2.f;
			P.Levels.Add(MakeLevel(TEXT("Decay Aura Lvl 1"), 0, 0.f, 0.f, 10.f));
			P.Levels.Add(MakeLevel(TEXT("Plague Aura Lvl 2"), 90, 0.f, 0.f, 28.f));
			P.Levels.Add(MakeLevel(TEXT("Doom Catalyst Lvl 3"), 150, 0.f, 0.f, 75.f));
			R.Add(P.Id, P);
		}
		// --- mine_shaft (towers.js:146-163) ---
		{
			FKodoStructurePreset P;
			// NOTE: WC3 has NO buildable mine shaft — gold comes from 83 pre-placed mines
			// drained by workers (§3/§5). We keep our buildable mine shaft because our
			// economy needs it; costs left at the prototype values (no WC3 data to map).
			P.Id = "mine_shaft"; P.DisplayName = TEXT("Mine Shaft");
			P.GoldCost = 100; P.WoodCost = 50; P.MaxHp = 400.f; P.Special = "mine_shaft"; P.bIs2x2 = true; P.FootprintSize = 2; P.Food = 4.f;
			P.Levels.Add(MakeLevel(TEXT("Basic Mine Shaft"), 0));
			P.Levels.Add(MakeLevel(TEXT("Hydraulic Mine Shaft"), 75, 900.f));
			P.Levels.Add(MakeLevel(TEXT("Core Gold Drill Lvl 3"), 120, 2000.f));
			R.Add(P.Id, P);
		}
		// --- lumber_mill (towers.js:164-181) ---
		{
			FKodoStructurePreset P;
			P.Id = "lumber_mill"; P.DisplayName = TEXT("Lumber Mill");
			P.GoldCost = 80; P.WoodCost = 20; P.MaxHp = 300.f; P.Special = "lumber_mill"; P.bIs2x2 = true; P.FootprintSize = 2; P.Food = 4.f;
			P.Levels.Add(MakeLevel(TEXT("Basic Lumber Mill"), 0));
			P.Levels.Add(MakeLevel(TEXT("Steam Sawmill Lvl 2"), 60, 750.f));
			P.Levels.Add(MakeLevel(TEXT("Automatic Logger Lvl 3"), 100, 1600.f));
			R.Add(P.Id, P);
		}
		// --- command_center (WC3 Castle hmtt: 2000g + 500l / 1000hp — §2/§5) ---
		{
			FKodoStructurePreset P;
			P.Id = "command_center"; P.DisplayName = TEXT("Command Center");
			P.GoldCost = 2000; P.WoodCost = 500; P.MaxHp = 1000.f; P.Special = "command_center"; P.bIs2x2 = true; P.FootprintSize = 4; P.Food = 0.f;
			P.Levels.Add(MakeLevel(TEXT("Command Center"), 2000, 1000.f));
			R.Add(P.Id, P);
		}
		// --- admin_tower (debug/control building, top-right corner; not a shooting tower) ---
		{
			FKodoStructurePreset P;
			P.Id = "admin_tower"; P.DisplayName = TEXT("Admin Tower");
			// Indestructible-ish control panel: 0 cost, huge HP, 2x2, no food upkeep.
			// Its Special tag excludes it from shooting-tower combat (like the economy buildings).
			P.GoldCost = 0; P.WoodCost = 0; P.MaxHp = 999999.f; P.Special = "admin_tower"; P.bIs2x2 = true; P.FootprintSize = 2; P.Food = 0.f;
			P.ConstructionSeconds = 0.f;
			P.Levels.Add(MakeLevel(TEXT("Admin Tower"), 0, 999999.f));
			R.Add(P.Id, P);
		}
		// --- upgrade_center (separate tech building: hosts all tower research) ---
		{
			FKodoStructurePreset P;
			P.Id = "upgrade_center"; P.DisplayName = TEXT("Upgrade Center");
			P.GoldCost = 50; P.WoodCost = 0; P.MaxHp = 250.f; P.Special = "upgrade_center"; P.bIs2x2 = true; P.FootprintSize = 4; P.Food = 4.f;
			// Economy-building construction time (mine_shaft/lumber_mill default 4.0 s).
			P.ConstructionSeconds = 4.f;
			P.Levels.Add(MakeLevel(TEXT("Upgrade Center"), 50, 250.f));
			R.Add(P.Id, P);
		}
		// --- war_altar (hero-upgrade building: permanent hero stat upgrades) ---
		{
			FKodoStructurePreset P;
			P.Id = "war_altar"; P.DisplayName = TEXT("War Altar");
			// 2x2 hero-tech building, 150g. Its Special tag keeps it OUT of the shooting-tower combat
			// loop (like upgrade_center / the economy buildings); it's still a normal selectable,
			// sellable, attackable player building (ECellType::Tower).
			P.GoldCost = 150; P.WoodCost = 0; P.MaxHp = 300.f; P.Special = "war_altar"; P.bIs2x2 = true; P.FootprintSize = 4; P.Food = 4.f;
			P.ConstructionSeconds = 4.f;
			P.Levels.Add(MakeLevel(TEXT("War Altar"), 150, 300.f));
			R.Add(P.Id, P);
		}

		return R;
	}
}

namespace KodoStructures
{
	const TMap<FName, FKodoStructurePreset>& Registry()
	{
		static const TMap<FName, FKodoStructurePreset> Cached = BuildRegistry();
		return Cached;
	}

	const FKodoStructurePreset* Find(const FName Id)
	{
		return Registry().Find(Id);
	}

	FKodoStructureStats GetStatsForLevel(const FName Id, const int32 Level)
	{
		FKodoStructureStats Out;
		const FKodoStructurePreset* Preset = Find(Id);
		if (!Preset)
		{
			return Out;
		}

		// Base fields first (towers.js:227-241), then level overrides where set.
		Out.Id = Preset->Id;
		Out.DisplayName = Preset->DisplayName;
		Out.WoodCost = Preset->WoodCost;
		Out.MaxHp = Preset->MaxHp;
		Out.RangeTiles = Preset->RangeTiles;
		Out.CooldownSeconds = Preset->CooldownSeconds;
		Out.Damage = Preset->Damage;
		Out.Level = Level;

		if (Preset->Levels.Num() > 0)
		{
			const int32 LevelIdx = FMath::Clamp(Level - 1, 0, Preset->Levels.Num() - 1);
			const FKodoStructureStats& L = Preset->Levels[LevelIdx];
			Out.DisplayName = L.DisplayName;
			Out.GoldCost = L.GoldCost;
			if (L.MaxHp > 0.f) { Out.MaxHp = L.MaxHp; }
			if (L.RangeTiles > 0.f) { Out.RangeTiles = L.RangeTiles; }
			if (L.CooldownSeconds > 0.f) { Out.CooldownSeconds = L.CooldownSeconds; }
			if (L.Damage > 0.f) { Out.Damage = L.Damage; }
			Out.SlowPercent = L.SlowPercent;
			Out.StunChance = L.StunChance;
			Out.StunDurationSeconds = L.StunDurationSeconds;
			Out.SplashRadiusTiles = L.SplashRadiusTiles;
			Out.MaxTargets = L.MaxTargets;
		}
		return Out;
	}
}
