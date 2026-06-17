// Kodo Tag: Survivor — UE Migration, Phase 5.
// AnimInstance base for Runner/Kodo skeletal meshes. Parent your editor-made
// Animation Blueprints to this class: Speed drives the Idle<->Run blend space,
// bIsAttacking triggers Attack/Chomp montages, bIsDead the Death state.
// (Skeletal meshes, blend spaces, and AnimBP graphs are editor-authored assets;
// this class is the C++ hookup so no Blueprint needs to poll game code.)

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "KodoAnimInstance.generated.h"

UCLASS()
class KODOTAGV2_API UKodoAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** UU/s locomotion speed: feed the Idle->Run blend space. */
	UPROPERTY(BlueprintReadOnly, Category = "Kodo")
	float Speed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Kodo")
	bool bIsMoving = false;

	/** True while a Kodo chews a structure or the Runner swings/casts. */
	UPROPERTY(BlueprintReadOnly, Category = "Kodo")
	bool bIsAttacking = false;

	UPROPERTY(BlueprintReadOnly, Category = "Kodo")
	bool bIsDead = false;
};
