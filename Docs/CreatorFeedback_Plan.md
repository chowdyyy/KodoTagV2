# Kodo Tag V2 — Creator Feedback Implementation Plan

Feedback source: the original Warcraft III Kodo Tag map author, after reviewing the UE demo.
Engine: Unreal Engine 5.7. Date: 2026-06-16.

The creator gave five points. Two are already implemented in the current codebase (the
demo reviewed was likely an older build); three are real work. Each item below lists the
creator's ask, the current state in code, the proposed change, and effort/risk.

---

## Summary table

| # | Feedback | Current state | Work needed | Effort | Risk |
|---|----------|---------------|-------------|--------|------|
| 1 | Buildings & walls 2x2 path | **Already done** | Verify only | ~0 | Low |
| 2 | Kodos get a 2x2 border vs. walls | **Mostly done** (path); visual clamp missing | Clamp body radius to footprint | S | Low |
| 3 | Health bar above Kodos | Not implemented | Add overhead health widget | M | Low |
| 4 | Lower shadows / raise light | Partially done | Raise sun pitch, soften shadows | S | Low |
| 5 | Diagonal movement | Not implemented (4-dir only) | 8-direction A* + corner rules | L | Med |

Effort key: S = under an hour, M = half a day, L = a day+ including tuning.

---

## 1. Give buildings and walls a 2x2 path  — ALREADY DONE

**Creator's intent:** simpler for the algorithm and clearer for players if every structure
occupies a clean 2x2 tile.

**Current state:** all wall and tower presets already set `bIs2x2 = true`
(`Source/KodoTagV2/Data/KodoStructureData.cpp`, lines 28, 38, 46, 57, 69, 84, 96, 108,
118, 128, 138). Placement snaps to the coarse 2x2 "build tile" lattice via
`KodoGrid::SnapToTile` (`Source/KodoTagV2/Grid/KodoGridTypes.h:114`), and
`PlaceStructure` validates/writes the full 2x2 footprint
(`Source/KodoTagV2/Grid/KodoStructureManager.cpp:162-210`). The code comments already
cite this exact feedback.

**Action:** none beyond a sanity check in a fresh build — place a wall and a tower, confirm
each occupies a 2x2 tile and snaps to the lattice. If the creator saw 1x1 structures, they
reviewed a build from before this change landed.

---

## 2. Add a 2x2 border to Kodos so they can't walk into walls  — MOSTLY DONE

**Creator's intent:** Kodos should keep clear of walls; they currently appear to walk into
them.

**Current state:** Kodos carry `FootprintSize = 2` (`Actors/KodoCharacter.h:80`), and the
A* search rejects any step whose 2x2 footprint overlaps a blocking cell
(`KodoGrid::IsCellBlockedForSize`, `Grid/KodoGridTypes.h:153-174`; used in
`Grid/KodoGridPathfinder.cpp:228`). So the *path* already respects a 2x2 clearance.

**What's missing:** the Kodo's *visual* body radius (`SizePx = 23` px ≈ a cell-and-a-half
in UU; `KodoCharacter.h:90`) is not clamped to the 2x2 footprint, and waypoint following
moves the actor toward raw cell centers (`Actors/KodoTagCharacterBase.cpp`, `StepAlongPath`).
A body wider than its cleared lane visibly overlaps an adjacent wall even though the path is
"legal." That overlap is almost certainly what the creator saw.

**Proposed change:**
- Confirm the path footprint origin convention (top-left of the 2x2) and offset the actor so
  the body centers on the 2x2 footprint middle rather than a single cell corner.
- Clamp the rendered body / capsule radius so it fits inside the 2x2 cleared area
  (`2 * CellSizeUU = 300 UU` wide; keep the collision capsule at roughly cell radius so it
  never pokes past the footprint).
- Optionally add a one-cell inflation when sampling blocked cells for the *body* (not the
  path) so fast Kodos cornering tightly still read as outside the wall.

**Files:** `Actors/KodoCharacter.cpp` (body scale / offset), `Actors/KodoTagCharacterBase.cpp`
(`StepAlongPath` centering), `Core/KodoTagUnits.h` (reference constants).

**Risk:** low. Pure visual/collision tuning; pathfinding logic unchanged.

---

## 3. Add a health bar above the Kodos  — NEW WORK

**Creator's intent:** overhead HP bars make it read as a real, fast-paced game.

**Current state:** no overhead health UI exists. HP is tracked on
`AKodoTagCharacterBase` (`Hp` / `MaxHp`, with `GetHp()` / `GetMaxHp()` accessors,
`Actors/KodoTagCharacterBase.h:42-46`) but never displayed in-world.

