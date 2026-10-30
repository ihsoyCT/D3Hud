-- Health bars for champions (blue), rares and bosses:
--   * over the monsters that are on the screen, and
--   * as a list on one side of the screen, one bar under the other with the monster's name inside it.
-- Under each bar the monster's affixes (Molten, Frozen, ...) are shown as tags, for the affixes ticked on this
-- plugin's page. Everything is drawn with the shared "draw" library, so it looks like the other plugins.
--
-- The reader publishes the life of those three kinds only (trash and minions are not read), and only once
-- it has located their attribute data; until then the bar of an elite is not drawn. Options (what to show,
-- list side and size, colours) are on this plugin's Settings page.

local draw = d3hud.require("draw")
local fmt = d3hud.require("fmt")
local store = d3hud.require("store")

d3hud.setting{ key = "champion", type = "bool", default = true, label = "Champions (blue)" }
d3hud.setting{ key = "rare", type = "bool", default = true, label = "Rares (yellow)" }
d3hud.setting{ key = "boss", type = "bool", default = true, label = "Bosses" }
d3hud.setting{ key = "hide_full", type = "bool", default = false, label = "Hide bars at full health" }
d3hud.setting{ key = "hide_on_map", type = "bool", default = true, label = "Hide while the big map is open" }
d3hud.setting{ key = "color_champion", type = "color", default = 0x5082FF, label = "Champion colour" }
d3hud.setting{ key = "color_rare", type = "color", default = 0xFFD700, label = "Rare colour" }
d3hud.setting{ key = "color_boss", type = "color", default = 0xBE46FF, label = "Boss colour" }

-- affixes under the bars; the ticked ones and their colours are kept in hidden settings, edited on the page
d3hud.setting{ key = "affixes_show", type = "bool", default = true, label = "Show the chosen affixes under the bars" }
d3hud.setting{ key = "affix_scale", type = "float", default = 1.0, min = 0.5, max = 3.0, label = "Affix text size" }
d3hud.setting{ key = "affix_fill", type = "int", default = draw.TAG_FILL, min = 0, max = 255,
    label = "Affix box fill opacity", tooltip = "0 = only the border, 255 = solid. The text turns black or white to stay readable." }
local DEFAULT_AFFIXES = "desecrator,frozen,jailer,molten,mortar,plagued,fire_chains"
d3hud.setting{ key = "affix_keys", type = "string", default = DEFAULT_AFFIXES, hidden = true }
d3hud.setting{ key = "affix_colors", type = "string", default = "", hidden = true }  -- "molten=16739645,..." (changed ones)

-- over the monsters
d3hud.setting{ key = "over", type = "bool", default = true, label = "Bars over the monsters" }
d3hud.setting{ key = "width", type = "int", default = 80, min = 30, max = 300, label = "Bar width (px)" }
d3hud.setting{ key = "height", type = "int", default = 8, min = 3, max = 30, label = "Bar height (px)" }
d3hud.setting{ key = "lift", type = "float", default = 8, min = 0, max = 25, label = "Height above the ground (game units)",
    tooltip = "How far above the monster's feet the bar floats. Taller monsters want a larger value." }
d3hud.setting{ key = "percent", type = "bool", default = true, label = "Show the percentage next to the bar" }

-- the list on the side of the screen
d3hud.setting{ key = "list", type = "bool", default = true, label = "List of bars on the side of the screen" }
d3hud.setting{ key = "list_side", type = "choice", default = 1, choices = { "Left", "Right" }, label = "Side" }
d3hud.setting{ key = "list_y", type = "int", default = 25, min = 0, max = 90, label = "Distance from the top (% of the screen)" }
d3hud.setting{ key = "list_width", type = "int", default = 230, min = 100, max = 500, label = "Bar width (px)" }
d3hud.setting{ key = "list_row", type = "int", default = 20, min = 12, max = 40, label = "Bar height (px)" }
d3hud.setting{ key = "list_rows", type = "int", default = 8, min = 1, max = 20, label = "Most bars shown" }
d3hud.setting{ key = "list_sort", type = "choice", default = 1, choices = { "Nearest first", "Lowest life first", "Bosses first" },
    label = "Order" }
