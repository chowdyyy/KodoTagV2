// Kodo Tag: Survivor — UE Migration, Phase 4.

#include "Actors/KodoProjectile.h"
#include "Actors/KodoCharacter.h"
#include "Core/KodoTagUnits.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "Engine/World.h"

AKodoProjectile::AKodoProjectile()
{
	PrimaryActorTick.bCanEverTick = true;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;
	BodyMesh->SetMobility(EComponentMobility::Movable);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		BodyMesh->SetStaticMesh(Sphere.Object);
	}
}

void AKodoProjectile::Init(AKodoCharacter* InTarget, const FKodoProjectileConfig& InConfig)
{
	TargetKodo = InTarget;
	Config = InConfig;

	// Visual: sphere scaled from the prototype's pixel radius, floor 0.5.
	const float Scale = FMath::Max(0.5f, Config.RadiusPx * 2.f * KodoUnits::PxToUU / 100.f);
	BodyMesh->SetWorldScale3D(FVector(Scale));

	if (UMaterialInterface* BaseMat = BodyMesh->GetMaterial(0))
	{
		BodyMID = UMaterialInstanceDynamic::Create(BaseMat, this);
		BodyMID->SetVectorParameterValue(FName("Color"), Config.Color);
		BodyMesh->SetMaterial(0, BodyMID);
	}
}

AKodoCharacter* AKodoProjectile::FindNearestKodo() const
{
	AKodoCharacter* Nearest = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<AKodoCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsDying())
		{
			continue;
		}
		const float Dist = FVector::Dist2D(It->GetActorLocation(), GetActorLocation());
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Nearest = *It;
		}
	}
	return Nearest;
}

void AKodoProjectile::PayloadHit(const FVector& HitLocation, AKodoCharacter* HitKodo)
{
	// Port of payloadHit (entities.js:1420-1458).
	if (Config.SplashRadiusTiles > 0.f)
	{
		// Splash: damage ALL Kodos within radius (entities.js:1422-1434).
		const float SplashUU = Config.SplashRadiusTiles * KodoUnits::CellSizeUU;
		TArray<AKodoCharacter*> InRange;
		for (TActorIterator<AKodoCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->IsDying() && FVector::Dist2D(It->GetActorLocation(), HitLocation) <= SplashUU)
			{
				InRange.Add(*It);
			}
		}
		for (AKodoCharacter* Kodo : InRange)
		{
			Kodo->ApplyDamageAmount(Config.Damage);
		}
	}
	else if (HitKodo)
	{
		HitKodo->ApplyDamageAmount(Config.Damage);

		// Frost: factor = 1 - slowPercent for 3.5 s (entities.js:1441-1445).
		if (Config.Type == FName("frost"))
		{
			HitKodo->ApplySlow(1.f - Config.SlowPercent, 3.5f);
		}
		// Stun roll (entities.js:1448-1452).
		if (Config.Type == FName("stun") && FMath::FRand() < Config.StunChance)
		{
			HitKodo->ApplyStun(Config.StunDurationSeconds);
		}
	}

	Destroy();
}

void AKodoProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Target died mid-flight: redirect to nearest, or detonate (entities.js:1380-1401).
	if (!TargetKodo.IsValid() || TargetKodo->IsDying())
	{
		AKodoCharacter* Nearest = FindNearestKodo();
		if (!Nearest)
		{
			PayloadHit(GetActorLocation(), nullptr);
			return;
		}
		TargetKodo = Nearest;
	}

	const FVector TargetLocation = TargetKodo->GetActorLocation();
	const FVector Delta = TargetLocation - GetActorLocation();
	const float Dist = Delta.Size2D();
	const float Step = Config.SpeedUU * DeltaSeconds;

	if (Dist <= Step)
	{
		PayloadHit(TargetLocation, TargetKodo.Get());
		return;
	}
	SetActorLocation(GetActorLocation() + Delta.GetSafeNormal2D() * Step);
}
