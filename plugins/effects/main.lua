-- Effects: everything running on your character, with the time left, in one list: buffs (pylons, shrines, item
-- powers) and cooldowns (health potion, skills, and the cheat death cooldown). A dock panel lists them; on request
-- the same entries are also drawn as bars on the screen.
--
-- The game keeps many more effects than anyone wants to see: item passives that renew themselves every second,
-- statuses that monsters put on you, the game's own housekeeping. Three modes (the panel and the options):
--   Important only (default): pylons, shrines, cheat death and the health potion, plus powers you add yourself
--   Without noise: everything except what is shorter than a few seconds, longer than half an hour, or on a list
--                  of name fragments
--   Everything

local draw = d3hud.require("draw")
local fmt = d3hud.require("fmt")

if not d3hud.effects then
    d3hud.log("this host is too old for effects (needs API 2)", "error")
    return
end

d3hud.setting{ key = "mode", type = "choice", default = 1, choices = { "Important only", "Without noise", "Everything" },
    label = "Which effects",
    tooltip = "Important: pylons, shrines, cheat death, health potion and the powers you add below. Without noise: all but item passives that renew themselves, effects monsters put on you and the game's housekeeping. Everything: the game's whole list." }
d3hud.setting{ key = "extra", type = "string", default = "",
    label = "More important powers (power numbers, comma separated)",
    tooltip = "Hover an entry in the panel (mode Without noise or Everything) to see its power number." }
d3hud.setting{ key = "buffs", type = "bool", default = true, label = "Show buffs (pylons, shrines, item powers)" }
d3hud.setting{ key = "cooldowns", type = "bool", default = true, label = "Show cooldowns (potion, skills, cheat death)" }
d3hud.setting{ key = "min_dur", type = "float", default = 3, min = 0, max = 60, label = "Hide effects shorter than (s)",
    tooltip = "Item passives renew a short buff every second or so, and cheat death renews its passive; those are not worth a line. Not applied in mode Everything." }
d3hud.setting{ key = "max_min", type = "int", default = 30, min = 1, max = 600, label = "Hide effects longer than (minutes)" }
d3hud.setting{ key = "hidden", type = "string", default = "monsteraffix,calloutcooldown,bantercooldown,generic_cooldown,loading,usehealthglyph,actorinvul",
    label = "Without noise: hide powers whose internal name contains (comma separated)",
    tooltip = "Part of the internal name, e.g. Generic_MonsterAffixMolten. Hover an entry in the panel to see its name." }
d3hud.setting{ key = "hud", type = "bool", default = false, label = "Also draw the effects as bars on the screen" }
d3hud.setting{ key = "hud_x", type = "int", default = 2, min = 0, max = 100, label = "Screen position: from the left (%)" }
d3hud.setting{ key = "hud_y", type = "int", default = 35, min = 0, max = 100, label = "Screen position: from the top (%)" }
d3hud.setting{ key = "hud_scale", type = "float", default = 1.0, min = 0.5, max = 3.0, label = "Size" }
d3hud.setting{ key = "hud_rows", type = "int", default = 8, min = 1, max = 30, label = "Most entries on the screen" }

