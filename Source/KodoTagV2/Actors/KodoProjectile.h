// Kodo Tag: Survivor — UE Migration, Phase 4.
// Homing projectile — port of Projectile (entities.js:1360-1458): retargets
// the nearest Kodo if its target dies mid-flight, applies splash/frost/stun
// payloads on hit. Used by towers and the Death Knight's Death Coil.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KodoProjectile.generated.h"

class AKodoCharacter;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/** Built by the firing site from FKodoStructureStats (game.js fireProjectile:1421-1450). */
struct FKodoProjectileConfig
{
	FName Type;                    // 'arrow'(None), 'frost', 'stun', 'aoe', 'death_coil'
	float Damage = 0.f;
	float SpeedUU = 0.f;           // prototype px/s * PxToUU
	float SlowPercent = 0.f;
	float StunChance = 0.f;
	float StunDurationSeconds = 0.f;
	float SplashRadiusTiles = 0.f;
	float RadiusPx = 4.f;          // visual size
	FLinearColor Color = FLinearColor(1.f, 0.84f, 0.04f);
};

UCLASS()
class KODOTAGV2_API AKodoProjectile : public AActor
{
	GENERATED_BODY()

public:
	AKodoProjectile();

	virtual void Tick(float DeltaSeconds) override;

	void Init(AKodoCharacter* InTarget, const FKodoProjectileConfig& InConfig);

protected:
	void PayloadHit(const FVector& HitLocation, AKodoCharacter* HitKodo);
	AKodoCharacter* FindNearestKodo() const;

	UPROPERTY(VisibleAnywhere, Category = "Kodo")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BodyMID;

	TWeakObjectPtr<AKodoCharacter> TargetKodo;
	FKodoProjectileConfig Config;
};
