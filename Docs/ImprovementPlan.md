# Kodo Tag V2 — Improvement & Performance Plan

Synthesis from the PERF logs, the extracted `.w3x` data, the balance report
(`kodo_balance_data.md`), and the live Warcraft III Kodo Tag: Reforged feature set.
Date: 2026-06-17.

---

## 1. Performance

### The headline finding: your FPS numbers are mostly a measurement artifact
The recent PERF logs read **3.0 FPS / 333.3 ms with only ~33 kodos**, then jump to
**20+ FPS** the instant the window is focused. 333.3 ms is exactly the Unreal editor's
**"Use Less CPU in Background"** throttle — PIE is pinned to ~3 fps whenever its window
isn't the foreground. So most of the "lag" you've been seeing while alt-tabbed is not the
game. **Before optimizing anything, measure properly:**
- Editor → Preferences → General → Performance → **uncheck "Use Less CPU in Background."**
- Or test in a **Standalone** window / packaged build (true frame rate, no editor overhead).
- Watch the `[PERF]` log only while the window is focused.

Once that's done we'll have a real baseline. Everything below is the ordered list of levers
if the focused number still needs work.

### Real performance levers (highest payoff first)
1. **Per-kodo health-bar widgets.** Each kodo carries a `UWidgetComponent` (a full Slate
   widget). At 200 units that's 200 widgets ticking/drawing. Replace with a single
   HUD-drawn batch of bars (one draw call) or Niagara/quads. Biggest CPU win at high counts.
2. **Flow-field recompute.** Already a big improvement over per-unit A*. At the current
   150×150 grid each recompute is cheap; if it ever spikes, throttle it to every N frames
   or only when the Runner's cell changes (already mostly does).
3. **Minimap rebuild.** The HUD rescans the whole grid (~22k cells) at ~12 Hz and re-uploads
   a texture. Cache it and only update changed cells (the grid already fires `OnCellChanged`).
4. **Per-kodo Tick.** 200 actors each running chase/eat/anim logic. Cheap individually, but
   consider a shared manager tick or time-slicing the AI across frames if profiling flags it.
5. **Structure visual updates** rebuild a dynamic material each `OnCellChanged`. Fine now;
   cache the MID per cell if tower spam becomes a thing.
6. **GPU stack** is already trimmed (Lumen/RT/VSM off). Good for a stylized top-down game.

---

## 2. Missing gameplay (vs the real Kodo Tag: Reforged)

The live map is a **30-minute survival** game for 4–10 players with a much deeper loop than
our current build. Ranked by how core they are:

1. **Worker / mechanic economy.** In the original, **Workers harvest the 83 gold mines** and
   **Mechanics repair buildings**; players also earn gold/lumber/XP for kills. We have a
   placeholder mine-shaft/lumber-mill economy instead. This is the biggest gameplay gap.
2. **Elevation / ramps** (your current request — see §3). Bases sit a step up; entrances are ramps.
3. **Hero abilities & items.** "If you get caught you can use abilities to help your team,"
   and "the best items are in the shop in the middle." We have hero classes but the spell (Q),
   the 6 inventory slots, and the merchant shop items are stubs.
4. **Game modes.** The original ships **Maze**, **Bunker** (kodos attack buildings), **Tower
   Defense** (stronger towers / weaker kodos), and **God** (stronger heroes & towers, more
   kodos). We have a single Normal/Hard/Insane difficulty multiplier — a mode selector at
   start would match the original.
5. **Full kodo roster.** The original has 34 tiered kodo types; we map to 4 archetypes. Could
   expand the spawn pool using the real stats already in `kodo_balance_data.md`.
6. **Win/lose conditions.** Survive 30 minutes = win; Runner death = lose. Our continuous
   spawner has no victory timer yet.
7. **Frost attack-speed slow + poison DoT**, multishot bounce falloff — effect fidelity gaps
   noted in the balance report (we model move-slow only).
8. **Multiplayer** (4–10 players). Large architectural item; single-player is fine for now.
9. **Audio** — no music/SFX yet.

---

## 3. Elevation & ramps (your request) — what I found and how to ship it

**Identified, yes.** From the terrain heightmap: the playable area has **3 levels** —
ground (≈62% of cells), raised one step (the bases, ~37%, the light-green platforms in
`elevation.png`), and a few level-2 peaks. Entrances are ramps (walkable height transitions);
the rest of each base perimeter is an impassable cliff face (already our ridge walls).

**Proposed implementation (keeps the current X/Y layout untouched — elevation is a new layer):**
- **Data:** add `E x y level` lines (raised cells) + a refined ramp flag to `KodoMapLayout.txt`.
  Pull the true ramp tiles from the w3e ramp flag so it's accurate (the first pass over-marked
  ramps because it counted every walkable cliff edge).
- **Game:** `UKodoGridSubsystem::GetElevationZ(cell)` (level × step, ramps interpolate). Raise
  the ground tiles + structures/trees/mines on raised cells by that Z, and set unit Z from it
  in spawn + movement so the Runner and kodos ride the platforms. Build deprojection samples
  the cell's elevation. (Pathfinding stays 2D — unaffected.)
- **Editor:** an elevation brush (raise/lower) + a ramp tool, with raised cells shaded and ramps
  highlighted, so you can fix anything that looks wrong. Saves the `E`/ramp lines.

This is a real 3D change (unit heights, ramps, rendering), so it's a focused mini-project —
worth doing as its own pass rather than bolting on.

---

## 4. Visual / polish
- **Blockout → real art.** Everything is engine cubes/cones/cylinders with flat tints. Biggest
  visual upgrade is swapping in real meshes (kodo creature, hero, tower models) — the
  `UKodoAnimInstance` hooks and color system are already in place to receive them.
- **Materials** — flat color now; a light texture/normal pass would lift it a lot.
- **Animations** — code-driven lunge/death stubs exist; real AnimBPs once meshes land.

---

## 5. Tech debt / correctness
- **Packaging:** `Content/Config/KodoMapLayout.txt` is read fine in-editor but won't be cooked
  into a packaged build — add `Content/Config` to "Additional Non-Asset Directories to Package"
  before shipping.
- **HUD relabel:** still says "Wave 0/10 / PREP"; with the continuous model it should read
  "Tier / Build phase / Next wave."
- **Difficulty/mode selection UI** at game start.
- **Ramp classification** refinement (see §3).

---

## 6. Suggested order
1. **Measure real FPS** (disable the background throttle) — 2 minutes, unblocks everything.
2. **Health-bar batching** if the focused number needs it — the one real perf win.
3. **Elevation pass** (your request) — data + game + editor.
4. **Worker/mechanic economy** — the biggest gameplay gap.
5. **Hero spell + merchant shop items**, then **game modes** and **win timer**.
6. **Real art** when you're ready to leave blockout.

Sources: live feature set from [Kodo Tag: Reforged (wc3maps.com)](https://wc3maps.com/map/414925);
balance/economy from the extracted `kodo_balance_data.md`; performance from the project's `[PERF]` logs.
