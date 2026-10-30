# AGENTS.md

Guidance for AI coding agents (and new contributors) working on D3Hud. The user-facing
description is in `README.md`; this file is what you need to change the code safely.

## What this is

A Diablo III HUD for Windows x64, C++23, CMake + Visual Studio 2022.

- `D3Hud.Reader` attaches to the game process, finds the game's data structures by shape
  (no hard-coded offsets for most things) and publishes `WorldSnapshot`s through a
  shared-memory ring.
- `D3Hud.Overlay` is a transparent, click-through DirectX 11 / DirectComposition window with
  an ImGui UI. It reads snapshots and draws; it also contains the salvage runner.
- `src/core` is shared, dependency-free logic and the snapshot model.

## Layout

```
src/core      Snapshot.h (frozen ABI), SharedRing, ReplayIO, SceneGen, Salvage.h, ItemSearch.h (pure, unit-tested)
src/reader    Process, AobScanner, ActorList, AcdTable, AttribTable, UiControls, SceneTable, SnoTable, BasesCache, main.cpp
src/overlay   main.cpp (window, loop, hotkeys), HudRenderer.{h,cpp} (all drawing, settings, panels),
              SalvageRunner (real mouse clicks), ReaderProcess (launches the reader)
src/tests     test_core.cpp (CHECK macro; run via ctest)
analysis      shipped data tables (*.tsv, scene_cells.bin) + notes (ENGINE_MINING.md: how structures were found)
tools         Python scripts that produced those tables (not needed at runtime)
third_party   vendored Dear ImGui
```

## Commands

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure      # unit tests: keep them green
cmake --install build --config Release --prefix dist       # the relocatable release folder
D3Hud.Overlay.exe --selftest 40                            # synthetic smoke test of the overlay
D3Hud.Reader.exe --selftest                                # reader smoke test (no game needed)
```

- **Close a running overlay/reader before building**: the linker cannot replace a running exe
  (`LNK1104`). Compile errors still show.
- Always build after an edit and look for `error` lines; the CI does the same on every pull request.
- Our targets build with `/W4 /permissive-` and must stay warning-free (CI shows the warnings). Fallible
  functions are `[[nodiscard]]`: check the result, or write `(void)` with a comment saying why it may fail.
- Run the whole test suite after touching `src/core`.

## Rules that are easy to break

1. **Snapshot ABI.** Any change to `WorldSnapshot`, `UiLayout`, `ActorSnapshot` or `ItemSnapshot`:
   bump `kSnapshotVersion` (Snapshot.h) *and* `kReplayVersion` (ReplayIO.cpp), update the
   `static_assert(sizeof(...))` values, and rebuild reader and overlay together. Old replays are
   rejected by design. Appending a new `ActorKind` at the end is safe.
2. **UI controls** are found by path. `GameUiPaths()` in `src/reader/main.cpp` and the `enum GameUi`
   above it must list the same controls in the same order. `ReadGameUi` fills `UiLayout`.
3. **Settings** live in `MinimapLayout` (HudRenderer.h). Adding one needs all four: the field with a
   default, a key in `MinimapLayout::Load`, a line in `MinimapLayout::Save`, and a control on a settings
   page (`DrawSettings` and the `Draw*Settings` functions). Forgetting Load/Save silently loses the setting.
4. **Finding new game data**: use `D3Hud.Reader.exe --attach "Diablo III64.exe" --ui <text>` to list UI
   controls and `--ui-watch <text> --seconds N` while playing to see which fields change. Measure first,
   then code; write what you found into `analysis/ENGINE_MINING.md`. Prefer reading UI geometry/values
   over new memory scans.
5. **Pure logic goes in `src/core`** with a test in `src/tests/test_core.cpp`
   (examples: salvage rules, item search, rift progress/timer math).
6. **The overlay window is `WS_EX_NOACTIVATE` and click-through** by default. Clickable regions are
   registered with `HudOptions::AddHot` (the owner lifts click-through while the cursor is inside).
   Text fields work only because `main.cpp` hands the keyboard focus to the overlay while
   `io.WantTextInput` is set.
7. **Windows headers**: `NOMINMAX`/`WIN32_LEAN_AND_MEAN` are used; `near`/`far` are macros in
   `windows.h` (do not name variables that). `std::print` format strings are compile-time checked.
8. **Paths and `windows.h`.** Include `Win32.h`, never `<windows.h>` directly (it sets `NOMINMAX` and
   `WIN32_LEAN_AND_MEAN` once). Exe-relative files go through `Paths.h`: `ExeDir()`/`ExePath()` (wide API),
   `std::filesystem::path` in every `Load`/`Save` signature, `OpenFile(path, L"rb")` instead of `fopen`,
   `Utf8(path)` for log and ImGui text. Do not use `GetModuleFileNameA`, `find_last_of('\\')` or `fopen` with
   narrow strings for those: they break for folders or user names with non-ASCII characters or spaces.
9. **Install layout**: the exes find their data files, `d3hud.ini`, `reader.log` next to themselves.
   Keep `cmake --install` and the CPack zip in sync with that (CI checks the files). The MSVC runtime
   is linked statically on purpose.

## Safety boundaries (do not weaken without asking)

- The reader only reads memory. The only code that sends input is `SalvageRunner`, and it must keep
  its abort conditions (Esc, mouse moved, game lost focus, salvage tab closed, item did not vanish).
  Do not add other input injection or any automation that plays the game.
- Salvage rules are conservative: unknown items are never destroyed, and the game's "salvage all"
  buttons are used only when every item of that quality is allowed (`CanSalvageAllOfQuality`).

## Style and process

- Match the surrounding code: naming, comment density, idioms. Comments say *why*, not what.
- Small, focused commits with an imperative message; do not mix unrelated changes. Build and test
  before committing; never commit a state that does not compile.
- Keep `README.md` (user-facing) current when behaviour or options change; put investigations in
  `FEATURE_IDEAS.md` / `analysis/`; the plugin idea is in `PLUGIN_PLAN.md`.
- Probe output (`rift_*watch*.txt`) and `dist/`, `build/` are git-ignored.

## Tooling pitfalls seen here

- Writing C++ string escapes (`\0`, `\n`, `\"`) through a shell here-doc or an inline script can turn
  them into real control characters (this once put NUL bytes into a source file). Use the editor tool
  for source edits, or build the escape from character codes, and check for NUL bytes after a scripted edit.
- PowerShell's `>` writes UTF-16; use `| Out-File -Encoding utf8` for text meant to be read by tools.
- After a game patch the reader may need updates: structures are found by shape, but UI paths and
  field offsets (e.g. the progress float at `+0xA60` of a progress bar control) are measured values.
