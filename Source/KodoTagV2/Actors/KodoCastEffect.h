// Kodo Tag: Survivor — UE Migration, Phase 5.
// Lightweight cast feedback: an expanding flat "shockwave" disc spawned at every
// successful hero ability cast (additional to any projectile). Self-destroys on finish.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KodoCastEffect.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class KODOTAGV2_API AKodoCastEffect : public AActor
{
	GENERATED_BODY()

public:
	AKodoCastEffect();

	virtual void Tick(float DeltaSeconds) override;

	/** Configure + start the expanding ring at Location, tinted Color, growing to MaxRadiusUU over Duration. */
	void Init(const FVector& Location, const FLinearColor& Color, float MaxRadiusUU, float Duration);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Kodo")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BodyMID;

	float Elapsed = 0.f;
	float Duration = 0.4f;
	float MaxRadiusUU = 300.f;
};
