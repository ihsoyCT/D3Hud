# D3Hud

Minimal, fully-auditable overlay for Diablo III. Strictly read-only: it reads
game memory and draws; it never sends input.

## Status

- **M0 (current)** — full pipeline with synthetic data: transparent click-through
  ImGui overlay, deterministic scene generator, record/replay, unit tests.
- M1 — live reader: attach, AOB offsets, player position.
- M2 — actor list -> minimap dots live (reader side done: `--actors`, `--publish`).
- M3 — item brain: unided reveal (type + ancient/primal) and salvage grading.

## Architecture

- `D3Hud.Reader` (later) — attaches to the game, publishes `WorldSnapshot`s.
- `D3Hud.Overlay` — Win32 + DX11 + DirectComposition, ImGui UI. Sources:
  synthetic (default), `--replay file`, (later) live shared memory.
- `D3Hud.Core` — `WorldSnapshot` POD model (frozen ABI, see `Snapshot.h`),
  scene generator, replay file I/O, world->minimap math.

## Build (VS2022 + CMake)

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cd build && ctest -C Release
```

## Run

```
build\Release\D3Hud.Overlay.exe                     # synthetic, whole primary screen
build\Release\D3Hud.Overlay.exe --target Notepad    # track & cover a window
build\Release\D3Hud.Overlay.exe --record demo.bin --seconds 10
build\Release\D3Hud.Overlay.exe --replay demo.bin
build\Release\D3Hud.Overlay.exe --live              # consume live reader snapshots
build\Release\D3Hud.Overlay.exe --colorkey          # legacy fallback (DComp unavailable)
build\Release\D3Hud.Overlay.exe --selftest 120      # headless smoke test
build\Release\D3Hud.Overlay.exe --selftest-hit      # automated click-through check
```

Live pipeline (game running, player in-world):

```
build\Release\D3Hud.Reader.exe --attach "Diablo III64" --publish
build\Release\D3Hud.Overlay.exe --live --target "Diablo III"
```

Or let the overlay run the reader: `--live` shows a **Start reader** button in
the Info panel (F8 to click it), `--start-reader` launches it right away. The
reader then runs without a console, logs to `reader.log` next to the exe, and
is stopped when the overlay quits. The overlay reconnects whenever a reader
(re)starts.

The reader finds the game's actor array by its record shape (no hardcoded
offsets) and publishes the player plus monsters, bosses, pylons, shrines,
chests and ground items. Labels come from `sno_actors.tsv` (copied next to the
exe at build time; override with `--sno <path>`). If the array is not found it
falls back to player-only tracking (stand still ~2 s at start).

```
build\Release\D3Hud.Reader.exe --attach "Diablo III64" --actors   # one-shot dump of drawable actors
```

Hotkeys: **F7** map orientation, **F8** click-through on/off (interactive mode
shows the *Minimap layout* panel), **F9** panels on/off, **END** quit; reader:
**F10** dumps the actor list to `build\Release\dumps\`.

The minimap draws its dots over the game's own minimap (top-right). Calibrate
once in F8 mode: move/scale until a pylon or waypoint dot sits on the game's
icon, then *Save* (stored in `d3hud.ini` next to the overlay exe, relative to
window height so it survives resolution changes).

See `TESTING.md` for the manual test matrix and the replay-first workflow.
