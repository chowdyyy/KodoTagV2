# Releasing Kodo Tag V2 (checklist)

Do these once to get the project on GitHub and publish a playable download. For later
updates, repeat **Part 3** (package) and **Part 4** (release) with a new version number.

---

## Part 1 — Put the source on GitHub (one time, hobby account)

Using **GitHub Desktop**:

1. Make sure Desktop is signed in as your **hobby** account: *File ▸ Options ▸ Accounts* (sign out of the work account if it's the one shown).
2. *File ▸ Add local repository* and set **Local path** to **`D:\Games\KodoTagV2`** — the real project root (the folder with `KodoTagV2.uproject` in it). **Not** the empty nested `D:\Games\KodoTagV2\KodoTagV2`.
3. It will say "this directory is not a Git repository" — click the **"create a repository"** link.
4. In the Create dialog: keep the path, leave **Git ignore = None** (we already have a `.gitignore`), leave README unchecked. Click **Create repository**.
5. Set the commit identity to the hobby account so commits aren't tagged with your work email — *Repository ▸ Open in Command Prompt*, then:
   ```
   git config user.name  "your-hobby-name"
   git config user.email "your-hobby-email@example.com"
   ```
6. Back in Desktop, the **Changes** tab should now list the project files. Confirm the generated folders (`Binaries/ Intermediate/ Saved/ DerivedDataCache/ .vs/`) are **not** listed (the `.gitignore` hides them). If the Changes tab is empty, you added the wrong (empty) folder in step 2 — remove it and re-add `D:\Games\KodoTagV2`.
7. Write a message ("Initial commit"), click **Commit to main**, then **Publish repository** (choose the hobby account + visibility). Verify the files appear on github.com under the hobby account.

---

## Part 2 — Install what you need to package (one time)

Only required to make the downloadable build. (Skip if already installed.)
- **Visual Studio 2022** with the **"Game development with C++"** workload.
- **Unreal Engine 5.7** (Epic Games Launcher).

---

## Part 3 — Package the game into a downloadable build

1. Open `KodoTagV2.uproject` in the Unreal Editor (5.7).
2. **Platforms ▸ Windows ▸ Package Project**. Choose **Shipping** (smaller/faster) or **Development**. Pick an output folder, e.g. `D:\KodoTagV2-Build`.
3. Wait for "Packaging complete." In the output you'll have a **`Windows`** folder containing `KodoTagV2.exe` and its data.
4. Quick test: run that `KodoTagV2.exe` to confirm it launches.
5. Right-click the **`Windows`** folder ▸ *Send to ▸ Compressed (zipped) folder*. Rename it **`KodoTagV2-Windows.zip`**.

> Command-line alternative:
> ```
> "<UE>\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="D:\Games\KodoTagV2\KodoTagV2.uproject" -noP4 -platform=Win64 -clientconfig=Shipping -cook -build -stage -pak -archive -archivedirectory="D:\KodoTagV2-Build"
> ```

---

## Part 4 — Publish the GitHub Release (browser, hobby account)

1. On the repo page (logged in as the hobby account), click **Releases ▸ Draft a new release**.
2. **Choose a tag** → type `v0.1` → "Create new tag: v0.1 on publish".
3. **Title:** `Kodo Tag V2 — v0.1 (first playable)`.
4. **Description:** paste the notes (see `release-notes` below).
5. **Attach** `KodoTagV2-Windows.zip` by dragging it into the binaries box; wait for the upload to finish.
6. Leave **Set as the latest release** checked → **Publish release**.
7. Final test: from the published release, download the zip into a *different* folder, extract, and run `KodoTagV2.exe` — that's exactly what players will do.

### release-notes (paste into the Description)
```
Kodo Tag V2 — an Unreal Engine 5 remake of the classic Warcraft III "Kodo Tag" map.

How to play (no Unreal needed):
1. Download KodoTagV2-Windows.zip below.
2. Right-click it → Extract All… to a folder.
3. Open the folder and run KodoTagV2.exe.
   (If Windows SmartScreen appears: More info → Run anyway — normal for unsigned hobby games.)

You're the Runner: build a maze of walls and towers, harvest gold and lumber, research
upgrades, and use your hero's abilities to survive the Kodo waves. Pick a mode
(Maze / God / Bunker) and difficulty on the start screen.

Prefer to run from source? See PLAYING.md in the repo.

Known issues:
- Blockout art (engine-primitive shapes), no audio yet.
```
