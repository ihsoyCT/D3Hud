# Writing a plugin

A plugin is a folder in `plugins/` next to `D3Hud.Overlay.exe`:

```
plugins/myplugin/
  plugin.lua   return { name = "myplugin", version = "1.0", api = 3 }
  main.lua     the code
  flame.png    optional pictures
```

`name` is letters, digits, `-` and `_`. `api` is the interface version the plugin was written for; use the highest of the
functions you call (`effects()` needs 2, `box{}` 3, pictures 4). The overlay loads every folder at start; **restart the overlay
to reload**.

**The full interface is in [`PLUGIN.md`](../PLUGIN.md)**: what a plugin can read, every function, events, boxes, panels, settings,
the shared libraries, limits, and the C API underneath. This page is the quick start.

## A first plugin

A countdown to the Channeling Pylon running out, in a box the user can move:

```lua
-- plugins/pylon/plugin.lua
return { name = "pylon", version = "1.0", api = 3 }
```

```lua
-- plugins/pylon/main.lua
local PYLON = 266258   -- the power of the Channeling Pylon; effects are matched by their sno

d3hud.box{
    id = "timer", title = "Channeling Pylon", x = 0.45, y = 0.2,
    draw = function(x, y)
        for _, e in ipairs(d3hud.effects()) do
            if e.sno == PYLON and e.timed then
                local text = string.format("%s: %.0f s", e.name, e.remaining)
                d3hud.draw_text(x, y, 0xFFFFFFFF, text)
                return d3hud.text_size(text, 1)   -- the room used; the host draws the fill and border around it
            end
        end                                      -- nothing returned: nothing to show right now
    end,
}
```

Restart the overlay. With a pylon running, the text is on the screen; **Settings > Plugins > Move on-screen boxes** lets you drag
it and change its colours. (That plugin uses `effects()` and `box{}`, so a real manifest would say `api = 3`.)

## The plugins that come with the program

Read them as examples: they are complete and tested.

| Folder | Shows | Uses |
|---|---|---|
| `timetracker/` | where your play time goes, per area | settings (hidden state), a page, a dock panel, a box |
| `healthbars/` | life bars over elites, a list with their affixes | `actors()`, `world_to_screen`, the `draw` library, a box, a page of its own |
| `damagemeter/` | DPS, fights, time to kill | measuring from `actors()` over time, a dock panel, a box |
| `effects/` | buffs and cooldowns with their time left | `effects()`, a dock panel, a box with bars, choice settings |
| `convention/` | seconds until the Convention of Elements element you want | learning a cycle from `effect.slot`, hidden settings as memory, pictures, a box |

## Good habits

- **Draw in a box**, not at coordinates of your own: the user moves it and changes its look, and you need no position settings.
- Call `d3hud.actors()` at most once per frame and keep the table; it copies every actor.
- Match effects and actors by `sno` or `kind`, not by their (readable) names.
- Keep the memory of a plugin in `hidden` settings (see `store` in `PLUGIN.md`); a plugin cannot write files.
- Do the per-snapshot work in a `snapshot` handler and only drawing in a box or `frame`.
- Check `if d3hud.effects then` or `if d3hud.image then` when a plugin should also run on an older program.

## Seeing what a plugin does

The **Plugins** page in Settings lists every plugin with an *enabled* checkbox, its errors, its log lines (`d3hud.log`) and what it
costs: the microseconds spent in its code per drawn frame (average and worst frame, refreshed twice a second; event handlers and
page, panel and box drawing included), so a slow plugin is easy to spot. After 5 errors a plugin is switched off until the next
start, or until you tick *enabled* again.

`D3Hud.Overlay.exe --settings-page timetracker.timetracker` opens Settings on one page (ids are `<plugin>.<page id>`, and
`<plugin>.settings` for the automatic options page).

## Testing a plugin

The unit tests (`src/tests/test_core.cpp`) load each bundled plugin against a **fake host**, feed it actors, effects and frames, and
read back what it draws and what its panel says; look at `TestEffectsPlugin` or `TestConventionPlugin` and copy the pattern. Run
them with `ctest --test-dir build -C Release --output-on-failure`.
