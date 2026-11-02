-- Rings on the ground for the areas that matter: the circles your items make (the Oculus Ring, and the Altar of Rites when you
-- drink a potion) in your colours, and the areas elite monsters leave (molten, desecrator, plagued, arcane, frozen, thunderstorm)
-- in red. The game draws them faintly; this makes them easy to see and says how long some have left.
--
-- The reader sees them as actors of kind ground_effect (`p2_itemPassive_unique_ring_017_dome*`, found with
-- `D3Hud.Reader --nearby`). The plain name is the Oculus Ring; the coloured variants (_blue, _teal, _purple) are the altar's, and purple_red is its damage effect.
-- The size and the life of a circle are not in the actor. For your own circles they are options (stand at the edge of one and
-- change "Radius"); for the monsters' areas the table below has them, taken from TurboHUD's EliteMonsterSkillPlugin.

if not d3hud.kind.ground_effect then
    d3hud.log("this host is too old for ground effects (needs API 5)", "error")
    return
end

local draw = d3hud.require("draw")

d3hud.setting{ key = "oculus", type = "bool", default = true, label = "Oculus Ring circles" }
d3hud.setting{ key = "altar", type = "bool", default = true, label = "Altar of Rites circles" }
d3hud.setting{ key = "color_oculus", type = "color", default = 0x7FDFFF, label = "Oculus colour" }
d3hud.setting{ key = "color_altar", type = "color", default = 0xFFD060, label = "Altar colour" }
d3hud.setting{ key = "hostile", type = "bool", default = true, label = "Areas elite monsters leave (molten, desecrator, ...)" }
d3hud.setting{ key = "color_hostile", type = "color", default = 0xFF5050, label = "Colour of the monsters' areas" }
-- The areas monsters leave are told apart by the actor's name; this is the list (the reader reads it from plugins.ini too,
-- to decide which actors are ground effects, so a name added here is picked up within a few seconds). It must stay equal to
-- kDefaultGroundEffects in src/core/GroundEffects.h (a test compares them). Find the name of an area you want with
-- `D3Hud.Reader.exe --attach "Diablo III64" --nearby` (stand next to it).
local DEFAULT_LIST = "monsteraffix_frozen_iceclusters:15.8:3:Frozen ball, monsteraffix_molten_deathstart_proxy:13:3:Molten blast, "
    .. "monsteraffix_molten_deathexplosion_proxy:13::Molten, monsteraffix_molten_firering:13::Molten, "
    .. "monsteraffix_desecrator_damage_aoe:8::Desecrator, x1_monsteraffix_thunderstorm_impact:16::Thunderstorm, "
    .. "monsteraffix_plagued_endcloud:12::Plagued, creepmobarm:12::Plagued, gluttony_gascloud_proxy:20::Gas cloud, "
    .. "monsteraffix_arcaneenchanted_petsweep:6::Arcane, monsteraffix_arcaneenchanted_petsweep_reverse:6::Arcane, "
    .. "arcaneenchanteddummy_spawn:6:2:Arcane, x1_monsteraffix_frozenpulse_monster:14::Frozen pulse"
d3hud.setting{ key = "list", type = "string", default = DEFAULT_LIST,
    label = "Monsters' areas: name[:radius[:seconds[:label]]], comma separated",
    tooltip = "Add the actor's name to draw another area (radius in game units, seconds if it lasts a fixed time, label for the text; each part after the name is optional). Delete an entry to stop drawing it. Clear the field and paste the default back to start over." }
d3hud.setting{ key = "radius_default", type = "float", default = 10, min = 2, max = 60, label = "Radius of a listed area that gives none (game units)" }
d3hud.setting{ key = "color_inside", type = "color", default = 0x70FF70, label = "Colour while you stand inside" }
d3hud.setting{ key = "radius_oculus", type = "float", default = 9.5, min = 3, max = 40, label = "Oculus radius (game units)",
    tooltip = "Not stored in the circle itself, so it is set here. Stand at the edge of a circle and change it until the ring matches." }
