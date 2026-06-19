// Kodo Tag: Survivor — UE Migration, Phase 1.

#include "Grid/KodoMapBootstrapper.h"
#include "Grid/KodoGridSubsystem.h"
#include "Data/KodoStructureData.h"
#include "Core/KodoTagUnits.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** Gold mine footprint, in cells. Doubled from the prototype's 2x2 to 4x4 (creator request). */
	constexpr int32 KodoMineSize = 4;

	FGridCell MakeCliffCell()
	{
		FGridCell Cell;
		Cell.Type = ECellType::Cliff;
		Cell.Hp = 999999.f;        // game.js:597
		Cell.MaxHp = 999999.f;
		Cell.Level = 1;
		Cell.StructureId = FName(TEXT("cliff"));
		return Cell;
	}

	FGridCell MakeTreeCell()
	{
		FGridCell Cell;
		Cell.Type = ECellType::Tree;
		Cell.Hp = 120.f;           // game.js:679 "Ancient Oak"
		Cell.MaxHp = 120.f;
		Cell.Level = 1;
		Cell.StructureId = FName(TEXT("tree"));
		return Cell;
	}

	FGridCell MakeGoldmineCell(const FIntPoint Master)
	{
		FGridCell Cell;
		Cell.Type = ECellType::Goldmine;
		Cell.Hp = 999999.f;        // game.js:661 indestructible
		Cell.MaxHp = 999999.f;
		Cell.Level = 1;
		Cell.StructureId = FName(TEXT("goldmine"));
		Cell.MasterCell = Master;
		return Cell;
	}

	FGridCell MakeMerchantCell(const FIntPoint Master)
	{
		FGridCell Cell;
		Cell.Type = ECellType::MerchantShop;
		Cell.StructureId = FName(TEXT("merchant_shop"));
		Cell.MasterCell = Master;
		return Cell;
	}

	/** Map an editor COLOR name to its grid color slot (editor color config). Returns false if unknown. */
	bool ResolveMapColorName(const FString& Name, EKodoMapColor& Out)
	{
		if (Name == TEXT("ridge"))  { Out = EKodoMapColor::Ridge;  return true; }
		if (Name == TEXT("tree"))   { Out = EKodoMapColor::Tree;   return true; }
		if (Name == TEXT("mine"))   { Out = EKodoMapColor::Mine;   return true; }
		if (Name == TEXT("wall"))   { Out = EKodoMapColor::Wall;   return true; }
		if (Name == TEXT("cc"))     { Out = EKodoMapColor::CommandCenter; return true; }
		if (Name == TEXT("hero"))   { Out = EKodoMapColor::Hero;   return true; }
		if (Name == TEXT("kodo"))   { Out = EKodoMapColor::Kodo;   return true; }
		if (Name == TEXT("ground")) { Out = EKodoMapColor::Ground; return true; }
		return false;
	}
}

AKodoMapBootstrapper::AKodoMapBootstrapper()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	// Engine material that exposes a "Color" vector param — used to guarantee tinting works
	// (the basic-shape meshes' default material doesn't reliably respond to SetVectorParameterValue).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TintMat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	TintBaseMaterial = TintMat.Succeeded() ? TintMat.Object : nullptr;

	// Ground: engine plane (100x100 UU) scaled to the full 240 m map.
	// Sits 2 cm BELOW Z = 0: template maps (Basic, etc.) have their own floor at
	// exactly Z = 0, and two coplanar surfaces Z-fight (flickering speckle bands).
	// Gameplay math is unaffected — deprojection and movement use the Z = 0 plane.
	GroundMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroundMesh"));
	GroundMesh->SetupAttachment(Root);
	GroundMesh->SetMobility(EComponentMobility::Movable);
	// Sit 1 cm ABOVE Z = 0 so our tinted grass occludes the template level's default
	// checkerboard floor (which sits at exactly Z = 0) instead of hiding beneath it.
	GroundMesh->SetRelativeLocation(FVector(KodoUnits::MapExtentUU * 0.5f, KodoUnits::MapExtentUU * 0.5f, 1.f));
	GroundMesh->SetRelativeScale3D(FVector(KodoUnits::MapExtentUU / 100.f, KodoUnits::MapExtentUU / 100.f, 1.f));
	if (PlaneMesh.Succeeded())
	{
		GroundMesh->SetStaticMesh(PlaneMesh.Object);
	}

	const auto MakeHism = [this](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UHierarchicalInstancedStaticMeshComponent* Hism =
			CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(Name);
		Hism->SetupAttachment(RootComponent);
		Hism->SetMobility(EComponentMobility::Movable);
		if (Mesh)
		{
			Hism->SetStaticMesh(Mesh);
		}
		return Hism;
	};

	UStaticMesh* Cube = CubeMesh.Succeeded() ? CubeMesh.Object : nullptr;
	UStaticMesh* Cone = ConeMesh.Succeeded() ? ConeMesh.Object : nullptr;

	CliffInstances = MakeHism(TEXT("CliffInstances"), Cube);
	// Base-perimeter "walls" don't cast shadows: the low twilight sun throws long
	// wall shadows that clutter the top-down read without adding value (creator feedback).
	CliffInstances->SetCastShadow(false);
	TreeInstances = MakeHism(TEXT("TreeInstances"), Cone);
	MineInstances = MakeHism(TEXT("MineInstances"), Cube);
	TentInstances = MakeHism(TEXT("TentInstances"), Cone);
	// Raised-base platforms: solid cube (not plane) so the step's cliff faces render
	// from the angled camera instead of a floating flat sheet.
	GrassToneInstances = MakeHism(TEXT("GrassToneInstances"), Cube);
	GrassToneInstances->SetCastShadow(false);

	// --- Prototype base table (game.js:524-559), TDD §1.2 ---
	MerchantShopCell = KodoUnits::MerchantShopCell; // (78, 80)

	FPocketBaseDefinition PlayerBase; // North base — player start
	PlayerBase.Left = 65; PlayerBase.Right = 95; PlayerBase.Top = 4; PlayerBase.Bottom = 26;
	PlayerBase.Entrances = {
		{ EKodoEntranceSide::South, 78, 82 },
		{ EKodoEntranceSide::West, 8, 10 }, { EKodoEntranceSide::West, 20, 22 },
		{ EKodoEntranceSide::East, 8, 10 }, { EKodoEntranceSide::East, 20, 22 } };
	PlayerBase.Mines = { FIntPoint(78, 6) };
	PlayerBase.bHasCommandCenter = true;

	FPocketBaseDefinition SouthWestBase;
	SouthWestBase.Left = 15; SouthWestBase.Right = 35; SouthWestBase.Top = 110; SouthWestBase.Bottom = 130;
	SouthWestBase.Entrances = {
		{ EKodoEntranceSide::North, 23, 27 },
		{ EKodoEntranceSide::East, 118, 122 } };
	SouthWestBase.Mines = { FIntPoint(23, 112) };

	FPocketBaseDefinition SouthEastBase;
	SouthEastBase.Left = 125; SouthEastBase.Right = 145; SouthEastBase.Top = 110; SouthEastBase.Bottom = 130;
	SouthEastBase.Entrances = {
		{ EKodoEntranceSide::North, 133, 137 },
		{ EKodoEntranceSide::West, 118, 122 } };
	SouthEastBase.Mines = { FIntPoint(127, 112), FIntPoint(139, 112) };

	Bases = { PlayerBase, SouthWestBase, SouthEastBase };

	// Double every entrance width (creator request): grow each span symmetrically
	// about its center, clamped to stay inside the corner blocks so walls don't open up.
	for (FPocketBaseDefinition& Base : Bases)
	{
		for (FKodoEntranceSpan& Span : Base.Entrances)
		{
			const int32 Width = Span.End - Span.Start + 1;
			Span.Start -= Width / 2;
			Span.End += Width - Width / 2; // total growth = Width => new width = 2 * Width

			const bool bVertical = Span.Side == EKodoEntranceSide::West || Span.Side == EKodoEntranceSide::East;
			const int32 Lo = (bVertical ? Base.Top : Base.Left) + 1;
			const int32 Hi = (bVertical ? Base.Bottom : Base.Right) - 1;

			// Align to the even 2x2 wall lattice — even Start, odd End => the gap is a
			// whole number of 2x2 tiles, so the player can seal it 100% with walls and
			// never be left with a 1-cell sliver that no snapped 2x2 wall can fill.
			Span.Start = FMath::Clamp(Span.Start, Lo, Hi) & ~1;
			Span.End = FMath::Clamp(Span.End, Lo, Hi) | 1;
			while (Span.Start < Lo) { Span.Start += 2; }
			while (Span.End > Hi) { Span.End -= 2; }
		}
	}
}

