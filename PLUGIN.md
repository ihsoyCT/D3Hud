# The plugin interface

This is the reference for everything a plugin can see and do. For a first plugin read `plugins/README.md`; for the
reasons behind the design see `PLUGIN_PLAN.md`; for the rules when changing the interface see `AGENTS.md` (rules 10 to 13).

- [How a plugin is loaded](#how-a-plugin-is-loaded)
- [What a plugin cannot do](#what-a-plugin-cannot-do)
- [Conventions](#conventions)
- [Reading the game](#reading-the-game): `frame`, `actors`, `item`, `effects`, `world_to_screen`, constants
- [Events](#events)
- [Drawing on the screen](#drawing-on-the-screen): text, rectangles, lines, pictures
- [Boxes](#boxes): what the user can move
- [Panels, pages and widgets](#panels-pages-and-widgets)
- [Settings](#settings)
- [Shared libraries](#shared-libraries): `draw`, `fmt`, `store`
- [The C host API](#the-c-host-api) and the [version history](#api-versions)

## How a plugin is loaded

```
plugins/myplugin/
  plugin.lua   return { name = "myplugin", version = "1.0", api = 3 }
  main.lua     the code
  flame.png    optional pictures (see Pictures)
```

- `plugin.lua` is a table and is run with **no globals at all**. `name` is letters, digits, `-` and `_`; `api` is the API
  version the plugin was written for. The host refuses a plugin that asks for a newer one than it has, so declare the
  highest version of any function you use (see [API versions](#api-versions)); a plugin written for an older version keeps
  working.
- Every folder of `plugins/` with a `plugin.lua` is loaded at start, in alphabetical order, one Lua state each. There is **no
  reload**: restart the overlay after changing a plugin.
- `main.lua` runs once, at load. This is where a plugin registers everything it has: settings, event handlers, boxes, panels,
  pages. After that the host calls the registered functions.
- An error inside a call is logged with the plugin's name (Settings > Plugins). After **5 errors** the plugin is switched off
  until the next start.
- The user can switch a plugin off and on at **Settings > Plugins** (an *enabled* checkbox): off, none of its code runs and its
  dock tabs and boxes disappear; on again clears the error count. The choice is remembered (`plugins.disabled` in `plugins.ini`).
- Everything runs on the overlay's UI thread, so a plugin cannot be preempted, and also cannot block: keep calls short. The
  Plugins page shows what each plugin costs per drawn frame.

## What a plugin cannot do

A plugin is meant to read and to draw, never to play. It has `string`, `table`, `math`, `utf8`, `coroutine` and the basic
functions (`pairs`, `ipairs`, `pcall`, `select`, `tostring`, `tonumber`, `type`, `error`, `assert`, `setmetatable`, ...) and
**no** `io`, `os`, `debug`, `package`, `load`, `dofile`, `print`, `collectgarbage`, `string.dump`; text chunks only.

| Limit | Value |
|---|---|
| Lua memory per plugin | 32 MB |
| Lua instructions per call from the host | 2 million (a longer call is stopped and counts as an error) |
| Errors before the plugin is switched off | 5 |
| Pictures | `name.png` in the plugin's own folder, 2 MB, 1024 x 1024 px, 64 per plugin |

There is no way to read game memory, press a key, click, write a file or reach the network. The only file access is
`d3hud.image`, and the **host** does it: a plugin names a picture and never sees a path. A plugin also cannot weaken the salvage
rules (a salvage policy hook, when it exists, can only make decisions more careful).

## Conventions

- **Screen coordinates** are pixels of the overlay window, which covers the game window: (0, 0) is the top left, x grows right,
  y grows down. `frame().screen_w` / `screen_h` are its size.
- **World coordinates** are the game's ground plane in world units; `x` and `y` of an actor are the two ground axes.
- **Colours** in the drawing functions are `0xAARRGGBB` numbers. Colours in settings, in the `draw` library and in a box's
  `fill` / `border` are `0xRRGGBB` (the library or the host adds the opacity).
- **Lists and indexes** are 1-based: `d3hud.actor(1)` is the first actor, `choice` settings count from 1, and the index in an
  event is 1-based with 0 meaning "none".
- **Times** are seconds; for effects they are seconds of *game* time.
- Tables you receive are fresh copies. Changing them does nothing to the game or to later calls.

## Reading the game

All of this is as of the **current snapshot**: the reader publishes about 60 per second, and the host hands the same one to
every plugin during a frame.

### `d3hud.frame()`

A table with the current facts.

| Field | |
|---|---|
| `frame_index`, `time_ms` | the snapshot's number and time (ms, the reader's clock) |
| `player_x`, `player_y`, `player_heading` | the player's position (world units) and heading (radians, 0 = east, counter-clockwise) |
| `world_sno` | an id of the *kind of place* (derived from the scenes: stable for a town), 0 while unknown; not the game's world SNO |
| `actor_count`, `item_count`, `effect_count` | how many `actors()` / items / `effects()` there are |
| `hovered_item` | index of the item whose game tooltip is open (1-based), 0 = none |
| `screen_w`, `screen_h` | size of the overlay in pixels |
| `rift_progress` | 0..1, valid while `ui.rift` |
| `rift_timer_fraction` | greater rift: elapsed share of the 15 minutes, -1 = unknown |
| `stash_tab` | the selected stash tab (page * 5 + tab), -1 = unknown |
| `ui.inventory`, `ui.stash`, `ui.salvage`, `ui.big_map`, `ui.minimap`, `ui.rift`, `ui.greater_rift` | booleans: which game windows are open |

### Actors: `d3hud.actors()`, `d3hud.actor(i)`

`actors()` returns **all** actors of the snapshot (monsters, pylons, shrines, chests, globes, portals, ground items, rift orbs, ground effects)
as an array; `actor(i)` returns one or `nil`. Each has:

| Field | |
|---|---|
| `id` | the actor's id (stable while it lives) |
| `x`, `y`, `dist`, `heading` | position in world units, distance from the player, heading in radians |
| `kind`, `kind_name` | the kind as a number and as text; the numbers are in `d3hud.kind` |
| `name` | the game's internal name, e.g. `x1_Fallen_Champion` (for ground items: the item's display name). **Match on this.** |
| `display_name` | what the game calls it, in English (`"Risen"`; a rare: its generated name, `"Kauza, Torturer of Souls"`), from `names_en.tsv` and `rare_names.tsv` (API 11); the internal name when the game has no English name for it. Show this to people |
| `type_name` | the kind of monster in English also for a rare (`"Returned Archer"`), same fallback (API 11) |
| `hp` | life as 0..1, **-1 when the life is not read** (only champions, rares and bosses are) |
| `hp_current`, `hp_max` | life in game units (0 when not read) |
| `affix_mask`, `affixes`, `affix_names` | champions, rares, bosses: the monster affixes. `affixes` are keys (`"molten"`), `affix_names` as the game shows them (`"Molten"`), both in the order of `d3hud.affixes` |
| `rarity` | ground items: 0 common, 1 magic, 2 rare, 3 legendary, 4 set, 5 unknown |
| `ancient`, `primal`, `bonus`, `unidentified` | ground items: booleans |
| `used` | pylons and shrines: already clicked |
| `flags` | the raw bits: 1 ancient, 2 primal, 4 bonus, 8 unidentified, 16 used |

Dead monsters are not in the list. `actors()` copies every actor into a table: call it **once per frame** and keep the result.

### Items: `d3hud.item(i)`

The `i`-th (1-based) of `frame().item_count` items, or `nil`. They are the backpack, the equipped items, the open stash tab,
and legendary, set and bonus items lying on the ground.

| Field | |
|---|---|
| `id`, `sno` | the item's id, its SNO |
| `name`, `type_name` | display name (filled for unidentified items too: the reveal) and type (`"Ring"`) |
| `rarity` | as for actors |
| `location` | 0 backpack, 1 head, 2 torso, 3 off hand, 4 main hand, 5 hands, 6 waist, 7 feet, 8 shoulders, 9 legs, 10 bracers, 11/12 rings, 13 neck, 15 stash, 254 on the ground, 255 unknown |
| `grid_x`, `grid_y`, `height` | cell of the backpack or stash (stash: `grid_y` counts tabs * 10 + row) and height in cells |
| `verdict` | what the built-in salvage rules say: 0 unknown, 1 junk, 2 keep, 3 check |
| `augment_rank` | Caldesann's Despair gem rank (0 = none) |
| `stack` | stack size of gems and crafting materials (API 8), 0 for everything else |
| `flags` | as for actors |

### Wallet: `d3hud.wallet()` (API 6)

Gold, blood shards and the crafting materials: the numbers of the game's material panel, as whole numbers in a table, or `nil`
while the reader has not found them (no character in the world yet). Keys: `gold`, `blood_shards`, `reusable_parts`,
`arcane_dust`, `veiled_crystal`, `deaths_breath`, `forgotten_soul`, `khanduran_rune`, `caldeum_nightshade`,
`arreat_war_tapestry`, `corrupted_angel_flesh`, `westmarch_holy_water`, `primordial_ashes`, `greater_rift_keystone` and the Hellfire materials `vial_of_putridness`, `idol_of_terror`, `heart_of_fright`, `leorics_regret` (which of the first three is which is a guess: all three read 1). The numbers are read about twice a
second. Gem stacks in the backpack are not part of it (they are item stacks).

### Player: `d3hud.player()` (API 7)

The character's own numbers, from its attribute map (read ten times a second), or `nil` while the reader has not read them.

| Field | |
|---|---|
| `class`, `class_id` | `"Demon Hunter"` and 3 (1 Barbarian, 2 Crusader, 3 Demon Hunter, 4 Monk, 5 Necromancer, 6 Witch Doctor, 7 Wizard; 0 unknown) |
| `level`, `paragon`, `paragon_points` | level (70 at the cap), paragon level, unspent paragon points |
| `life`, `life_max`, `life_regen` | game units (regeneration per second) |
| `resources` | one `{ type, name, current, max, regen }` per resource the class has. Names are known for the Demon Hunter's `Hatred` (5) and `Discipline` (6); other ids read `"Resource <n>"` until they are seen live |
| `xp_to_next`, `paragon_xp_to_next` | experience still **missing** for the next (paragon) level: it falls while you play and jumps up at a level-up, so count gains as the drops |
| `strength`, `dexterity`, `intelligence`, `vitality` | the totals |
| `armor`, `resist_all`, `resist` | `resist` is `{ physical, fire, lightning, cold, poison, arcane }` |
| `attack_speed`, `move_speed` | multipliers: `move_speed` 1.65 is 65% faster |
| `cooldown_reduction` | a fraction: 0.32 is 32% |
| `gold_find`, `magic_find` | percent |
| `highest_solo_rift`, `highest_unlocked_rift`, `last_rift_level` | the account's rift records |
| `deaths` | how often the character has died (API 9): take differences, e.g. at the start and end of a rift |

### Skills: `d3hud.skills()` (API 7)

The skills the character has equipped, actives and passives, ordered by power (**not** by action bar slot: the slot is not an
attribute and the reader does not know it yet). `frame().skill_count` is their number.

| Field | |
|---|---|
| `sno`, `name`, `code` | the power, as for effects |
| `passive` | a passive skill |
| `rune`, `rune_letter` | 0 / `""` for no rune, else 1..5 / `"A"`..`"E"` |
| `on_cooldown`, `cooldown_duration`, `cooldown_remaining`, `timed` | seconds of game time; `timed` is false while the game clock is not known |

### Passives: `d3hud.passives()` (API 10)

The powers that are on the character without a timer: passive skills, legendary item and gem powers, set bonuses, event
buffs. The game keeps them as buffs with a stack count and no end, so they never appear in `d3hud.effects()`. Each is
`{ sno, name, code, slot, stacks }`, once per icon slot (a power can hold several; Squirt's Necklace, power `483552`, keeps its
10 stacks in slot 5). `frame().passive_count` is their number. Use it to see which legendary powers are active and to measure the
uptime of a stack that has no timer.

### Health: `d3hud.health()` (API 7)

How well the reader sees the game. For each source one of `"unknown"`, `"ok"`, `"degraded"` (found, but part of it does not
resolve: a table the game rebuilt, attributes still being located) or `"missing"`: `actors`, `records`, `attributes`,
`inventory`, `player`, `ui`, `scenes`, `wallet`. `ok` is true when none is degraded or missing. Also `actors_seen`,
`actors_dropped` (more actors than the snapshot holds, 256), `items_dropped`, `monsters_unresolved`, `items_unresolved`,
`ui_missing` (dialogs that were never opened do not exist yet, so some missing controls are normal) and `ticks_per_second`
(the reader loop). Use it to tell "nothing there" from "the reader cannot see".

### Effects: `d3hud.effects()` (API 2)

Everything running on the player right now, as the game keeps it: **buffs** (pylons and shrines, passives, item powers, and what
a monster's affix puts on you) and powers on **cooldown** (health potion, skills). The cooldown of cheat death is a buff.

| Field | |
|---|---|
| `sno` | the power's SNO: **stable, match on this** |
| `name` | readable (`"Pages Buff Infinite Casting"`); `"Power 123"` for a power the host has no name for |
| `code` | the internal name (`"Generic_PagesBuffInfiniteCasting"`), empty when unknown |
| `kind` | `"buff"` or `"cooldown"` |
| `stacks` | stack count (buffs), 1 for cooldowns |
| `slot` | the buff's icon slot, 0..31. A power can use several; Convention of Elements uses the slot as its active element (1 to 7 = Arcane, Cold, Fire, Holy, Lightning, Physical, Poison) |
| `duration`, `remaining`, `elapsed` | seconds of game time; `fraction` = `remaining / duration` (1 just started, 0 about to end) |
| `timed` | false while the game clock is not known yet: `remaining`, `elapsed` and `fraction` are then 0 |

Slots of one power with exactly the same start and end are one effect. The list is noisy on purpose: item passives renew a short
buff every second, and the game keeps some bookkeeping buffs (`Cooldown`, `Banter Cooldown`), so **filter by `sno`**. Examples:
Channeling Pylon `266258` (60 s), health potion `30211` (cooldown, 30 s), Enchantress cheat death `485530` (its 120 s buff is the
cooldown; the same power also renews a short one). The game clock is estimated from the effects' own ticks and is right to about
0.1 s once something on the player has changed.

### Other

| | |
|---|---|
| `d3hud.world_to_screen(x, y [, height])` | screen pixels `sx, sy` of a world position (`height` above the ground), or `nil` when it is off the screen |
| `d3hud.kind` | the actor kind numbers by name: `player`, `trash`, `champion`, `rare`, `minion`, `boss`, `pylon`, `shrine`, `chest`, `globe`, `ground_item`, `portal`, `pylon_spawn`, `rift_orb`, `rift_orb_tiered`, `ground_effect` (API 5) |
| `d3hud.affixes` | the game's 30 monster affixes: `{ {key = "molten", name = "Molten"}, ... }` |
| `d3hud.api` | the host's API version |
| `d3hud.log(message [, "info"\|"warn"\|"error"])` | a line in the plugin's log on the Plugins page and on the console |

## Events

`d3hud.on(event, fn)` subscribes; a plugin can subscribe to the same event more than once. `fn(ev)` gets a table with `type`
(the event's name), `index` (1-based, 0 = none), `value` and `text`. An unknown event name is an error.

| Event | When | Data |
|---|---|---|
| `frame` | every drawn frame, before the panels | drawing is allowed here |
| `snapshot` | a new snapshot arrived (about 60 per second live) | |
| `world_change` | the kind of place changed | `value` = the new `world_sno` |
| `inventory_open`, `inventory_close` | the game's inventory opened or closed | |
| `item_hover` | the item under the cursor changed | `index` = the item, 0 when the hover ended |
| `setting_changed` | the user changed an option on the plugin's page | `text` = `plugin.key` |
| `drop`, `hotkey` | **named but never delivered yet** | |

`frame` runs once per drawn frame, `snapshot` once per snapshot. Put work
that depends on the game's state in `snapshot`, and on-screen drawing in a [box](#boxes).

## Drawing on the screen

These draw on the overlay's top layer, in call order (a later call is on top). They work only **while the host is running one
of your callbacks that draws**: a `frame` handler or a box's `draw`. Everything is in screen pixels.

| | |
|---|---|
| `d3hud.draw_text(x, y, argb, text [, scale])` | text with its top left corner at x, y; `scale` 1.0 is the UI's text size |
| `d3hud.text_size(text [, scale])` | `width, height` of that text in pixels |
| `d3hud.draw_rect(x0, y0, x1, y1, argb [, filled = true [, thickness]])` | a rectangle (outline or filled) |
| `d3hud.draw_line(x0, y0, x1, y1, argb [, thickness])` | a line |
| `d3hud.image(name)` | a picture, see below (API 4) |
| `d3hud.draw_image(img, x, y [, w, h [, argb tint]])` | draws it; `w`, `h` default to its own size; `tint` white = as it is (API 4) |

### Pictures (API 4)

```lua
local flame = d3hud.image("flame.png")   -- once, when the plugin loads. nil, message when it cannot be loaded
d3hud.draw_image(flame, x, y, 32, 32)
```

`d3hud.image` returns `{ id, w, h }` (its size in pixels). The host reads and decodes the PNG from **the plugin's own folder**:
the name must be plain (`name.png`: letters, digits, `_` and `-`, no directories, no other extension), the file at most 2 MB and
1024 x 1024 pixels, and a plugin can have 64. Transparency works. Loading the same name twice gives the same picture. A host
older than API 4 has no `d3hud.image`; check `if d3hud.image then` to stay compatible with it. There is no picture widget for
panels and settings pages yet.

## Boxes

Anything a plugin draws at a **fixed place on the screen** belongs in a box (API 3): the user can move it and change how it
looks, and the plugin needs no position setting of its own. What belongs to the world (a bar over a monster) is drawn at the
monster's screen position as before, without a box.

```lua
d3hud.box{
    id = "countdown", title = "My countdown",   -- the title is what the frame says while the user moves boxes
    x = 0.45, y = 0.24,                          -- the top left corner until moved, as a share of the screen
    fill = 0x101820, fill_alpha = 150,           -- optional suggested look
    border = 0x6080A0, border_alpha = 200, padding = 8,
    draw = function(x, y)                        -- once per drawn frame: x, y = top left corner of the content
        d3hud.draw_text(x, y, 0xFFFFFFFF, "hello")
        return d3hud.text_size("hello", 1)       -- the room the content uses: width, height
    end,
}
```

- `draw(x, y)` is called every frame the plugin is on. It draws relative to `x`, `y` and **returns the width and height it
  used**. Returning nothing means "nothing to show right now": nothing is drawn, no background either.
- The **host draws the fill and the border** behind what `draw` drew, with padding. `fill` and `border` are `0xRRGGBB` with
  `fill_alpha` and `border_alpha` 0..255 (defaults 130 and 160); leave them out for the program's standard dark translucent look.
  The user can change colour, opacity, border width and padding on the plugin's page (a fully transparent fill and border hide
  the background) and go back to the default.
- Settings > Plugins > **Move on-screen boxes** (also *Move on the screen* on the plugin's page) puts every box into a frame with
  its plugin and title, **even a box with nothing to show**, so the user can drag it; **Done** ends it, **Reset positions** puts
  every box back. The position is stored as a share of the screen, so it survives another resolution, and a box always stays
  fully on the screen. The clicks work without F8.
- `id` must be unique inside the plugin. Boxes of a switched-off plugin are not drawn.

## Panels, pages and widgets

Both draw with the same small set of widgets and are called only while they are on screen.

- `d3hud.panel{ id, title, width, height, draw = function(ui) ... end }` is a **dock panel**: a tab in the dock, expanding when
  hovered, pinned with F8 + click, draggable. `width` and `height` greater than 0 make it resizable. The host remembers its
  place and offers *show in dock*, *keep open* and *reset position* on the plugin's page. Every panel also gets a row in Settings > Hotkeys ("Toggle panel: ...", unbound until the user picks a key) that pins or unpins it; the plugin does nothing for this. Boxes (`d3hud.box`) are drawn below the panels and settings windows.
- `d3hud.page{ id, title, group, order, draw = function(ui) ... end }` is a **page of the Settings window** for what the schema of
  [settings](#settings) cannot express. The plugin's *first* page also shows its options, its dock panel controls and its box
  controls. A plugin that registers no page but has options, a panel or a box gets an automatic one. Open one directly with
  `D3Hud.Overlay.exe --settings-page <plugin>.<page id>`; the automatic one is `<plugin>.settings`.

`ui` is the widget set (a part of Dear ImGui). Widgets that change a value return `changed, newvalue`.

| | |
|---|---|
| `ui.text(s)`, `ui.text_disabled(s)`, `ui.text_colored(argb, s)` | text |
| `ui.separator()`, `ui.separator_text(label)`, `ui.same_line()`, `ui.spacing()` | layout |
| `ui.button(label)` | true when clicked |
| `ui.checkbox(label, value)` | `changed, value` |
| `ui.slider_int(label, v, min, max)`, `ui.slider_float(label, v, min, max [, "%.2f"])` | `changed, value` (the format is one of `%.0f`, `%.1f`, `%.2f`, `%.3f`) |
| `ui.combo(label, index, {items})` | `changed, index` (1-based) |
| `ui.color_edit(label, rgb)` | `changed, rgb`; a label starting with `##` gives a compact swatch |
| `ui.input_text(label, text)` | `changed, text` |
| `ui.collapsing_header(label)` | true while open |
| `ui.is_item_hovered()`, `ui.tooltip(text)` | for the last widget |

Widget labels must be unique inside one callback (add `##something` to hide the part after it). `text_colored` takes `0xAARRGGBB`.

## Settings

```lua
d3hud.setting{ key = "scale", type = "float", default = 1.5, min = 0.5, max = 4, label = "Size", tooltip = "..." }
local scale = d3hud.get("scale")
d3hud.set("scale", 2)
```

`d3hud.setting{...}` declares an option (call it at load) with `key` (unique in the plugin), `type`, `label`, `tooltip`, `default`,
`min`, `max`, `choices` and `hidden`. Visible settings appear on the plugin's automatic options page; the host draws the control.

| `type` | `get` returns | |
|---|---|---|
| `"bool"` | boolean | |
| `"int"`, `"float"` | number | clamped to `min`..`max`; a slider when `max > min` |
| `"color"` | integer `0xRRGGBB` | a colour picker |
| `"string"` | string | a text field (up to 4096 characters are read back) |
| `"choice"` | integer, **1-based** | `choices = { "A", "B" }`, a drop-down |

- `d3hud.get(key)` and `d3hud.set(key, value)` raise an error for a key the plugin has not declared.
- `hidden = true` keeps the setting out of the page: it is how a plugin **stores its own state** (the time tracker keeps its totals
  in one, the Convention plugin its learned order). A plugin cannot write files, so settings are the only memory it has across
  runs; `store` below packs small tables into a string.
- Everything is saved in `plugins.ini` as `<plugin>.<key>` (about every 5 seconds when something changed, and on exit). A
  value found in the file before the plugin declares its key is kept and applied (within the limits).
- Changing an option on the page fires `setting_changed`.

## Shared libraries

`local draw = d3hud.require("draw")` loads `plugins/lib/<name>.lua` into the plugin's own sandbox, once per plugin. Only plain names
are accepted (no paths). The libraries are part of the interface: they change only together with the API version, and new
plugins should use them instead of their own helpers.

**`draw`**: the standard look. Colours are `0xRRGGBB`; the library adds the opacity.

| | |
|---|---|
| `draw.bar(x0, y0, w, h, fraction, rgb)` | a bar: black border, dark back, `rgb` as fill |
| `draw.tag(x, y, text, rgb, scale [, opts])` | a label box: border in `rgb`, translucent fill of the same colour, black or white text chosen by `draw.contrast`. `opts.alpha` (fill 0..255, default `draw.TAG_FILL`), `opts.padding`, `opts.text` forces the text colour. Returns width, height |
| `draw.tags(x, y, items, scale, maxw [, opts])` | a row of tags, `items = { {text=, color=}, ... }`; what does not fit into `maxw` is left out; `opts.anchor = "center"`, `opts.gap` |
| `draw.tag_size(text, scale [, opts])` | width, height and padding of a tag |
| `draw.shadowed(x, y, argb, text, scale)` | text with a black drop shadow |
| `draw.fit(text, maxw, scale)` | text cut with `..` to fit |
| `draw.contrast(rgb, alpha)`, `draw.luminance(rgb)`, `draw.argb(rgb, alpha)` | colour helpers |
| `draw.WHITE`, `draw.BLACK`, `draw.PANEL`, `draw.TAG_FILL` | constants (`PANEL` is the dark box behind a list) |

**`fmt`**: `fmt.hms(seconds)` ("1:02:05"), `fmt.big(n)` (1234567 gives "1.23M"), `fmt.dur(seconds)` (3.14 gives "3.1s", 75 gives
"1:15"), `fmt.pct(0.43)` ("43%"), `fmt.pretty("x1_Fallen_Champion")` ("Fallen Champion").

**`store`**: `store.pack_list(list [, sep])`, `store.unpack_list(text [, sep])`, `store.pack_map(map [, {sep, format}])`,
`store.unpack_map(text [, {sep, number}])`. For packing a small table into the text of a hidden string setting; names must not
contain the separator or `=`.

## The C host API

Lua is one consumer of a plain C interface, `src/core/HostApi.h`; a native plugin (a DLL) would be another, but **there is no DLL
loader yet**. The header must keep compiling as C. Rules that keep old plugins working:

- Every struct starts with a `size` field. New fields go at the **end**; read `size` before using one.
- The host table `d3h_host` starts with its own `size` and `api_version`. Test for a function with
  `D3H_HOST_HAS(host, field)` (the table is long enough to contain it) and a non-null pointer.
- `D3H_API_VERSION` only grows. The snapshot structs are never exposed: data is copied into the API structs.
- Strings are UTF-8 and NUL-terminated; a pointer handed to a callback is valid only during it. Everything is called on the UI
  thread.

| Group | Functions of `d3h_host` | Since |
|---|---|---|
| diagnostics | `log` | 1 |
| data | `get_frame`, `get_actor`, `get_item`, `world_to_screen`, `world_to_minimap`\*, `item_screen_rect`\* | 1 |
| events | `subscribe(event, fn, user)` | 1 |
| drawing | `draw_line`, `draw_rect`, `draw_text`, `text_size`, `draw_circle`\*, `draw_quad`\* | 1 |
| UI | `register_panel`, `register_page`, `hot_rect`\* | 1 |
| settings | `define_setting`, `get_bool`, `get_int`, `get_float`, `get_color`, `get_string`, `set_value`, `set_string` | 1 |
| hotkeys, policy | `register_hotkey`\*, `set_salvage_policy`\* | 1 |
| effects | `get_effect` | 2 |
| boxes | `register_box` | 3 |
| images | `load_image`, `draw_image` | 4 |

\* In the header but **not provided by the overlay yet** (the table entry is null): check before use. The `drop` event is the
same.

The structs are `d3h_frame`, `d3h_actor`, `d3h_item`, `d3h_effect`, `d3h_passive`, `d3h_player`, `d3h_skill`, `d3h_health`, `d3h_image`, `d3h_event`, `d3h_setting_desc`,
`d3h_panel_desc`, `d3h_page_desc`, `d3h_box_desc` and the widget table `d3h_ui`. The Lua tables above are these structs with
friendlier names.

## API versions

| Version | Adds |
|---|---|
| 1 | frame, actors, items, events, drawing, dock panels, pages, settings, libraries |
| 2 | `d3hud.effects()`, `frame().effect_count` |
| 3 | `d3hud.box{}`: movable boxes with a host-drawn fill and border |
| 4 | `d3hud.image()`, `d3hud.draw_image()` |
| 5 | actor kind `ground_effect` (`d3hud.kind.ground_effect`): the circles the Oculus Ring and the Altar of Rites leave on the ground, and the areas elite affixes leave (the `name` says which) |
| 6 | `d3hud.wallet()`: gold, blood shards and crafting materials |
| 7 | `d3hud.player()`, `d3hud.skills()`, `d3hud.health()`, `frame().skill_count` |
| 8 | `item.stack` |
| 9 | `player().deaths` |
| 10 | `d3hud.passives()`, `frame().passive_count` |
| 11 | `actor.display_name` |

Changing the interface means: the new entry in `HostApi.h` (at the end, a new version number), its implementation in
`src/overlay/PluginHost.cpp`, the Lua binding in `src/plugins/LuaPlugin.cpp` (registered only when the host has the function), a
test against the fake host in `src/tests/test_core.cpp`, and this file.
