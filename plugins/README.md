# Writing a plugin

A plugin is a folder in `plugins/` next to `D3Hud.Overlay.exe`:

```
plugins/myplugin/
  plugin.lua   return { name = "myplugin", version = "1.0", api = 1 }
  main.lua     the code
```

`name` is letters, digits, `-` and `_`. `api` is the host API version the plugin was written for (the
current one is 1). The overlay loads every folder at start; **restart the overlay to reload**.
`timetracker/` is a complete example (time spent per area, with a settings page).

The Plugins page in Settings lists every plugin, whether it runs, its errors and its log lines.
`D3Hud.Overlay.exe --settings-page timetracker.timetracker` opens Settings on one page (ids are
`<plugin>.<page id>`, and `<plugin>.settings` for the automatic options page).

## What a plugin can and cannot do

Plugins run in a sandbox: one Lua 5.4 state each, with `string`, `table`, `math`, `utf8`, `coroutine` and
the basic functions. There is **no** `io`, `os`, `debug`, `require`, `load`, `print`. A script gets 32 MB
of memory and a budget of 2 million Lua instructions per call (an endless loop is stopped). An error is
logged with the plugin's name; after 5 errors the plugin is switched off until the next start.

Plugins can read what the HUD sees, draw text, add settings and Settings pages. They cannot read game
memory, press keys or click, touch files or the network. (See `PLUGIN_PLAN.md` for why.)

## The `d3hud` table

| | |
|---|---|
| `d3hud.log(msg [, "info"\|"warn"\|"error"])` | write to the Plugins page and the console |
| `d3hud.on(event, fn)` | subscribe; `fn(ev)` gets `ev.type`, `ev.index` (1-based, 0 = none), `ev.value`, `ev.text` |
| `d3hud.frame()` | a table with the current facts (below) |
| `d3hud.actor(i)`, `d3hud.item(i)` | the i-th (1-based) actor / item of the snapshot, or `nil` |
| `d3hud.world_to_screen(x, y [, height])` | screen pixels of a world position, or `nil` when off screen |
| `d3hud.draw_text(x, y, argb, text [, scale])` | text on the screen, from a `frame` event handler |
| `d3hud.setting{...}`, `d3hud.get(key)`, `d3hud.set(key, value)` | persisted settings (below) |
| `d3hud.page{id, title, group, order, draw}` | a page in the Settings window; `draw(ui)` runs while it is shown |
| `d3hud.api` | the host API version |

**Events:** `frame` (every drawn frame), `snapshot` (a new snapshot arrived: ~60/s live), `world_change`
(`ev.value` = world SNO), `inventory_open`, `inventory_close`, `item_hover` (`ev.index`), `hotkey`
(not delivered yet), `setting_changed` (`ev.text` = `plugin.key`, when changed in the options page).

**`d3hud.frame()`** returns `frame_index`, `time_ms`, `player_x`, `player_y`, `player_heading`,
`world_sno`, `actor_count`, `item_count`, `hovered_item` (1-based, 0 = none), `screen_w`, `screen_h`,
`rift_progress` (0..1), `rift_timer_fraction` (-1 unknown), `stash_tab`, and `ui`:
`ui.inventory`, `ui.stash`, `ui.salvage`, `ui.big_map`, `ui.minimap`, `ui.rift`, `ui.greater_rift`.

**Settings:** `d3hud.setting{ key = "x", type = "bool"|"int"|"float"|"color"|"string"|"choice",
label = "...", tooltip = "...", default = ..., min = ..., max = ..., choices = {"A", "B"}, hidden = true }`.
Visible settings appear on the plugin's automatic options page; `hidden` ones are state the plugin
keeps for itself (the time tracker stores its totals this way). Values are clamped to `min`/`max` by the
host. `choice` values are 1-based. Colours are `0xRRGGBB` numbers. Everything is stored in `plugins.ini`.

**`ui` (inside a page's `draw(ui)`):** `text`, `text_disabled`, `text_colored(argb, text)`, `separator`,
`separator_text`, `same_line`, `spacing`, `button(label)`, `checkbox(label, value)`,
`slider_int(label, v, min, max)`, `slider_float(label, v, min, max [, "%.2f"])`, `combo(label, index, {items})`,
`color_edit(label, rgb)`, `input_text(label, text)`, `collapsing_header(label)`, `is_item_hovered()`,
`tooltip(text)`. Widgets that edit a value return `changed, newvalue`:

```lua
local changed, v = ui.checkbox("enabled", enabled)
if changed then enabled = v end
```

## Not available yet

The host table in `src/core/HostApi.h` already reserves these; this host does not provide them yet:
`world_to_minimap` / item rectangles, shapes (`draw_line`, `draw_rect`, ...), dock panels, hot rects,
plugin hotkeys, the salvage policy hook and the `drop` event. Check `src/overlay/PluginHost.h` for the
current list.
