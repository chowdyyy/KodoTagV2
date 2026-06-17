// Kodo Tag: Survivor — UE Migration, Phase 3/4.
// The player hero: click-to-move, harvesting/backpack/deposit loop
// (game.js:921-1150), hero classes and active spells (entities.js:646-737).

#pragma once

#include "CoreMinimal.h"
#include "Actors/KodoTagCharacterBase.h"
#include "Core/KodoTagGameState.h" // EKodoGameMode + AKodoTagGameState
#include "RunnerCharacter.generated.h"

class AKodoTagGameState;

UENUM()
enum class EKodoHarvestKind : uint8
{
	None,
	Tree,
	Goldmine
};

/** Hero classes (entities.js:9, 38-40). */
UENUM(BlueprintType)
enum class EKodoHeroClass : uint8
{
	MountainKing, // Thunder Clap (12 s)
	DeathKnight,  // Death Coil (10 s), passive structure regen
	Blademaster,  // Wind Walk (15 s), passive +15% speed
	Tinker        // Deploy Bot (20 s), passive +30% repair speed
};

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

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetHeldGold() const { return HeldGold; }

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	int32 GetHeldWood() const { return HeldWood; }

	// --- Hero classes & spells (Phase 4) ---

	/** Q key. Port of castSpell (entities.js:646-737). Thin wrapper for the slot-1 signature spell. */
	UFUNCTION(BlueprintCallable, Category = "KodoTag")
	void CastSpell();

	/** Unified active-skill entry. Slot 0 = signature spell (slot 1 in the UI), Slot 2 = researched
	 *  active (slot 3 in the UI). Slot 1 is the passive — not castable. Each active path checks
	 *  unlocked (slot 3 needs bSkill3Unlocked), cooldown, then mana; on cast spends mana + sets cooldown. */
	UFUNCTION(BlueprintCallable, Category = "KodoTag")
	void CastSkill(int32 Slot);

	UFUNCTION(BlueprintCallable, Category = "KodoTag")
	void SetHeroClass(EKodoHeroClass NewClass);

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	EKodoHeroClass GetHeroClass() const { return HeroClass; }

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

	// --- Merchant: Boots of Speed + God-mode buffs (game modes) ---

	/** Boots of Speed merchant cost, in gold. */
	static constexpr int32 BootsCost = 150;

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	bool HasBoots() const { return bHasBoots; }

	/** God: MaxHp *= 2 and heal to full; ModeSpeedMult = 1.25. Maze: no-op (mult stays 1). */
	void ApplyModeBuffs(EKodoGameMode Mode);

	/** Buy Boots of Speed: if not owned and the player can afford BootsCost gold, spend it,
	 *  set bHasBoots, return true; otherwise false. */
	bool BuyBoots();

protected:
	virtual void OnZeroHp() override;

	void TickHarvesting(float DeltaSeconds);
	void TickDeposit();
	void StartAutoDepositRun(const FIntPoint& HarvestCell, EKodoHarvestKind Kind);
	bool PathToNeighborOf(const FIntPoint& TargetCell, int32 SearchRadius);
	AKodoTagGameState* GetKodoGameState() const;
	void Msg(const FString& Text, const FColor& Color) const;

	UPROPERTY(EditAnywhere, Category = "KodoTag")
	EKodoHeroClass HeroClass = EKodoHeroClass::MountainKing;

	/** Hero level (1..MaxHeroLevel) and XP accumulated toward the next level. */
	int32 HeroLevel = 1;
	int32 HeroXp = 0;

	/** Spell timers in seconds (entities.js:37-40). */
	float SpellCooldown = 0.f;
	float WindWalkTimer = 0.f;
	float InvulnerableTimer = 0.f;

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

	bool bDead = false;

	/** Owns Boots of Speed (merchant): +25% move speed. */
	bool bHasBoots = false;
	/** Per-mode move-speed multiplier (God = 1.25, Maze = 1.0). */
	float ModeSpeedMult = 1.f;
};