void AKodoMapBootstrapper::BeginPlay()
{
	Super::BeginPlay();

	UKodoGridSubsystem* Grid = GetWorld()->GetSubsystem<UKodoGridSubsystem>();
	if (Grid)
	{
		BuildGrid(*Grid);
		PlaceAdminTower(*Grid); // before visuals so its cells get HISM/structure bodies in the pass
		BuildVisuals(*Grid);
	}

	// --- Phase 5 visual pass: terrain tones + nature tints + twilight atmosphere ---

	// WC3-ish Lordaeron palette via material tints (real texture-blend master material is
	// an editor-authored asset; these tints replace the checkerboard). Ground/Cliff/Tree/
	// Mine tints now read from the grid's color table (editor color config), defaulted to
	// the prototype palette. The merchant tent keeps its fixed purple (not editor-themed).
	const FLinearColor GroundCol = Grid ? Grid->GetMapColor(EKodoMapColor::Ground) : FLinearColor(0.10f, 0.17f, 0.07f);
	const FLinearColor RidgeCol  = Grid ? Grid->GetMapColor(EKodoMapColor::Ridge)  : FLinearColor(0.42f, 0.40f, 0.37f);
	const FLinearColor TreeCol   = Grid ? Grid->GetMapColor(EKodoMapColor::Tree)   : FLinearColor(0.06f, 0.24f, 0.08f);
	const FLinearColor MineCol   = Grid ? Grid->GetMapColor(EKodoMapColor::Mine)   : FLinearColor(0.85f, 0.65f, 0.15f);
	TintComponent(GroundMesh, GroundCol);                                  // summer grass
	TintComponent(CliffInstances, RidgeCol);                              // lighter warm stone
	TintComponent(TreeInstances, TreeCol);                                // canopy green
	TintComponent(MineInstances, MineCol);                                // gold seams
	TintComponent(TentInstances, FLinearColor(0.55f, 0.2f, 0.75f));        // merchant purple

	SetupAtmosphere();
}

void AKodoMapBootstrapper::TintComponent(UPrimitiveComponent* Component, const FLinearColor& Color)
{
	if (!Component)
	{
		return;
	}
	// Build the dynamic material from the known-tintable engine material (the mesh's own
	// default material doesn't always expose a settable color), then set several common
	// parameter names so the tint takes regardless of the material's exact param name.
	UMaterialInterface* Base = TintBaseMaterial ? TintBaseMaterial.Get() : Component->GetMaterial(0);
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Component);
	if (!MID)
	{
		return;
	}
	MID->SetVectorParameterValue(FName("Color"), Color);
	MID->SetVectorParameterValue(FName("BaseColor"), Color);
	MID->SetVectorParameterValue(FName("Tint"), Color);
	Component->SetMaterial(0, MID);
}

