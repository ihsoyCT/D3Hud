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
src/core      Snapshot.h (frozen ABI), SharedRing, ReplayIO, SceneGen, Salvage.h, ItemSearch.h, Settings.h, Effects.h (buffs/cooldowns,
              game clock estimate, power names; pure, unit-tested),
              HostApi.h (the plain-C plugin API)
src/plugins   LuaPlugin (sandboxed Lua + host API binding), PluginSettings (no UI, unit-tested)
src/reader    Process, AobScanner, ActorList, AcdTable, AttribTable, UiControls, SceneTable, SnoTable, BasesCache, main.cpp
src/overlay   main.cpp (window, loop, hotkeys), HudRenderer.{h,cpp} (all drawing, settings, panels), PluginHost (host API on ImGui),
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
3. **Settings** live in `MinimapLayout` (HudRenderer.h). Adding one needs three things: the field with a
   default, one `t.Add("key", field, lo, hi)` line in `MinimapLayout::Describe` (HudRenderer.cpp; it gives
   Load, Save and clamping), and a control on a settings page (pages are registered in
   `RegisterBuiltinSettingsPages`: `SettingsPage` = id, title, group, order, draw callback; plugins will use
   `AddSettingsPage` too). Without the `Describe` line the setting is silently not saved.
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
10. **Plugin API** (`src/core/HostApi.h`) is plain C and versioned: structs start with `size`, additions go at the
   END, `D3H_API_VERSION` only grows. Never expose the Snapshot structs directly; copy into the API structs
   (PluginHost.cpp). A new host function needs: the table entry, the implementation in `PluginHost.cpp`, the Lua
   binding in `LuaPlugin.cpp` (registered only if the host provides it), a line in `plugins/README.md`, and a
   test against the fake host in `test_core.cpp`. `HostApi.h` must keep compiling as C (`host_api_c.c`).
11. **Lua is compiled as C++** (CMakeLists.txt): its errors are then C++ exceptions and the destructors of our bindings
   run. As C it uses longjmp, which skips them and crashed when an error passed through a binding that owned a
   `std::string`. Do not switch it back, and include the Lua headers without `extern "C"`.
12. **Plugin libraries** (`plugins/lib`, `d3hud.require`) are part of the plugin API and the standard way to draw
   (`draw`): new plugins use them instead of their own helpers. A change to what a library function does needs an
   API version bump. Libraries have tests in `test_core.cpp` (`TestLuaLibraries`).
13. **Plugin sandbox.** Lua gets only base (whitelisted), string, table, math, utf8, coroutine; text chunks only; a
   memory cap and an instruction budget per call. Do not add io/os/debug/package, `load`, or any function that
   reaches game memory, injects input, or touches files or the network; plugins stay unable to play the game.
   The one file access is `d3hud.image`: the HOST reads a PNG from the plugin's own folder by a plain `name.png`
   (`IsPlainImageName`, size caps in `PluginAssets.h`); the plugin never sees a path. Keep it that narrow.

## Safety boundaries (do not weaken without asking)

- The reader only reads memory. The only code that sends input is `SalvageRunner`, and it must keep
  its abort conditions (Esc, mouse moved, game lost focus, salvage tab closed, item did not vanish).
  Do not add other input injection or any automation that plays the game.
- Plugins cannot weaken the salvage rules: a plugin verdict may only make a decision more conservative, and
  "junk" is honoured only for items the built-in rules already allow (enforced in C++, not in the plugin).
- Salvage rules are conservative: unknown items are never destroyed, and the game's "salvage all"
  buttons are used only when every item of that quality is allowed (`CanSalvageAllOfQuality`).

## Style and process

- Match the surrounding code: naming, comment density, idioms. Comments say *why*, not what.
- Own resources with RAII, not paired calls: `UniqueHandle` (Win32.h) for kernel handles, `File` (Paths.h)
  for `FILE*`, `PanelScope` (HudRenderer.h) for the five dock panels. No new manual `CloseHandle`/`fclose`.
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
