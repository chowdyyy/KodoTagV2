// Kodo Tag: Survivor — UE Migration, Phase 2.

#include "Actors/KodoCharacter.h"
#include "Actors/KodoWaveController.h"
#include "Actors/RunnerCharacter.h"
#include "Grid/KodoGridSubsystem.h"
#include "Core/KodoTagGameState.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/WidgetComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/KodoHealthBarWidget.h"
#include "Core/KodoTint.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

namespace
{
	/**
	 * Per-type base stats, now ADOPTING THE ORIGINAL WC3 MODEL (kodo_balance_data.md §1/§5).
	 * Speeds/ranges are in WC3 units (umvs / ua1r); HP/Dmg/Armor are raw WC3 values.
	 * WC3 speed/range are converted to UU at use-time via KodoUnits::Wc3ToUU (~×1.172).
	 *
	 * Archetype -> WC3 roster mapping (picks documented in the report's §5 Action column):
	 *   Standard ≈ mid kodo o011:  500 HP / 350 spd / 7 dmg / armor 1
	 *   Speed    ≈ fast o003:      450 HP / 370 spd / 5 dmg / armor 2  (1x1 footprint)
	 *   Tank     ≈ tank o01Z:     5000 HP / 280 spd / 12 dmg / armor 4
	 *   Blink    ≈ heavy o00U band:1500 HP / 310 spd / 7 dmg / armor 1  (+blink ability)
	 */
	struct FKodoTypeStats
	{
		float Hp;
		float SpeedWc3;   // WC3 units/sec (umvs)
		float Damage;     // WC3 ua1b
		float Armor;      // WC3 udef
		float SizePx;     // prototype px — drives eat range + body scale only
		int32 Footprint;
		int32 GoldReward;
		int32 WoodReward;
	};

	FKodoTypeStats StatsFor(const EKodoType Type)
	{
		switch (Type)
		{
		case EKodoType::Speed: return { 450.f,  370.f,  5.f, 2.f, 12.f, 1, 12, 4 }; // WC3 o003 "fast"
		case EKodoType::Tank:  return { 5000.f, 280.f, 12.f, 4.f, 27.f, 2, 20, 6 }; // WC3 o01Z "tank"
		case EKodoType::Blink: return { 1500.f, 310.f,  7.f, 1.f, 22.f, 2, 25, 8 }; // WC3 o00U band + blink
		default:               return { 500.f,  350.f,  7.f, 1.f, 23.f, 2,  8, 3 }; // WC3 o011 "mid" (standard)
		}
	}
}

AKodoCharacter::AKodoCharacter()
{
	// Movement is driven by the AI state machine below, not the base auto-follow.
	bAutoFollowPath = false;

	// Overhead health bar (creator feedback). Screen space keeps it a fixed pixel size
	// facing the top-down camera; no render target, so it scales to 200+ units cheaply.
	HealthBarComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthBar"));
	HealthBarComponent->SetupAttachment(RootComponent);
	HealthBarComponent->SetWidgetSpace(EWidgetSpace::Screen);
	HealthBarComponent->SetWidgetClass(UKodoHealthBarWidget::StaticClass());
	HealthBarComponent->SetDrawSize(FVector2D(110.f, 14.f));
	HealthBarComponent->SetRelativeLocation(FVector(0.f, 0.f, 150.f)); // above the body
	HealthBarComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HealthBarComponent->SetGenerateOverlapEvents(false);
	HealthBarComponent->SetCastShadow(false);
	HealthBarComponent->SetTickWhenOffscreen(false);

	// Front indicator ("snout/head"). Child of the per-type BodyMesh (created in the base
	// constructor, valid here) so it inherits the body's rotation and always marks the front.
	// Mesh/tint/offset are finalised per-type in InitKodo; here we just wire it up cheaply:
	// no collision, no overlap events, no shadow (top-down blockout style).
	SnoutMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SnoutMesh"));
	SnoutMesh->SetupAttachment(BodyMesh);
	SnoutMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SnoutMesh->SetGenerateOverlapEvents(false);
	SnoutMesh->SetCastShadow(false);
}

void AKodoCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HealthBarComponent)
	{
		// 2x2 Kodos render offset to the footprint center; match that for the bar.
		if (FootprintSize == 2)
		{
			HealthBarComponent->SetRelativeLocation(
				FVector(KodoUnits::CellSizeUU * 0.5f, KodoUnits::CellSizeUU * 0.5f, 150.f));
		}
		HealthBarWidget = Cast<UKodoHealthBarWidget>(HealthBarComponent->GetUserWidgetObject());
		UpdateHealthBar();
	}
}

