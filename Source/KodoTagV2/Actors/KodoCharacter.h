// Kodo Tag: Survivor — UE Migration, Phase 2.
// Hostile Kodo beast. Port of the Kodo class (entities.js:740-1091):
// 4 variants, staggered A* chase loop, wall-eating state machine, global
// enrage response, and Blink teleportation.

#pragma once

#include "CoreMinimal.h"
#include "Actors/KodoTagCharacterBase.h"
#include "KodoCharacter.generated.h"

class AKodoWaveController;
class ARunnerCharacter;
class UWidgetComponent;
class UKodoHealthBarWidget;

/** entities.js:744 — 'standard', 'speed', 'tank', 'blink'. */
UENUM(BlueprintType)
enum class EKodoType : uint8
{
	Standard,
	Speed,
	Tank,
	Blink
};

UCLASS()
class KODOTAGV2_API AKodoCharacter : public AKodoTagCharacterBase
{
	GENERATED_BODY()

public:
	AKodoCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Configure type, spawn tier and owner. Call right after spawning.
	 * WC3 model (kodo_balance_data.md §1): kodo stats are FIXED per type — there is
	 * no per-wave geometric HP/speed multiplier. Escalation comes from spawning
	 * tougher TYPES over time; we additionally apply a mild HP growth with tier
	 * (HP *(1 + 0.15*Tier)) as a stand-in so a long game keeps pressure on.
	 */
	void InitKodo(EKodoType InType, int32 SpawnTier, AKodoWaveController* InWaveController);

	/** Armor-mitigated damage. WC3 reduction: mult = 1 - (0.06*A)/(1+0.06*A). */
	virtual void ApplyDamageAmount(float Amount) override;

	/** True when no unblocked size-1 path to the target exists (feeds the global enrage timer). */
	bool IsBaseBlocked() const { return bBaseIsBlocked; }

	/** Drop current path and recalc immediately (kodo.path=[] + cooldown 0, game.js:2201-2206). */
	void ResetPathNow()
	{
		Path.Reset();
		PathRecalcCooldown = 0.f;
	}

	UFUNCTION(BlueprintPure, Category = "Kodo")
	EKodoType GetKodoType() const { return KodoType; }

	// --- Status effects (consumed by Frost/Stun towers in Phase 4) ---

	/** entities.js:847-850: keep longest timer, keep strongest (lowest) factor. */
	void ApplySlow(float InSlowFactor, float DurationSeconds);

	/** entities.js:852-854: keep longest timer. Stunned Kodos are completely frozen. */
	void ApplyStun(float DurationSeconds);

	/** Blueprint hook for chomp feedback (animation/Niagara in Phase 5). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Kodo")
	void OnChompStructure(const FIntPoint& Cell);

	/** True during the death animation: excluded from targeting/eating/waves. */
	bool IsDying() const { return bDying; }

protected:
	virtual void OnZeroHp() override;

	void RunChaseAndChewLogic(float DeltaSeconds);
	void UpdateEnrageVisual(bool bEnraged);
	ARunnerCharacter* ResolveRunner();

	/** Refresh the overhead bar; only touches the widget when the fraction actually changes. */
	void UpdateHealthBar();

	/** Screen-space overhead HP bar (creator feedback). Pure-C++ widget, no .uasset. */
	UPROPERTY(VisibleAnywhere, Category = "Kodo")
	TObjectPtr<UWidgetComponent> HealthBarComponent;

	UPROPERTY()
	TObjectPtr<UKodoHealthBarWidget> HealthBarWidget;

	/** Last pushed fraction; -1 forces the first update. */
	float LastHealthFraction = -1.f;

	UPROPERTY(VisibleAnywhere, Category = "Kodo")
	EKodoType KodoType = EKodoType::Standard;

	/** 1 for Speed Kodos, 2 for Standard/Tank/Blink (entities.js:746). */
	int32 FootprintSize = 2;

	/** Wall-chew damage per chomp after multipliers (entities.js:751, 886). */
	float CurrentDamage = 12.f;
	float BaseDamage = 12.f;

	/** WC3 armor (udef, 0-5). Reduces incoming damage; see ApplyDamageAmount. */
	UPROPERTY(VisibleAnywhere, Category = "Kodo")
	float Armor = 0.f;

	/** Speed after wave multiplier, before slow/enrage factors (UU/s). */
	float BaseKodoSpeedUU = 0.f;

	/** Prototype pixel radius (entities.js:755+) — drives eat range and body scale. */
	float SizePx = 23.f;

	/** Bounty on death (entities.js:752-787, 824-845). */
	int32 GoldReward = 8;
	int32 WoodReward = 3;

	// Chase state (entities.js:798-817)
	float PathRecalcCooldown = 0.f;
	FIntPoint LastTargetCell = FIntPoint(-9999, -9999);
	bool bBaseIsBlocked = false;
	bool bIsAttackingStructure = false;
	/** True this frame when Bunker mode retargeted this kodo onto a building (suppresses runner-eat). */
	bool bBunkerSiege = false;

	// Status effects
	float StunTimer = 0.f;
	float SlowTimer = 0.f;
	float SlowFactor = 1.f;

	// Wall chewing (entities.js:1027-1039)
	float AttackCooldown = 0.f;

	// Blink (entities.js:784-786, 960-983)
	float BlinkTimer = 0.f;
	float BlinkCooldownSeconds = 5.f;

	bool bEnragedVisualActive = false;
	FVector BaseBodyScale = FVector::OneVector;

	// Code-driven blockout animations (Phase 5; replaced by AnimBP montages with real meshes).
	bool bDying = false;
	float DeathTimer = 0.f;
	float ChompAnimTimer = 0.f;
	FVector BaseBodyOffset = FVector::ZeroVector;

	UPROPERTY()
	TWeakObjectPtr<AKodoWaveController> WaveController;

	TWeakObjectPtr<ARunnerCharacter> CachedRunner;
};
