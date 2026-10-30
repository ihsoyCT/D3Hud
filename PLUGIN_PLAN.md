# Plugin system plan

Status: in progress. Written 2026-10-30, reviewed the same day.

Decisions after review: plugins are written by the owner only (light sandbox), Lua only (the DLL track
stays parked), Lua 5.4 vendored as plain C in `third_party/lua`, and `input.click` is left out of the
first API version. A Lua `salvage_verdict` can only make decisions more conservative: "junk" is honoured
only for items `IsSalvageCandidate`/`CanSalvageAllOfQuality` already allow, enforced in C++.

Progress: step 0b part 1 done: `src/core/Settings.h` (`SettingsTable`) replaces the hand-written
`MinimapLayout::Load`/`Save`; the built-ins register through `MinimapLayout::Describe`.
Milestones 2-4 done (the goal of the plugin PR): `src/core/HostApi.h` (plain C), the sandboxed Lua runtime
(`src/plugins/`), the overlay host (`PluginHost`) and the first plugin, `plugins/timetracker` (time per area). Deviations
from the plan: the manifest is `plugin.lua` (a table, run with no globals) instead of `plugin.json`, to avoid a
JSON parser; plugin settings live in `plugins.ini` (separate from `d3hud.ini`); dock panels, hot rects, plugin
hotkeys, the drawing shapes, the `drop` event and the salvage policy hook are in the header but not provided yet.

Step 0b part 2 done: the Settings window lists `SettingsPage` entries (`AddSettingsPage`); the built-in pages are
registered the same way, grouped Interface / Panels / Features.

**Recommendation:** define one small, versioned host API and put Lua on top of
it first. DLL plugins come later as a second consumer of the same API. What
plugins can see and do matters more than the Lua-vs-DLL choice.

## Lua vs DLL

| | Lua | DLL |
|---|---|---|
| Crash isolation | A script error is caught; only that plugin is disabled. | A crash takes down the whole overlay. |
| Sandbox | Easy: only the host API, no `io`/`os`. | None; a DLL can do anything. |
| Hot reload | Trivial, reload the file. | File is locked; needs shadow-copying. |
| ABI | None. | Must be a plain C ABI; ImGui version/context shared by hand. |
| Speed | Fine for UI and rules; per-actor loops at 60 Hz need care. | Native. |
| Authoring | A text editor. | Compiler and headers. |

Plugins run next to a game, in the same process as an input-clicking engine, so
sandbox and crash isolation decide it: Lua is the default; DLLs are for heavy
or trusted native work.

## Step 0: refactor first

`HudRenderer.cpp` does everything (~2200 lines). Before any scripting, split it
into built-in "features" behind one interface: Minimap, Drops, Items, Actors,
Info, Salvage, ItemBadges, GroundLabels. Each has `OnSnapshot`, `Draw`,
`Settings`, `Load`/`Save`. The built-ins become the first plugins, so the
interface is exercised before any script uses it.

## Step 0b: settings registry and typed store (same step as the split)

Today the Settings window is a hard-coded list of pages and `MinimapLayout`
has hand-written ini `Load`/`Save`. Plugins cannot hook into that, so
abstract it first, and make the built-in features use it:

- **Settings pages are a registry.** The built-ins (Minimap, Auto salvage,
  Hotkeys, Ground labels, ...) are registered pages exactly like plugin pages.
  A page has an id, title, group, order and either a schema (auto-rendered) or
  a `draw()` callback using the curated widget set. Plugins get their own tab
  or group in the left list; the host owns the two-column window, resizing,
  scale and persistence of the window itself.
- **Typed settings store.** Values keyed `plugin.key` with a schema (type,
  default, range). Replaces the hand-written Load/Save; each plugin gets its
  own section in `d3hud.ini`. Profiles, "reset everything" and "open settings
  folder" then cover plugins automatically.
