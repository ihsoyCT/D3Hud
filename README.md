# D3Hud

A heads-up display for Diablo III (Windows, x64). A small background **reader**
reads the game's memory; a transparent **overlay** draws on top of the game window:
a minimap with markers, labels for good drops, rift progress, a stash search,
statistics, and an optional **auto salvage**.

> **Read this first**
> - The reader reads another process's memory, and auto salvage sends **real mouse
>   clicks** into the game window. Use it at your own risk; it may be against the
>   game's rules for your account or realm.
> - Everything is built for the current game client. After a game patch some things
>   may stop working until the reader is updated.

## Contents

- [Quick start](#quick-start)
- [Features](#features)
- [Settings](#settings)
- [Plugins](#plugins)
- [Hotkeys](#hotkeys)
- [Command line](#command-line)
- [Files the program uses and creates](#files-the-program-uses-and-creates)
- [Build, install and package](#build-install-and-package)
- [Troubleshooting](#troubleshooting)
- [Project layout and further reading](#project-layout-and-further-reading)

## Quick start

You need the release folder (see [Build, install and package](#build-install-and-package)
to make one). It is self-contained and can live anywhere you can write to, for
example `C:\Games\D3Hud`. No installer and no extra runtimes.

1. Run Diablo III in **windowed** or **windowed fullscreen** mode (an overlay cannot
   draw over exclusive fullscreen) and get into the game world.
2. Double-click `D3Hud.Overlay.exe`. With no options it covers the Diablo III window,
   starts the reader for you and shows live data. A console window stays open and logs
   what it is doing. You may start it before the game: it stays hidden until a window
   titled "Diablo III" exists, starts the reader when the game appears, and picks the
   game up again if you restart it.
3. After a few seconds the minimap markers appear. The first start scans the game's
   memory (a few seconds up to ~15 s); later starts against the same running game
   are much faster because the found addresses are cached.
4. Hover the small tabs in the bottom-right corner to peek at the panels. Press **F8**
   to make the overlay clickable, click a tab to pin its panel open, and open
   **Settings** to configure everything.

To quit, press **End**. The reader stops with the overlay.

The minimap markers are drawn over the game's own minimap, and the overlay finds that
minimap by itself (it reads the position and size of the game's minimap control, so it works
at any resolution). If it ever cannot, or the zoom is off, adjust it in **Settings > Minimap**
(*zoom*, and the manual position when automatic following is turned off).

## Features

**Minimap and map**
- Markers for monsters (trash, champions, rares, minions, bosses), pylons and shrines
  (with names, distance and a pulsing ring), chests, portals, health globes, ground items
  and the **progress orbs** of rifts (blue for normal rifts, yellow for greater rifts).
- Map prediction: the walkable level layout (also unexplored parts), entrance/exit labels.
- The same markers on the game's big TAB map.
- An arrow on the rim to the nearest unused pylon when it is off the map, and a ring around the
  nearest rare or champion.
- Every kind can be switched off, recoloured, resized, reshaped or limited by distance.

**Items and drops**
- Labels over legendary and set items on the ground, with *Ancient* / *Primal* in front;
  filters, colours and size are configurable.
- **Item badges** on inventory and stash icons: `A` (ancient), `P` (primal), with the
  Caldesann's augment rank in front (`125A`).
- A card for **unidentified** items that shows what they really are (name, ancient/primal)
  when the game shows their tooltip.
- **Drops panel**: a session log, drops per hour, ancient/primal counts, a list of runs
  (one per world visit) with their own counts, optional sound/flash alert for primal or
  ancient drops, CSV export.
- Optional run timer at the top of the screen.

**Rifts**
- **Rift progress** (percent) and an **ETA** for normal and greater rifts.
- Greater rifts: the real 15 minute timer and your pace compared with the time left.

**Stash search**
- A search box below the stash grid. Type words (`inna`, `ancient ring`, `primal`,
  `unidentified`); matching items are outlined and the rest dimmed, and the stash tabs
  and pages that hold matches are marked with a count.
- The game only exposes the stash tab that is open, so the overlay remembers each tab as
  you open it (this session only). Tabs it has not seen yet are tinted red; open them
  once to include them.

**Auto salvage** (sends mouse clicks to the game; Esc or moving the mouse stops it)
- A **Salvage** button appears under the anvil while the blacksmith's salvage tab is open;
  it works without F8.
- You mark a **safe area** of backpack cells that are never touched. The safe area and its
  editor are shown only at the salvage tab (can be turned off).
- Rules: which rarities, keep every ancient / primal, keep ancient and primal versions of
  worn items, never-salvage item types, a limit per run, extra delay, a confirm step.
- Uses the game's own **salvage all blue / yellow** buttons when it is safe (no protected
  item of that colour in the bag), then clicks the rest quickly, with legendary and set items
  confirmed automatically.
- Items marked for salvage get a red mark while the tab is open.

**The HUD itself**
- Panels (Info, Drops, Items, Actors, Settings) start as small tabs in a screen corner.
  Hover to peek, F8 + click to pin, drag a title bar to move, the X in the title bar of a pinned panel closes it again; positions are remembered.
- Text uses the Windows font Segoe UI (Tahoma or Arial if it is missing) at 16 px; "UI scale" in Settings > General makes everything larger or smaller. Rebindable hotkeys, profiles (save/load whole setups), UI scale, dock options,
  compact mode, reader autostart and on-screen diagnostics.

## Settings

Open **Settings** (the cogwheel, always the last tab of the dock; pin it with F8 + click; it does not open on hover). The window
can be resized by dragging its bottom-right corner, and the size is remembered.

| Page | What is there |
|---|---|
| General | UI scale, dock corner / opacity / auto-hide, panel opacity, compact mode |
| Hotkeys | Rebind every hotkey, including a toggle for each panel |
| Profiles | Save, load and delete whole setups (`d3hud.<name>.ini`) |
| Info / Drops / Items / Actors | Show in dock, pin, reset position; plus drop alerts, run timer and rift options (Drops) and badges, hover card and stash search (Items) |
| Minimap | Placement and zoom, global marker options, a table with every actor kind |
| Ground labels | Which items get labels, text size, colours |
| Auto salvage | Safe area, rules, speed options, run safety, preview colour |
| Reader / diagnostics | Autostart the reader, publish rate, on-screen diagnostics, open the settings folder, reset everything |
| Plugins | Every loaded plugin with its status, the time it costs per frame, and its log; plugins add their own pages and options here |

Most changes are saved immediately; on the Minimap page press **Save**.

## Plugins

Small Lua scripts in the `plugins` folder add features without rebuilding the program. One is included:

- **Time tracker** (`plugins/timetracker`) records where your play time goes and shows it as percentages
  per area, for this session and all time. Rifts and greater rifts are detected automatically. For
  everything else you stand in a world once, open *Settings > Time tracker* and say which area it belongs
  to (for example *Town* or *Bounties*); the answer is remembered. The page also holds the options (rename
  or add areas, show the split on screen). A **Time** tab in the dock shows the same split when you hover it.
- **Health bars** (`plugins/healthbars`) draws a life bar over every champion (blue), rare and boss on the
  screen, and lists the same bars one under the other on the left or right edge of the screen with the
  monster's name inside each bar. Under a bar it shows the monster's affixes as boxes (border and
  translucent fill in the affix colour, black or white text): the plugin's page has a row for each of the 30
  affixes with a checkbox and a colour, and the options set the text size and the fill opacity. Size, order and what to show are on
  its settings page. The reader reads the life of those three
  kinds only, and only once it has found their data, so a bar can appear a few seconds late.

- **Damage meter** (`plugins/damagemeter`) measures the damage you deal to champions, rares and bosses from how their
  life falls: current DPS, the peak, the total and duration of each fight, a prediction of how long the elites on
  the screen will live, and your records. It shows in a **DPS** tab of the dock and optionally as a line on the
  screen. Trash is not counted (its life is not read).

- **Effects** (`plugins/effects`) lists what is running on your character with the time left, in an **Effects** tab of
  the dock: cooldowns (health potion, the cheat death cooldown) and buffs (pylons, shrines), the soonest to end first,
  highlighted in yellow below 5 seconds. By default it shows only the important ones: pylons, shrines, cheat death and the
  health potion. A selector at the bottom of the tab switches to *Without noise* (everything except item powers that renew
  themselves every second and the statuses monsters put on you) or *Everything*; hover an entry for the game's name and
  number of the power, and add the numbers of powers you care about to the options to make them important too. The same
  list can also be drawn as bars on the screen. The times follow the game's clock, which the program estimates; they are
  right to about a tenth of a second.

Plugins run in a sandbox: they can read what the HUD sees and draw text and settings pages, but cannot
read game memory, press keys, or touch files. To write one see `plugins/README.md`. Restart the overlay
after adding or changing a plugin.

## Hotkeys

Defaults (all can be changed in Settings > Hotkeys). They are global, so they work while
the game has the focus.

| Key | Action |
|---|---|
| F7 | Cycle the map orientation |
| F8 | Make the overlay clickable / click-through |
| F9 | Show or hide the panels |
| End | Quit |
| Esc / moving the mouse | Stop a running auto salvage |

Typing into a text field (stash search, profile name) works: the overlay takes the
keyboard while a field is active and gives it back to the game afterwards.

## Command line

```
D3Hud.Overlay.exe [options]        no options = cover "Diablo III", manage the reader, live data
  --target <title substring>   cover this window instead of "Diablo III"
  --no-reader                  do not start the reader (run D3Hud.Reader.exe --publish yourself)
  --demo                       synthetic demo scene instead of the game (no game needed)
  --replay <file>              play a recorded .bin instead
  --record <file> [--seconds N]  record the synthetic scene
  --colorkey                   fallback if DirectComposition is not available
  --selftest <frames>          run N frames and exit (smoke test)
  --settings-page <id>         start with Settings open on that page (e.g. minimap, timetracker.timetracker)
  --help
```

The reader is started once the game window exists, restarted if the game is restarted,
and not restarted after you press *Stop reader* in the Info panel. If it stops right away
four times in a row the overlay gives up and shows a hint (details in `reader.log`).

```
D3Hud.Reader.exe --attach "Diablo III64" --publish     # live snapshots for the overlay
        [--hz N]                                       #   publish rate (default 60)
        [--sno <path>]                                 #   actor table (default: next to the exe)
D3Hud.Reader.exe --attach "Diablo III64" --actors      # one-shot dump of drawable actors
D3Hud.Reader.exe --attach "Diablo III64" --items       # equipped and backpack items
D3Hud.Reader.exe --attach "Diablo III64" --affixes     # research: where do elites keep their affixes (see analysis/MONSTER_AFFIXES.md)
D3Hud.Reader.exe --attach "Diablo III64" --ui <text>   # list UI controls whose path contains <text>
D3Hud.Reader.exe --attach "Diablo III64" --ui-watch <text> [--seconds N]
                                                       # watch what changes in those controls
D3Hud.Reader.exe --list [filter]                       # list processes
```

`--ui-watch` is a research tool: run it while playing to find which memory field of a UI
control follows something on screen (this is how the rift progress was found).

## Files the program uses and creates

Next to the exes (the folder is relocatable, nothing is written elsewhere):

| File | |
|---|---|
| `sno_actors.tsv`, `items_gbid.tsv`, `sno_powers.tsv`, `scene_cells.bin` | Data tables the program needs (shipped) |
| `d3hud.ini` | All settings, panel positions, hotkeys and the safe area (created on first save) |
| `d3hud.<name>.ini` | Saved profiles |
| `plugins/`, `plugins.ini` | Lua plugins (shipped) and the settings and data they keep |
| `reader.log` | Output of the background reader |
| `d3hud_drops.csv` | Created by *Export session to CSV* in the Drops settings |
| `dumps\` | Created by the reader's F10 actor dump |

Keep the folder somewhere your user can write to (not `Program Files`).

## Build, install and package

Requirements: Windows, Visual Studio 2022 (C++ desktop workload), CMake 3.20 or newer.
Dear ImGui is vendored in `third_party/imgui`.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cd build && ctest -C Release            # unit tests
```

Binaries are in `build\Release\`. To run from the build tree: `build\Release\D3Hud.Overlay.exe`.

**A relocatable release folder**

```
cmake --install build --config Release                # -> .\dist
cmake --install build --config Release --prefix D:\D3Hud
```

The folder contains `D3Hud.Overlay.exe`, `D3Hud.Reader.exe`, the three data files,
this README and `licenses\`. It runs on a machine without the Visual C++
redistributable, because the C runtime is linked statically (the default; turn it off
with `-DD3HUD_STATIC_RUNTIME=OFF`). To get a zip of the same layout:

```
cd build
cpack -C Release                                      # -> D3Hud-<version>-win64.zip
```

Close a running overlay before rebuilding: Windows does not let the linker replace a
running exe.

**GitHub Actions**

- `.github/workflows/ci.yml` builds and runs the tests on every pull request (and on pushes
  to `main`), checks that the release folder can be installed, and attaches the zip of that
  commit to the run as an artifact.
- `.github/workflows/release.yml` runs when a GitHub release is published: it builds, runs
  the tests and uploads `D3Hud-<tag>-win64.zip` to that release. To make a release, create
  one on GitHub (a new tag such as `v0.2.0` is fine); the zip appears a few minutes later.

## Troubleshooting

- **Nothing visible at all**: the overlay hides until a window whose title contains
  "Diablo III" exists; check the console window it opened. If your game window has a
  different title, start it with `--target "<part of the title>"`.
- **No markers, "reader stopped"**: the game must be running and you must be in the game
  world. Open the Info panel and use *Start reader*; check `reader.log` next to the exes.
- **Nothing draws over the game**: switch the game to windowed or windowed fullscreen.
- **Markers are offset from the game's minimap**: Settings > Minimap says whether the game's
  minimap was found. If not, turn off *follow automatically* and set the position and radius by hand
  (press Save). If the markers are too spread out or too bunched up, change *zoom*; if they are
  rotated, press F7 to cycle the orientation.
- **The first start takes long**: the reader scans the game's memory once; the result is
  cached for the running game.
- **Salvage stops with "an item did not salvage"**: the game missed a click. Raise
  *extra delay between clicks* or turn off *fast mode* in Settings > Auto salvage.
- **Stash search shows red tabs**: those tabs have not been opened since the overlay started.
- **A key does nothing**: another program may have registered it as a global hotkey;
  rebind it in Settings > Hotkeys.
- **Start over**: Settings > Reader / diagnostics > *Reset everything*, or delete `d3hud.ini`.

## Project layout and further reading

```
src/core      snapshot model (frozen ABI), replay I/O, shared-memory ring, rules (salvage, search), host API (HostApi.h)
src/plugins   Lua plugin runtime (sandbox, host API binding) and the plugin settings store
src/reader    the memory reader (process attach, actor / item / UI / scene discovery)
src/overlay   Win32 + DirectX 11 + DirectComposition window, ImGui renderer, salvage runner
src/tests     unit tests (ctest)
analysis      data tables the program loads, plus notes on the game's memory layout
tools         Python scripts used to mine those tables (not needed at runtime)
```

- `TESTING.md` — manual test matrix and the replay-first workflow
- `FEATURE_IDEAS.md` — ideas and investigations
- `plugins/README.md` — writing a Lua plugin; `PLUGIN_PLAN.md` — the design and what is still planned
- `analysis/ENGINE_MINING.md` — how the game's data structures were found
