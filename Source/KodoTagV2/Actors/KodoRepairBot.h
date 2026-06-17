// Kodo Tag: Survivor — UE Migration, Phase 4.
// Tinker's deployable repair bot (game.js deployRepairBot:1464-1471, tick
// 1183-1208): heals every damaged structure in the surrounding 3x3 cells by
// +20 HP per 1.0 s tick, for 15 s. Also used by the Auto-Repair Kit item (P3B).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KodoRepairBot.generated.h"

class UKodoGridSubsystem;
class UStaticMeshComponent;

UCLASS()
class KODOTAGV2_API AKodoRepairBot : public AActor
{
	GENERATED_BODY()

public:
	AKodoRepairBot();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void InitAtCell(const FIntPoint& InCell);

	/** Override the per-tick heal (Tinker passive + hero-level scaling pass this in). */
	void SetHealPerTick(const float InHealPerTick) { HealPerTick = InHealPerTick; }

protected:
	UPROPERTY(EditAnywhere, Category = "Kodo") float LifetimeSeconds = 15.f; // game.js:1468
	UPROPERTY(EditAnywhere, Category = "Kodo") float HealPerTick = 20.f;     // game.js:1197
	UPROPERTY(EditAnywhere, Category = "Kodo") float TickInterval = 1.f;     // game.js:1188

	UPROPERTY(VisibleAnywhere, Category = "Kodo")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY()
	TObjectPtr<UKodoGridSubsystem> Grid;

	FIntPoint Cell = FIntPoint::ZeroValue;
	float LifeRemaining = 15.f;
	float HealTick = 0.f;
};