-- The important effects, with the names people know them by (the generic name is what the game calls the power).
-- (The "ShrineDesecrated*" powers 30476-30479 / 260348-9 are left out on purpose: they showed up with every health
-- potion, so they are not the shrines' buffs.)
local KNOWN = {
    [262935] = "Power Pylon", [486865] = "Power Pylon", [488072] = "Power Pylon",
    [263029] = "Conduit Pylon", [403404] = "Conduit Pylon", [486873] = "Conduit Pylon",
    [266254] = "Shield Pylon",
    [266258] = "Channeling Pylon", [486868] = "Channeling Pylon",
    [266271] = "Speed Pylon", [487614] = "Speed Pylon",
    [278268] = "Blessed Shrine", [278269] = "Enlightened Shrine", [278270] = "Fortune Shrine", [278271] = "Frenzied Shrine",
    [269350] = "Bandit Shrine", [487311] = "Bandit Shrine",
    [484963] = "Cheat Death", [485530] = "Cheat Death", [485531] = "Cheat Death", [485532] = "Cheat Death",
    [30211] = "Health Potion",
}

-- The game keeps the cheat death cooldown as a long buff (a short one renews the passive itself): it is a cooldown.
local CHEAT_DEATH = { [484963] = true, [485530] = true, [485531] = true, [485532] = true }

local BUFF_RGB, COOLDOWN_RGB = 0x5CC85C, 0xE8A040
local BUFF_COLOR, COOLDOWN_COLOR, SOON_COLOR = 0xFF7CD67C, 0xFFE8A84C, 0xFFFFE066
local SOON = 5  -- seconds: the time left is highlighted below this

local frame_no = 0
local cache_frame, cache = -1, nil

local function number_set(text)
    local set = {}
    for piece in string.gmatch(text, "%d+") do set[tonumber(piece)] = true end
    return set
end

local function hidden_fragments()
    local list = {}
    for piece in string.gmatch(string.lower(d3hud.get("hidden")), "[^,]+") do
        local trimmed = string.match(piece, "^%s*(.-)%s*$")
        if trimmed ~= "" then list[#list + 1] = trimmed end
    end
    return list
end

local function shown_name(e)
    return KNOWN[e.sno] or e.name
end

-- The effects to show, cooldowns first, each group by the time left (soonest first). Cached per drawn frame:
-- the panel and the screen bars ask for the same list.
local function visible()
    if cache_frame == frame_no and cache then return cache end
    local mode = d3hud.get("mode")  -- 1 important only, 2 without noise, 3 everything
    local extra = mode == 1 and number_set(d3hud.get("extra")) or nil
    local show_buffs, show_cd = d3hud.get("buffs"), d3hud.get("cooldowns")
    local min_dur, max_dur = d3hud.get("min_dur"), d3hud.get("max_min") * 60
    local fragments = hidden_fragments()
    local cooldowns, buffs, hidden = {}, {}, 0
    for _, e in ipairs(d3hud.effects()) do
        local cd = e.kind == "cooldown" or (CHEAT_DEATH[e.sno] and e.duration >= 30)
        local wanted = cd and show_cd or (not cd and show_buffs)
        if wanted then
            local keep = true
            if mode == 1 then
                keep = (KNOWN[e.sno] ~= nil or extra[e.sno] == true) and e.duration >= min_dur and e.duration <= max_dur
            elseif mode == 2 then
                if e.duration < min_dur or e.duration > max_dur then keep = false end
                local code = string.lower(e.code)
                for _, f in ipairs(fragments) do
                    if string.find(code, f, 1, true) then keep = false break end
                end
            end
            if keep then
                local row = { e = e, name = shown_name(e), cd = cd }
                if cd then cooldowns[#cooldowns + 1] = row else buffs[#buffs + 1] = row end
            else
                hidden = hidden + 1
            end
        end
    end
    local function by_time(a, b)
        if a.e.remaining ~= b.e.remaining then return a.e.remaining < b.e.remaining end
        if a.name ~= b.name then return a.name < b.name end
        return a.e.sno < b.e.sno
    end
    table.sort(cooldowns, by_time)
    table.sort(buffs, by_time)
    cache_frame, cache = frame_no, { cooldowns = cooldowns, buffs = buffs, hidden = hidden }
    return cache
end

local function time_text(e)
    if not e.timed then return "?" end
    return fmt.dur(e.remaining)
end

local function row_text(row)
    local e = row.e
    return string.format("%6s   %s%s", time_text(e), row.name, e.stacks > 1 and string.format("  x%d", e.stacks) or "")
end

local function row_color(row)
    local e = row.e
    if e.timed and e.remaining < SOON then return SOON_COLOR end
    return row.cd and COOLDOWN_COLOR or BUFF_COLOR
end

d3hud.on("frame", function()
    frame_no = frame_no + 1
    if not d3hud.get("hud") then return end
    local v = visible()
    local f = d3hud.frame()
    local scale = d3hud.get("hud_scale")
    local x, y = f.screen_w * d3hud.get("hud_x") / 100, f.screen_h * d3hud.get("hud_y") / 100
    local w, h = 220 * scale, 16 * scale
    local left = d3hud.get("hud_rows")
    for _, group in ipairs({ v.cooldowns, v.buffs }) do
        for _, row in ipairs(group) do
            if left <= 0 then return end
            left = left - 1
            local e = row.e
            local fraction = (e.timed and e.duration > 0) and (e.remaining / e.duration) or 1
            draw.bar(x, y, w, h, fraction, row.cd and COOLDOWN_RGB or BUFF_RGB)
            local label = draw.fit(row.name, w - 52 * scale, scale * 0.85)
            draw.shadowed(x + 4, y + 1 * scale, draw.WHITE, label, scale * 0.85)
            local t = time_text(e)
            local tw = d3hud.text_size(t, scale * 0.85)
            draw.shadowed(x + w - tw - 4, y + 1 * scale, row_color(row), t, scale * 0.85)
            y = y + h + 4 * scale
        end
    end
end)

local function show_row(ui, row)
    ui.text_colored(row_color(row), row_text(row))
    if ui.is_item_hovered() then
        ui.tooltip(string.format("%s\npower %d%s", row.e.code ~= "" and row.e.code or row.e.name, row.e.sno,
            row.e.timed and string.format("\n%s of %s", fmt.dur(row.e.remaining), fmt.dur(row.e.duration)) or ""))
    end
end

-- the dock panel: "Effects"
d3hud.panel{
    id = "effects", title = "Effects", width = 320, height = 300,
    draw = function(ui)
        local v = visible()
        local f = d3hud.frame()
        if f.effect_count == 0 then
            ui.text_disabled("nothing running (or the reader has not found your character yet)")
        end
        if d3hud.get("cooldowns") then
            ui.separator_text("Cooldowns")
            if #v.cooldowns == 0 then ui.text_disabled("none") end
            for _, row in ipairs(v.cooldowns) do show_row(ui, row) end
        end
        if d3hud.get("buffs") then
            ui.separator_text("Buffs")
            if #v.buffs == 0 then ui.text_disabled("none") end
            for _, row in ipairs(v.buffs) do show_row(ui, row) end
        end
        ui.separator()
        local changed, mode = ui.combo("show", d3hud.get("mode"), { "Important only", "Without noise", "Everything" })
        if changed then d3hud.set("mode", mode) end
        if v.hidden > 0 then ui.text_disabled(string.format("%d more hidden", v.hidden)) end
    end,
}
