// Kodo Tag: Survivor — UE Migration, Phase 3/4.
// The player hero: click-to-move, harvesting/backpack/deposit loop
// (game.js:921-1150), hero classes and active spells (entities.js:646-737).

#pragma once

#include "CoreMinimal.h"
#include "Actors/KodoTagCharacterBase.h"
#include "Core/KodoTagGameState.h" // EKodoGameMode + AKodoTagGameState
#include "RunnerCharacter.generated.h"

class AKodoTagGameState;
class AKodoCharacter;

UENUM()
enum class EKodoHarvestKind : uint8
{
	None,
	Tree,
	Goldmine
};

/**
 * Hero classes — WC3 Kodo Tag hero overhaul (Pass 1: combat foundation only).
 * Melee: MountainKing, Blademaster, Paladin. Ranged: Archmage, FarSeer, Dreadlord.
 * The full per-hero ability kits arrive in a later pass; this set drives basic-attack
 * combat data + placeholder signature spells.
 */
UENUM(BlueprintType)
enum class EKodoHeroClass : uint8
{
	MountainKing, // melee
	Blademaster,  // melee
	Archmage,     // ranged
	FarSeer,      // ranged
	Paladin,      // melee
	Dreadlord     // ranged
};

/** Per-hero basic-attack combat data (Pass 1). Keyed by EKodoHeroClass via a file-static table. */
struct FKodoHeroCombatData
{
	bool bRanged = false;
	float AttackRangeUU = 0.f;   // distance to the target at which the hero stops and attacks
	float AttackDamage = 0.f;    // damage applied per attack
	float AttackInterval = 1.f;  // seconds between attacks
	float MoveSpeedMult = 1.f;   // multiplier on the base hero move speed
};

/** Resolve the combat data for a hero class (file-static table; defined in the .cpp). */
const FKodoHeroCombatData& KodoHeroCombat(EKodoHeroClass HeroClass);

/**
 * How an ability is targeted (Pass 5 — targeted casting). Instant fires immediately (self/instant);
 * TargetKodo waits for the player to click a kodo; TargetGround waits for a ground point.
 */
enum class EKodoCastTarget : uint8
{
	Instant,
	TargetKodo,
	TargetGround
};

/**
 * One ability slot's metadata (Pass 2 — full 4-slot WC3 kits). Every hero has exactly 4 slots:
 * slot0 = primary active, slot1 = secondary active, slot2 = PASSIVE, slot3 = ULTIMATE (active).
 * Availability is LEVEL-based (UnlockLevel), independent of the old research flags.
 */
struct FKodoAbility
{
	FString Name;
	bool bPassive = false;
	float ManaCost = 0.f;
	float Cooldown = 0.f;
	int32 UnlockLevel = 1;
	EKodoCastTarget Target = EKodoCastTarget::Instant;
	FString Description; // one-line "what it does" for the HUD hover tooltip
};

/** Resolve a hero class + slot's ability metadata (file-static, function-local-static storage). */
const FKodoAbility& KodoAbility(EKodoHeroClass Class, int32 Slot);

/**
 * Purchasable hero items (Pass 3 — functional merchant + 6-slot inventory). Passives apply a
 * permanent stat hook while owned; consumables are spent (removed) on click for an instant effect.
 */
UENUM()
enum class EKodoItem : uint8
{
	None,
	BootsOfSpeed,     // passive: +25% move speed
	ClawsOfAttack,    // passive: +10 basic-attack damage
	RingOfProtection, // passive: -15% incoming damage
	PotionOfHealing,  // consumable: restore 50% of MaxHp
	PotionOfMana,     // consumable: restore 50% of MaxMana
	TomeOfExperience  // consumable: grant ~1 level of XP
};

/**
 * Hero permanent-upgrade stats, purchased at the War Altar (combat viability). Each stat has its
 * own level (0..5); see ARunnerCharacter::HeroStatLevels and UpgradeHeroStat. Indexed by enum value.
 */
UENUM(BlueprintType)
enum class EKodoHeroStat : uint8
{
	Damage,      // +8 base attack damage per level
	Armor,       // -10% incoming damage per level (cap 50%)
	AttackSpeed, // -12% attack interval per level (cap -60%, i.e. min 0.4x)
	ManaRegen,   // +2 mana/s per level
	MaxHealth    // +75 max HP per level
};

/** Static metadata for an item: display name, gold cost, and whether using it consumes it. */
struct FKodoItemDef
{
	FString Name;
	int32 Cost = 0;
	bool bConsumable = false;
};

