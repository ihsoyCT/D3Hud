-- Health bars over elites and bosses: blue (champion) packs, rares and bosses that are on the screen.
--
-- The reader publishes the life of those three kinds only (trash and minions are not read), and only once
-- it has located their attribute data; until then the bar of an elite is not drawn. Options (what to show,
-- size, colours) are on this plugin's Settings page.

d3hud.setting{ key = "champion", type = "bool", default = true, label = "Champions (blue)" }
d3hud.setting{ key = "rare", type = "bool", default = true, label = "Rares (yellow)" }
d3hud.setting{ key = "boss", type = "bool", default = true, label = "Bosses" }
d3hud.setting{ key = "width", type = "int", default = 80, min = 30, max = 300, label = "Bar width (px)" }
d3hud.setting{ key = "height", type = "int", default = 8, min = 3, max = 30, label = "Bar height (px)" }
d3hud.setting{ key = "lift", type = "float", default = 8, min = 0, max = 25, label = "Height above the ground (game units)",
    tooltip = "How far above the monster's feet the bar floats. Taller monsters want a larger value." }
d3hud.setting{ key = "percent", type = "bool", default = true, label = "Show the percentage" }
d3hud.setting{ key = "hide_full", type = "bool", default = false, label = "Hide bars at full health" }
d3hud.setting{ key = "hide_on_map", type = "bool", default = true, label = "Hide while the big map is open" }
d3hud.setting{ key = "color_champion", type = "color", default = 0x5082FF, label = "Champion colour" }
d3hud.setting{ key = "color_rare", type = "color", default = 0xFFD700, label = "Rare colour" }
d3hud.setting{ key = "color_boss", type = "color", default = 0xBE46FF, label = "Boss colour" }

local KIND = d3hud.kind
local DARK = 0xC0101010
local BORDER = 0xFF000000
local WHITE = 0xFFFFFFFF

d3hud.on("frame", function()
    local f = d3hud.frame()
    if f.actor_count == 0 or (f.ui.big_map and d3hud.get("hide_on_map")) then return end

    -- settings are read once per frame, not per actor
    local colour = {}
    if d3hud.get("champion") then colour[KIND.champion] = 0xFF000000 | d3hud.get("color_champion") end
    if d3hud.get("rare") then colour[KIND.rare] = 0xFF000000 | d3hud.get("color_rare") end
    if d3hud.get("boss") then colour[KIND.boss] = 0xFF000000 | d3hud.get("color_boss") end
    if next(colour) == nil then return end
    local w, h = d3hud.get("width"), d3hud.get("height")
    local lift = d3hud.get("lift")
    local percent, hide_full = d3hud.get("percent"), d3hud.get("hide_full")

    for _, a in ipairs(d3hud.actors()) do
        local c = colour[a.kind]
        if c and a.hp > 0 and not (hide_full and a.hp >= 0.999) then  -- hp < 0: not read yet; 0: dead
            local sx, sy = d3hud.world_to_screen(a.x, a.y, lift)
            if sx then
                local x0, y0 = sx - w / 2, sy - h / 2
                d3hud.draw_rect(x0 - 1, y0 - 1, x0 + w + 1, y0 + h + 1, BORDER)
                d3hud.draw_rect(x0, y0, x0 + w, y0 + h, DARK)
                d3hud.draw_rect(x0, y0, x0 + w * math.min(a.hp, 1), y0 + h, c)
                if percent then
                    d3hud.draw_text(x0 + w + 4, y0 - 2, WHITE, string.format("%d%%", math.floor(a.hp * 100 + 0.5)), 0.8)
                end
            end
        end
    end
end)