d3hud.setting{ key = "list_all", type = "bool", default = false, label = "Include monsters that are off the screen" }
d3hud.setting{ key = "list_names", type = "bool", default = true, label = "Name inside the bar" }
d3hud.setting{ key = "list_percent", type = "bool", default = true, label = "Percentage inside the bar" }
d3hud.setting{ key = "list_box", type = "bool", default = true, label = "Dark box behind the list" }

local KIND = d3hud.kind

-- Default colours of the affixes; the colour picker on this plugin's page overrides them (kept in affix_colors).
local COLOR_FALLBACK = 0xFFC040
local DEFAULT_COLORS = {
    molten = 0xFF6A3D, mortar = 0xFFA040, fire_chains = 0xFF5030, frozen = 0x6FD0FF, frozen_pulse = 0x9FE0FF,
    plagued = 0x9ACD32, poison = 0x7CFC00, desecrator = 0xB06CFF, jailer = 0xFF4FA3, electrified = 0x7FDFFF,
    arcane = 0xE070FF, vortex = 0xB0B0FF, waller = 0xC0C0C0, thunderstorm = 0xFFFF70, health_link = 0x70FFB0,
    teleporter = 0xA0FFFF, wormhole = 0xD0A0FF, orbiter = 0xFFD070, avenger = 0xFF8080, reflect = 0xFFFFFF,
}

local chosen = {}   -- key -> true: the affixes to show
local colors = {}   -- key -> 0xRRGGBB set by the user
local function color_of(key) return colors[key] or DEFAULT_COLORS[key] or COLOR_FALLBACK end

local function load_state()
    chosen = {}
    for _, key in ipairs(store.unpack_list(d3hud.get("affix_keys"))) do chosen[key] = true end
    colors = store.unpack_map(d3hud.get("affix_colors"), { sep = ",", number = true })