**Proposed change (pure C++, no .uasset — matches the project's code-only convention):**
- Add a `UWidgetComponent` to `AKodoTagCharacterBase` (or just the Kodo) positioned ~1 unit
  above the body, set to `Screen` space so the bar always faces the camera at a fixed size.
- Drive it with a small C++ `UUserWidget` subclass (e.g. `UKodoHealthBarWidget`) holding a
  `UProgressBar`, bound to `GetHp() / GetMaxHp()`. Color thresholds can mirror the existing
  HUD details panel (green / orange / red), keeping the look consistent with `KodoHudWidget`.
- Hide the bar at full HP (optional, WC3-style) and while `IsDying()` is true.
- Performance: with 200+ units, use `Screen` space + tick-throttled updates (only refresh on
  HP change, not every frame) and disable the widget's own tick.

**Files:** new `UI/KodoHealthBarWidget.{h,cpp}`, edits to `Actors/KodoTagCharacterBase.{h,cpp}`
or `Actors/KodoCharacter.{h,cpp}`, and the `Build.cs` already includes UMG (added in Phase 5).

**Risk:** low. The main watch-item is draw-call/perf cost at high unit counts — mitigated by
screen-space bars and change-only updates. Could later batch into a single HUD-drawn pass if
needed.

---

## 4. Lower the other shadows or raise the light source  — PARTIALLY DONE

**Creator's intent:** keep the clean top-down overview that removing the main shadow gave.

**Current state:** walls and cliffs already cast no shadow
(`Grid/KodoMapBootstrapper.cpp:113,118`; `Grid/KodoStructureManager.cpp:953`). The
directional sun is at a deliberately low twilight pitch of **−22°** with intensity 5.0
(`Grid/KodoMapBootstrapper.cpp:242-245`), which is exactly what produces the long shadows
the creator still sees on the remaining actors (Kodos, trees, towers).

**Proposed change (pick per art direction):**
- **Raise the sun:** change pitch from −22° toward roughly **−55° to −70°** so shadows
  shorten dramatically while keeping some directionality. One-line change at
  `KodoMapBootstrapper.cpp:242`.
- **And/or soften shadows:** reduce dynamic shadow strength (lower
  `DynamicShadowDistanceMovableLight` / cascade count, or set a `ShadowAmount` < 1) so
  remaining shadows are faint rather than removed entirely.
- Keep the warm amber color so the dusk mood survives; just lift the angle.

**Files:** `Grid/KodoMapBootstrapper.cpp` (sun rotation + light component settings).

**Risk:** low. Easy to A/B; revert is a single value.

---

## 5. Allow diagonal movement for Runner and Kodos  — NEW WORK (biggest item)

**Creator's intent:** units should move diagonally, not just along 4 cardinal directions.

**Current state:** the A* search uses a strict 4-neighbor set —
`Dirs[4] = {N, E, S, W}` (`Grid/KodoGridPathfinder.cpp:188`), with a matching 4-dir BFS in
`FindClosest2x2ReachableCell` (line 294). This is a faithful port of the JS prototype, which
was also 4-directional. So units stair-step instead of cutting corners. (Note: the *renderer*
already moves smoothly between waypoints, so the fix is purely in the path graph, not the
movement code.)

**Proposed change — add 8-directional A*:**
- Extend the neighbor set to 8 directions (add the four diagonals).
- Use the correct diagonal cost: a diagonal step costs `√2 ≈ 1.414×` a cardinal step, and the
  heuristic must switch from Manhattan to **octile distance**
  (`max(dx,dy) + (√2−1)·min(dx,dy)`) so A* stays admissible and fast. (`Heuristic()` at
  `KodoGridPathfinder.cpp:134` currently returns Manhattan.)
- **Corner-cutting rule:** only allow a diagonal step if *both* orthogonally-adjacent cells
  are also unblocked for the unit's footprint — otherwise units would clip diagonally through
  wall corners. For 2x2 Kodos this means checking the footprint clearance of both intermediate
  positions, reusing `IsCellBlockedForSize`.
- Apply the same diagonal expansion to `FindClosest2x2ReachableCell` BFS for consistency.
- Keep tie-break order stable so paths stay deterministic.

**Performance:** 8-dir roughly doubles neighbor expansions; with octile heuristic and the
existing binary heap it stays well within budget. The real stress test is the creator's
"200+ units" point — see the cross-cutting note below.

**Files:** `Grid/KodoGridPathfinder.cpp` (neighbor set, costs, heuristic, corner rule),
possibly `KodoGridPathfinder.h` if a config flag is wanted to toggle 4-/8-dir.

**Risk:** medium. Diagonal corner-cutting and 2x2 footprints interact subtly; needs careful
testing so Kodos never slip through wall corners. Recommend a verification pass (below).

---

## Cross-cutting: the 200+ unit scalability point

The creator explicitly hopes pathfinding "also works with 200+ units." Two current design
facts matter:
- Each Kodo recalculates its own A* on a staggered cooldown (`PathRecalcCooldown`,
  `KodoCharacter.h:97`), and `FindPath` allocates a fresh `Cols*Rows` node grid per call
  (`KodoGridPathfinder.cpp:166` — 160×160 = 25,600 nodes each time). At 200+ units this is
  the main cost center.
- Mitigations to evaluate (not all needed up front): reuse a pooled node buffer instead of
  reallocating per call; cap recalcs-per-frame with a shared budget queue; share a flow-field
  / single-target path among Kodos heading to the same base cell. Worth a dedicated profiling
  pass once items 2–5 land.

---

## Suggested execution order

1. **Item 4 (raise sun)** — one-line, instant visual win, validates the build loop.
2. **Item 3 (health bars)** — self-contained, high visible impact for the "real game" feel.
3. **Item 2 (Kodo body clamp)** — small, removes the wall-overlap artifact.
4. **Item 5 (diagonal A*)** — the substantial one; do it with a verification pass.
5. **Item 1** — verify only.
6. **Scalability profiling** — spawn 200+ Kodos and measure before/after.

Each lands as its own batch through `Tools\RunPhase1Build.bat` so build errors stay isolated.

## Verification plan

- **Item 5:** unit-test the pathfinder on a small fixture grid — assert no path ever cuts a
  blocked corner, and that diagonal paths are shorter than the old L-shaped ones on open
  terrain. A subagent code-review pass on the corner-cutting logic is recommended.
- **Item 2:** in-editor visual check — Kodo body stays inside its lane against a wall.
- **Item 3:** spawn a wave, damage Kodos, confirm bars track HP and cull correctly on death.
- **Item 4:** screenshot before/after at the same camera angle.
- **Scalability:** spawn 200+ Kodos, watch frame time and the pathfinder's per-frame cost.