void AKodoCharacter::UpdateHealthBar()
{
	if (!HealthBarComponent)
	{
		return;
	}

	// Always visible while alive and on-screen (offscreen bars cost nothing and can't be
	// seen anyway). Full-HP Kodos show a full green bar.
	const bool bShow = !bDying && Hp > 0.f && WasRecentlyRendered(0.5f);
	HealthBarComponent->SetVisibility(bShow);
	if (!bShow)
	{
		LastHealthFraction = -1.f; // force a refresh when it next becomes visible
		return;
	}

	const float Fraction = MaxHp > 0.f ? Hp / MaxHp : 0.f;
	if (FMath::IsNearlyEqual(Fraction, LastHealthFraction, 0.005f))
	{
		return; // change-only: skip the widget touch when HP hasn't moved
	}
	LastHealthFraction = Fraction;

	if (!HealthBarWidget)
	{
		HealthBarWidget = Cast<UKodoHealthBarWidget>(HealthBarComponent->GetUserWidgetObject());
	}
	if (HealthBarWidget)
	{
		HealthBarWidget->SetHealth(Fraction);
	}
}

void AKodoCharacter::InitKodo(const EKodoType InType, const int32 SpawnTier,
                              AKodoWaveController* InWaveController)
{
	KodoType = InType;
	WaveController = InWaveController;

	const FKodoTypeStats Stats = StatsFor(InType);
	FootprintSize = Stats.Footprint;
	SizePx = Stats.SizePx;

	// WC3 has NO per-wave HP/speed multiplier (kodo_balance_data.md §1). Escalation is
	// driven by the spawner picking tougher TYPES over time; we add only a mild HP
	// growth with the spawn tier as a stand-in to keep long games tense.
	const float TierHpMult = 1.f + 0.15f * static_cast<float>(FMath::Max(0, SpawnTier));
	MaxHp = Stats.Hp * TierHpMult;
	Hp = MaxHp;

	// WC3 speed (umvs, units/sec) -> our UU/s via the tile-scale factor (~×1.172).
	BaseKodoSpeedUU = Stats.SpeedWc3 * KodoUnits::Wc3ToUU;
	MoveSpeedUU = BaseKodoSpeedUU;
	BaseDamage = Stats.Damage;
	CurrentDamage = BaseDamage;
	Armor = Stats.Armor;                       // WC3 udef (chaos attack, but kodos still carry armor)
	GoldReward = Stats.GoldReward;
	WoodReward = Stats.WoodReward;

	// Blink cadence: every 5 s, first one staggered 2-5 s (entities.js:784-786).
	if (InType == EKodoType::Blink)
	{
		BlinkCooldownSeconds = 5.f;
		BlinkTimer = 2.f + FMath::FRand() * 3.f;
	}

	// Size the body to its grid footprint so the size relationship reads at a glance:
	// a 2x2 Kodo is twice the 1-cell player, while a 1x1 (Speed) Kodo matches the
	// player. This is the heart of the maze mechanic — a 1-cell gap lets player-sized
	// Kodos slip through but forces the big 2x2 Kodos to turn back and detour past the
	// towers. A 10% border keeps the body off the walls of its own lane. Big Kodos are
	// also made taller so they read as bulkier, not just wider.
	const float FootprintWidthUU = FootprintSize * KodoUnits::CellSizeUU;
	// Per-type silhouette: distinct mesh SHAPE + proportions (plus the color below) so the four
	// types read apart at a glance. Footprint (1 Speed / 2 others) is unchanged for the maze.
	const TCHAR* TypeMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	// Big (2-cell) Kodos fill their FULL 2x2 footprint so they read the SAME SIZE as a 2x2
	// wall/tower (creator request). Speed (1-cell) stays small/player-sized. Types still read
	// apart by SHAPE (cylinder/cube/cone), color, and height — not width.
	float DiamFactor = 1.0f, BodyHeightScale = 1.9f;
	switch (InType)
	{
	case EKodoType::Speed: TypeMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder"); DiamFactor = 0.70f; BodyHeightScale = 1.5f; break; // small slim (1-cell)
	case EKodoType::Tank:  TypeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");          DiamFactor = 1.0f;  BodyHeightScale = 1.5f; break; // full 2x2 blocky
	case EKodoType::Blink: TypeMeshPath = TEXT("/Engine/BasicShapes/Cone.Cone");          DiamFactor = 1.0f;  BodyHeightScale = 2.8f; break; // full 2x2 tall spike
	default: break; // Standard: full 2x2 cylinder
	}
	const float BodyDiameterUU = FootprintWidthUU * DiamFactor;
	BaseBodyScale = FVector(BodyDiameterUU / 100.f, BodyDiameterUU / 100.f, BodyHeightScale);
	if (BodyMesh)
	{
		if (UStaticMesh* TypeMesh = LoadObject<UStaticMesh>(nullptr, TypeMeshPath)) { BodyMesh->SetStaticMesh(TypeMesh); }
		BodyMesh->SetRelativeScale3D(BaseBodyScale);
		// 2x2 Kodos: position point is the TOP-LEFT cell center (JS waypoint math);
		// shift the visual half a cell so the body sits centered on the footprint.
		if (FootprintSize == 2)
		{
			BaseBodyOffset = FVector(KodoUnits::CellSizeUU * 0.5f, KodoUnits::CellSizeUU * 0.5f, 0.f);
			BodyMesh->SetRelativeLocation(BaseBodyOffset);
		}

		// Prototype type colors (entities.js:754-783): standard #ff3b30, speed
		// #b026ff, tank #ff9f0a, blink #00f0ff. Each type keeps a DISTINCT tint so the four
		// types read apart at a glance (alongside their distinct footprints/heights below):
		// Speed = small purple 1x1, Standard = red 2x2, Tank = orange 2x2, Blink = cyan 2x2.
		// The editor Kodo color (editor color config, defaulted to the standard red) themes
		// the STANDARD kodo only, so theming the base color doesn't erase type readability.
		FLinearColor TypeColor(1.f, 0.23f, 0.19f);
		switch (InType)
		{
		case EKodoType::Speed: TypeColor = FLinearColor(0.69f, 0.15f, 1.f); break;
		case EKodoType::Tank:  TypeColor = FLinearColor(1.f, 0.62f, 0.04f); break;
		case EKodoType::Blink: TypeColor = FLinearColor(0.f, 0.94f, 1.f); break;
		default:               if (Grid) { TypeColor = Grid->GetMapColor(EKodoMapColor::Kodo); } break;
		}
		KodoTint::Apply(BodyMesh, TypeColor);

		// Front "snout" indicator: a small cone poking out the +X (forward) face of the body
		// so the top-down camera can read which way each kodo is heading. It is a child of
		// BodyMesh, so its transform is expressed in the BODY's local space — which is scaled
		// by BaseBodyScale. We divide the world-space sizes/offsets by that scale to keep the
		// snout a consistent real size regardless of the body's per-type stretch. Tank gets a
		// chunkier snout, Speed a longer pointier one; others a neutral nub.
		if (SnoutMesh)
		{
			if (UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone")))
			{
				SnoutMesh->SetStaticMesh(Cone);
			}

			// Desired world-space snout footprint (UU): diameter ~35% of the body, length tuned per type.
			float SnoutDiamUU = BodyDiameterUU * 0.35f;
			float SnoutLenUU  = BodyDiameterUU * 0.40f;
			switch (InType)
			{
			case EKodoType::Speed: SnoutDiamUU = BodyDiameterUU * 0.30f; SnoutLenUU = BodyDiameterUU * 0.60f; break; // pointier
			case EKodoType::Tank:  SnoutDiamUU = BodyDiameterUU * 0.45f; SnoutLenUU = BodyDiameterUU * 0.40f; break; // chunkier
			default: break;
			}

			// The engine cone is ~100 UU tall along +Z and ~100 UU wide; we want it pointing along
			// +X. Pitch +90° rotates its +Z tip toward +X. Sizes/offset are pre-divided by the body
			// scale so the snout reads the same physical size under any per-type body stretch.
			const float SX = FMath::Max(KINDA_SMALL_NUMBER, BaseBodyScale.X);
			const float SZ = FMath::Max(KINDA_SMALL_NUMBER, BaseBodyScale.Z);
			SnoutMesh->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
			SnoutMesh->SetRelativeScale3D(FVector(
				(SnoutDiamUU / 100.f) / SX,
				(SnoutDiamUU / 100.f) / SX,
				(SnoutLenUU  / 100.f) / SX));
			// Sit on the front rim of the body: half the body diameter out along +X, at mid-height.
			// BodyMesh already carries BaseBodyOffset (footprint centring), so the snout's local
			// offset is relative to the body centre — no need to re-add it here.
			SnoutMesh->SetRelativeLocation(FVector(
				(BodyDiameterUU * 0.5f) / SX,
				0.f,
				(35.f) / SZ));

			// Contrasting tint: a darkened version of the body color so the front reads as a
			// distinct "head" from the top-down camera without clashing with the type color.
			const FLinearColor SnoutColor = TypeColor * 0.35f;
			KodoTint::Apply(SnoutMesh, SnoutColor);
		}
	}
	GetCapsuleComponent()->SetCapsuleSize(BodyDiameterUU * 0.5f, 88.f);
}

void AKodoCharacter::ApplySlow(const float InSlowFactor, const float DurationSeconds)
{
	SlowTimer = FMath::Max(SlowTimer, DurationSeconds);   // entities.js:848
	SlowFactor = FMath::Min(SlowFactor, InSlowFactor);    // keep strongest slow
}

void AKodoCharacter::ApplyStun(const float DurationSeconds)
{
	StunTimer = FMath::Max(StunTimer, DurationSeconds);   // entities.js:853
}

void AKodoCharacter::ApplyDamageAmount(const float Amount)
{
	// WC3 armor reduction (kodo_balance_data.md §1): each point of armor cuts incoming
	// damage by 6% with diminishing returns. mult = 1 - (0.06*A)/(1 + 0.06*A).
	const float A = FMath::Max(0.f, Armor);
	const float Mitigation = (0.06f * A) / (1.f + 0.06f * A);
	const float Reduced = Amount * (1.f - Mitigation);
	Super::ApplyDamageAmount(Reduced);
}

void AKodoCharacter::OnZeroHp()
{
	if (bDying)
	{
		return;
	}
	// Bounty (entities.js:826-828): gold + wood + score = goldReward * 10.
	if (AKodoTagGameState* GS = GetWorld()->GetGameState<AKodoTagGameState>())
	{
		GS->Gold += GoldReward;
		GS->Wood += WoodReward;
		GS->Score += GoldReward * 10;
	}

	// Hero XP reward, scaled by kodo type (tougher kodos grant more). Awarded once, guarded
	// by the same bDying check above so a kodo can't grant XP twice.
	if (ARunnerCharacter* Runner = ResolveRunner())
	{
		int32 XpReward = 12; // Standard
		switch (KodoType)
		{
		case EKodoType::Speed: XpReward = 8;  break;
		case EKodoType::Tank:  XpReward = 25; break;
		case EKodoType::Blink: XpReward = 20; break;
		default:               XpReward = 12; break;
		}
		Runner->GainXp(XpReward);
	}

	// Death animation: keel over, then despawn (montage hook in AnimBP later).
	bDying = true;
	bAnimDead = true;
	DeathTimer = 0.6f;
	SetActorEnableCollision(false);
}

ARunnerCharacter* AKodoCharacter::ResolveRunner()
{
	if (!CachedRunner.IsValid())
	{
		CachedRunner = Cast<ARunnerCharacter>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ARunnerCharacter::StaticClass()));
	}
	return CachedRunner.Get();
}