end
local function save_state()
    local keys = {}
    for _, a in ipairs(d3hud.affixes) do
        if chosen[a.key] then keys[#keys + 1] = a.key end
    end
    d3hud.set("affix_keys", store.pack_list(keys))
    d3hud.set("affix_colors", store.pack_map(colors, { sep = "," }))
end
load_state()

-- the chosen affixes this monster has, as tags { {text = "Molten", color = 0xRRGGBB}, ... }, or nil
local function affix_tags(a)
    if #a.affixes == 0 then return nil end
    local tags
    for i, key in ipairs(a.affixes) do
        if chosen[key] then
            tags = tags or {}
            tags[#tags + 1] = { text = a.affix_names[i], color = color_of(key) }
        end
    end
    return tags
end

local SORTS = {
    function(a, b) return a.dist < b.dist end,
    function(a, b) return a.hp < b.hp end,
    function(a, b)
        if a.kind ~= b.kind then return (a.kind == KIND.boss) and (b.kind ~= KIND.boss) end
        return a.dist < b.dist
    end,
}

local function draw_list(f, elites, tag_opts)
    local rows = d3hud.get("list_rows")
    local w, h = d3hud.get("list_width"), d3hud.get("list_row")
    table.sort(elites, SORTS[d3hud.get("list_sort")])
    local shown = math.min(#elites, rows)
    if shown == 0 then return end
    local gap, pad = 4, 6
    local ascale = d3hud.get("affix_scale")
    local _, tagh = draw.tag_size("Ag", ascale, tag_opts)  -- one line of affix tags under a bar
    local total = -gap
    for i = 1, shown do total = total + h + gap + (elites[i].tags and tagh + 2 or 0) end
    local x0 = d3hud.get("list_side") == 1 and 12 + pad or f.screen_w - 12 - pad - w
    local y0 = f.screen_h * d3hud.get("list_y") / 100 + pad
    if d3hud.get("list_box") then
        d3hud.draw_rect(x0 - pad, y0 - pad, x0 + w + pad, y0 + total + pad, draw.PANEL)
    end
    local scale = math.max(0.5, (h - 6) / 14)  -- text that fits the bar height (default font is about 14 px tall)
    local names, pct = d3hud.get("list_names"), d3hud.get("list_percent")
    local y = y0
    for i = 1, shown do
        local a = elites[i]
        draw.bar(x0, y, w, h, a.hp, a.color)
        local room = w - 8
        local _, th = d3hud.text_size("Ag", scale)
        if pct then
            local text = fmt.pct(a.hp)
            local tw = d3hud.text_size(text, scale)
            draw.shadowed(x0 + w - tw - 4, y + (h - th) / 2, draw.WHITE, text, scale)
            room = room - tw - 4
        end
        if names then
            draw.shadowed(x0 + 4, y + (h - th) / 2, draw.WHITE, draw.fit(fmt.pretty(a.name), room, scale), scale)
        end
        y = y + h + gap
        if a.tags then
            draw.tags(x0, y - gap + 2, a.tags, ascale, w, tag_opts)
            y = y + tagh + 2
        end
    end
end

d3hud.on("frame", function()
    local f = d3hud.frame()
    if f.actor_count == 0 or (f.ui.big_map and d3hud.get("hide_on_map")) then return end

    -- settings are read once per frame, not per actor
    local colour = {}
    if d3hud.get("champion") then colour[KIND.champion] = d3hud.get("color_champion") end
    if d3hud.get("rare") then colour[KIND.rare] = d3hud.get("color_rare") end
    if d3hud.get("boss") then colour[KIND.boss] = d3hud.get("color_boss") end
    if next(colour) == nil then return end
    local over, list = d3hud.get("over"), d3hud.get("list")
    if not over and not list then return end
    local hide_full = d3hud.get("hide_full")
    local show_affixes = d3hud.get("affixes_show")
    local tag_opts = { alpha = d3hud.get("affix_fill") }

    -- the elites that are alive and whose life is known (hp < 0: not read yet, 0: dead)
    local lift = d3hud.get("lift")
    local list_all = d3hud.get("list_all")
    local elites, listed = {}, {}
    for _, a in ipairs(d3hud.actors()) do
        local c = colour[a.kind]
        if c and a.hp > 0 and not (hide_full and a.hp >= 0.999) then
            a.color = c
            a.sx, a.sy = d3hud.world_to_screen(a.x, a.y, lift)  -- nil when off the screen
            if show_affixes then a.tags = affix_tags(a) end
            elites[#elites + 1] = a
            if a.sx or list_all then listed[#listed + 1] = a end
        end
    end
    if #elites == 0 then return end

    if over then
        local w, h = d3hud.get("width"), d3hud.get("height")
        local percent = d3hud.get("percent")
        local ascale = d3hud.get("affix_scale")
        local centred = { alpha = tag_opts.alpha, anchor = "center" }
        for _, a in ipairs(elites) do
            if a.sx then
                local x0, y0 = a.sx - w / 2, a.sy - h / 2
                draw.bar(x0, y0, w, h, a.hp, a.color)
                if percent then draw.shadowed(x0 + w + 4, y0 - 2, draw.WHITE, fmt.pct(a.hp), 0.8) end
                if a.tags then draw.tags(a.sx, y0 + h + 3, a.tags, ascale, math.max(w, 200), centred) end  -- under the bar
            end
        end
    end
    if list then draw_list(f, listed, tag_opts) end
end)

-- the page: one row per affix of the game: show it under the bars? and in which colour
d3hud.page{
    id = "affixes", title = "Health bars", group = "Plugins", order = 1100,
    draw = function(ui)
        ui.separator_text("Affixes under the health bars")
        ui.text_disabled("Tick the affixes to show under the bar of every monster that has them, and pick their colours.")
        if ui.button("all") then
            for _, a in ipairs(d3hud.affixes) do chosen[a.key] = true end
            save_state()
        end
        ui.same_line()
        if ui.button("none") then
            chosen = {}
            save_state()
        end
        ui.same_line()
        if ui.button("the dangerous ones") then
            d3hud.set("affix_keys", DEFAULT_AFFIXES)
            load_state()
        end
        ui.same_line()
        if ui.button("default colours") then
            colors = {}
            save_state()
        end
        for _, a in ipairs(d3hud.affixes) do
            local changed, value = ui.checkbox(a.name, chosen[a.key] or false)
            if changed then
                chosen[a.key] = value or nil
                save_state()
            end
            ui.same_line()
            local picked, rgb = ui.color_edit("##color_" .. a.key, color_of(a.key))
            if picked then
                colors[a.key] = rgb
                save_state()
            end
        end
    end,
}