d3hud.setting{ key = "radius_altar", type = "float", default = 9.5, min = 3, max = 40, label = "Altar radius (game units)" }
d3hud.setting{ key = "life", type = "float", default = 7.4, min = 1, max = 30, label = "Seconds a circle lasts",
    tooltip = "Measured: every circle was gone 7.4 s after it appeared." }
d3hud.setting{ key = "countdown", type = "bool", default = true, label = "Show the seconds left in the middle" }
d3hud.setting{ key = "clock", type = "bool", default = true, label = "Ring as a clock (the part that has gone fades)",
    tooltip = "Works where the time is known: your circles and the monster areas with a countdown." }
d3hud.setting{ key = "clock_faint", type = "int", default = 60, min = 0, max = 255, label = "Clock: opacity of the part that has gone (0..255)" }
d3hud.setting{ key = "thickness", type = "float", default = 2.5, min = 1, max = 8, label = "Line thickness (px)" }
d3hud.setting{ key = "hide_on_map", type = "bool", default = true, label = "Hide while the big map is open" }

local SEGMENTS = 56
local COS, SIN = {}, {}
for i = 0, SEGMENTS do
    COS[i], SIN[i] = math.cos(i * 2 * math.pi / SEGMENTS), math.sin(i * 2 * math.pi / SEGMENTS)
end

-- The areas monsters leave, by the actor's name in lower case: { what to call it, radius or nil, seconds or nil }, built
-- from the "list" setting (again whenever the text changes).
local hostile_text, HOSTILE = nil, {}

local function trim(t) return (t:gsub("^%s+", ""):gsub("%s+$", "")) end

