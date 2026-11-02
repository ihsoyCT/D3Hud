-- Rift log: what each rift or greater rift gave you. A panel shows the run in progress and the last runs:
-- level (greater rifts), time, time until the guardian appeared, deaths, paragon experience gained, and the share of the
-- time that chosen buffs were up (Squirt's Necklace, a pylon, ...). The runs are kept between sessions.
--
-- Where the numbers come from (all read by the reader, see PLUGIN.md):
--   the run starts and ends with the game's rift bar; the guardian appears when the bar is full;
--   deaths = difference of the character's death counter; experience = what the paragon counter dropped by;
--   level = "last rift started" of the account; the time left in a greater rift = the timer bar.
-- Buffs to measure: open "Buffs on you right now" in the panel while the buff is up and click it. The list is the plugin's
-- "tracked" setting (power number, a colon and a name; comma between entries) and can be edited on the plugin's page.

local fmt = d3hud.require("fmt")

if not d3hud.passives then
    d3hud.log("this host is too old for the player data (needs API 10)", "error")
    return
end

d3hud.setting{ key = "tracked", type = "string", default = "483552:Squirt", label = "Buffs to measure (power:name, ...)",
    tooltip = "Pick them from the panel while they are up; e.g. 123456:Squirt" }
d3hud.setting{ key = "keep", type = "int", default = 12, min = 1, max = 40, label = "Runs to keep" }
d3hud.setting{ key = "history", type = "string", default = "", hidden = true }

local GRACE_MS = 4000      -- the rift bar may vanish for a moment (loading); longer than this the run is over
local MIN_RUN_MS = 20000   -- shorter is not a run
local MAX_GAP_MS = 2000    -- a longer gap between snapshots (paused, reader stopped) is not counted
local LIMIT_SECONDS = 900  -- greater rift time limit

-- ---- the list of buffs to measure -------------------------------------------------------------------------------

local function tracked_list()
    local list = {}
    for entry in string.gmatch(d3hud.get("tracked"), "[^,]+") do
        local sno, name = string.match(entry, "^%s*(%d+)%s*:?%s*(.-)%s*$")
        if sno then list[#list + 1] = { sno = tonumber(sno), name = name ~= "" and name or ("Power " .. sno) } end
    end
    return list
end

local function save_tracked(list)
    local parts = {}
    for _, t in ipairs(list) do parts[#parts + 1] = string.format("%d:%s", t.sno, (string.gsub(t.name, "[,:]", ""))) end
    d3hud.set("tracked", table.concat(parts, ", "))
end

-- ---- history ----------------------------------------------------------------------------------------------------
-- one run: kind,level,seconds,boss_seconds,deaths,xp,left,uptime  (uptime = sno:name:percent joined by "/"), runs joined by ";"

local history = {}

local function pack_run(r)
    local up = {}
    for _, u in ipairs(r.up) do
        up[#up + 1] = string.format("%d:%s:%d", u.sno, (string.gsub(u.name, "[,:;/]", "")), math.floor(u.pct + 0.5))
    end
    return string.format("%s,%d,%d,%d,%d,%.0f,%d,%s", r.kind == "Greater Rift" and "G" or "R", r.level or 0, math.floor(r.seconds),
        r.boss and math.floor(r.boss) or -1, r.deaths, r.xp, r.left and math.floor(r.left) or -1, table.concat(up, "/"))
end

local function unpack_run(text)
    local kind, level, secs, boss, deaths, xp, left, up =
        string.match(text, "^(%a),(%-?%d+),(%-?%d+),(%-?%d+),(%-?%d+),(%-?[%d%.]+),(%-?%d+),(.*)$")
    if not kind then return nil end
    local r = { kind = kind == "G" and "Greater Rift" or "Rift", level = tonumber(level) > 0 and tonumber(level) or nil,
        seconds = tonumber(secs), boss = tonumber(boss) >= 0 and tonumber(boss) or nil, deaths = tonumber(deaths),
        xp = tonumber(xp), left = tonumber(left) >= 0 and tonumber(left) or nil, up = {} }
    for sno, name, pct in string.gmatch(up, "(%d+):([^:/]*):(%d+)") do
        r.up[#r.up + 1] = { sno = tonumber(sno), name = name, pct = tonumber(pct) }
    end
    return r
end

local function load_history()
    history = {}
    for text in string.gmatch(d3hud.get("history"), "[^;]+") do
        local r = unpack_run(text)
        if r then history[#history + 1] = r end   -- newest first
    end
end

local function save_history()
    while #history > d3hud.get("keep") do table.remove(history) end
    local parts = {}
    for _, r in ipairs(history) do parts[#parts + 1] = pack_run(r) end
    d3hud.set("history", table.concat(parts, ";"))
end

load_history()

-- ---- following the run ------------------------------------------------------------------------------------------

local cur = nil            -- the run in progress
local last_ms = nil        -- time of the previous snapshot
local gone_ms = 0          -- how long the rift bar has been missing

local function kind_now(f)
    if f.ui.greater_rift then return "Greater Rift" end
    if f.ui.rift then return "Rift" end
    return nil
end

local function buffs_up()
    local up = {}
    for _, e in ipairs(d3hud.effects()) do
        if e.kind == "buff" and (not e.timed or e.remaining > 0) then up[e.sno] = e.name end
    end
    for _, p in ipairs(d3hud.passives()) do up[p.sno] = p.name end   -- permanent powers: up while they have stacks
    return up
end

local function finish()
    if not cur then return end
    local r = cur
    cur = nil
    if r.active_ms < MIN_RUN_MS then return end
    local result = { kind = r.kind, level = r.level, seconds = r.active_ms / 1000, boss = r.boss_ms and r.boss_ms / 1000 or nil,
        deaths = r.deaths, xp = r.xp, left = r.left, up = {} }
    for _, t in ipairs(r.tracked) do
        result.up[#result.up + 1] = { sno = t.sno, name = t.name, pct = 100 * (r.up_ms[t.sno] or 0) / r.active_ms }
    end
    table.insert(history, 1, result)
    save_history()
end

d3hud.on("snapshot", function()
    local f = d3hud.frame()
    local p = d3hud.player()
    local now = f.time_ms
    local dt = last_ms and now - last_ms or 0
    last_ms = now
    if dt < 0 or dt > MAX_GAP_MS then dt = 0 end
    local kind = kind_now(f)
    if not kind then
        if cur then
            gone_ms = gone_ms + dt
            if gone_ms > GRACE_MS then finish() end
        end
        return
    end
    gone_ms = 0
    if cur and cur.kind ~= kind then finish() end
    if not p then return end
    if not cur then
        cur = { kind = kind, active_ms = 0, deaths0 = p.deaths, deaths = 0, xp = 0, last_xp = p.paragon_xp_to_next,
                level = kind == "Greater Rift" and p.last_rift_level or nil, up_ms = {}, tracked = tracked_list() }
    end
    cur.active_ms = cur.active_ms + dt
    cur.deaths = math.max(0, p.deaths - cur.deaths0)
    if p.paragon_xp_to_next < cur.last_xp then cur.xp = cur.xp + (cur.last_xp - p.paragon_xp_to_next) end
    cur.last_xp = p.paragon_xp_to_next
    if not cur.boss_ms and f.rift_progress >= 0.999 then cur.boss_ms = cur.active_ms end
    if kind == "Greater Rift" and f.rift_timer_fraction >= 0 then cur.left = math.max(0, (1 - f.rift_timer_fraction) * LIMIT_SECONDS) end
    if #cur.tracked > 0 and dt > 0 then
        local up = buffs_up()
        for _, t in ipairs(cur.tracked) do
            if up[t.sno] then cur.up_ms[t.sno] = (cur.up_ms[t.sno] or 0) + dt end
        end
    end
end)

-- ---- the panel --------------------------------------------------------------------------------------------------

local function clock(seconds)
    seconds = math.max(0, math.floor(seconds + 0.5))
    return string.format("%d:%02d", seconds // 60, seconds % 60)
end

local function describe(r)
    local parts = { r.kind == "Greater Rift" and (r.level and ("GR " .. r.level) or "GR") or "Rift", clock(r.seconds) }
    if r.boss then parts[#parts + 1] = "guardian at " .. clock(r.boss) end
    parts[#parts + 1] = r.deaths == 1 and "1 death" or (r.deaths .. " deaths")
    parts[#parts + 1] = fmt.big(r.xp) .. " xp"
    if r.left then parts[#parts + 1] = clock(r.left) .. " left" end
    for _, u in ipairs(r.up) do parts[#parts + 1] = string.format("%s %d%%", u.name, u.pct) end
    return table.concat(parts, "   ")
end

d3hud.panel{
    id = "riftlog", title = "Rift log", width = 560, height = 420,
    draw = function(ui)
        ui.separator_text("Now")
        if cur then
            local r = { kind = cur.kind, level = cur.level, seconds = cur.active_ms / 1000, deaths = cur.deaths, xp = cur.xp,
                        boss = cur.boss_ms and cur.boss_ms / 1000 or nil, left = cur.left, up = {} }
            for _, t in ipairs(cur.tracked) do
                r.up[#r.up + 1] = { name = t.name, pct = cur.active_ms > 0 and 100 * (cur.up_ms[t.sno] or 0) / cur.active_ms or 0 }
            end
            ui.text(describe(r))
            ui.text(string.format("progress %.0f%%", d3hud.frame().rift_progress * 100))
        else
            ui.text_disabled("not in a rift")
        end
        ui.separator_text("Last runs")
        if #history == 0 then ui.text_disabled("none yet (a run counts after 20 seconds)") end
        local n, secs, xp, deaths = 0, 0, 0, 0
        for _, r in ipairs(history) do
            ui.text(describe(r))
            n, secs, xp, deaths = n + 1, secs + r.seconds, xp + r.xp, deaths + r.deaths
        end
        if n > 1 then
            ui.separator()
            ui.text(string.format("%d runs: average %s, %s xp, %.1f deaths; together %s xp in %s", n, clock(secs / n),
                fmt.big(xp / n), deaths / n, fmt.big(xp), clock(secs)))
        end
        if #history > 0 and ui.button("clear the list") then history = {}; save_history() end
        ui.separator_text("Buffs to measure")
        local list = tracked_list()
        for i, t in ipairs(list) do
            ui.text(string.format("%s (%d)", t.name, t.sno))
            ui.same_line()
            if ui.button("remove##rm" .. i) then
                table.remove(list, i)
                save_tracked(list)
                break
            end
        end
        if #list == 0 then ui.text_disabled("none: open the list below while the buff is up and click it") end
        if ui.collapsing_header("Buffs on you right now") then
            local have = {}
            for _, t in ipairs(list) do have[t.sno] = true end
            for _, e in ipairs(d3hud.effects()) do
                if e.kind == "buff" and not have[e.sno] and (not e.timed or e.remaining > 0) then
                    if ui.button(string.format("%s (%d)##pick%d_%d", e.name, e.sno, e.sno, e.slot)) then
                        list[#list + 1] = { sno = e.sno, name = e.name }
                        save_tracked(list)
                        break
                    end
                end
            end
        end
        if ui.collapsing_header("Permanent powers (passives, item powers)") then
            local have = {}
            for _, t in ipairs(list) do have[t.sno] = true end
            for _, p in ipairs(d3hud.passives()) do
                if not have[p.sno] and p.slot == 0 or (not have[p.sno] and p.stacks > 1) then
                    if ui.button(string.format("%s (%d)##perm%d_%d", p.name, p.sno, p.sno, p.slot)) then
                        list[#list + 1] = { sno = p.sno, name = p.name }
                        save_tracked(list)
                        break
                    end
                end
            end
        end
    end,
}