void AKodoMapBootstrapper::SetupAtmosphere()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Directional sun: low warm twilight angle. Reuse the level's light if present.
	ADirectionalLight* Sun = Cast<ADirectionalLight>(
		UGameplayStatics::GetActorOfClass(World, ADirectionalLight::StaticClass()));
	if (!Sun)
	{
		Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 5000.f), FRotator::ZeroRotator, Params);
	}
	if (Sun)
	{
		if (USceneComponent* SunRoot = Sun->GetRootComponent())
		{
			SunRoot->SetMobility(EComponentMobility::Movable);
		}
		// Creator feedback: long shadows clutter the top-down overview. Raise the sun
		// to a steep angle so shadows fall short and tight beneath each unit, while a
		// warm amber tint keeps the dusk mood. (Was -22 deg = long evening shadows.)
		Sun->SetActorRotation(FRotator(-58.f, 35.f, 0.f));
		if (UDirectionalLightComponent* LightComp = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			LightComp->SetIntensity(5.f);
			LightComp->SetLightColor(FLinearColor(1.f, 0.82f, 0.6f)); // amber, slightly lifted for the higher sun
			LightComp->SetAtmosphereSunLight(true);
			// Soften what shadows remain so they read as a faint contact shadow, not a
			// hard silhouette: shorter dynamic-shadow reach + a light shadow bias.
			LightComp->SetDynamicShadowDistanceMovableLight(8000.f);
			LightComp->SetShadowBias(0.6f);
		}
	}

	// Sky atmosphere + volumetric clouds, spawned by reflected class path so we
	// don't depend on engine-version-specific header locations.
	const auto SpawnEngineActorIfMissing = [World, &Params](const TCHAR* ClassPath)
	{
		if (UClass* ActorClass = FindObject<UClass>(nullptr, ClassPath))
		{
			if (!UGameplayStatics::GetActorOfClass(World, ActorClass))
			{
				const FVector Location = FVector::ZeroVector;
				const FRotator Rotation = FRotator::ZeroRotator;
				World->SpawnActor(ActorClass, &Location, &Rotation, Params);
			}
		}
	};
	SpawnEngineActorIfMissing(TEXT("/Script/Engine.SkyAtmosphere"));
	SpawnEngineActorIfMissing(TEXT("/Script/Engine.VolumetricCloud"));

	// Real-time-capture skylight for Lumen bounce color.
	if (!UGameplayStatics::GetActorOfClass(World, ASkyLight::StaticClass()))
	{
		if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0, 0, 4000.f), FRotator::ZeroRotator, Params))
		{
			if (USkyLightComponent* SkyComp = Sky->GetLightComponent())
			{
				SkyComp->SetMobility(EComponentMobility::Movable);
				SkyComp->bRealTimeCapture = true;
				SkyComp->SetIntensity(1.2f);
				SkyComp->MarkRenderStateDirty();
			}
		}
	}

	// Misty ground fog for the dark-fantasy twilight feel.
	if (!UGameplayStatics::GetActorOfClass(World, AExponentialHeightFog::StaticClass()))
	{
		if (AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(
			FVector(0, 0, 0), FRotator::ZeroRotator, Params))
		{
			if (UExponentialHeightFogComponent* FogComp = Fog->GetComponent())
			{
				FogComp->SetMobility(EComponentMobility::Movable);
				FogComp->SetFogDensity(0.018f);
				FogComp->SetFogHeightFalloff(0.4f);
				FogComp->SetStartDistance(1500.f);
				FogComp->SetFogInscatteringColor(FLinearColor(0.45f, 0.5f, 0.65f)); // cool blue mist
			}
		}
	}

}

bool AKodoMapBootstrapper::IsEntrance(const TArray<FKodoEntranceSpan>& Entrances, const EKodoEntranceSide Side,
                                      const int32 Coord)
{
	// Port of game.js:562-570: west/east spans test Y, north/south spans test X.
	for (const FKodoEntranceSpan& Span : Entrances)
	{
		if (Span.Side == Side && Coord >= Span.Start && Coord <= Span.End)
		{
			return true;
		}
	}
	return false;
}

