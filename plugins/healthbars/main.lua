-- Health bars for champions (blue), rares and bosses:
--   * over the monsters that are on the screen, and
--   * as a list on one side of the screen, one bar under the other with the monster's name inside it.
--
-- The reader publishes the life of those three kinds only (trash and minions are not read), and only once
-- it has located their attribute data; until then the bar of an elite is not drawn. Options (what to show,
-- list side and size, colours) are on this plugin's Settings page.

d3hud.setting{ key = "champion", type = "bool", default = true, label = "Champions (blue)" }
d3hud.setting{ key = "rare", type = "bool", default = true, label = "Rares (yellow)" }
d3hud.setting{ key = "boss", type = "bool", default = true, label = "Bosses" }
d3hud.setting{ key = "hide_full", type = "bool", default = false, label = "Hide bars at full health" }
d3hud.setting{ key = "hide_on_map", type = "bool", default = true, label = "Hide while the big map is open" }
d3hud.setting{ key = "color_champion", type = "color", default = 0x5082FF, label = "Champion colour" }
d3hud.setting{ key = "color_rare", type = "color", default = 0xFFD700, label = "Rare colour" }
d3hud.setting{ key = "color_boss", type = "color", default = 0xBE46FF, label = "Boss colour" }

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
local DARK = 0xC0101010
local BORDER = 0xFF000000
local WHITE = 0xFFFFFFFF
local SHADOW = 0xFF000000

-- "x1_Skeleton_Archer" -> "Skeleton Archer"; "FallenHound_Rare" stays readable too
local function pretty(name)
    name = string.gsub(name, "^%l%d+_", "")
    name = string.gsub(name, "_", " ")
    return (string.gsub(name, "(%l)(%u)", "%1 %2"))
end

-- shortens text so it is at most `maxw` pixels wide
local function fit(text, maxw, scale)
    if d3hud.text_size(text, scale) <= maxw then return text end
    while #text > 1 do
        text = string.sub(text, 1, -2)
        if d3hud.text_size(text .. "..", scale) <= maxw then return text .. ".." end
    end
    return text
end

local function shadowed(x, y, color, text, scale)
    d3hud.draw_text(x + 1, y + 1, SHADOW, text, scale)
    d3hud.draw_text(x, y, color, text, scale)
end

local function bar(x0, y0, w, h, hp, color)
    d3hud.draw_rect(x0 - 1, y0 - 1, x0 + w + 1, y0 + h + 1, BORDER)
    d3hud.draw_rect(x0, y0, x0 + w, y0 + h, DARK)
    d3hud.draw_rect(x0, y0, x0 + w * math.min(hp, 1), y0 + h, color)
end

local SORTS = {
    function(a, b) return a.dist < b.dist end,
    function(a, b) return a.hp < b.hp end,
    function(a, b)
        if a.kind ~= b.kind then return (a.kind == KIND.boss) and (b.kind ~= KIND.boss) end
        return a.dist < b.dist
    end,
}

local function draw_list(f, elites)
    local rows = d3hud.get("list_rows")
    local w, h = d3hud.get("list_width"), d3hud.get("list_row")
    table.sort(elites, SORTS[d3hud.get("list_sort")])
    local shown = math.min(#elites, rows)
    if shown == 0 then return end
    local gap = 4
    local pad = 6
    local x0 = d3hud.get("list_side") == 1 and 12 + pad or f.screen_w - 12 - pad - w
    local y0 = f.screen_h * d3hud.get("list_y") / 100 + pad
    if d3hud.get("list_box") then
        d3hud.draw_rect(x0 - pad, y0 - pad, x0 + w + pad, y0 + shown * (h + gap) - gap + pad, 0x90000000)
    end
    local scale = math.max(0.5, (h - 6) / 14)  -- text that fits the bar height (default font is about 14 px tall)
    local names, pct = d3hud.get("list_names"), d3hud.get("list_percent")
    for i = 1, shown do
        local a = elites[i]
        local y = y0 + (i - 1) * (h + gap)
        bar(x0, y, w, h, a.hp, a.color)
        local pct_text = pct and string.format("%d%%", math.floor(a.hp * 100 + 0.5)) or nil
        local room = w - 8
        local tw, th = 0, select(2, d3hud.text_size("Ag", scale))
        if pct_text then
            tw = d3hud.text_size(pct_text, scale)
            shadowed(x0 + w - tw - 4, y + (h - th) / 2, WHITE, pct_text, scale)
            room = room - tw - 4
        end
        if names then
            shadowed(x0 + 4, y + (h - th) / 2, WHITE, fit(pretty(a.name), room, scale), scale)
        end
    end
end

d3hud.on("frame", function()
    local f = d3hud.frame()
    if f.actor_count == 0 or (f.ui.big_map and d3hud.get("hide_on_map")) then return end

    -- settings are read once per frame, not per actor
    local colour = {}
    if d3hud.get("champion") then colour[KIND.champion] = 0xFF000000 | d3hud.get("color_champion") end
    if d3hud.get("rare") then colour[KIND.rare] = 0xFF000000 | d3hud.get("color_rare") end
    if d3hud.get("boss") then colour[KIND.boss] = 0xFF000000 | d3hud.get("color_boss") end
    if next(colour) == nil then return end
    local over, list = d3hud.get("over"), d3hud.get("list")
    if not over and not list then return end
    local hide_full = d3hud.get("hide_full")

    -- the elites that are alive and whose life is known (hp < 0: not read yet, 0: dead)
    local lift = d3hud.get("lift")
    local list_all = d3hud.get("list_all")
    local elites, listed = {}, {}
    for _, a in ipairs(d3hud.actors()) do
        local c = colour[a.kind]
        if c and a.hp > 0 and not (hide_full and a.hp >= 0.999) then
            a.color = c
            a.sx, a.sy = d3hud.world_to_screen(a.x, a.y, lift)  -- nil when off the screen
            elites[#elites + 1] = a
            if a.sx or list_all then listed[#listed + 1] = a end
        end
    end
    if #elites == 0 then return end

    if over then
        local w, h = d3hud.get("width"), d3hud.get("height")
        local percent = d3hud.get("percent")
        for _, a in ipairs(elites) do
            if a.sx then
                local x0, y0 = a.sx - w / 2, a.sy - h / 2
                bar(x0, y0, w, h, a.hp, a.color)
                if percent then
                    shadowed(x0 + w + 4, y0 - 2, WHITE, string.format("%d%%", math.floor(a.hp * 100 + 0.5)), 0.8)
                end
            end
        end
    end
    if list then draw_list(f, listed) end
end)