/** Resolve an item's definition (file-static table; defined in the .cpp). */
const FKodoItemDef& KodoItemDef(EKodoItem Item);

UCLASS()
class KODOTAGV2_API ARunnerCharacter : public AKodoTagCharacterBase
{
	GENERATED_BODY()

public:
	ARunnerCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void ApplyDamageAmount(float Amount) override;

	// --- Harvesting (Phase 3) ---
	void CommandHarvest(const FIntPoint& TargetCell, EKodoHarvestKind Kind);
	void CancelHarvest();

	// --- Basic-attack orders (Pass 1 combat foundation) ---

	/** Order the hero to attack a kodo: move into AttackRange, then deal AttackDamage on the
	 *  AttackInterval cadence. Cancels harvest and clears any building attack target. */
	void OrderAttack(AKodoCharacter* TargetKodo);

	/** Order the hero to attack (demolish) the player building at Cell. Cancels harvest and
	 *  clears any kodo attack target. Works for any wall/tower/CC cell the player owns. */
	void OrderAttackCell(const FIntPoint& Cell);

	/** Clear any active attack order (kodo or building). */
	void CancelAttack();

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetHeldGold() const { return HeldGold; }

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetHeldWood() const { return HeldWood; }

	// --- Hero classes & spells (Phase 4) ---

	/** Q key. Port of castSpell (entities.js:646-737). Thin wrapper for the slot-1 signature spell. */
	UFUNCTION(BlueprintCallable, Category = "KodoTag")
	void CastSpell();

	/** Unified active-skill entry (Pass 2). Slots 0..3: slot0/slot1 actives, slot2 passive (not
	 *  castable), slot3 ultimate. Gating is LEVEL-based via IsAbilityUnlocked(); each active checks
	 *  unlocked, per-slot cooldown, then mana; on cast spends mana + sets SkillCooldowns[slot]. */
	UFUNCTION(BlueprintCallable, Category = "KodoTag")
	void CastSkill(int32 Slot);

	/** How slot S is targeted (Instant / TargetKodo / TargetGround). Instant for out-of-range. */
	EKodoCastTarget GetAbilityTarget(int32 Slot) const
	{
		return (Slot >= 0 && Slot <= 3) ? KodoAbility(HeroClass, Slot).Target : EKodoCastTarget::Instant;
	}

	/** Pre-check the cast gates (passive / locked / cooldown / mana) WITHOUT casting. Returns true
	 *  when slot S is castable right now; on false, OutReason holds a player-facing explanation. */
	bool CanCastSkill(int32 Slot, FString& OutReason) const;

	/** Full gated cast with an explicit target: TargetLocation for ground spells, TargetKodo for
	 *  unit-target spells (null -> fall back to nearest-kodo search). Spends mana + sets the slot
	 *  cooldown only on success. CastSkill(Slot) forwards here with the hero's own location. */
	void CastSkillAt(int32 Slot, const FVector& TargetLocation, AKodoCharacter* TargetKodo);

	/** True when the ability is available: either the hero's level meets the slot's unlock level, OR
	 *  the matching Upgrade-Center research was bought (slot 1 = Hero Skill 2, slot 3 = Hero Skill 3).
	 *  This makes the research buttons actually unlock the secondary active + the ultimate early. */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	bool IsAbilityUnlocked(int32 Slot) const
	{
		if (Slot < 0 || Slot > 3) { return false; }
		if (HeroLevel >= KodoAbility(HeroClass, Slot).UnlockLevel) { return true; }
		if (Slot == 1 && bSkill2Unlocked) { return true; }
		if (Slot == 3 && bSkill3Unlocked) { return true; }
		return false;
	}

	/** Per-slot ability cooldown remaining in seconds (HUD readout). */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	float GetSkillCooldownRemaining(int32 Slot) const
	{
		return (Slot >= 0 && Slot <= 3) ? SkillCooldowns[Slot] : 0.f;
	}

	UFUNCTION(BlueprintCallable, Category = "KodoTag")
	void SetHeroClass(EKodoHeroClass NewClass);

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	EKodoHeroClass GetHeroClass() const { return HeroClass; }