bool AKodoMapBootstrapper::LoadLayoutFromFile(UKodoGridSubsystem& Grid)
{
	const FString Path = FPaths::ProjectContentDir() / TEXT("Config/KodoMapLayout.txt");
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path) || Lines.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[KodoMap] %s not found — using procedural layout"), *Path);
		return false;
	}

	int32 Placed = 0;
	int32 LimitN = Grid.GetCols(); // playable map limit (from the KODOMAP header); full grid by default
	bool bWallEdge = false;
	for (const FString& Raw : Lines)
	{
		const FString Line = Raw.TrimStartAndEnd();
		if (Line.IsEmpty())
		{
			continue;
		}
		if (Line.StartsWith(TEXT("KODOMAP")))
		{
			// Header: "KODOMAP <size> <size> [WALL]" — the playable map limit + edge-wall flag.
			TArray<FString> H;
			Line.ParseIntoArray(H, TEXT(" "), true);
			if (H.Num() >= 2) { LimitN = FCString::Atoi(*H[1]); }
			bWallEdge = Line.Contains(TEXT("WALL"));
			continue;
		}
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() < 3)
		{
			continue;
		}

		// Editor spawn/merchant overrides (editor spawn config). Stored on the grid so the
		// game mode / wave controller / build-exclusion all read a single source of truth.
		if (Tok[0] == TEXT("KSPAWN"))
		{
			Grid.SetKodoSpawnCell(FIntPoint(FCString::Atoi(*Tok[1]), FCString::Atoi(*Tok[2])));
			continue;
		}
		if (Tok[0] == TEXT("RSPAWN"))
		{
			Grid.SetRunnerSpawnCell(FIntPoint(FCString::Atoi(*Tok[1]), FCString::Atoi(*Tok[2])));
			continue;
		}
		if (Tok[0] == TEXT("MERCHANT"))
		{
			Grid.SetMerchantCell(FIntPoint(FCString::Atoi(*Tok[1]), FCString::Atoi(*Tok[2])));
			continue;
		}
		// Editor color override: "COLOR <name> <rrggbb>" (hex, no '#') (editor color config).
		if (Tok[0] == TEXT("COLOR"))
		{
			EKodoMapColor Slot;
			if (ResolveMapColorName(Tok[1], Slot))
			{
				const FColor Hex = FColor::FromHex(FString(TEXT("#")) + Tok[2]);
				Grid.SetMapColor(Slot, FLinearColor::FromSRGBColor(Hex));
			}
			continue;
		}
		// Raised-base terrain elevation layer (independent of the X/Y layout).
		// "E x y level" — raised cell at the given elevation level.
		if (Tok[0] == TEXT("E"))
		{
			if (Tok.Num() >= 4)
			{
				Grid.SetElevation(FIntPoint(FCString::Atoi(*Tok[1]), FCString::Atoi(*Tok[2])),
				                  FCString::Atoi(*Tok[3]));
			}
			continue;
		}
		// "RAMP x y DIR" — walkable ramp; DIR (E/W/S/N) = ascent direction toward the upper ground.
		if (Tok[0] == TEXT("RAMP"))
		{
			if (Tok.Num() >= 3)
			{
				int32 Dir = 3; // default N
				if (Tok.Num() >= 4)
				{
					const TCHAR D = Tok[3][0];
					Dir = (D == TEXT('E')) ? 0 : (D == TEXT('W')) ? 1 : (D == TEXT('S')) ? 2 : 3;
				}
				Grid.SetRamp(FIntPoint(FCString::Atoi(*Tok[1]), FCString::Atoi(*Tok[2])), Dir);
			}
			continue;
		}
		const TCHAR Type = Tok[0][0];
		const FIntPoint C(FCString::Atoi(*Tok[1]), FCString::Atoi(*Tok[2]));
		if (!Grid.IsInBounds(C))
		{
			continue;
		}
		switch (Type)
		{
		case 'C': Grid.SetCell(C, MakeCliffCell()); ++Placed; break;
		case 'T': Grid.SetCell(C, MakeTreeCell());  ++Placed; break;
		case 'G':
			if (Tok.Num() >= 5)
			{
				const FIntPoint Master(FCString::Atoi(*Tok[3]), FCString::Atoi(*Tok[4]));
				Grid.SetCell(C, MakeGoldmineCell(Master)); ++Placed;
			}
			break;
		// MERCHANT is handled above (Grid.SetMerchantCell); the tent itself is placed
		// below at Grid.GetMerchantCell(), which the build-exclusion logic also reads.
		default: break;
		}
	}

	// Map limit: wall off everything outside the playable NxN area (editor "Wall the map
	// edge"). A 2-cell cliff ring at the limit bounds the arena without a grid resize.
	LimitN = FMath::Clamp(LimitN, 8, Grid.GetCols());
	if (bWallEdge && LimitN <= Grid.GetCols())
	{
		const int32 L = LimitN - 1;
		for (int32 I = 0; I < LimitN; ++I)
		{
			for (int32 T = 0; T < 2; ++T)
			{
				Grid.SetCell(FIntPoint(I, T), MakeCliffCell());          // top edge
				Grid.SetCell(FIntPoint(I, L - T), MakeCliffCell());      // bottom edge
				Grid.SetCell(FIntPoint(T, I), MakeCliffCell());          // left edge
				Grid.SetCell(FIntPoint(L - T, I), MakeCliffCell());      // right edge
			}
		}
	}

	// 2x2 merchant tent at the editor-configured master cell (editor spawn config).
	const FIntPoint MerchantMaster = Grid.GetMerchantCell();
	for (int32 Dx = 0; Dx < 2; ++Dx)
	{
		for (int32 Dy = 0; Dy < 2; ++Dy)
		{
			Grid.SetCell(FIntPoint(MerchantMaster.X + Dx, MerchantMaster.Y + Dy), MakeMerchantCell(MerchantMaster));
		}
	}

	// Clear a safe pocket around the spawn cells and the merchant so nothing spawns
	// or starts buried in an imported cliff/tree (editor spawn config).
	const FIntPoint SafeCenters[] = {
		Grid.GetRunnerSpawnCell(), Grid.GetKodoSpawnCell(), KodoUnits::KodoRetreatCell };
	for (const FIntPoint& S : SafeCenters)
	{
		for (int32 Dx = -2; Dx <= 2; ++Dx)
		{
			for (int32 Dy = -2; Dy <= 2; ++Dy)
			{
				const FIntPoint Cell(S.X + Dx, S.Y + Dy);
				if (Grid.IsInBounds(Cell))
				{
					Grid.SetCell(Cell, FGridCell());
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[KodoMap] Loaded original layout: %d cells from %s"), Placed, *Path);
	return Placed > 0;
}

void AKodoMapBootstrapper::BuildGrid(UKodoGridSubsystem& Grid)
{
	// Prefer the real Warcraft III layout extracted from the .w3x; fall back to the
	// procedural pocket bases below only if the data file is missing.
	if (LoadLayoutFromFile(Grid))
	{
		return;
	}

	// Subsystem Initialize() already zeroed every cell to Empty/grass (game.js:504-521 pass 1).

	for (const FPocketBaseDefinition& Base : Bases)
	{
		// Clear base interior to grass (game.js:574-589).
		for (int32 X = Base.Left + 1; X < Base.Right; ++X)
		{
			for (int32 Y = Base.Top + 1; Y < Base.Bottom; ++Y)
			{
				Grid.SetCell(FIntPoint(X, Y), FGridCell());
			}
		}

		// West & East walls (game.js:591-619): full Y extent, skipping entrance spans.
		for (int32 Y = Base.Top; Y <= Base.Bottom; ++Y)
		{
			if (!IsEntrance(Base.Entrances, EKodoEntranceSide::West, Y))
			{
				Grid.SetCell(FIntPoint(Base.Left, Y), MakeCliffCell());
			}
			if (!IsEntrance(Base.Entrances, EKodoEntranceSide::East, Y))
			{
				Grid.SetCell(FIntPoint(Base.Right, Y), MakeCliffCell());
			}
		}

		// North & South walls (game.js:621-649): full X extent, skipping entrance spans.
		for (int32 X = Base.Left; X <= Base.Right; ++X)
		{
			if (!IsEntrance(Base.Entrances, EKodoEntranceSide::North, X))
			{
				Grid.SetCell(FIntPoint(X, Base.Top), MakeCliffCell());
			}
			if (!IsEntrance(Base.Entrances, EKodoEntranceSide::South, X))
			{
				Grid.SetCell(FIntPoint(X, Base.Bottom), MakeCliffCell());
			}
		}

		// 4x4 gold mines + flanking tree clusters (doubled from the prototype's 2x2
		// per creator request, game.js:651-700). Mine.X/Mine.Y stay the top-left master.
		for (const FIntPoint& Mine : Base.Mines)
		{
			for (int32 Dx = 0; Dx < KodoMineSize; ++Dx)
			{
				for (int32 Dy = 0; Dy < KodoMineSize; ++Dy)
				{
					Grid.SetCell(FIntPoint(Mine.X + Dx, Mine.Y + Dy), MakeGoldmineCell(Mine));
				}
			}

			// Trees flank the wider mine: two columns left (x-2, x-1) and two right
			// (x+size, x+size+1), spanning its two top rows, clamped to the base interior.
			const int32 TreeColumns[4] = { Mine.X - 2, Mine.X - 1, Mine.X + KodoMineSize, Mine.X + KodoMineSize + 1 };
			for (const int32 Tx : TreeColumns)
			{
				if (Tx < Base.Left + 1 || Tx >= Base.Right)
				{
					continue;
				}
				for (int32 Ty = Mine.Y; Ty < Mine.Y + 2; ++Ty)
				{
					if (Ty > Base.Top && Ty < Base.Bottom)
					{
						Grid.SetCell(FIntPoint(Tx, Ty), MakeTreeCell());
					}
				}
			}
		}
	}

	// 2x2 merchant tent at the central spawn zone (game.js:704-721).
	for (int32 Dx = 0; Dx < 2; ++Dx)
	{
		for (int32 Dy = 0; Dy < 2; ++Dy)
		{
			Grid.SetCell(FIntPoint(MerchantShopCell.X + Dx, MerchantShopCell.Y + Dy), MakeMerchantCell(MerchantShopCell));
		}
	}
}

void AKodoMapBootstrapper::PlaceAdminTower(UKodoGridSubsystem& Grid)
{
	// One indestructible 2x2 control panel near the TOP-RIGHT corner (highest X, lowest Y).
	// Scan inward from the corner along an expanding diagonal band and drop it on the first
	// empty 2x2 footprint found, so it lands on buildable ground regardless of the imported
	// layout's cliffs/trees in that corner. Mirrors the fixed-merchant placement pattern.
	const FKodoStructurePreset* Preset = KodoStructures::Find(FName("admin_tower"));
	const float Hp = Preset ? Preset->MaxHp : 999999.f;

	const int32 Cols = Grid.GetCols();
	const int32 Rows = Grid.GetRows();
	// Start a few tiles in from the absolute corner so it sits clearly inside the arena.
	const int32 StartX = Cols - 6; // near max X
	const int32 StartY = 4;        // near min Y

	const auto Is2x2Empty = [&Grid](const FIntPoint& Origin) -> bool
	{
		for (int32 Dx = 0; Dx < 2; ++Dx)
		{
			for (int32 Dy = 0; Dy < 2; ++Dy)
			{
				const FIntPoint C(Origin.X + Dx, Origin.Y + Dy);
				if (!Grid.IsInBounds(C) || Grid.GetCell(C).Type != ECellType::Empty)
				{
					return false;
				}
			}
		}
		return true;
	};

	// Search outward from (StartX, StartY): march left and down in widening rings until a
	// clear 2x2 footprint is found. Bounded scan keeps it cheap and deterministic.
	FIntPoint Origin(-1, -1);
	for (int32 Ring = 0; Ring < 40 && Origin.X < 0; ++Ring)
	{
		for (int32 Off = 0; Off <= Ring && Origin.X < 0; ++Off)
		{
			// Prefer staying near the top-right: subtract from X (move left), add to Y (move down).
			const FIntPoint Candidate(StartX - (Ring - Off), StartY + Off);
			if (Is2x2Empty(Candidate))
			{
				Origin = Candidate;
			}
		}
	}
	if (Origin.X < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[KodoMap] No clear 2x2 for the Admin Tower near the top-right corner."));
		return;
	}

	for (int32 Dx = 0; Dx < 2; ++Dx)
	{
		for (int32 Dy = 0; Dy < 2; ++Dy)
		{
			const FIntPoint Target(Origin.X + Dx, Origin.Y + Dy);
			FGridCell NewCell;
			NewCell.Type = ECellType::Tower;     // a Tower cell so the structure manager renders it
			NewCell.Hp = Hp;
			NewCell.MaxHp = Hp;
			NewCell.Level = 1;
			NewCell.StructureId = FName("admin_tower");
			NewCell.bUnderConstruction = false;  // pre-built, no construction timer
			NewCell.MasterCell = Origin;          // top-left master (2x2)
			Grid.SetCell(Target, NewCell);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[KodoMap] Admin Tower placed at (%d,%d)."), Origin.X, Origin.Y);
}

AKodoMapBootstrapper::FCellRender AKodoMapBootstrapper::EvaluateCell(UKodoGridSubsystem& Grid, const FIntPoint& Coord)
{
	// Single source of truth for what one cell renders. The transform math below is byte-identical
	// to the original BuildVisuals per-cell logic — only the delivery (returned struct vs. inline
	// array append) changed. Ramp cells still write their walk-surface Z via SetRampSlope.
	const float Cell = KodoUnits::CellSizeUU;
	const float Step = KodoUnits::ElevationLevelStepUU;
	const int32 X = Coord.X;
	const int32 Y = Coord.Y;

	FCellRender Out;

	const float ElevZ = Grid.GetElevationZ(Coord);
	const int32 Lvl = Grid.GetElevationLevel(Coord);
	const bool bRamp = Grid.IsRamp(Coord);
	const FGridCell& State = Grid.GetCell(Coord);
	const bool bRidge = State.Type == ECellType::Cliff;

	// Highest-elevation 4-neighbour drives the ridge wall height.
	int32 HiNbrLvl = 0;
	{
		const int32 NDx[4] = { 1, -1, 0, 0 };
		const int32 NDy[4] = { 0, 0, 1, -1 };
		for (int32 d = 0; d < 4; ++d)
		{
			HiNbrLvl = FMath::Max(HiNbrLvl, Grid.GetElevationLevel(FIntPoint(X + NDx[d], Y + NDy[d])));
		}
	}

	// --- Raised terrain rendering (GROUND instance) ---
	if (bRidge)
	{
		// A ridge IS the edge of the upper ground: a tall cliff wall rising from the
		// lower ground (Z=0) to the upper-ground top plus a rim lip, so the height
		// reads clearly as the boundary of the plateau even when zoomed out.
		const int32 EdgeLvl = FMath::Max(FMath::Max(Lvl, HiNbrLvl), 1);
		const float WallTop = EdgeLvl * Step + 45.f; // +lip: rim stands above the platform
		FVector C = Grid.CellToWorldCenter(Coord);
		C.Z = WallTop * 0.5f;
		Out.GroundHism = CliffInstances;
		Out.GroundXform = FTransform(FQuat::Identity, C, FVector(Cell / 100.f, Cell / 100.f, WallTop / 100.f));
		Out.bHasGround = true;
	}
	else if (bRamp)
	{
		// Directional ramp (editor-authored dir). Walk the ascent axis to find this cell's
		// place in the run, then build one continuous slope so a 2-cell run = a 2-cell ramp,
		// axis-aligned to the chosen direction. The slope's edge heights are stored on the
		// grid so units/buildings ride the angle (continuous, no sinking).
		const int32 Dir = FMath::Max(0, Grid.GetRampDir(Coord));
		const int32 DXs[4] = { 1, -1, 0, 0 };
		const int32 DYs[4] = { 0, 0, 1, -1 };
		const int32 ddx = DXs[Dir], ddy = DYs[Dir];

		int32 Below = 0; // ramp cells toward the lower ground (-dir)
		for (FIntPoint c(Coord.X - ddx, Coord.Y - ddy); Grid.IsRamp(c); c = FIntPoint(c.X - ddx, c.Y - ddy)) { ++Below; }
		int32 Above = 0; // ramp cells toward the plateau (+dir)
		FIntPoint Top = Coord;
		for (FIntPoint c(Coord.X + ddx, Coord.Y + ddy); Grid.IsRamp(c); c = FIntPoint(c.X + ddx, c.Y + ddy)) { ++Above; Top = c; }
		const int32 RunLen = Below + Above + 1;

		// Plateau height the ramp climbs to = the level just past the top of the run.
		const FIntPoint Beyond(Top.X + ddx, Top.Y + ddy);
		int32 PlLvl = Grid.GetElevationLevel(Beyond);
		if (PlLvl <= 0) { PlLvl = FMath::Max(1, Grid.GetElevationLevel(FIntPoint(Beyond.X + ddx, Beyond.Y + ddy))); }
		const float RisePerCell = (PlLvl * Step) / RunLen;
		const float BotZ = Below * RisePerCell;           // low (-dir) edge of this cell
		const float TopZ = (Below + 1) * RisePerCell;      // high (+dir) edge of this cell
		Grid.SetRampSlope(Coord, BotZ, TopZ);              // units/buildings ride this

		if (TopZ - BotZ > 0.5f)
		{
			const float Rise = TopZ - BotZ;
			const float SlopeLen = FMath::Sqrt(Cell * Cell + Rise * Rise);
			const float PitchDeg = FMath::RadiansToDegrees(FMath::Atan2(Rise, Cell)); // +X end up
			const float YawDeg = (Dir == 0) ? 0.f : (Dir == 1) ? 180.f : (Dir == 2) ? 90.f : 270.f;
			FVector C = Grid.CellToWorldCenter(Coord);
			C.Z = 0.5f * (BotZ + TopZ);
			const FRotator Rot(PitchDeg, YawDeg, 0.f);
			Out.GroundHism = GrassToneInstances;
			Out.GroundXform = FTransform(Rot.Quaternion(), C, FVector(SlopeLen / 100.f, Cell / 100.f, 0.40f));
			Out.bHasGround = true;
		}
	}
	else if (Lvl >= 1)
	{
		// Interior upper ground: a solid platform block whose sides are the cliff faces.
		FVector C = Grid.CellToWorldCenter(Coord);
		C.Z = ElevZ * 0.5f;
		Out.GroundHism = GrassToneInstances;
		Out.GroundXform = FTransform(FQuat::Identity, C, FVector(Cell / 100.f, Cell / 100.f, ElevZ / 100.f));
		Out.bHasGround = true;
	}

	// --- Feature rendering (FEATURE instance) ---
	if (State.Type != ECellType::Empty)
	{
		FVector Center = Grid.CellToWorldCenter(Coord);
		// Trees/mines/merchant sit on top of the raised ground (0 for ground-level cells).
		Center.Z += ElevZ;

		switch (State.Type)
		{
		case ECellType::Cliff:
			break; // rendered above as the tall cliff-edge wall

		case ECellType::Tree:
			// Shorter, slimmer cone (~1.6 m) so forests don't dominate the top-down read.
			Center.Z += 80.f;
			Out.FeatureHism = TreeInstances;
			Out.FeatureXform = FTransform(FQuat::Identity, Center, FVector(0.85f, 0.85f, 1.6f));
			Out.bHasFeature = true;
			break;

		case ECellType::Goldmine:
			// Low wide block per cell (2x2 cells form the full mine).
			Center.Z += 40.f;
			Out.FeatureHism = MineInstances;
			Out.FeatureXform = FTransform(FQuat::Identity, Center, FVector(1.4f, 1.4f, 0.8f));
			Out.bHasFeature = true;
			break;

		case ECellType::MerchantShop:
			// Tent: squat cone per cell.
			Center.Z += 90.f;
			Out.FeatureHism = TentInstances;
			Out.FeatureXform = FTransform(FQuat::Identity, Center, FVector(1.5f, 1.5f, 1.8f));
			Out.bHasFeature = true;
			break;

		default:
			break; // Wall/Tower are player-built (Phase 3/4)
		}
	}

	return Out;
}

void AKodoMapBootstrapper::BuildVisuals(UKodoGridSubsystem& Grid)
{
	// Accumulate every instance's world-space transform per HISM (plus the owning cell, in a
	// parallel array) during the cell loop, then add them all at once after the loop. A single
	// AddInstances() rebuilds each cluster tree ONCE (vs. one rebuild per AddInstance), which is
	// what made each editor edit freeze the game. The parallel cell arrays let us populate the
	// stable CellToInst maps so the incremental UpdateCellVisual path works after a full build.
	TArray<FTransform> CliffXf;      TArray<FIntPoint> CliffCells;
	TArray<FTransform> TreeXf;       TArray<FIntPoint> TreeCells;
	TArray<FTransform> MineXf;       TArray<FIntPoint> MineCells;
	TArray<FTransform> TentXf;       TArray<FIntPoint> TentCells;
	TArray<FTransform> GrassToneXf;  TArray<FIntPoint> GrassToneCells; // ramp wedges + interior platform blocks

	const auto Accumulate = [&](UHierarchicalInstancedStaticMeshComponent* Hism, const FTransform& Xf, const FIntPoint& C)
	{
		if (Hism == CliffInstances)          { CliffXf.Add(Xf);      CliffCells.Add(C); }
		else if (Hism == TreeInstances)      { TreeXf.Add(Xf);       TreeCells.Add(C); }
		else if (Hism == MineInstances)      { MineXf.Add(Xf);       MineCells.Add(C); }
		else if (Hism == TentInstances)      { TentXf.Add(Xf);       TentCells.Add(C); }
		else if (Hism == GrassToneInstances) { GrassToneXf.Add(Xf);  GrassToneCells.Add(C); }
	};

	for (int32 X = 0; X < Grid.GetCols(); ++X)
	{
		for (int32 Y = 0; Y < Grid.GetRows(); ++Y)
		{
			const FIntPoint Coord(X, Y);
			const FCellRender R = EvaluateCell(Grid, Coord);
			if (R.bHasGround)  { Accumulate(R.GroundHism, R.GroundXform, Coord); }
			if (R.bHasFeature) { Accumulate(R.FeatureHism, R.FeatureXform, Coord); }
		}
	}

	// Batched commit + map population: one AddInstances per HISM (transforms are world-space,
	// matching the old per-instance AddInstance(..., bWorldSpace=true)). After adding, record each
	// instance's slot index in CellToInst (slots are appended in order, so index i == cell i) and
	// clear the free-list — the maps now describe every live instance for incremental updates.
	const auto Commit = [](UHierarchicalInstancedStaticMeshComponent* Hism, const TArray<FTransform>& Xf,
	                       const TArray<FIntPoint>& Cells, TMap<FIntPoint, int32>& CellToInst, TArray<int32>& FreeSlots)
	{
		CellToInst.Reset();
		FreeSlots.Reset();
		if (Hism && Xf.Num() > 0)
		{
			Hism->AddInstances(Xf, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
			for (int32 i = 0; i < Cells.Num(); ++i)
			{
				CellToInst.Add(Cells[i], i);
			}
		}
	};
	Commit(CliffInstances, CliffXf, CliffCells, CliffCellToInst, CliffFreeSlots);
	Commit(TreeInstances, TreeXf, TreeCells, TreeCellToInst, TreeFreeSlots);
	Commit(MineInstances, MineXf, MineCells, MineCellToInst, MineFreeSlots);
	Commit(TentInstances, TentXf, TentCells, TentCellToInst, TentFreeSlots);
	Commit(GrassToneInstances, GrassToneXf, GrassToneCells, GrassToneCellToInst, GrassToneFreeSlots);

	// Tint the raised ground (platforms + ramps) a touch lighter than the lower ground so the
	// plateau reads as distinct, raised terrain even from a top-down angle.
	const FLinearColor UpperGround = FMath::Lerp(Grid.GetMapColor(EKodoMapColor::Ground), FLinearColor::White, 0.18f);
	TintComponent(GrassToneInstances, UpperGround);
}

void AKodoMapBootstrapper::SetSlot(UHierarchicalInstancedStaticMeshComponent* Hism, TMap<FIntPoint, int32>& CellToInst,
                                   TArray<int32>& FreeSlots, const FIntPoint& Cell, const FTransform& Xform)
{
	if (!Hism)
	{
		return;
	}
	if (const int32* Existing = CellToInst.Find(Cell))
	{
		// Cell already has a slot — update it in place (world space, mark dirty, teleport so there's no interp).
		Hism->UpdateInstanceTransform(*Existing, Xform, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
		return;
	}
	if (FreeSlots.Num() > 0)
	{
		// Recycle a previously hidden slot.
		const int32 Slot = FreeSlots.Pop(EAllowShrinking::No);
		Hism->UpdateInstanceTransform(Slot, Xform, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
		CellToInst.Add(Cell, Slot);
		return;
	}
	// No free slot — append a new instance (its index is stable for the HISM's lifetime).
	const int32 NewIdx = Hism->AddInstance(Xform, /*bWorldSpace*/ true);
	CellToInst.Add(Cell, NewIdx);
}

void AKodoMapBootstrapper::ClearSlot(UHierarchicalInstancedStaticMeshComponent* Hism, TMap<FIntPoint, int32>& CellToInst,
                                     TArray<int32>& FreeSlots, const FIntPoint& Cell)
{
	if (!Hism)
	{
		return;
	}
	if (const int32* Existing = CellToInst.Find(Cell))
	{
		const int32 Idx = *Existing;
		// NEVER RemoveInstance (it shifts every later index and corrupts the maps). Instead hide the
		// instance far below the world at a degenerate scale, and return its slot to the free-list.
		const FTransform Hidden(FQuat::Identity, FVector(0.f, 0.f, -100000.f), FVector(0.0001f));
		Hism->UpdateInstanceTransform(Idx, Hidden, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
		FreeSlots.Add(Idx);
		CellToInst.Remove(Cell);
	}
}

void AKodoMapBootstrapper::UpdateCellVisual(UKodoGridSubsystem& Grid, const FIntPoint& Cell)
{
	const FCellRender R = EvaluateCell(Grid, Cell);

	// GROUND: a cell holds at most one ground slot (Cliff OR GrassTone). Set it on the applicable
	// HISM and clear it on the other so a cell that changed ground type doesn't leave a stale body.
	if (R.bHasGround && R.GroundHism == CliffInstances)
	{
		SetSlot(CliffInstances, CliffCellToInst, CliffFreeSlots, Cell, R.GroundXform);
		ClearSlot(GrassToneInstances, GrassToneCellToInst, GrassToneFreeSlots, Cell);
	}
	else if (R.bHasGround && R.GroundHism == GrassToneInstances)
	{
		SetSlot(GrassToneInstances, GrassToneCellToInst, GrassToneFreeSlots, Cell, R.GroundXform);
		ClearSlot(CliffInstances, CliffCellToInst, CliffFreeSlots, Cell);
	}
	else
	{
		ClearSlot(CliffInstances, CliffCellToInst, CliffFreeSlots, Cell);
		ClearSlot(GrassToneInstances, GrassToneCellToInst, GrassToneFreeSlots, Cell);
	}

	// FEATURE: a cell holds at most one feature slot (Tree/Mine/Tent). Set it on the applicable
	// HISM and clear it on the other two.
	if (R.bHasFeature && R.FeatureHism == TreeInstances)
	{
		SetSlot(TreeInstances, TreeCellToInst, TreeFreeSlots, Cell, R.FeatureXform);
		ClearSlot(MineInstances, MineCellToInst, MineFreeSlots, Cell);
		ClearSlot(TentInstances, TentCellToInst, TentFreeSlots, Cell);
	}
	else if (R.bHasFeature && R.FeatureHism == MineInstances)
	{
		SetSlot(MineInstances, MineCellToInst, MineFreeSlots, Cell, R.FeatureXform);
		ClearSlot(TreeInstances, TreeCellToInst, TreeFreeSlots, Cell);
		ClearSlot(TentInstances, TentCellToInst, TentFreeSlots, Cell);
	}
	else if (R.bHasFeature && R.FeatureHism == TentInstances)
	{
		SetSlot(TentInstances, TentCellToInst, TentFreeSlots, Cell, R.FeatureXform);
		ClearSlot(TreeInstances, TreeCellToInst, TreeFreeSlots, Cell);
		ClearSlot(MineInstances, MineCellToInst, MineFreeSlots, Cell);
	}
	else
	{
		ClearSlot(TreeInstances, TreeCellToInst, TreeFreeSlots, Cell);
		ClearSlot(MineInstances, MineCellToInst, MineFreeSlots, Cell);
		ClearSlot(TentInstances, TentCellToInst, TentFreeSlots, Cell);
	}
}

void AKodoMapBootstrapper::UpdateCellRegion(UKodoGridSubsystem& Grid, const FIntPoint& Center)
{
	// The edited cell plus its 8 neighbors — a ridge wall's height (highest 4-neighbour level) and
	// a ramp's slope (run length from neighbors) depend on adjacent cells, so they re-evaluate too.
	for (int32 Dx = -1; Dx <= 1; ++Dx)
	{
		for (int32 Dy = -1; Dy <= 1; ++Dy)
		{
			const FIntPoint C(Center.X + Dx, Center.Y + Dy);
			if (Grid.IsInBounds(C))
			{
				UpdateCellVisual(Grid, C);
			}
		}
	}
}

bool AKodoMapBootstrapper::SaveLayoutToFile(const UKodoGridSubsystem& Grid) const
{
	// Serialize the CURRENT live grid back to the map file in the EXACT format
	// LoadLayoutFromFile parses (so an editor edit round-trips). Lines are grouped in the
	// same order the file currently uses: header, COLOR*, spawns, E*, RAMP*, G*, C*, T*.
	const int32 Cols = Grid.GetCols();
	const int32 Rows = Grid.GetRows();

	// Linear -> sRGB FColor (ToFColor(true)) is the inverse of the loader's
	// FLinearColor::FromSRGBColor(FColor), so colors round-trip.
	const auto HexOf = [&Grid](EKodoMapColor Which) -> FString
	{
		const FColor C = Grid.GetMapColor(Which).ToFColor(/*bSRGB*/ true);
		return FString::Printf(TEXT("%02x%02x%02x"), C.R, C.G, C.B);
	};

	TArray<FString> Lines;

	// Header: "KODOMAP <Cols> <Rows>". We don't persist a WALL edge flag (no grid getter
	// exposes it), so omit it — the loader treats its absence as "no edge wall".
	Lines.Add(FString::Printf(TEXT("KODOMAP %d %d"), Cols, Rows));

	// COLOR <name> <rrggbb> — names must match the loader's ResolveMapColorName.
	Lines.Add(FString::Printf(TEXT("COLOR ridge %s"),  *HexOf(EKodoMapColor::Ridge)));
	Lines.Add(FString::Printf(TEXT("COLOR tree %s"),   *HexOf(EKodoMapColor::Tree)));
	Lines.Add(FString::Printf(TEXT("COLOR mine %s"),   *HexOf(EKodoMapColor::Mine)));
	Lines.Add(FString::Printf(TEXT("COLOR wall %s"),   *HexOf(EKodoMapColor::Wall)));
	Lines.Add(FString::Printf(TEXT("COLOR cc %s"),     *HexOf(EKodoMapColor::CommandCenter)));
	Lines.Add(FString::Printf(TEXT("COLOR hero %s"),   *HexOf(EKodoMapColor::Hero)));
	Lines.Add(FString::Printf(TEXT("COLOR kodo %s"),   *HexOf(EKodoMapColor::Kodo)));
	Lines.Add(FString::Printf(TEXT("COLOR ground %s"), *HexOf(EKodoMapColor::Ground)));

	// Spawn + merchant master cells.
	const FIntPoint KSpawn = Grid.GetKodoSpawnCell();
	const FIntPoint RSpawn = Grid.GetRunnerSpawnCell();
	const FIntPoint Merchant = Grid.GetMerchantCell();
	Lines.Add(FString::Printf(TEXT("KSPAWN %d %d"), KSpawn.X, KSpawn.Y));
	Lines.Add(FString::Printf(TEXT("RSPAWN %d %d"), RSpawn.X, RSpawn.Y));
	Lines.Add(FString::Printf(TEXT("MERCHANT %d %d"), Merchant.X, Merchant.Y));

	// Collect terrain lines into separate buckets so they emit in the canonical group order.
	TArray<FString> ElevLines;
	TArray<FString> RampLines;
	TArray<FString> MineLines;
	TArray<FString> CliffLines;
	TArray<FString> TreeLines;

	for (int32 X = 0; X < Cols; ++X)
	{
		for (int32 Y = 0; Y < Rows; ++Y)
		{
			const FIntPoint Coord(X, Y);

			// Raised-base elevation (E x y level) — only persisted when raised.
			const int32 Level = Grid.GetElevationLevel(Coord);
			if (Level >= 1)
			{
				ElevLines.Add(FString::Printf(TEXT("E %d %d %d"), X, Y, Level));
			}

			// Walkable ramp (RAMP x y DIR) — DIR letter from GetRampDir (0=E 1=W 2=S 3=N).
			if (Grid.IsRamp(Coord))
			{
				const int32 Dir = Grid.GetRampDir(Coord);
				const TCHAR DirLetter = (Dir == 0) ? TEXT('E') : (Dir == 1) ? TEXT('W')
				                      : (Dir == 2) ? TEXT('S') : TEXT('N'); // 3 or fallback => N
				RampLines.Add(FString::Printf(TEXT("RAMP %d %d %c"), X, Y, DirLetter));
			}

			// Terrain cell types: only C/T/G are saved map terrain (Wall/Tower/MerchantShop/
			// Empty are player-built or runtime, matching what LoadLayoutFromFile expects).
			const FGridCell& State = Grid.GetCell(Coord);
			switch (State.Type)
			{
			case ECellType::Cliff:
				CliffLines.Add(FString::Printf(TEXT("C %d %d"), X, Y));
				break;
			case ECellType::Tree:
				TreeLines.Add(FString::Printf(TEXT("T %d %d"), X, Y));
				break;
			case ECellType::Goldmine:
			{
				// G x y mx my — the mine's 2x2 master cell; fall back to x,y if unset (-1,-1).
				const FIntPoint Master = (State.MasterCell.X >= 0 && State.MasterCell.Y >= 0)
				                       ? State.MasterCell : Coord;
				MineLines.Add(FString::Printf(TEXT("G %d %d %d %d"), X, Y, Master.X, Master.Y));
				break;
			}
			default:
				break; // Empty / Wall / Tower / MerchantShop — not saved terrain.
			}
		}
	}

	Lines.Append(ElevLines);
	Lines.Append(RampLines);
	Lines.Append(MineLines);
	Lines.Append(CliffLines);
	Lines.Append(TreeLines);

	const FString Path = FPaths::ProjectContentDir() / TEXT("Config/KodoMapLayout.txt");
	const FString Contents = FString::Join(Lines, TEXT("\n")) + TEXT("\n");
	if (!FFileHelper::SaveStringToFile(Contents, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("[KodoMap] Failed to save layout to %s"), *Path);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[KodoMap] Saved layout (%d lines) to %s"), Lines.Num(), *Path);
	return true;
}

void AKodoMapBootstrapper::RebuildTerrainVisuals(UKodoGridSubsystem& Grid)
{
	// Drop all blockout terrain instances, then re-add + re-tint from the current grid.
	// (Player-built Wall/Tower structures live on the StructureManager via OnCellChanged
	// and are NOT touched here.) BuildVisuals only AddInstance/SetRampSlope/Tint, so a
	// clear-then-build is safe to repeat.
	if (CliffInstances)      { CliffInstances->ClearInstances(); }
	if (TreeInstances)       { TreeInstances->ClearInstances(); }
	if (MineInstances)       { MineInstances->ClearInstances(); }
	if (TentInstances)       { TentInstances->ClearInstances(); }
	if (GrassToneInstances)  { GrassToneInstances->ClearInstances(); }

	// The HISMs are now empty, so the stable-slot bookkeeping must be reset too — BuildVisuals
	// repopulates CellToInst from scratch (and Commit() clears the free-lists). Reset here as well
	// so a HISM that ends up with zero instances still has empty maps/free-lists.
	CliffCellToInst.Reset();     CliffFreeSlots.Reset();
	TreeCellToInst.Reset();      TreeFreeSlots.Reset();
	MineCellToInst.Reset();      MineFreeSlots.Reset();
	TentCellToInst.Reset();      TentFreeSlots.Reset();
	GrassToneCellToInst.Reset(); GrassToneFreeSlots.Reset();

	BuildVisuals(Grid);
}