void AKodoCharacter::UpdateEnrageVisual(const bool bEnraged)
{
	if (bEnraged == bEnragedVisualActive || !BodyMesh)
	{
		return;
	}
	bEnragedVisualActive = bEnraged;
	BodyMesh->SetRelativeScale3D(bEnraged ? BaseBodyScale * 1.15f : BaseBodyScale);
}

void AKodoCharacter::SmoothFacingToActorYaw(const float DeltaSeconds)
{
	// The base movement/chew code already SNAPPED the actor yaw to the desired facing
	// (and leaves it untouched when not moving). Read that as the target and ease the real
	// yaw toward it so the body swings into its heading instead of popping. Rotation only —
	// the path and speed are untouched.
	const float TargetYaw = GetActorRotation().Yaw;

	if (!bFacingInit)
	{
		// First frame: adopt the target immediately so a freshly spawned kodo doesn't spin
		// up from yaw 0.
		CurrentYaw = TargetYaw;
		bFacingInit = true;
	}
	else
	{
		// RInterpTo on a yaw-only rotator takes the shortest angular route and never snaps;
		// ~10/s gives a brisk-but-readable turn at our movement speeds.
		const FRotator Current(0.f, CurrentYaw, 0.f);
		const FRotator Target(0.f, TargetYaw, 0.f);
		CurrentYaw = FMath::RInterpTo(Current, Target, DeltaSeconds, 10.f).Yaw;
	}

	SetActorRotation(FRotator(0.f, CurrentYaw, 0.f));
}

void AKodoCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds); // base auto-follow disabled; just engine plumbing

	UpdateHealthBar(); // change-only; hides itself while dying / at full HP

	// Dying: keel over sideways and sink, then despawn.
	if (bDying)
	{
		DeathTimer -= DeltaSeconds;
		const float Alpha = 1.f - FMath::Clamp(DeathTimer / 0.6f, 0.f, 1.f);
		if (BodyMesh)
		{
			BodyMesh->SetRelativeRotation(FRotator(0.f, 0.f, 90.f * Alpha));
			BodyMesh->SetRelativeLocation(BaseBodyOffset - FVector(0.f, 0.f, 60.f * Alpha));
		}
		if (DeathTimer <= 0.f)
		{
			Destroy();
		}
		return;
	}

	// Chomp lunge animation (set when a chomp lands).
	if (ChompAnimTimer > 0.f && BodyMesh)
	{
		ChompAnimTimer = FMath::Max(0.f, ChompAnimTimer - DeltaSeconds);
		const float Alpha = 1.f - ChompAnimTimer / 0.25f;
		const float Lunge = FMath::Sin(Alpha * PI) * 45.f;
		BodyMesh->SetRelativeLocation(BaseBodyOffset + GetActorForwardVector() * Lunge);
		if (ChompAnimTimer <= 0.f)
		{
			BodyMesh->SetRelativeLocation(BaseBodyOffset);
		}
	}

	// Periodic runner-bite cooldown ticks regardless of stun/slow so contact stays survivable.
	BiteCooldown = FMath::Max(0.f, BiteCooldown - DeltaSeconds);

	if (Grid)
	{
		RunChaseAndChewLogic(DeltaSeconds);
	}
}