	// --- Ability metadata getters (HUD command card; forward to the KodoAbility table) ---

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	FString GetAbilityName(int32 Slot) const { return KodoAbility(HeroClass, Slot).Name; }

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	bool IsAbilitySlotPassive(int32 Slot) const { return KodoAbility(HeroClass, Slot).bPassive; }

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetAbilityUnlockLevel(int32 Slot) const { return KodoAbility(HeroClass, Slot).UnlockLevel; }

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	FString GetAbilityDescription(int32 Slot) const { return KodoAbility(HeroClass, Slot).Description; }

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	float GetAbilityManaCost(int32 Slot) const { return KodoAbility(HeroClass, Slot).ManaCost; }

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	float GetAbilityCooldown(int32 Slot) const { return KodoAbility(HeroClass, Slot).Cooldown; }

	/** True for ranged hero classes (Archmage / FarSeer / Dreadlord); drives the HUD melee/ranged tag. */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	bool IsHeroRanged() const { return CombatData.bRanged; }

	/** Find the nearest alive kodo within a generous range and issue an attack order on it. */
	UFUNCTION(BlueprintCallable, Category = "KodoTag")
	void AttackNearestKodo();

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	float GetSpellCooldownRemaining() const { return SpellCooldown; }

	// --- Mana (hero resource for active skills) ---

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	float GetMana() const { return Mana; }

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	float GetMaxMana() const { return MaxMana; }

	// --- Hero skill research unlocks (slot 2 = passive, slot 3 = second active) ---

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	bool IsSkill2Unlocked() const { return bSkill2Unlocked; }

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	bool IsSkill3Unlocked() const { return bSkill3Unlocked; }

	void UnlockSkill2() { bSkill2Unlocked = true; }
	void UnlockSkill3() { bSkill3Unlocked = true; }

	// --- Mana-regen research (Upgrade Center): one-time, doubles passive regen ---

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	bool IsManaRegenUpgraded() const { return bManaRegenUpgraded; }

	void UpgradeManaRegen() { if (!bManaRegenUpgraded) { bManaRegenUpgraded = true; ManaRegenPerSec *= 2.f; } }

	// --- Hero XP & leveling (gameplay layer) ---

	/** Hero level ramps from 1 to MaxHeroLevel; each level boosts MaxHp and ability magnitude. */
	static constexpr int32 MaxHeroLevel = 10;

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetHeroLevel() const { return HeroLevel; }

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetHeroXp() const { return HeroXp; }

	/** XP needed to reach the next level from the current one: HeroLevel * 100 (simple ramp). */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetXpForNextLevel() const;

	/** Add XP and apply any level-ups (called when a kodo dies). */
	void GainXp(int32 Amount);

	/** Ability magnitude scalar for the current level: 1 + 0.15*(level-1) (~2.35x at level 10). */
	float GetAbilityScale() const { return 1.f + 0.15f * static_cast<float>(HeroLevel - 1); }

	/** invulnerableTimer > 0: damage immune (entities.js:75-77; god mode arrives with game modes). */
	bool IsInvulnerable() const { return InvulnerableTimer > 0.f; }

	/** Kodos lose aggro and retreat while wind-walking OR invulnerable (entities.js:908). */
	bool IsIgnoredByKodos() const { return WindWalkTimer > 0.f || InvulnerableTimer > 0.f; }

	void ActivateInvulnerability(float DurationSeconds) { InvulnerableTimer = DurationSeconds; }

	// --- Merchant items + hero inventory (Pass 3) + God-mode buffs (game modes) ---

	/** Boots of Speed merchant cost, in gold (retained for legacy HUD/UI references). */
	static constexpr int32 BootsCost = 150;

	/** Max items the hero can carry. */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetInventorySize() const { return 6; }

	/** Item held in a given slot, or None if the slot is empty / out of range. */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	EKodoItem GetInventoryItem(int32 Slot) const
	{
		return (Slot >= 0 && Slot < Inventory.Num()) ? Inventory[Slot] : EKodoItem::None;
	}

	/** Number of items currently held. */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetInventoryCount() const { return Inventory.Num(); }

	/** True when the hero owns at least one of the given item. */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	bool HasItem(EKodoItem Item) const { return Inventory.Contains(Item); }

	/** Legacy accessor: Boots of Speed is now just an inventory item. */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	bool HasBoots() const { return HasItem(EKodoItem::BootsOfSpeed); }

	/** Buy an item: gated on free inventory slot, affordable gold, and (for passives) not already
	 *  owned. On success spends the gold, adds it to the inventory, and returns true; otherwise it
	 *  messages the reason and returns false. */
	bool BuyItem(EKodoItem Item);

