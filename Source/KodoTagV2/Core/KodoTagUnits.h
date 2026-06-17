// Kodo Tag: Survivor — UE Migration, Phase 1.
// THE ONLY place coordinate/scale conversion constants live (PHASE1_TDD.md §2.2).
// Prototype internal unit: pixels, tileSize = 28 px (game.js:23).

#pragma once

#include "CoreMinimal.h"

namespace KodoUnits
{
	/** Prototype tile size in pixels (game.js:23). */
	constexpr float ProtoTileSizePx = 28.f;

	/** Phase 1 decision (TDD §2.1): 1 grid cell = 150 cm. */
	constexpr float CellSizeUU = 150.f;

	/** Multiply any prototype px value by this to get Unreal Units. ~5.3571 */
	constexpr float PxToUU = CellSizeUU / ProtoTileSizePx;

	/** Grid dimensions = the WC3 map's PLAYABLE area (148x150 tiles), one cell per tile,
	 * squared to 150 for simplicity. This is the faithful 1:1 size that matches the
	 * original minimap exactly (bases sit at the playable edges). */
	constexpr int32 GridCols = 150;
	constexpr int32 GridRows = 150;

	/** World height per elevation level: 24 px per level (game.js:497). ~128.57 UU */
	constexpr float ElevationStepUU = 24.f * PxToUU;

	/** World Z per elevation LEVEL for the raised-base terrain layer (E/RAMP map data).
	 *  ~0.85 of a cell so the step reads clearly as real height even when zoomed out. */
	constexpr float ElevationLevelStepUU = 130.f;  // world Z per elevation level (raised bases)

	/** Total map extent per axis: 24,000 UU = 240 m. */
	constexpr float MapExtentUU = GridCols * CellSizeUU;

	// --- Named cells (prototype ground truth) ---

	/** Runner spawn — central base, near the original merchant. */
	inline const FIntPoint RunnerSpawnCell(75, 76);

	/** Kodo spawn portal — north area of the playable map. */
	inline const FIntPoint KodoSpawnCell(75, 18);

	/** Kodo retreat target when Runner is wind-walking/invulnerable. */
	inline const FIntPoint KodoRetreatCell(75, 80);

	/** Merchant tent 2x2 master cell — the original WC3 merchant location (playable cell). */
	inline const FIntPoint MerchantShopCell(79, 82);

	/** Merchant shop active radius in tiles (game.js:81). */
	constexpr float MerchantRadiusTiles = 5.5f;

	// --- Reference speeds (entities.js:19; px/s converted to UU/s) ---
	constexpr float RunnerSpeedUU      = 155.f * PxToUU; // ~830.4 UU/s
	constexpr float BlademasterSpeedUU = 178.f * PxToUU; // +15% passive

	// --- WC3 conversion factors (kodo_balance_data.md §1) ---
	// WC3 "tile" = 128 units; one of our cells = CellSizeUU (150 UU). So any WC3
	// distance/speed (units or units/sec) maps to our world by this scale (~1.172).
	constexpr float Wc3TileUnits   = 128.f;
	constexpr float Wc3ToUU        = CellSizeUU / Wc3TileUnits; // ~1.1719

	// --- Economy (kodo_balance_data.md §3) ---
	// WC3 CONFIRMED: each gold mine seeds 12,500 gold (SetResourceAmount $30d4).
	// We have no buildable mine-shaft / worker-harvest loop yet (deferred), so this
	// is recorded as a constant for the future mine-reserve / drain system.
	constexpr int32 GoldMineReserve = 12500;
}
