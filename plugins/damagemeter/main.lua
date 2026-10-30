-- Damage meter: how much damage you deal to elites and bosses, how fast, and when they will be dead.
--
-- The reader publishes the exact life of champions, rares and bosses, so the damage is measured from how that
-- life falls from one snapshot to the next (healing, health link and regeneration are ignored). Trash is not
-- counted: its life is not read. A fight starts with the first damage and ends when nothing was hurt for a few
-- seconds. The numbers show in the "DPS" dock panel and, optionally, as a line on the screen.
--
-- Caveats: damage that kills is counted from the monster's last known life (overkill cannot be seen); damage
-- shared by Health Link shows on every linked monster; a monster that leaves the reader's range while hurt is
-- not counted as a kill unless it was almost dead.

local draw = d3hud.require("draw")
local fmt = d3hud.require("fmt")

d3hud.setting{ key = "window", type = "float", default = 5, min = 1, max = 15, label = "DPS window (s)",
    tooltip = "The current DPS is the damage of this many seconds, divided by them." }
d3hud.setting{ key = "idle", type = "float", default = 4, min = 1, max = 15, label = "A fight ends after no damage for (s)" }
d3hud.setting{ key = "ttk", type = "bool", default = true, label = "Predict the time to kill the elites on the screen" }
d3hud.setting{ key = "hud", type = "bool", default = true, label = "Show the DPS on the screen" }
d3hud.setting{ key = "hud_x", type = "int", default = 50, min = 0, max = 100, label = "Screen position: from the left (%)" }
d3hud.setting{ key = "hud_y", type = "int", default = 13, min = 0, max = 100, label = "Screen position: from the top (%)" }
d3hud.setting{ key = "hud_scale", type = "float", default = 1.0, min = 0.5, max = 3.0, label = "Size" }
d3hud.setting{ key = "best_dps", type = "float", default = 0, hidden = true }
d3hud.setting{ key = "best_total", type = "float", default = 0, hidden = true }

local KIND = d3hud.kind
local TRACKED = { [KIND.champion] = true, [KIND.rare] = true, [KIND.boss] = true }
local SHOW_LAST = 8   -- seconds the summary of a finished fight stays on the screen
local KILL_FRACTION = 0.15  -- an elite that vanishes below this much life, right after being hurt, was killed
local MAX_GAP = 5     -- a longer pause between snapshots restarts the measuring (reader stopped, game paused)

local seen = {}       -- id -> { hp, max, name, frac, hurt = time of the last damage }
local events = {}     -- { t, dmg } of the last seconds, for the rolling DPS and the peak
local fight           -- { start, last, total, kills, peak, target = {name, max} } while one is going
local last_fight      -- the summary of the last finished fight (+ ended = when)
local history = {}    -- finished fights of this session, newest last
local last_t
local state = { dps = 0, remaining = 0, elites = 0, ttk = nil }  -- what the screen and the panel show

local function pretty(name) return fmt.pretty(name) end