	/** Use a consumable in the given slot (heal / mana / xp), then remove it. Passives do nothing. */
	void UseInventorySlot(int32 Slot);

	/** God: MaxHp *= 2 and heal to full; ModeSpeedMult = 1.25. Maze: no-op (mult stays 1). */
	void ApplyModeBuffs(EKodoGameMode Mode);

	/** Legacy entry: buys Boots of Speed via the unified BuyItem() path. */
	bool BuyBoots();

	// --- Hero permanent upgrades (War Altar; combat viability) ---

	/** Base hero max HP before any MaxHealth-stat or game-mode bonuses (combat viability raise). */
	static constexpr float HeroBaseMaxHp = 250.f;

	/** Max level for every hero-upgrade stat. */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetHeroStatMaxLevel() const { return 5; }

	/** Current APPLIED level (0..5) of a hero-upgrade stat (does NOT count queued upgrades). */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetHeroStatLevel(EKodoHeroStat Stat) const
	{
		const int32 Idx = static_cast<int32>(Stat);
		return (Idx >= 0 && Idx < 5) ? HeroStatLevels[Idx] : 0;
	}

	/** How many upgrades of a given stat are currently queued (not yet applied) — drives the "+N queued" label. */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetQueuedStatCount(EKodoHeroStat Stat) const;

	/** Gold cost of the NEXT purchasable level of a hero-upgrade stat, counting queued ones:
	 *  100 + 75*(appliedLevel + queuedCount). Matches what UpgradeHeroStat will charge. */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetHeroStatCost(EKodoHeroStat Stat) const
	{
		return 100 + 75 * (GetHeroStatLevel(Stat) + GetQueuedStatCount(Stat));
	}

	/** Progress of a queued upgrade of the given stat for the HUD loading bar. Returns false if none
	 *  queued. If this stat is the front (currently researching) item, OutFrac is its elapsed fraction
	 *  and OutRemaining its seconds left; if queued-but-not-started, OutFrac is 0 and OutRemaining its
	 *  full duration. */
	bool GetStatUpgradeProgress(EKodoHeroStat Stat, float& OutFrac, float& OutRemaining) const;

	/** Queue one level of a hero-upgrade stat (TIMED research): rejects when the prospective level
	 *  (applied + queued) would exceed max or gold is short; otherwise spends gold, appends a timed
	 *  entry, and returns true. The front entry counts down in Tick and applies on completion. */
	UFUNCTION(BlueprintCallable, Category = "KodoTag")
	bool UpgradeHeroStat(EKodoHeroStat Stat);

protected:
	virtual void OnZeroHp() override;

	void TickHarvesting(float DeltaSeconds);
	void TickDeposit();

	/** Drive the active attack order: close to range (re-path toward the target) then deal
	 *  interval damage. Clears the target when the kodo dies / the building is gone. */
	void TickAttack(float DeltaSeconds);

	/** Advance the timed hero-stat upgrade queue: only the FRONT entry counts down; on completion it
	 *  applies the stat (++level, MaxHealth heals), messages, and pops. Called from Tick. */
	void TickStatUpgrades(float DeltaSeconds);
	void StartAutoDepositRun(const FIntPoint& HarvestCell, EKodoHarvestKind Kind);
	bool PathToNeighborOf(const FIntPoint& TargetCell, int32 SearchRadius);
	AKodoTagGameState* GetKodoGameState() const;
	void Msg(const FString& Text, const FColor& Color) const;

	UPROPERTY(EditAnywhere, Category = "KodoTag")
	EKodoHeroClass HeroClass = EKodoHeroClass::MountainKing;

	/** Cached basic-attack combat data for the current HeroClass (set in SetHeroClass). */
	FKodoHeroCombatData CombatData;

	/** Hero level (1..MaxHeroLevel) and XP accumulated toward the next level. */
	int32 HeroLevel = 1;
	int32 HeroXp = 0;

	/** Spell timers in seconds (entities.js:37-40). SpellCooldown is retained for any non-ability
	 *  use; the 4-slot ability kits now use the per-slot SkillCooldowns array instead. */
	float SpellCooldown = 0.f;
	float WindWalkTimer = 0.f;
	float InvulnerableTimer = 0.f;

	/** Per-ability-slot cooldowns (Pass 2). Indexed by slot 0..3; ticked down each Tick. */
	float SkillCooldowns[4] = { 0.f, 0.f, 0.f, 0.f };