local function hostile_table()
    local text = d3hud.get("list")
    if text ~= hostile_text then
        hostile_text, HOSTILE = text, {}
        for entry in text:gmatch("[^,]+") do
            local f = {}
            for part in (entry .. ":"):gmatch("([^:]*):") do f[#f + 1] = part end
            local name = trim(f[1] or ""):lower()
            if name ~= "" then
                local label = trim(f[4] or "")
                local radius, life = tonumber(f[2]), tonumber(f[3])
                HOSTILE[name] = { label ~= "" and label or name, radius and radius > 0 and radius or nil,
                                  life and life > 0 and life or nil }
            end
        end
    end
    return HOSTILE
end

local first_seen = {}  -- actor id -> time_ms the circle was first seen
local circles = {}     -- this snapshot's areas: { x, y, dist, id, label, radius, life, kind = "oculus" | "altar" | "hostile" }
local now_ms = 0

-- What the altar's colours are known to be; any other variant is labelled with its colour.
-- The potion's circles (Triune Circle theme): what each gives while you stand in it, and its own colour. The game names
-- the variants by colour (confirmed in the game: purple_red is the damage one, teal the cooldowns, blue the resource cost).
local ALTAR = {
    purple_red = { "Damage +50%", 0xFF6EE0 },
    blue = { "Resource cost -50%", 0x5AA0FF },
    teal = { "Cooldowns faster", 0x5CE07C },
}

-- "" for the Oculus Ring's dome, "teal" for dome_teal, nil for any other name
local function variant(name)
    return string.lower(name):match("ring_017_dome_?(.*)$")
end

d3hud.on("snapshot", function()
    local f = d3hud.frame()
    now_ms = f.time_ms
    local seen, list = {}, {}
    for _, a in ipairs(d3hud.actors()) do
        if a.kind == d3hud.kind.ground_effect then
            seen[a.id] = true
            first_seen[a.id] = first_seen[a.id] or now_ms
            local hostile = hostile_table()[string.lower(a.name)]
            if hostile then
                list[#list + 1] = { x = a.x, y = a.y, dist = a.dist, id = a.id, kind = "hostile",
                                    label = hostile[1], radius = hostile[2] or d3hud.get("radius_default"), life = hostile[3] }
            elseif variant(a.name) then
                local v = variant(a.name)
                local known = ALTAR[v]
                list[#list + 1] = { x = a.x, y = a.y, dist = a.dist, id = a.id, kind = v == "" and "oculus" or "altar",
                                    label = v == "" and "Oculus" or known and known[1] or ("Altar " .. v:gsub("_", " ")),
                                    rgb = known and known[2] }
            end
        end
    end
    for id in pairs(first_seen) do
        if not seen[id] then first_seen[id] = nil end
    end
    circles = list
end)

-- The ring point that is at 12 o'clock on the screen, and which way round the ring goes clockwise there (+1 or -1 along
-- the segments). Found with a small circle around the middle, which is on the screen whenever the middle is.
local function clock_start(x, y)
    local cx, cy = d3hud.world_to_screen(x, y)
    if not cx then return 0, 1 end
    local top, top_y = 0, math.huge
    for i = 0, SEGMENTS - 1 do
        local sx, sy = d3hud.world_to_screen(x + COS[i], y + SIN[i])
        if sy and sy < top_y then top, top_y = i, sy end
    end
    -- the screen's y points down, so a positive cross product of two steps around is clockwise
    local ax, ay = d3hud.world_to_screen(x + COS[0], y + SIN[0])
    local bx, by = d3hud.world_to_screen(x + COS[SEGMENTS // 4], y + SIN[SEGMENTS // 4])
    local dir = 1
    if ax and bx and (ax - cx) * (by - cy) - (ay - cy) * (bx - cx) < 0 then dir = -1 end
    return top, dir
end

d3hud.on("frame", function()
    if #circles == 0 then return end
    if d3hud.get("hide_on_map") and d3hud.frame().ui.big_map then return end
    local life, thick = d3hud.get("life"), d3hud.get("thickness")    for _, c in ipairs(circles) do
        local enabled = d3hud.get(c.kind)
        if enabled then
            local radius = c.radius or d3hud.get("radius_" .. c.kind)
            local inside = c.dist <= radius
            -- standing in your own circle is good news (green); in a monster's area it is not (the same red, brighter)
            local rgb = c.rgb or d3hud.get("color_" .. c.kind)
            if inside and c.kind ~= "hostile" then rgb = d3hud.get("color_inside") end
            local alpha = inside and 255 or 210
            local argb = draw.argb(rgb, alpha)
            -- the seconds left, where the life is known (always for the item circles, only some monster areas)
            local total = c.life or (c.kind ~= "hostile" and life or nil)
            local left = total and total - (now_ms - (first_seen[c.id] or now_ms)) / 1000
            -- the clock: the ring is solid for the part that is still to come and faint for the part that has gone, like
            -- a clock hand sweeping round; the hand is the line from the middle
            local solid = SEGMENTS
            if d3hud.get("clock") and left then solid = math.floor(math.max(0, math.min(1, left / total)) * SEGMENTS + 0.5) end
            local faint = draw.argb(rgb, d3hud.get("clock_faint"))
            local start, dir = clock_start(c.x, c.y)
            local px, py
            for k = 0, SEGMENTS do
                local i = (start + dir * k) % SEGMENTS
                local sx, sy = d3hud.world_to_screen(c.x + COS[i] * radius, c.y + SIN[i] * radius)
                -- the part that has gone is the one after 12 o'clock, so the boundary (the hand) moves clockwise
                if sx and px then d3hud.draw_line(px, py, sx, sy, k > SEGMENTS - solid and argb or faint, inside and thick + 1 or thick) end
                px, py = sx, sy  -- nil off the screen: the next segment is not joined to it
            end
            if solid < SEGMENTS then
                local i = (start + dir * (SEGMENTS - solid)) % SEGMENTS
                local hx, hy = d3hud.world_to_screen(c.x, c.y)
                local ex, ey = d3hud.world_to_screen(c.x + COS[i] * radius, c.y + SIN[i] * radius)
                if hx and ex then d3hud.draw_line(hx, hy, ex, ey, argb, thick) end
            end
            if d3hud.get("countdown") and left then
                local cx, cy = d3hud.world_to_screen(c.x, c.y)
                if cx and left > 0 then
                    local text = string.format("%s %.1f", c.label, left)
                    local tw, th = d3hud.text_size(text, 1.2)
                    draw.shadowed(cx - tw / 2, cy - th / 2, argb, text, 1.2)
                end
            end
        end
    end
end)
