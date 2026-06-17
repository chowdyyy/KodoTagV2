// Kodo Tag: Survivor — UE Migration, Phase 1.
// Shared parent for the Runner and (Phase 2) Kodos: HP plumbing and
// grid-waypoint movement, ported from entities.js:121-165.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Grid/KodoGridTypes.h"
#include "Core/KodoTagUnits.h"
#include "KodoTagCharacterBase.generated.h"

class UKodoGridSubsystem;
class UStaticMeshComponent;

UCLASS()
class KODOTAGV2_API AKodoTagCharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	AKodoTagCharacterBase();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Assign a new path (replaces any current one). Steps exclude the start cell, per pathfinder contract. */
	UFUNCTION(BlueprintCallable, Category = "KodoTag")
	void SetPath(const TArray<FKodoPathStep>& InPath);

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	bool HasPath() const { return Path.Num() > 0; }

	/** Current grid cell (floor of world position / cell size, matching the JS convention). */
	UFUNCTION(BlueprintPure, Category = "KodoTag")
	FIntPoint GetGridCell() const;

	/** Reduce HP; calls OnZeroHp() when it reaches 0. Virtual so the Runner can gate on invulnerability. */
	UFUNCTION(BlueprintCallable, Category = "KodoTag")
	virtual void ApplyDamageAmount(float Amount);

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	float GetHp() const { return Hp; }

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	float GetMaxHp() const { return MaxHp; }

	UFUNCTION(BlueprintPure, Category = "KodoTag")
	float GetMoveSpeed() const { return MoveSpeedUU; }

	// --- Animation state, consumed by UKodoAnimInstance (Phase 5) ---

	/** Current locomotion speed for blend spaces: full speed while pathing, 0 idle. */
	UFUNCTION(BlueprintPure, Category = "KodoTag|Anim")
	float GetAnimSpeed() const { return HasPath() ? MoveSpeedUU : 0.f; }

	UFUNCTION(BlueprintPure, Category = "KodoTag|Anim")
	bool IsAnimAttacking() const { return bAnimAttacking; }

	UFUNCTION(BlueprintPure, Category = "KodoTag|Anim")
	bool IsAnimDead() const { return bAnimDead; }

protected:
	/** Called once when HP hits 0. Default does nothing; subclasses decide (Runner: game over, Kodo: die). */
	virtual void OnZeroHp() {}

	/**
	 * One frame of waypoint following — port of entities.js:121-165 (face target,
	 * constant-speed step, snap + pop). Called from Tick when bAutoFollowPath;
	 * Kodos call it manually from their own AI loop after the chew check.
	 */
	void StepAlongPath(float DeltaSeconds);

	/** Kodos set this false and drive movement from their AI state machine. */
	bool bAutoFollowPath = true;

	/** Anim hookup flags (set by subclasses: Kodos while chewing, anyone when dying). */
	bool bAnimAttacking = false;
	bool bAnimDead = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoTag")
	float MaxHp = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoTag")
	float Hp = 100.f;

	/** UU/s. Prototype movement is constant-velocity (no acceleration curves) — parity over CMC niceties (TDD §6.3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KodoTag")
	float MoveSpeedUU = KodoUnits::RunnerSpeedUU;

	/** Blockout-grade visible body (replaced by skeletal meshes in Phase 5). */
	UPROPERTY(VisibleAnywhere, Category = "KodoTag")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** Remaining waypoints, front = next. */
	TArray<FKodoPathStep> Path;

	/** Cached world grid. */
	UPROPERTY()
	TObjectPtr<UKodoGridSubsystem> Grid;
};