	// --- Pass 2 buff/channel timers (ticked in Tick) ---
	/** MountainKing Avatar: temporary basic-attack damage buff while > 0. */
	float AttackBuffTimer = 0.f;
	float AttackBuffMult = 1.f;
	/** Blademaster Mirror Image: incoming-damage cut (×DamageReductionMult) while > 0. */
	float DamageReductionTimer = 0.f;
	float DamageReductionMult = 1.f;
	/** Blademaster Bladestorm channel: periodic AoE around the hero while > 0. */
	float BladestormTimer = 0.f;
	float BladestormTickTimer = 0.f;
	/** Archmage Blizzard channel: damage waves around the cast spot while > 0. */
	float BlizzardTimer = 0.f;
	float BlizzardTickTimer = 0.f;
	FVector BlizzardCenter = FVector::ZeroVector;
	/** Ground-targeted cast center (Pass 5). Set on a TargetGround cast; the channel Tick waves
	 *  (e.g. Blizzard) read this instead of the hero's live location. */
	FVector GroundCastLoc = FVector::ZeroVector;
	/** Pet "summon" stand-in (Water Elemental / Feral Spirit): periodic AoE near the hero while > 0. */
	float SummonTimer = 0.f;
	float SummonTickTimer = 0.f;
	float SummonDamage = 0.f;

	/** Hero mana: regenerates over time, spent by active skills. */
	float MaxMana = 100.f;
	float Mana = 100.f;
	float ManaRegenPerSec = 6.f;

	/** Hero-skill research unlocks: slot 2 = the per-class passive, slot 3 = the second active spell.
	 *  Per-hero-instance flags set by the Upgrade Center research (HeroSkill2 / HeroSkill3). */
	bool bSkill2Unlocked = false;
	bool bSkill3Unlocked = false;

	/** Mana-regen research applied (doubles ManaRegenPerSec once). Set by UpgradeManaRegen(). */
	bool bManaRegenUpgraded = false;

	/** Backpack (entities.js:48-56): combined carry cap 50. */
	UPROPERTY(EditAnywhere, Category = "KodoTag") int32 MaxCarry = 50;
	int32 HeldGold = 0;
	int32 HeldWood = 0;

	EKodoHarvestKind PendingKind = EKodoHarvestKind::None;
	FIntPoint PendingCell = FIntPoint(-1, -1);
	EKodoHarvestKind ActiveKind = EKodoHarvestKind::None;
	FIntPoint ActiveCell = FIntPoint(-1, -1);
	float HarvestTickTimer = 0.f;
	EKodoHarvestKind ReturnKind = EKodoHarvestKind::None;
	FIntPoint ReturnCell = FIntPoint(-1, -1);

	// --- Attack-order state (Pass 1). Only one target is active at a time. ---
	/** Current kodo attack target (null when attacking a building or nothing). */
	TWeakObjectPtr<AKodoCharacter> AttackTargetKodo;
	/** Current building attack target cell, or (-1,-1) when none. */
	FIntPoint AttackTargetCell = FIntPoint(-1, -1);
	/** Accumulates DeltaSeconds; an attack fires once it reaches the hero's AttackInterval. */
	float AttackTickTimer = 0.f;

	bool bDead = false;

	/** Hero inventory (merchant items), capped at GetInventorySize(). Passives apply while held;
	 *  consumables are removed when used. */
	TArray<EKodoItem> Inventory;
	/** Per-mode move-speed multiplier (God = 1.25, Maze = 1.0). */
	float ModeSpeedMult = 1.f;

	/** War Altar permanent-upgrade levels (0..5 each), indexed by EKodoHeroStat:
	 *  [0]=Damage [1]=Armor [2]=AttackSpeed [3]=ManaRegen [4]=MaxHealth. Default all 0. */
	int32 HeroStatLevels[5] = { 0, 0, 0, 0, 0 };

	/** One queued, timed War-Altar stat upgrade (mirrors the Upgrade-Center research pattern, but
	 *  kept on the hero). TimeRemaining counts down only while this is the front entry. */
	struct FHeroStatUpgrade
	{
		EKodoHeroStat Stat = EKodoHeroStat::Damage;
		float TimeRemaining = 0.f;
		float TotalTime = 0.f;
	};

	/** Sequential queue of pending hero-stat upgrades (like a build queue): only the front item counts
	 *  down (TickStatUpgrades); on completion it applies + pops. Empty when nothing is researching. */
	TArray<FHeroStatUpgrade> PendingStatUpgrades;
};
