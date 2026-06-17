// Kodo Tag: Survivor — UE Migration, Phase 1.

#include "Actors/KodoTagCharacterBase.h"
#include "Grid/KodoGridSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"

AKodoTagCharacterBase::AKodoTagCharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// Runner radius 12 px -> ~64 UU (entities.js:21, scaled per KodoUnits::PxToUU).
	GetCapsuleComponent()->SetCapsuleSize(64.f, 88.f);

	// Blockout body: engine cylinder (100 UU tall, 100 UU diameter).
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(RootComponent);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetRelativeScale3D(FVector(1.28f, 1.28f, 1.76f));
	// No dynamic shadows on units: with 100+ Kodos shadow casters dominate the GPU
	// cost (and the creator wanted fewer shadows for a cleaner top-down read anyway).
	BodyMesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylinderMesh.Object);
	}
}

void AKodoTagCharacterBase::BeginPlay()
{
	Super::BeginPlay();

	Grid = GetWorld()->GetSubsystem<UKodoGridSubsystem>();

	// Movement is manual waypoint stepping on a flat plane; disable gravity/falling.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->GravityScale = 0.f;
		Move->SetMovementMode(MOVE_Flying);
	}
}

void AKodoTagCharacterBase::SetPath(const TArray<FKodoPathStep>& InPath)
{
	Path = InPath;
}

FIntPoint AKodoTagCharacterBase::GetGridCell() const
{
	return Grid ? Grid->WorldToCell(GetActorLocation()) : FIntPoint::ZeroValue;
}

void AKodoTagCharacterBase::ApplyDamageAmount(const float Amount)
{
	if (Hp <= 0.f)
	{
		return;
	}
	Hp = FMath::Max(0.f, Hp - Amount);
	if (Hp <= 0.f)
	{
		OnZeroHp();
	}
}

void AKodoTagCharacterBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bAutoFollowPath)
	{
		StepAlongPath(DeltaSeconds);
	}
}

void AKodoTagCharacterBase::StepAlongPath(float DeltaSeconds)
{
	// Waypoint follow — port of entities.js:121-165: face target, constant-speed
	// step, snap to waypoint center when within one step, then pop it.
	if (Path.Num() == 0 || !Grid)
	{
		return;
	}

	const FVector Location = GetActorLocation();

	// Ride the raised-base terrain elevation: lerp the actor Z toward the spawn baseline
	// (88 = capsule half-height above ground) plus the CONTINUOUS terrain height at our exact
	// position — on a ramp this interpolates along the slope, so units walk the angle instead of
	// sinking/floating. XY stays exactly as the path dictates.
	constexpr float SpawnBaselineZ = 88.f;
	const float DesiredZ = SpawnBaselineZ + Grid->GetElevationZAtWorld(Location);
	const float RiddenZ = FMath::FInterpTo(Location.Z, DesiredZ, DeltaSeconds, 8.f);

	FVector Target = Grid->CellToWorldCenter(Path[0].Cell);
	Target.Z = RiddenZ; // follow terrain height; Z managed by elevation layer

	const FVector Delta = Target - Location;
	const float Dist = Delta.Size2D();

	if (Dist > 1.f)
	{
		const float YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		SetActorRotation(FRotator(0.f, YawDeg, 0.f));
	}

	const float Step = MoveSpeedUU * DeltaSeconds;
	if (Dist <= Step)
	{
		SetActorLocation(FVector(Target.X, Target.Y, RiddenZ));
		Path.RemoveAt(0);
	}
	else
	{
		const FVector NextXY = Location + Delta.GetSafeNormal2D() * Step;
		SetActorLocation(FVector(NextXY.X, NextXY.Y, RiddenZ));
	}
}
