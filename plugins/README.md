# Writing a plugin

A plugin is a folder in `plugins/` next to `D3Hud.Overlay.exe`:

```
plugins/myplugin/
  plugin.lua   return { name = "myplugin", version = "1.0", api = 1 }
  main.lua     the code
```

`name` is letters, digits, `-` and `_`. `api` is the host API version the plugin was written for (the
current one is 4; a plugin that uses `effects()` needs `api = 2`, one that uses `box{}` `api = 3`, one that shows pictures `api = 4`). The overlay loads every folder at start; **restart the overlay to reload**.
`timetracker/` (time per area: page, options, dock panel), `healthbars/` (draws on the screen from
`actors()`), `effects/` (a dock panel and bars from `effects()`) and `convention/` (learns a cycle from the `slot` of an effect) are
complete examples.

The Plugins page in Settings lists every plugin with an **enabled** checkbox (off: its code stops at once and its dock
tabs go away; remembered in `plugins.ini` as `plugins.disabled`; switching it on again also gives a plugin that stopped
after repeated errors another try), whether it runs, its errors, its log lines and what it costs:
microseconds spent in its code per drawn frame (average and worst frame, refreshed twice a second; event handlers
and page / panel drawing included), so a slow plugin is easy to spot.
`D3Hud.Overlay.exe --settings-page timetracker.timetracker` opens Settings on one page (ids are
`<plugin>.<page id>`, and `<plugin>.settings` for the automatic options page).

## What a plugin can and cannot do

Plugins run in a sandbox: one Lua 5.4 state each, with `string`, `table`, `math`, `utf8`, `coroutine` and
the basic functions. There is **no** `io`, `os`, `debug`, `require`, `load`, `print`. A script gets 32 MB
of memory and a budget of 2 million Lua instructions per call (an endless loop is stopped). An error is
logged with the plugin's name; after 5 errors the plugin is switched off until the next start.

Plugins can read what the HUD sees, draw text and pictures from their own folder, add settings and Settings pages. They cannot read game
memory, press keys or click, touch files or the network. (See `PLUGIN_PLAN.md` for why.)

## The `d3hud` table

| | |
|---|---|
| `d3hud.log(msg [, "info"\|"warn"\|"error"])` | write to the Plugins page and the console |
| `d3hud.require(name)` | a shared library from `plugins/lib` (below), loaded once per plugin |
| `d3hud.on(event, fn)` | subscribe; `fn(ev)` gets `ev.type`, `ev.index` (1-based, 0 = none), `ev.value`, `ev.text` |
| `d3hud.frame()` | a table with the current facts (below) |
| `d3hud.actors()` | **all** actors of the snapshot as an array of tables (fields below) |
| `d3hud.effects()` | the player's running **buffs and cooldowns** as an array of tables (below). Needs `api = 2` |
| `d3hud.actor(i)`, `d3hud.item(i)` | the i-th (1-based) actor / item of the snapshot, or `nil` |
| `d3hud.world_to_screen(x, y [, height])` | screen pixels of a world position, or `nil` when off screen |
| `d3hud.draw_text(x, y, argb, text [, scale])` | text on the screen, from a `frame` event handler |
| `d3hud.text_size(text [, scale])` | width, height in pixels of text as `draw_text` draws it |
| `d3hud.draw_rect(x0, y0, x1, y1, argb [, filled = true [, thickness]])` | a rectangle, from a `frame` handler |
| `d3hud.draw_line(x0, y0, x1, y1, argb [, thickness])` | a line, from a `frame` handler |
| `d3hud.setting{...}`, `d3hud.get(key)`, `d3hud.set(key, value)` | persisted settings (below) |
| `d3hud.page{id, title, group, order, draw}` | a page in the Settings window; `draw(ui)` runs while it is shown. The first page of a plugin also shows its options and dock panel controls |
| `d3hud.box{id, title, x, y, fill, fill_alpha, border, border_alpha, padding, draw}` | a **movable box** on the screen (below). `draw(x, y)` returns the width and height it used. Needs `api = 3` |
| `d3hud.image(name)` | a picture from the plugin's own folder: `{id, w, h}`, or `nil, message` (below). Needs `api = 4` |
| `d3hud.draw_image(img, x, y [, w, h [, argb tint]])` | draws it; `w`, `h` default to its own size; `tint` 0xAARRGGBB (white = as is) |
| `d3hud.panel{id, title, width, height, draw}` | a dock panel: a tab in the dock, hover to peek, F8 + click to pin, drag to move; `draw(ui)` like a page. `width`/`height` > 0 make it resizable |
| `d3hud.affixes` | all 30 monster affixes of the game: `{ {key = "molten", name = "Molten"}, ... }` |
| `d3hud.kind` | names for the actor kind numbers: `d3hud.kind.rare`, `.champion`, `.boss`, `.pylon`, ... |
| `d3hud.api` | the host API version |