- **Extras plugins can register:** hotkeys (shown in the Hotkeys tab,
  namespaced so plugins cannot clash), `on_setting_changed` events, a per-plugin
  "reset to defaults", and a settings version so a plugin can migrate old
  values.
- The host does all saving; plugins never write files (capability-gated like
  the rest of the API).

## Step 1: the host API

The core of the design. A plain-C struct of function pointers plus POD snapshot
types, with a `size` field and an API version for forward compatibility. Lua
and DLLs both get it.

**Read-only data**
- `world`: player position/heading, actors, items, scenes. Iterators over the
  existing POD arrays, not per-frame table copies.
- `ui`: inventory, stash, salvage and TAB-map state, backpack geometry.
  Helpers: `world_to_screen`, `world_to_minimap`, `item_screen_rect`.
- `names`: actor and item name tables.

**Events**
- `on_frame`, `on_snapshot`, `on_drop` (new legendary on the ground),
  `on_inventory_open`/`on_inventory_close`, `on_world_change`, `on_item_hover`,
  `on_hotkey`.

**Drawing and UI**
- Layered draw primitives: line, rect, circle, quad, text.
- `panel.register{id, title, draw=fn}`: the host owns dock, hover, pin, drag and
  position persistence (the framework already built). Plugins draw a curated
  ImGui subset (~60 widgets).
- `ui.hot_rect(...)`: a region where the overlay takes clicks without F8, as
  the salvage button does.

**Settings** (see Step 0b)
- `settings.define{key, type, default, min, max}`: auto-rendered in the plugin's
  settings page and saved per plugin. Plugins never touch files.
- `settings.page{title, group, draw=fn}`: a custom page for anything the schema
  cannot express (tables, previews, key capture).

**Policy hooks** (the core calls them for user-customisable rules)
- Main example: `salvage_verdict(item) -> keep | junk | check`.

**Privileged actions**
- `input.click(rect)` is queued through the existing SalvageRunner engine, so
  its abort-on-Esc / mouse-move safety stays.
- Needs a capability in the plugin manifest that the user approves.

**Kept out of the API**
- Game memory access and raw input injection, so plugins cannot break the
  safety rules.
- Reader plugins: memory reading stays native.

## Step 2: Lua track

- Embed Lua 5.4 (not LuaJIT: its FFI would bypass the sandbox). Raw C API with
  a thin helper rather than sol2 (heavy to compile).
- One Lua state per plugin, whitelisted environment, instruction-count hook
  against runaway loops.
- Disable a plugin after repeated errors; show the error in a Plugins section
  of Settings.
- Layout: `plugins/<name>/plugin.json` (name, version, `api` version,
  capabilities) and `main.lua`. File watcher for hot reload.
- Hot paths such as minimap markers stay in C++ and are configured
  declaratively from Lua; Lua does not draw per actor.

## Step 3: DLL track (later)

- Exports: `d3hud_plugin_info()`, `init(const HostApi*)`,
  `frame(const FrameCtx*)`, `shutdown()`.
- ImGui via the host's function table, or the plugin gets the context and
  allocators with a version check.
- Loader wraps calls in SEH and shadow-copies the DLL for reload.
- Clearly marked as trusted code.

## Milestones

1. Typed settings store (`SettingsTable`) and settings page registry (both done), then split
   HudRenderer into features, no behaviour change.
2. Write the host API header (done).
3. Lua runtime, loader, sandbox, manifest (done).
4. A first plugin as the proof (done: time tracker). Later: port Drops and the marker styles to Lua.
5. Move salvage rules to a Lua policy hook.
6. Plugin manager UI and hot reload.
7. DLL loader.

## Open decisions

- **Who writes plugins?** Only the owner: sandbox and capability approval can
  stay light. Shared plugins: they need to be solid.
- **Is Lua alone enough?** Skip the DLL track until a plugin needs native speed.

Suggested start: step 1 (the refactor). It needs no scripting runtime and
improves the code on its own.
