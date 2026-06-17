# Kodo Tag V2

An **Unreal Engine 5.7** (C++) remake of the classic **Warcraft III** custom map *Kodo Tag*. You play a lone **Runner** who survives waves of charging **Kodos** by building a maze of walls and towers, harvesting resources, researching upgrades, and using hero abilities. Survive the timer and you win; get eaten and it's over.

This is a code-only project — the entire game (gameplay, HUD, units, map) is built in C++ with no `.uasset` content, using engine primitive meshes as a blockout. The map is driven by a data file extracted 1:1 from the original Warcraft III `.w3x`.

> Fan remake for learning/personal use. *Warcraft III* and *Kodo Tag* are the property of their respective owners; no original Blizzard assets are included.

---

## The game

- **You are the Runner.** Kodos spawn continuously from a portal and path toward you. If one reaches you, you die.
- **Build a maze.** Place 1×1 walls and towers to force the big (2×2) Kodos onto long detours past your towers, while small (1×1) Kodos can slip through 1-cell gaps — the core "tag" mechanic.
- **Economy.** Mine gold (credited directly while mining) and chop trees for lumber (carried back to a Command Center). Spend it on buildings and research.
- **Towers & research.** Morph basic towers into Frost, Stun, AoE, Multishot, Aura, etc. Research upgrades take time to complete and can be queued.
- **Hero.** Four hero classes (Mountain King, Death Knight, Blademaster, Tinker), each with 3 skill slots — one active spell by default, plus a researched passive and a researched active. Active skills cost **mana**.
- **Kodo types.** Distinct silhouettes: Speed (small, fast, 1×1), Standard, Tank (large, 2×2), and Blink (teleporter).
- **Game modes** (chosen on the start screen): **Maze** (standard), **Bunker** (Kodos attack your buildings; towers hit harder), **God** (buffed hero & towers, more Kodos), each with Easy/Normal/Hard/Insane difficulty.
- **Admin Tower** (top-right corner): spawn a specific Kodo type on demand, pause/resume the spawner, or bump difficulty — handy for testing.

## Controls (default)

| Input | Action |
|---|---|
| Left-click | Select unit/building, or place the active blueprint |
| Right-click | Move / harvest / cancel blueprint |
| Spacebar | Lock/unlock camera to the hero |
| Mouse wheel | Zoom |
| Mouse to screen edge | Pan camera |
| `W` / `C` / `T` | Select Wall / Command Center / Basic Tower blueprint |
| `Q` | Cast hero spell (slot 1) |
| `R` | Cast hero skill 3 (once researched) |
| `7` `8` `9` `0` | Pick hero class |
| `F1`–`F7` | Research shortcuts (also available on the building's command card) |

Building far from the hero makes it walk over and start the build, then it finishes on its own. Research happens at the **Command Center** (economy: gold/lumber/building HP) and the **Upgrade Center** (offense: tower unlocks, hero skills, mana regen) — you don't need to stand next to them.

---

## Building & running

Requirements: **Unreal Engine 5.7** installed (the project expects it at `D:\Program Files\UE_5.7`; adjust if yours differs) and Visual Studio with the C++ game toolchain.

1. **Close the Unreal editor** if it's open (it locks the module and the build becomes a no-op).
2. Run **`Tools\RunPhase1Build.bat`** to compile.
3. Check the result in **`Saved\Logs\Phase1Build.log`** (look for `SUCCEEDED`/`FAILED`).
4. Open `KodoTagV2.uproject` in the Unreal editor and press **Play**.
5. On the start screen, pick a **mode** and **difficulty**, then begin.

Alternatively, double-click `KodoTagV2.uproject`; if prompted to rebuild modules, accept.

---

## Map editor

The map (cliffs/ridges, trees, gold mines, spawn points, elevation, ramps, and in-game colors) is data-driven from **`Content/Config/KodoMapLayout.txt`**. You edit it with the standalone, no-install browser tool:

**`Tools/MapEditor.html`** — open it in a modern browser (Chrome or Edge recommended).

What you can do:

- **Paint terrain:** Ridge/cliff, Tree, Gold mine (2×2), and Erase, with an adjustable brush size.
- **Select & move:** the Select tool lets you click a feature, box-select many, drag them, and Delete to remove.
- **Spawn points:** place the Kodo spawn, Runner spawn, and Merchant.
- **Elevation:** Raise/Lower cells to build stepped plateaus (bases sit higher), and the **Ramp** tool with a direction (N/E/S/W) marks sloped walkable entrances. Elevation is shaded (lighter = higher) and ramps show a direction arrow.
- **Colors:** set the in-game tint for ridges, trees, mines, walls, command center, hero, Kodos, and ground.
- **Map size:** set the grid limit and optionally wall the map edge.

**Saving:** click **💾 Save to map file**. In Chrome/Edge it writes directly to a `.txt` via the File System Access API — point it at `Content/Config/KodoMapLayout.txt`. In other browsers it downloads `KodoMapLayout.txt`, which you then drop into `Content/Config/`. Changes apply the **next time you launch** the game.

The editor has the current map embedded, so it opens showing your live layout. Use **📂 Open a .txt…** to load a different layout and **↺ Reset to loaded** to revert.

---

## Project structure

```
KodoTagV2.uproject          Unreal project file
Source/KodoTagV2/           All C++ gameplay code
  Actors/                   Runner (hero), Kodo, wave controller, projectiles, repair bot
  Camera/                   Player controller (input, selection, build ghost), camera pawn
  Core/                     Game mode, game state, shared constants (KodoTagUnits.h)
  Grid/                     Grid subsystem, pathfinding/flow-field, map bootstrapper, structure manager
  Data/                     Structure/tower stat tables
  UI/                       C++-built HUD widgets
Content/Config/             KodoMapLayout.txt (the map data the game loads at launch)
Tools/                      RunPhase1Build.bat, MapEditor.html
Config/                     DefaultEngine.ini etc.
```

Generated folders (`Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, `.vs/`) are build artifacts and should **not** be committed — see `.gitignore`.
