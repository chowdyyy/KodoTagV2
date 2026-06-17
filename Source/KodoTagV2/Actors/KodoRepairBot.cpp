// Kodo Tag: Survivor — UE Migration, Phase 4.

#include "Actors/KodoRepairBot.h"
#include "Grid/KodoGridSubsystem.h"
#include "Core/KodoTagUnits.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"

AKodoRepairBot::AKodoRepairBot()
{
	PrimaryActorTick.bCanEverTick = true;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;
	BodyMesh->SetMobility(EComponentMobility::Movable);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetRelativeScale3D(FVector(0.6f, 0.6f, 0.6f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		BodyMesh->SetStaticMesh(Sphere.Object);
	}
}

void AKodoRepairBot::BeginPlay()
{
	Super::BeginPlay();
	Grid = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
	LifeRemaining = LifetimeSeconds;
	HealTick = 0.f; // first tick heals immediately, like healTick: 0 (game.js:1469)
}

void AKodoRepairBot::InitAtCell(const FIntPoint& InCell)
{
	Cell = InCell;
}

void AKodoRepairBot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Hover bob for visibility.
	if (UWorld* World = GetWorld(); World && Grid)
	{
		FVector Loc = Grid->CellToWorldCenter(Cell);
		Loc.Z = 220.f + 30.f * FMath::Sin(World->GetTimeSeconds() * 4.f);
		SetActorLocation(Loc);
	}

	HealTick -= DeltaSeconds;
	if (HealTick <= 0.f && Grid)
	{
		HealTick = TickInterval;

		// Heal every damaged non-empty cell in the 3x3 around the bot (game.js:1190-1202).
		for (int32 Dx = -1; Dx <= 1; ++Dx)
		{
			for (int32 Dy = -1; Dy <= 1; ++Dy)
			{
				const FIntPoint Target(Cell.X + Dx, Cell.Y + Dy);
				if (!Grid->IsInBounds(Target))
				{
					continue;
				}
				FGridCell State = Grid->GetCell(Target);
				if (State.Type != ECellType::Empty && State.Hp < State.MaxHp)
				{
					State.Hp = FMath::Min(State.MaxHp, State.Hp + HealPerTick);
					Grid->SetCell(Target, State);
				}
			}
		}
	}

	LifeRemaining -= DeltaSeconds;
	if (LifeRemaining <= 0.f)
	{
		Destroy();
	}
}
