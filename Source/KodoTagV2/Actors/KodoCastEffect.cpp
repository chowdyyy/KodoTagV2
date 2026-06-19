// Kodo Tag: Survivor — UE Migration, Phase 5.

#include "Actors/KodoCastEffect.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AKodoCastEffect::AKodoCastEffect()
{
	PrimaryActorTick.bCanEverTick = true;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;
	BodyMesh->SetMobility(EComponentMobility::Movable);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetCastShadow(false);

	// Flat disc built from the engine cylinder (mirrors KodoMapBootstrapper's basic-shape loads).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(Cylinder.Object);
	}
}

void AKodoCastEffect::Init(const FVector& Location, const FLinearColor& Color, const float InMaxRadiusUU, const float InDuration)
{
	MaxRadiusUU = FMath::Max(50.f, InMaxRadiusUU);
	Duration = FMath::Max(0.05f, InDuration);
	Elapsed = 0.f;

	// Slight lift so the disc sits just above the ground instead of z-fighting it.
	FVector Loc = Location;
	Loc.Z += 5.f;
	SetActorLocation(Loc);

	// Start small; Tick expands the XY footprint while keeping Z thin.
	BodyMesh->SetWorldScale3D(FVector(0.1f, 0.1f, 0.15f));

	if (UMaterialInterface* BaseMat = BodyMesh->GetMaterial(0))
	{
		BodyMID = UMaterialInstanceDynamic::Create(BaseMat, this);
		BodyMID->SetVectorParameterValue(FName("Color"), Color);
		BodyMesh->SetMaterial(0, BodyMID);
	}
}

void AKodoCastEffect::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Elapsed += DeltaSeconds;
	if (Elapsed >= Duration)
	{
		Destroy();
		return;
	}

	// Normalized 0..1 progress over the lifetime.
	const float T = FMath::Clamp(Elapsed / Duration, 0.f, 1.f);

	// Engine cylinder is 100 UU across at scale 1, so radius maps to scale via /50.
	const float MaxScaleXY = MaxRadiusUU / 50.f;

	// Expand for most of the life, then briefly pop/shrink at the very end for a "shockwave" feel.
	float ScaleXY;
	if (T < 0.85f)
	{
		ScaleXY = FMath::Lerp(0.1f, MaxScaleXY, T / 0.85f);
	}
	else
	{
		const float TailT = (T - 0.85f) / 0.15f; // 0..1 over the final 15%
		ScaleXY = FMath::Lerp(MaxScaleXY, MaxScaleXY * 0.2f, TailT);
	}

	BodyMesh->SetWorldScale3D(FVector(ScaleXY, ScaleXY, 0.15f));
}
