// Kodo Tag: Survivor — UE Migration, Phase 1.

#include "Camera/KodoCameraPawn.h"
#include "Core/KodoTagUnits.h"
#include "Actors/RunnerCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

namespace
{
	// Prototype rig: offset (up 520 px, south 460 px) x zoom — game.js:1555-1558.
	constexpr float RigUpPx = 520.f;
	constexpr float RigSouthPx = 460.f;
}

AKodoCameraPawn::AKodoCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// The world orientation must NOT depend on PlayerStart rotation or control
	// rotation — otherwise the whole view (and every pan direction) can end up
	// rotated per-map. The rig's yaw is authored on the spring arm alone.
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	FocusRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FocusRoot"));
	RootComponent = FocusRoot;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(FocusRoot);
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = false;

	// Pitch: atan(520/460) ~= 48.5 deg down. Yaw -90: arm forward points north (-Y),
	// so the camera body hangs SOUTH (+Y) of the focus point, matching the prototype.
	const float PitchDeg = FMath::RadiansToDegrees(FMath::Atan2(RigUpPx, RigSouthPx));
	SpringArm->SetRelativeRotation(FRotator(-PitchDeg, -90.f, 0.f));

	const float BaseArmLengthUU = FMath::Sqrt(RigUpPx * RigUpPx + RigSouthPx * RigSouthPx) * KodoUnits::PxToUU;
	SpringArm->TargetArmLength = BaseArmLengthUU * TargetZoomFactor;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	Camera->SetFieldOfView(50.f);

	ZoomFactor = TargetZoomFactor;
}

void AKodoCameraPawn::BeginPlay()
{
	Super::BeginPlay();
	// Neutralize whatever rotation the spawn point gave us: north must be
	// screen-up everywhere (main view, minimap, pan directions).
	SetActorRotation(FRotator::ZeroRotator);
}

void AKodoCameraPawn::SetFollowTarget(AActor* Target)
{
	FollowTarget = Target;
}

void AKodoCameraPawn::AddPanInput(const FVector2D& Direction)
{
	if (Direction.IsNearlyZero())
	{
		return;
	}
	bCameraLocked = false; // arrow/edge panning unlocks the camera (game.js:3051)
	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	AddActorWorldOffset(FVector(Direction.X, Direction.Y, 0.f).GetClampedToMaxSize(1.f) * PanSpeedUU * Dt);
	ClampToMapBounds();
}

void AKodoCameraPawn::AddZoomInput(const float AxisValue)
{
	// Wheel up (+) zooms in => smaller zoom factor, like the prototype's zoomScale.
	// Max zoom-out raised to 3.2 (was 1.6) so the camera can pull back twice as far.
	TargetZoomFactor = FMath::Clamp(TargetZoomFactor - AxisValue * 0.1f, 0.5f, 3.2f);
}

void AKodoCameraPawn::LockToTarget()
{
	bCameraLocked = true;
}

void AKodoCameraPawn::JumpFocusTo(const FVector& WorldLocation)
{
	bCameraLocked = false; // minimap pan unlocks, like game.js:2938
	SetActorLocation(FVector(WorldLocation.X, WorldLocation.Y, 0.f));
	ClampToMapBounds();
}

void AKodoCameraPawn::ClampToMapBounds()
{
	FVector Location = GetActorLocation();
	Location.X = FMath::Clamp(Location.X, 0.f, KodoUnits::MapExtentUU);
	Location.Y = FMath::Clamp(Location.Y, 0.f, KodoUnits::MapExtentUU);
	Location.Z = 0.f;
	SetActorLocation(Location);
}

void AKodoCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Lazy target resolution avoids GameMode/possession ordering issues.
	if (!FollowTarget.IsValid())
	{
		if (AActor* Runner = UGameplayStatics::GetActorOfClass(GetWorld(), ARunnerCharacter::StaticClass()))
		{
			FollowTarget = Runner;
			// First acquisition: snap straight onto the Runner (game.js:775-777).
			FVector Snap = Runner->GetActorLocation();
			Snap.Z = 0.f;
			SetActorLocation(Snap);
			ClampToMapBounds();
		}
	}

	// Zoom interpolation.
	ZoomFactor = FMath::FInterpTo(ZoomFactor, TargetZoomFactor, DeltaSeconds, ZoomInterpSpeed);
	const float BaseArmLengthUU = FMath::Sqrt(RigUpPx * RigUpPx + RigSouthPx * RigSouthPx) * KodoUnits::PxToUU;
	SpringArm->TargetArmLength = BaseArmLengthUU * ZoomFactor;

	// Locked follow (game.js:884-887).
	if (bCameraLocked && FollowTarget.IsValid())
	{
		FVector Desired = FollowTarget->GetActorLocation();
		Desired.Z = 0.f;
		SetActorLocation(FMath::VInterpTo(GetActorLocation(), Desired, DeltaSeconds, FollowInterpSpeed));
		ClampToMapBounds();
	}
}
