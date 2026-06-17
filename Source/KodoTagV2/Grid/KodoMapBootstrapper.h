// Kodo Tag: Survivor — UE Migration, Phase 1.
// Port of initGrid() (game.js:500-722): three pocket bases with perimeter
// cliffs + entrance gaps, 2x2 gold mines with flanking trees, and the 2x2
// merchant tent. Base layouts are DATA (editable per-instance), defaulted to
// the prototype's exact values (TDD §1.2, §5). Visuals are blockout-grade
// HISM instances, replaced by Nanite meshes in Phase 5.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Grid/KodoGridTypes.h"
#include "KodoMapBootstrapper.generated.h"

class UKodoGridSubsystem;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMeshComponent;

UENUM()
enum class EKodoEntranceSide : uint8
{
	North, // gap in the top wall (y = Top), span over X
	South, // gap in the bottom wall (y = Bottom), span over X
	East,  // gap in the right wall (x = Right), span over Y
	West   // gap in the left wall (x = Left), span over Y
};

USTRUCT()
struct FKodoEntranceSpan
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "KodoMap")
	EKodoEntranceSide Side = EKodoEntranceSide::North;

	UPROPERTY(EditAnywhere, Category = "KodoMap")
	int32 Start = 0;

	UPROPERTY(EditAnywhere, Category = "KodoMap")
	int32 End = 0;
};

USTRUCT()
struct FPocketBaseDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "KodoMap") int32 Left = 0;
	UPROPERTY(EditAnywhere, Category = "KodoMap") int32 Right = 0;
	UPROPERTY(EditAnywhere, Category = "KodoMap") int32 Top = 0;
	UPROPERTY(EditAnywhere, Category = "KodoMap") int32 Bottom = 0;

	UPROPERTY(EditAnywhere, Category = "KodoMap")
	TArray<FKodoEntranceSpan> Entrances;

	/** 2x2 gold mine master cells (top-left). */
	UPROPERTY(EditAnywhere, Category = "KodoMap")
	TArray<FIntPoint> Mines;

	/** Marks the player's intended starting base. The CC itself is PLAYER-BUILT (150g/50w), never pre-spawned (TDD §5). */
	UPROPERTY(EditAnywhere, Category = "KodoMap")
	bool bHasCommandCenter = false;
};

UCLASS()
class KODOTAGV2_API AKodoMapBootstrapper : public AActor
{
	GENERATED_BODY()

public:
	AKodoMapBootstrapper();

	virtual void BeginPlay() override;

protected:
	/** Writes all structures into the grid subsystem (the data pass). */
	void BuildGrid(UKodoGridSubsystem& Grid);

	/**
	 * Load the real Warcraft III Kodo Tag layout (cliffs/trees/mines) from
	 * Content/Config/KodoMapLayout.txt, extracted from the original .w3x. Returns
	 * false (and BuildGrid falls back to the procedural bases) if the file is missing.
	 */
	bool LoadLayoutFromFile(UKodoGridSubsystem& Grid);

	/** Adds one HISM instance per occupied cell (the visual pass); also writes ramp walk-surface Z. */
	void BuildVisuals(UKodoGridSubsystem& Grid);

	/** Places ONE indestructible 2x2 admin_tower control panel near the top-right corner
	 *  (highest X / lowest Y) on the first empty buildable 2x2 footprint found scanning inward. */
	void PlaceAdminTower(UKodoGridSubsystem& Grid);

	static bool IsEntrance(const TArray<FKodoEntranceSpan>& Entrances, EKodoEntranceSide Side, int32 Coord);

	/** Prototype base table, game.js:524-559. */
	UPROPERTY(EditAnywhere, Category = "KodoMap")
	TArray<FPocketBaseDefinition> Bases;

	/** Merchant tent 2x2 master cell, game.js:704-706. */
	UPROPERTY(EditAnywhere, Category = "KodoMap")
	FIntPoint MerchantShopCell;

	UPROPERTY(VisibleAnywhere, Category = "KodoMap") TObjectPtr<UStaticMeshComponent> GroundMesh;
	UPROPERTY(VisibleAnywhere, Category = "KodoMap") TObjectPtr<UHierarchicalInstancedStaticMeshComponent> CliffInstances;
	UPROPERTY(VisibleAnywhere, Category = "KodoMap") TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TreeInstances;
	UPROPERTY(VisibleAnywhere, Category = "KodoMap") TObjectPtr<UHierarchicalInstancedStaticMeshComponent> MineInstances;
	UPROPERTY(VisibleAnywhere, Category = "KodoMap") TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TentInstances;

	/** Phase 5: alternating grass-tone tiles breaking up the flat ground color. */
	UPROPERTY(VisibleAnywhere, Category = "KodoMap") TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GrassToneInstances;

	/** Engine tintable material used as the base for all colored instances (has a "Color" param). */
	UPROPERTY() TObjectPtr<UMaterialInterface> TintBaseMaterial;

	/** Tints a component by assigning a dynamic instance of the engine tintable material. */
	void TintComponent(class UPrimitiveComponent* Component, const FLinearColor& Color);

	/** Phase 5: twilight lighting rig (Lumen GI is enabled project-wide in DefaultEngine.ini). */
	void SetupAtmosphere();
};