void AKodoCharacter::RunChaseAndChewLogic(const float DeltaSeconds)
{
	// 1. Stun: completely frozen (entities.js:858-861).
	if (StunTimer > 0.f)
	{
		StunTimer = FMath::Max(0.f, StunTimer - DeltaSeconds);
		return;
	}

	// 2. GLOBAL enrage — driven by the wave controller's blocked timer
	// (game.js:1158-1170, entities.js:863-875): x1.5 speed, x2 damage.
	const bool bEnraged = WaveController.IsValid() && WaveController->IsEnrageActive();
	const float SpeedMult = bEnraged ? 1.5f : 1.f;
	const float DamageMult = bEnraged ? 2.f : 1.f;
	UpdateEnrageVisual(bEnraged);

	// Slow stacking (entities.js:877-885).
	if (SlowTimer > 0.f)
	{
		SlowTimer = FMath::Max(0.f, SlowTimer - DeltaSeconds);
		MoveSpeedUU = BaseKodoSpeedUU * SlowFactor * SpeedMult;
		if (SlowTimer == 0.f)
		{
			SlowFactor = 1.f;
		}
	}
	else
	{
		MoveSpeedUU = BaseKodoSpeedUU * SpeedMult;
	}
	CurrentDamage = BaseDamage * DamageMult;

	// 3. Path recalc loop (entities.js:896-958).
	ARunnerCharacter* Runner = ResolveRunner();
	if (!Runner)
	{
		return;
	}

	if (PathRecalcCooldown > 0.f)
	{
		PathRecalcCooldown -= DeltaSeconds;
	}

	const FIntPoint MyCell = GetGridCell();
	// Wind-walking/invulnerable Runner is ignored: Kodos retreat to the central
	// portal cell (80, 82) instead (entities.js:907-910).
	const bool bRunnerIgnored = Runner->IsIgnoredByKodos();
	FIntPoint TargetCell = bRunnerIgnored ? KodoUnits::KodoRetreatCell : Runner->GetGridCell();

	// BUNKER MODE: kodos siege BUILDINGS, not the runner. Retarget to a walkable cell
	// adjacent to the nearest player structure; the existing flow-field path + footprint
	// chew loop below then eats it. If no structures remain, fall through to chasing the
	// runner (TargetCell stays as resolved above). Maze/God are untouched.
	const AKodoTagGameState* ModeGS = GetWorld()->GetGameState<AKodoTagGameState>();
	bBunkerSiege = false;
	if (ModeGS && ModeGS->GameMode == EKodoGameMode::Bunker && !bRunnerIgnored)
	{
		FIntPoint StructCell;
		if (Grid->FindNearestStructureCell(MyCell, StructCell))
		{
			// Pick the walkable neighbour of the structure closest to me as the path goal.
			// The 8 neighbours cover walls/towers of any footprint; the chew loop handles the eat.
			const int32 NDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
			const int32 NDy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
			FIntPoint BestAdj = StructCell;
			int32 BestAdjDist = TNumericLimits<int32>::Max();
			for (int32 d = 0; d < 8; ++d)
			{
				const FIntPoint Adj(StructCell.X + NDx[d], StructCell.Y + NDy[d]);
				if (!Grid->IsInBounds(Adj) || Grid->IsCellBlockedForSize(Adj, 1))
				{
					continue;
				}
				const int32 Dist = FMath::Max(FMath::Abs(Adj.X - MyCell.X), FMath::Abs(Adj.Y - MyCell.Y));
				if (Dist < BestAdjDist)
				{
					BestAdjDist = Dist;
					BestAdj = Adj;
				}
			}
			TargetCell = BestAdj;
			bBunkerSiege = true;
		}
	}

	// Target moved -> rebuild this frame so pursuit stays responsive.
	if (TargetCell != LastTargetCell)
	{
		PathRecalcCooldown = 0.f;
		LastTargetCell = TargetCell;
	}

	// Shared flow fields: ONE Dijkstra from the Runner, reused by every Kodo, so all of
	// them get the shortest viable path the instant the Runner moves — no per-unit A*.
	// (Recomputed at most once per frame, and again whenever the grid changes.)
	Grid->EnsureFlowField(TargetCell);

	const bool bWantsRecalc = (Path.Num() == 0 || PathRecalcCooldown <= 0.f);
	if (bWantsRecalc)
	{
		const bool bSize1Reachable = Grid->IsFieldReachable(1, MyCell);
		bool bGotPath = false;

		if (FootprintSize == 2)
		{
			if (Grid->IsFieldReachable(2, MyCell))
			{
				// Reachable with the full 2x2 body: follow the shared field (instant, no A*).
				TArray<FKodoPathStep> NewPath;
				if (Grid->BuildFieldPath(2, MyCell, NewPath)) { Path = NewPath; bGotPath = true; }
				bBaseIsBlocked = false;
			}
			else if (bSize1Reachable)
			{
				// A route exists but only through 1-cell gaps a 2x2 body can't fit: head to
				// the closest reachable 2x2 cell and chew from there. Rare → budgeted A*.
				bBaseIsBlocked = false;
				static constexpr int32 MaxPathfindsPerFrame = 6;
				if (Grid->TryConsumePathBudget(MaxPathfindsPerFrame))
				{
					const FIntPoint PathTarget = Grid->FindClosest2x2ReachableCell(MyCell, TargetCell);
					TArray<FKodoPathStep> NewPath;
					if (Grid->FindPath(MyCell, PathTarget, 2, NewPath) && NewPath.Num() > 0) { Path = NewPath; bGotPath = true; }
				}
			}
			else
			{
				bBaseIsBlocked = true; // no route at all → feeds the enrage timer
			}
		}
		else // Speed Kodo, 1x1
		{
			if (bSize1Reachable)
			{
				TArray<FKodoPathStep> NewPath;
				if (Grid->BuildFieldPath(1, MyCell, NewPath)) { Path = NewPath; bGotPath = true; }
				bBaseIsBlocked = false;
			}
			else
			{
				bBaseIsBlocked = true;
			}
		}

		// Field paths are cheap, so refresh often; combined with the target-moved trigger
		// above this keeps every Kodo locked onto your latest position with no lag.
		PathRecalcCooldown = bGotPath ? (0.35f + FMath::FRand() * 0.25f) : (0.1f + FMath::FRand() * 0.1f);
	}

	// 4. Blink teleport: skip up to 3 waypoints ahead along the path (entities.js:960-983).
	if (KodoType == EKodoType::Blink && Path.Num() > 3)
	{
		BlinkTimer -= DeltaSeconds;
		if (BlinkTimer <= 0.f)
		{
			const int32 BlinkDepth = FMath::Min(Path.Num() - 1, 3);
			FVector TeleportTo = Grid->CellToWorldCenter(Path[BlinkDepth].Cell);
			TeleportTo.Z = GetActorLocation().Z;
			SetActorLocation(TeleportTo);
			Path.RemoveAt(0, BlinkDepth + 1); // drop bypassed waypoints
			BlinkTimer = BlinkCooldownSeconds;
		}
	}

	// 5. Move toward next waypoint — or chew whatever wall/tower stands in the
	// footprint (entities.js:985-1078). Walls appear in the path's footprint only
	// when built AFTER the path was computed; that is exactly the wall-eat trigger.
	if (Path.Num() > 0)
	{
		const FIntPoint NextCell = Path[0].Cell;

		FIntPoint BlockCell = NextCell;
		bool bFoundBlock = false;
		for (int32 Dx = 0; Dx < FootprintSize && !bFoundBlock; ++Dx)
		{
			for (int32 Dy = 0; Dy < FootprintSize && !bFoundBlock; ++Dy)
			{
				const FIntPoint TestCell(NextCell.X + Dx, NextCell.Y + Dy);
				const ECellType Type = Grid->GetCell(TestCell).Type;
				if (Type == ECellType::Wall || Type == ECellType::Tower) // only player structures are chewable
				{
					BlockCell = TestCell;
					bFoundBlock = true;
				}
			}
		}

		if (bFoundBlock)
		{
			bIsAttackingStructure = true;
			bAnimAttacking = true;

			// Face the wall (snapped target), then smooth the actual yaw toward it.
			const FVector BlockCenter = Grid->CellToWorldCenter(BlockCell);
			const FVector Delta = BlockCenter - GetActorLocation();
			SetActorRotation(FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)), 0.f));
			SmoothFacingToActorYaw(DeltaSeconds);

			// Stand and chew: one chomp every 1.2 s (entities.js:1027-1039).
			if (AttackCooldown > 0.f)
			{
				AttackCooldown -= DeltaSeconds;
			}
			else
			{
				const FGridCell& BlockState = Grid->GetCell(BlockCell);
				const float DamageDealt = BlockState.bUnderConstruction ? CurrentDamage * 2.f : CurrentDamage;

				const bool bDestroyed = Grid->DamageCell(BlockCell, DamageDealt);
				OnChompStructure(BlockCell);
				ChompAnimTimer = 0.25f; // lunge punch
				AttackCooldown = 1.2f;

				if (bDestroyed)
				{
					// Structure down: clear path to force an instant recalc (entities.js:1041-1049).
					bIsAttackingStructure = false;
					Path.Reset();
				}
			}
			return; // don't move (and skip runner-eat) while attacking — entities.js:1051
		}

		bIsAttackingStructure = false;
		bAnimAttacking = false;
		StepAlongPath(DeltaSeconds);
		// StepAlongPath snapped the actor yaw to the movement direction (only when actually
		// moving); smooth the real yaw toward that so the kodo turns into its heading.
		SmoothFacingToActorYaw(DeltaSeconds);
	}

	// 6. Eat-runner contact check (entities.js:1080-1090): radius sum in prototype px.
	// Skipped entirely while the Runner is wind-walking/invulnerable (runnerIgnored),
	// or in Bunker mode where kodos siege buildings instead of eating the runner.
	if (!bRunnerIgnored && !bBunkerSiege)
	{
		const float EatRangeUU = (SizePx + 12.f) * KodoUnits::PxToUU;
		if (FVector::Dist2D(Runner->GetActorLocation(), GetActorLocation()) <= EatRangeUU)
		{
			// Periodic, non-lethal BITE (combat viability). The old per-frame lethal 100 killed the
			// hero the instant it approached. Now contact deals a moderate bite on a 1.2 s cadence so
			// melee is dangerous but survivable. BiteDamage scales mildly with this kodo's CurrentDamage
			// (tougher tiers bite a bit harder) but stays clamped to a non-lethal 15-25 band.
			if (BiteCooldown <= 0.f)
			{
				const float BiteDamage = FMath::Clamp(15.f + CurrentDamage * 0.25f, 15.f, 25.f);
				Runner->ApplyDamageAmount(BiteDamage); // entities.js:1085 (was lethal 100/frame)
				BiteCooldown = 1.2f;
			}
		}
	}
}