local function add_damage(t, dmg, name, max)
    if not fight then fight = { start = t, last = t, total = 0, kills = 0, peak = 0 } end
    fight.total = fight.total + dmg
    fight.last = t
    events[#events + 1] = { t = t, dmg = dmg }
    if not fight.target or max > fight.target.max then fight.target = { name = name, max = max } end
end

local function finish(t)
    local dur = math.max(fight.last - fight.start, 0.5)
    local summary = { total = fight.total, dur = dur, dps = fight.total / dur, peak = fight.peak, kills = fight.kills,
                      name = fight.target and pretty(fight.target.name) or "?", ended = t }
    history[#history + 1] = summary
    if #history > 12 then table.remove(history, 1) end
    last_fight = summary
    if summary.dps > d3hud.get("best_dps") then d3hud.set("best_dps", summary.dps) end
    if summary.total > d3hud.get("best_total") then d3hud.set("best_total", summary.total) end
    fight = nil
end

local function update()
    local f = d3hud.frame()
    local t = f.time_ms / 1000
    if last_t and (t < last_t or t - last_t > MAX_GAP) then seen, events, fight = {}, {}, nil end
    last_t = t

    local current, remaining, elites = {}, 0, 0
    for _, a in ipairs(d3hud.actors()) do
        if TRACKED[a.kind] and a.hp >= 0 and a.hp_max > 0 then
            current[a.id] = true
            local s = seen[a.id]
            if s then
                local lost = s.hp - a.hp_current
                if lost > 0 and lost <= s.max * 1.01 then
                    add_damage(t, lost, a.name, a.hp_max)
                    s.hurt = t
                end
                s.hp, s.max, s.frac, s.name = a.hp_current, a.hp_max, a.hp, a.name
            else
                seen[a.id] = { hp = a.hp_current, max = a.hp_max, frac = a.hp, name = a.name, hurt = -1e9 }
            end
            if a.hp > 0 and d3hud.world_to_screen(a.x, a.y, 0) then  -- alive and on the screen
                remaining = remaining + a.hp_current
                elites = elites + 1
            end
        end
    end

    -- an elite that is gone: killed if it was almost dead and had just been hurt; its last life is the final blow
    for id, s in pairs(seen) do
        if not current[id] then
            if s.frac <= KILL_FRACTION and t - s.hurt < 2 then
                if s.hp > 0 then add_damage(t, s.hp, s.name, s.max) end
                if fight then fight.kills = fight.kills + 1 end
            end
            seen[id] = nil
        end
    end

    local window = d3hud.get("window")
    while events[1] and events[1].t < t - 15 do table.remove(events, 1) end
    local sum, sum1 = 0, 0
    for _, e in ipairs(events) do
        if e.t > t - window then sum = sum + e.dmg end
        if e.t > t - 1 then sum1 = sum1 + e.dmg end
    end
    if fight then
        fight.peak = math.max(fight.peak, sum1)
        state.dps = sum / math.max(1, math.min(window, t - fight.start))
        if t - fight.last > d3hud.get("idle") then
            finish(t)
            state.dps = 0
        end
    else
        state.dps = 0
    end
    state.remaining, state.elites = remaining, elites
    state.ttk = (d3hud.get("ttk") and fight and state.dps > 0 and remaining > 0) and remaining / state.dps or nil
    state.t = t
end

d3hud.on("snapshot", update)

local function lines()
    local out = {}
    if fight then
        out[#out + 1] = string.format("DPS %s/s   peak %s/s   this fight %s in %s", fmt.big(state.dps), fmt.big(fight.peak),
            fmt.big(fight.total), fmt.dur(math.max(fight.last - fight.start, 0)))
        if state.ttk then
            out[#out + 1] = string.format("%d elite%s on the screen: %s of life left, about %s to go", state.elites,
                state.elites == 1 and "" or "s", fmt.big(state.remaining), fmt.dur(state.ttk))
        end
    elseif last_fight and state.t and state.t - last_fight.ended < SHOW_LAST then
        out[#out + 1] = string.format("%s: %s in %s (%s/s)%s", last_fight.name, fmt.big(last_fight.total), fmt.dur(last_fight.dur),
            fmt.big(last_fight.dps), last_fight.kills > 0 and string.format(", %d killed", last_fight.kills) or "")
    end
    return out
end

d3hud.on("frame", function()
    if not d3hud.get("hud") then return end
    local out = lines()
    if #out == 0 then return end
    local f = d3hud.frame()
    local scale = d3hud.get("hud_scale")
    local x, y = f.screen_w * d3hud.get("hud_x") / 100, f.screen_h * d3hud.get("hud_y") / 100
    for _, text in ipairs(out) do
        local w, h = d3hud.text_size(text, scale)
        draw.shadowed(x - w / 2, y, draw.WHITE, text, scale)
        y = y + h + 2
    end
end)

-- the dock panel: "DPS"
d3hud.panel{
    id = "dps", title = "DPS",
    draw = function(ui)
        if not fight then ui.text_disabled("no fight right now") end
        for _, text in ipairs(lines()) do ui.text(text) end
        ui.separator_text("Records")
        ui.text(string.format("best DPS %s/s     most damage in a fight %s", fmt.big(d3hud.get("best_dps")),
            fmt.big(d3hud.get("best_total"))))
        ui.separator_text("Fights this session")
        if #history == 0 then ui.text_disabled("none yet") end
        for i = #history, 1, -1 do
            local h = history[i]
            ui.text(string.format("%s   %s in %s   %s/s   peak %s/s%s", h.name, fmt.big(h.total), fmt.dur(h.dur), fmt.big(h.dps),
                fmt.big(h.peak), h.kills > 0 and string.format("   %d killed", h.kills) or ""))
        end
        if ui.button("clear the session") then history, last_fight = {}, nil end
        ui.same_line()
        if ui.button("reset the records") then
            d3hud.set("best_dps", 0)
            d3hud.set("best_total", 0)
        end
    end,
}
