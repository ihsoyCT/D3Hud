-- Time tracker: how much of your play time goes where (town, bounties, rifts, ...).
--
-- Rifts and greater rifts are detected from the rift progress bar. Everything else is sorted by the
-- world you are in: open the "Time tracker" page in Settings, stand in a world and pick which area it
-- belongs to once (e.g. "Town"); the choice is remembered. Unassigned worlds count as "Other".
-- The list of areas (and the on-screen display) are options on the "timetracker options" page.

local SAVE_EVERY = 30      -- seconds of tracked time between saves
local MAX_GAP_MS = 5000    -- a longer gap between snapshots (reader stopped, game paused) is not counted
local BUILTIN = { "Rift", "Greater Rift" }
local FALLBACK = "Other"

d3hud.setting{ key = "areas", type = "string", default = "Town, Bounties, Other",
    label = "Areas (comma separated)",
    tooltip = "Names you can assign worlds to. Rift and Greater Rift are detected automatically." }
d3hud.setting{ key = "show_hud", type = "bool", default = false, label = "Show the session split on screen" }
d3hud.setting{ key = "totals", type = "string", default = "", hidden = true }
d3hud.setting{ key = "worlds", type = "string", default = "", hidden = true }

local fmt = d3hud.require("fmt")
local store = d3hud.require("store")

local function seconds_text(v) return string.format("%.1f", v) end

local total = store.unpack_map(d3hud.get("totals"), { number = true })  -- all time, seconds per area
local session = {}                             -- since the overlay started
local worlds = {}                          -- world sno (as text) -> area name
local last_time, unsaved = nil, 0
local confirm_reset = nil

local function clean(name)
    return (string.gsub(name, "[=;]", ""))
end

local function area_list()
    local list, seen = {}, {}
    for name in string.gmatch(d3hud.get("areas"), "[^,]+") do
        name = clean((string.gsub(name, "^%s*(.-)%s*$", "%1")))
        if name ~= "" and not seen[name] then
            seen[name] = true
            list[#list + 1] = name
        end
    end
    if not seen[FALLBACK] then list[#list + 1] = FALLBACK end
    return list
end

-- worlds: world sno (as text) -> area name
local function save_worlds() d3hud.set("worlds", store.pack_map(worlds)) end
worlds = store.unpack_map(d3hud.get("worlds"))

local function save()
    d3hud.set("totals", store.pack_map(total, { format = seconds_text }))
    unsaved = 0
end

local function area_of(f)
    if f.ui.greater_rift then return "Greater Rift" end
    if f.ui.rift then return "Rift" end
    return worlds[tostring(f.world_sno)] or FALLBACK
end

d3hud.on("snapshot", function()
    local f = d3hud.frame()
    if last_time then
        local dt = f.time_ms - last_time
        if dt > 0 and dt <= MAX_GAP_MS then
            local area = area_of(f)
            local s = dt / 1000
            total[area] = (total[area] or 0) + s
            session[area] = (session[area] or 0) + s
            unsaved = unsaved + s
            if unsaved >= SAVE_EVERY then save() end
        end
    end
    last_time = f.time_ms
end)

local function sum(t)
    local n = 0
    for _, v in pairs(t) do n = n + v end
    return n
end

-- Areas to list: the configured ones, the rift ones, and any with recorded time (renamed areas).
local function rows()
    local list, seen = {}, {}
    local function add(name)
        if not seen[name] then seen[name] = true; list[#list + 1] = name end
    end
    for _, a in ipairs(area_list()) do add(a) end
    for _, a in ipairs(BUILTIN) do add(a) end
    local extra = {}
    for a in pairs(total) do if not seen[a] then extra[#extra + 1] = a end end
    table.sort(extra)
    for _, a in ipairs(extra) do add(a) end
    return list
end

-- the session split on the screen: a box the user can move (Settings > Plugins) and give another look
d3hud.box{
    id = "split", title = "Time per area", x = 0.006, y = 0.055,
    draw = function(x, y)
        if not d3hud.get("show_hud") then return end
        local all = sum(session)
        if all <= 0 then return end
        local parts = {}
        for _, a in ipairs(rows()) do
            local v = session[a]
            if v and v > 0 then parts[#parts + 1] = string.format("%s %d%%", a, math.floor(v * 100 / all + 0.5)) end
        end
        local text = table.concat(parts, "  |  ")
        d3hud.draw_text(x, y, 0xFFFFFFFF, text)
        return d3hud.text_size(text, 1)
    end,
}

local function section(ui, title, data)
    ui.separator_text(title)
    local all = sum(data)
    if all <= 0 then
        ui.text_disabled("nothing recorded yet")
        return
    end
    for _, a in ipairs(rows()) do
        local v = data[a] or 0
        if v > 0 then
            local pct = v * 100 / all
            ui.text(string.format("%-14s %9s  %5.1f%%  %s", a, fmt.hms(v), pct, string.rep("|", math.floor(pct / 2.5 + 0.5))))
        end
    end
    ui.text_disabled(string.format("total %s", fmt.hms(all)))
end

-- A dock panel: hover its tab ("Time") to peek at the split, F8 + click to keep it open.
d3hud.panel{
    id = "split", title = "Time",
    draw = function(ui)
        local f = d3hud.frame()
        ui.text(string.format("Now: %s", area_of(f)))
        section(ui, "This session", session)
        section(ui, "All time", total)
    end,
}

d3hud.page{
    id = "timetracker", title = "Time tracker", group = "Plugins", order = 1000,
    draw = function(ui)
        local f = d3hud.frame()
        local current = area_of(f)
        ui.text(string.format("Now: %s   (world %d)", current, f.world_sno))
        if f.world_sno ~= 0 and not f.ui.rift and not f.ui.greater_rift then
            local list = area_list()
            local index = 1
            for i, a in ipairs(list) do if a == current then index = i end end
            local changed, picked = ui.combo("this world belongs to", index, list)
            if changed then
                worlds[tostring(f.world_sno)] = list[picked]
                save_worlds()
            end
        else
            ui.text_disabled("(rifts are detected automatically; stand in another world to assign it)")
        end

        section(ui, "This session", session)
        section(ui, "All time", total)

        ui.spacing()
        if ui.button("reset session") then session = {} end
        ui.same_line()
        if confirm_reset then
            if ui.button("really erase all-time totals?") then
                total, confirm_reset = {}, nil
                save()
            end
            ui.same_line()
            if ui.button("cancel") then confirm_reset = nil end
        elseif ui.button("reset all time") then
            confirm_reset = true
        end
    end,
}
