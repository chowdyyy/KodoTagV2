// Kodo Tag: Survivor — UE Migration, Phase 1.
// RTS camera rig (TDD §6.1). Replicates the prototype's fixed-pitch rig:
// camera sits SOUTH of the focus point at offset (up 520, south 460) x zoom
// in prototype px (game.js:1554-1558) => pitch atan(520/460) ~ 48.5 degrees.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "KodoCameraPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;

UCLASS()
class KODOTAGV2_API AKodoCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AKodoCameraPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Pan the focus point (world XY direction, normalized-ish). Unlocks follow (game.js:3051). */
	void AddPanInput(const FVector2D& Direction);

	/** Mouse-wheel ticks; positive = zoom in. */
	void AddZoomInput(float AxisValue);

	/** Spacebar: re-lock follow on the Runner (game.js:3045-3048). */
	void LockToTarget();

	void SetFollowTarget(AActor* Target);

	/** Minimap click/drag: jump the focus point to a world XY position (unlocks follow). */
	void JumpFocusTo(const FVector& WorldLocation);

	bool IsCameraLocked() const { return bCameraLocked; }

	float GetZoomFactor() const { return ZoomFactor; }

protected:
	/** Focus point gliding on the ground plane — the root. */
	UPROPERTY(VisibleAnywhere, Category = "KodoCamera")
	TObjectPtr<USceneComponent> FocusRoot;

	UPROPERTY(VisibleAnywhere, Category = "KodoCamera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "KodoCamera")
	TObjectPtr<UCameraComponent> Camera;

	/** Prototype zoomScale default 0.8 (game.js:24); clamped to [0.5, 3.2] (zoom-out range doubled). */
	UPROPERTY(EditAnywhere, Category = "KodoCamera", meta = (ClampMin = "0.5", ClampMax = "3.2"))
	float TargetZoomFactor = 0.8f;

	UPROPERTY(EditAnywhere, Category = "KodoCamera")
	float ZoomInterpSpeed = 8.f;

	UPROPERTY(EditAnywhere, Category = "KodoCamera")
	float FollowInterpSpeed = 10.f;

	/** ~24 px/keypress-repeat continuous-ized (TDD §6.2, deliberate idiom change). */
	UPROPERTY(EditAnywhere, Category = "KodoCamera")
	float PanSpeedUU = 1930.f;

	bool bCameraLocked = true; // locked by default (game.js:29)
	float ZoomFactor = 0.8f;

	TWeakObjectPtr<AActor> FollowTarget;

	void ClampToMapBounds();
};
