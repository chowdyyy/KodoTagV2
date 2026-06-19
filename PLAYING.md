# How to Play Kodo Tag V2

There are two ways to play. Pick the one that matches you:

- **A. Just want to play (no coding):** download a ready-made build and double-click it. *(Available only if the author has published a Release — see the repo's "Releases" section.)*
- **B. Run from the source code:** install the Unreal Engine and let a script build + launch it. Works for everyone who has the source, but needs a one-time setup and a first-build wait.

---

## A. Play the ready-made build (easiest, no installs)

> This works only if there's a build under the repo's **Releases** (right-hand side of the GitHub page). If there isn't one yet, use option B, or ask the author to publish one (instructions for them are at the bottom).

1. On the GitHub page, click **Releases**.
2. Download the file named something like **`KodoTagV2-Windows.zip`**.
3. **Right-click the zip → Extract All…** to a folder (don't run it from inside the zip).
4. Open the extracted folder and double-click **`KodoTagV2.exe`**.
5. If Windows SmartScreen shows a blue "Windows protected your PC" box, click **More info → Run anyway** (this is normal for unsigned hobby games).

That's it — no Unreal Engine needed. Skip to **How to play the game** below.

---

## B. Run from source (needs the Unreal Editor)

You'll install two free things once, then a single script builds and launches the game for you.

### Step 1 — Install Visual Studio 2022 (the C++ compiler)
1. Download **Visual Studio 2022 Community** (free): https://visualstudio.microsoft.com/downloads/
2. In the installer, on the **Workloads** screen, check **"Game development with C++"**.
3. Install. (This is what compiles the game's code.)

### Step 2 — Install Unreal Engine 5.7
1. Download the **Epic Games Launcher**: https://www.unrealengine.com/en-US/download
2. Open it, go to the **Unreal Engine ▸ Library** tab, click the **+**, and install version **5.7**.
3. Let it finish (it's a large download).

### Step 3 — Get the game files
- On the GitHub page, click the green **Code ▸ Download ZIP**, then extract it somewhere simple like `C:\Games\KodoTagV2`.
- (Or, if you use Git: `git clone <repo-url>`.)

### Step 4 — Play
1. Open the extracted folder, go into the **`Tools`** folder.
2. Double-click **`Play.bat`**.
3. A black window opens and says it's building. **The first build can take 5–20 minutes** — this is normal; later launches are fast. Leave it running.
4. The game window opens automatically when the build finishes.

If it can't find the engine, it'll tell you to install UE 5.7 (Step 2). If the build fails, it points you to a log file — the usual cause is the Visual Studio C++ game workload from Step 1 being missing.

---

## How to play the game

You're the lone **Runner**. Waves of **Kodos** charge at you from a portal — if one reaches you, you lose. Survive the match timer to win.

- At the start screen, pick a **Mode** (Maze / God / Bunker) and **Difficulty**, then begin.
- **Build a maze** of walls and towers to force the big Kodos onto long detours past your towers. Small (fast) Kodos can slip through 1-cell gaps.
- **Get resources:** stand by a gold mine to mine gold (auto-banked) and chop trees for lumber (carry it back to your Command Center).
- **Research upgrades** at the Command Center (economy) and Upgrade Center (combat + hero skills). Research takes time and can be queued.
- **Use your hero's abilities** (they cost mana) to survive when cornered.

### Controls

| Input | Action |
|---|---|
| Left-click | Select a unit/building, or place the active blueprint |
| Right-click | Move / harvest / cancel |
| Spacebar | Lock/unlock the camera to your hero |
| Mouse wheel | Zoom · move mouse to screen edge to pan |
| `W` / `C` / `T` | Wall / Command Center / Basic Tower blueprint |
| `Q` | Hero spell · `R` hero skill 3 (once researched) |
| `7` `8` `9` `0` | Pick hero class |
| `F1`–`F7` | Research shortcuts (also on the building's command card) |

To quit a from-source session, just close the game window.

---

## Make your own map (works in the downloaded build too)

The build ships with the same map editor the game uses, so anyone can design their own layout — no Unreal needed.

1. In your game folder, go to **`KodoTagV2\Content\Config\`**. You'll find **`MapEditor.html`** and **`KodoMapLayout.txt`** side by side.
2. Open **`MapEditor.html`** in **Chrome or Edge** (those allow saving straight back to the file).
3. It opens showing the current map. Edit away — paint ridges/trees/gold mines, set spawn points, raise/lower elevation and place ramps (with a direction), tweak colors and map size. (Tip: use **📂 Open a .txt…** and pick the `KodoMapLayout.txt` in that folder if you want to be sure you're editing the build's map.)
4. Click **💾 Save to map file** and save over **`KodoMapLayout.txt`** in that same `Content\Config\` folder.
   - In Chrome/Edge it writes directly. In other browsers it **downloads** `KodoMapLayout.txt` — move that download into `Content\Config\`, replacing the old one.
5. **Relaunch the game** — it loads your edited map.

To go back to the original map, re-download the release and copy its `KodoMapLayout.txt` back.

---

## For the author — publishing a one-click build for players

To give non-technical players the zero-install option (A above), package a Windows build once and attach it to a GitHub Release:

1. In the Unreal Editor: **Platforms ▸ Windows ▸ Package Project** (choose **Shipping** or **Development**). Pick an output folder.
   - Or command line:
     ```
     "<UE>\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="<path>\KodoTagV2.uproject" -noP4 -platform=Win64 -clientconfig=Shipping -cook -build -stage -pak -archive -archivedirectory="<output>"
     ```
2. Zip the packaged `Windows` folder (the one containing `KodoTagV2.exe`) as `KodoTagV2-Windows.zip`.
3. On GitHub: **Releases ▸ Draft a new release ▸** add a tag (e.g. `v0.1`), attach the zip, publish.

Players then just download, unzip, and run — no Unreal Engine required.