**Events:** `frame` (every drawn frame), `snapshot` (a new snapshot arrived: ~60/s live), `world_change`
(`ev.value` = world SNO), `inventory_open`, `inventory_close`, `item_hover` (`ev.index`), `hotkey`
(not delivered yet), `setting_changed` (`ev.text` = `plugin.key`, when changed in the options page).

**Actors** (`d3hud.actors()`, `d3hud.actor(i)`) have: `id`, `x`, `y` (world units), `dist` (from the
player), `heading`, `kind` (number) and `kind_name`, `name` (the game's internal name), `hp` (0..1, **-1 when
the life is not read**; only champions, rares and bosses are), `hp_current`, `hp_max`, and for ground items
`rarity`, `ancient`, `primal`, `bonus`, `unidentified`; for pylons and shrines `used`; for champions, rares
and bosses their affixes: `affixes` (keys such as `"molten"`) and `affix_names` (`"Molten"`), plus `affix_mask`; and the raw `flags`. This
is everything the reader publishes about an actor. Other game attributes are not part of the snapshot; making one
available means reading it in the reader and extending the snapshot (see `AGENTS.md`, rule 1).

**Effects** (`d3hud.effects()`, API 2): everything that is running on the player right now, as the game keeps it:
buffs (pylons, passives, item powers, and the status a monster's affix puts on you) and powers on cooldown
(health potion, skills, and the cooldown of cheat death, which the game keeps as a buff). Each entry has `sno`
(the power's SNO: stable, match on this), `name` (readable, e.g. `"Pages Buff Infinite Casting"`; `"Power 123"` for an unknown power),
`code` (the internal name, `"Generic_PagesBuffInfiniteCasting"`; empty when unknown), `kind` (`"buff"` or `"cooldown"`), `stacks`,
`slot` (the buff's icon slot, 0..31: a power can use several, and Convention of Elements uses the slot as its active element,
1 to 7 = Arcane, Cold, Fire, Holy, Lightning, Physical, Poison),
`duration`, `remaining`, `elapsed` (seconds of game time), `fraction` (`remaining / duration`: 1 just started, 0 about to end) and
`timed` (false while the game clock is not known yet; `remaining`, `elapsed` and `fraction` are then 0). The list
is noisy on purpose: item passives refresh themselves every second or so and some entries are not meant to be
seen (`Cooldown`, `Banter Cooldown`), so filter by `sno`. Known ones: Channeling Pylon `266258` (60 s), health potion `30211`
(cooldown, 30 s), Enchantress cheat death `485530` (a 120 s buff is the cooldown; the same power also refreshes a short one),
`d3hud.frame().effect_count` is their number. The timings follow the game clock, which the reader estimates from the
ticks of the effects themselves (accurate to about 0.1 s once something has changed on the player; see
`analysis/ENGINE_MINING.md`).

```lua
for _, e in ipairs(d3hud.effects()) do
    if e.sno == 266258 and e.timed then d3hud.draw_text(20, 200, 0xFFFFFFFF, string.format("%s %.0f s", e.name, e.remaining)) end
end
```

**`d3hud.frame()`** returns `frame_index`, `time_ms`, `player_x`, `player_y`, `player_heading`,
`world_sno` (not the game's world SNO but an id of the kind of place, derived from the scenes: stable for a town, 0 while unknown), `actor_count`, `item_count`, `hovered_item` (1-based, 0 = none), `screen_w`, `screen_h`,
`rift_progress` (0..1), `rift_timer_fraction` (-1 unknown), `stash_tab`, `effect_count`, and `ui`:
`ui.inventory`, `ui.stash`, `ui.salvage`, `ui.big_map`, `ui.minimap`, `ui.rift`, `ui.greater_rift`.

**Settings:** `d3hud.setting{ key = "x", type = "bool"|"int"|"float"|"color"|"string"|"choice",
label = "...", tooltip = "...", default = ..., min = ..., max = ..., choices = {"A", "B"}, hidden = true }`.
Visible settings appear on the plugin's automatic options page; `hidden` ones are state the plugin
keeps for itself (the time tracker stores its totals this way). Values are clamped to `min`/`max` by the
host. `choice` values are 1-based. Colours are `0xRRGGBB` numbers. Everything is stored in `plugins.ini`.

**`ui` (inside a page's `draw(ui)`):** `text`, `text_disabled`, `text_colored(argb, text)`, `separator`,
`separator_text`, `same_line`, `spacing`, `button(label)`, `checkbox(label, value)`,
`slider_int(label, v, min, max)`, `slider_float(label, v, min, max [, "%.2f"])`, `combo(label, index, {items})`,
`color_edit(label, rgb)` (a label starting with `##` gives a compact swatch for a row of widgets), `input_text(label, text)`, `collapsing_header(label)`, `is_item_hovered()`,
`tooltip(text)`. Widgets that edit a value return `changed, newvalue`:

```lua
local changed, v = ui.checkbox("enabled", enabled)
if changed then enabled = v end
```

## Boxes: what you draw on the screen

Anything a plugin shows at a fixed place on the screen should be a **box**, not drawn at coordinates the plugin chose: the
user can then move it (Settings > Plugins > *Move on-screen boxes*) and change its look, and the plugin needs no position
settings of its own. (Things that belong to the world, such as a bar over a monster, are drawn at its screen position as
before.)

```lua
d3hud.box{
    id = "countdown", title = "My countdown",   -- title: what the frame shows while the user moves boxes
    x = 0.45, y = 0.24,                          -- where the top left corner is until moved, as a share of the screen
    fill = 0x101820, fill_alpha = 150,           -- optional suggested look; the user can change it
    draw = function(x, y)                        -- once per drawn frame; x, y = top left corner of the content
        d3hud.draw_text(x, y, 0xFFFFFFFF, "hello")
        return d3hud.text_size("hello", 1)       -- the room used: width, height. Nothing returned: nothing to show now
    end,
}
```

The host draws the **fill and border** behind what `draw` drew (so plugins look alike), with some padding, in colours the user
picks on the plugin's Settings page. `fill` / `border` are `0xRRGGBB` with `fill_alpha` / `border_alpha` 0..255 (defaults 130
and 160; no `fill`/`border`: the program's standard dark translucent look), `padding` in pixels. A box that returns nothing
draws nothing, but is still framed while the user moves boxes, so it can be placed even if it is empty at the moment. The
position is remembered per box. Use `draw_*` and the `draw` library in `draw`; the drawing calls land in the box's place
only if you draw relative to `x`, `y`.

## Images

```lua
local flame = d3hud.image("flame.png")      -- once, when the plugin loads; nil, message if it cannot be loaded
d3hud.box{ id = "x", title = "X", draw = function(x, y)
    d3hud.draw_image(flame, x, y, 32, 32)    -- or (flame, x, y) for its own size; a fifth argument tints it
    return 32, 32
end }
```

Put a PNG next to `main.lua` and ask for it by name. The file is read and decoded by the **host**, so a plugin never sees a
path and cannot read or write anything else: the name must be plain (`name.png`: letters, digits, `_` and `-`, no
directories, no other extension), the file at most 2 MB and 1024 x 1024 pixels, and one plugin can have 64 pictures.
Transparency works. Pictures can be drawn wherever the other `draw_*` functions can, so also inside a box. (`tools/gen_element_icons.py`
made the icons of the `convention/` plugin; there is no way to show pictures inside a panel or a settings page yet.)

## Shared libraries and how things are drawn

`plugins/lib/<name>.lua` holds libraries every plugin can use: `local draw = d3hud.require("draw")`. Only plain
names are accepted (no paths), and a library runs in the plugin's own sandbox. They are part of the plugin API:
they change only together with the API version.

**`draw`: the standard look.** Plugins pass colours as `0xRRGGBB` (like colour settings); the library adds alpha.

| | |
|---|---|
| `draw.bar(x0, y0, w, h, fraction, rgb)` | bar: black border, dark back, `rgb` as fill |
| `draw.tag(x, y, text, rgb, scale [, opts])` | a box (affixes, labels): border in `rgb`, translucent fill of the same colour, black or white text chosen by `draw.contrast` so it stays readable. `opts.alpha` is the fill opacity 0..255 (default `draw.TAG_FILL`), `opts.padding`, `opts.text` forces the text colour. Returns width, height |
| `draw.tags(x, y, items, scale, maxw [, opts])` | a row of tags, `items = { {text=, color=}, ... }`; what does not fit into `maxw` is left out; `opts.anchor = "center"`, `opts.gap` |
| `draw.tag_size(text, scale [, opts])` | width, height, padding of a tag |
| `draw.shadowed(x, y, argb, text, scale)` | text with a black drop shadow (white by default: `draw.WHITE`) |
| `draw.fit(text, maxw, scale)` | text cut with ".." to fit |
| `draw.contrast(rgb, alpha)`, `draw.luminance(rgb)`, `draw.argb(rgb, alpha)`, `draw.PANEL` | helpers; `PANEL` is the dark box behind a list |

**`fmt`:** `fmt.hms(seconds)` ("1:02:05"), `fmt.pct(0.43)` ("43%"), `fmt.pretty("x1_Fallen_Champion")` ("Fallen Champion").

**`store`:** packs small tables into the text of a hidden string setting: `store.pack_map(t [, {sep, format}])`,
`store.unpack_map(text [, {sep, number}])`, `store.pack_list(list [, sep])`, `store.unpack_list(text [, sep])`.

## Not available yet

The host table in `src/core/HostApi.h` already reserves these; this host does not provide them yet:
`world_to_minimap` / item rectangles, `draw_circle` / `draw_quad`, hot rects, plugin hotkeys, the salvage
policy hook and the `drop` event. Check `src/overlay/PluginHost.h` for the
current list.
