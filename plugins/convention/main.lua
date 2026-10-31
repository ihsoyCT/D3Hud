-- Convention of Elements: how many seconds until the element you want comes round.
--
-- The ring gives one element at a time and moves on every 4 seconds. The game keeps the element that is active as a
-- buff of the ring (power 430674) in an icon slot of its own, one slot per element, and the slot changes every
-- 240 ticks. The order of the elements differs between hero classes, so it is not written down here: the plugin
-- watches which slot follows which (about one lap, 16 to 28 s) and relearns as soon as the game contradicts what it
-- has learned, e.g. after you change the hero. What it cannot know is which element YOU want: press "mine" in the
-- Convention tab next to it; that is remembered for that cycle (a different hero class has a different one and asks
-- again).
--
-- The slots are the seven elements in alphabetical order (the game's own: TurboHUD's per class lists of slots match
-- the classes' elements, e.g. Wizard 1 2 3 5 = Arcane, Cold, Fire, Lightning).

local draw = d3hud.require("draw")
local store = d3hud.require("store")

if not d3hud.effects then
    d3hud.log("this host is too old for effects (needs API 2)", "error")
    return
end

d3hud.setting{ key = "hud", type = "bool", default = true, label = "Show the countdown on the screen",
    tooltip = "Move it and change its colours with the buttons below (the box on the screen)." }
d3hud.setting{ key = "hud_scale", type = "float", default = 1.6, min = 0.5, max = 4.0, label = "Size" }
d3hud.setting{ key = "warn", type = "float", default = 1.5, min = 0, max = 4, label = "Highlight when it is this close (s)" }
d3hud.setting{ key = "cycle", type = "string", default = "", hidden = true }  -- learned: slot=next slot
d3hud.setting{ key = "mine", type = "string", default = "", hidden = true }   -- your element per cycle: cycle=slot

local COE = 430674
local MAX_STEP = 8     -- the element buffs last 4 s; the ring's 16 s marker (a lap) is not one
local GRACE = 2        -- seconds without the ring's buff before we forget where in the cycle we were
local YELLOW, WHITE, GRAY = 0xFFFFE066, 0xFFFFFFFF, 0xFFB0B0B8

-- slot -> the element, with a colour people know it by (0xAARRGGBB)
local ELEMENTS = {
    [1] = { "Arcane", 0xFFC76BFF }, [2] = { "Cold", 0xFF6FD0FF }, [3] = { "Fire", 0xFFFF7A33 },
    [4] = { "Holy", 0xFFF5D76E }, [5] = { "Lightning", 0xFF6F8CFF }, [6] = { "Physical", 0xFFD8D8D8 },
    [7] = { "Poison", 0xFF86DC4E },
}
local function element_name(slot) return ELEMENTS[slot] and ELEMENTS[slot][1] or ("Element " .. slot) end
local function element_color(slot) return ELEMENTS[slot] and ELEMENTS[slot][2] or WHITE end

local following = {}  -- slot -> the slot that comes after it
local mine = {}       -- cycle (its slots, "2-3-5-6") -> the slot you want
local cur             -- { slot, remaining, step } of the element that is active now
local last_slot, last_seen
local lap             -- the learned cycle as a list of slots starting at `cur`, once it is complete

local function numbers(map)
    local out = {}
    for k, v in pairs(map) do out[tonumber(k)] = tonumber(v) end
    return out
end

following = numbers(store.unpack_map(d3hud.get("cycle"), { sep = "," }))
for cycle, slot in pairs(store.unpack_map(d3hud.get("mine"), { sep = ";", number = true })) do mine[cycle] = slot end

local function save_cycle() d3hud.set("cycle", store.pack_map(following, { sep = "," })) end
local function save_mine() d3hud.set("mine", store.pack_map(mine, { sep = ";" })) end

-- the elements in the order they come, starting at `from`, when the learned order closes into a loop; else nil
local function loop_from(from)
    local list, seen, slot = {}, {}, from
    while slot and not seen[slot] do
        seen[slot] = true
        list[#list + 1] = slot
        slot = following[slot]
    end
    if slot == from then return list end
end

local function cycle_key(list)
    local sorted = { table.unpack(list) }
    table.sort(sorted)
    return table.concat(sorted, "-")
end

-- the ring's active element this frame: the 4 s buff of the ring (not the 16 s lap marker)
local function active_element()
    local best
    for _, e in ipairs(d3hud.effects()) do
        if e.sno == COE and e.kind == "buff" and e.timed and e.duration > 0 and e.duration <= MAX_STEP and e.remaining > 0 then
            if not best or e.remaining < best.remaining then best = e end
        end
    end
    return best
end

local function learn(prev, slot)
    if following[prev] and following[prev] ~= slot then
        following = {}  -- the game disagrees: another hero, or the ring changed
    end
    if following[prev] ~= slot then
        following[prev] = slot
        save_cycle()
    end
end

local function update()
    local now = d3hud.frame().time_ms / 1000
    local e = active_element()
    if e then
        if last_slot and e.slot ~= last_slot then learn(last_slot, e.slot) end
        last_slot, last_seen = e.slot, now
        cur = { slot = e.slot, remaining = e.remaining, step = e.duration }
        lap = loop_from(e.slot)
    elseif last_seen and now - last_seen > GRACE then
        cur, last_slot, last_seen, lap = nil, nil, nil, nil
    elseif cur then
        cur.remaining = 0  -- between two elements for a moment
    end
end

-- The slot you want for the cycle that is running (nil: not chosen yet, or the cycle is not known yet).
local function wanted()
    return lap and mine[cycle_key(lap)] or nil
end

-- `slot` as `active, seconds`: true and the time it has left while it is the active element, else false and the time
-- until it starts (nil while the order up to it is not known). Between two elements the active one has 0 s left and
-- the next one starts in 0 s.
local function until_slot(slot)
    if not cur then return nil end
    if slot == cur.slot then return true, cur.remaining end
    local t, s = cur.remaining, following[cur.slot]
    for _ = 1, 32 do
        if not s then return nil end
        if s == slot then return false, t end
        t = t + cur.step
        s = following[s]
    end
end

local function countdown_text()
    local w = wanted()
    if not w then
        if not lap then return "Convention of Elements: learning the order...", GRAY end
        return "Convention of Elements: pick your element in the Convention tab", GRAY
    end
    local active, seconds = until_slot(w)
    if seconds == nil then return nil end
    local name = element_name(w)
    if active then return string.format("%s NOW  %.1f", string.upper(name), seconds), element_color(w) end
    return string.format("%s in %.1f", name, seconds), seconds <= d3hud.get("warn") and YELLOW or element_color(w)
end

d3hud.on("frame", update)

-- the countdown on the screen: a box the user can move (Settings > Plugins) and give another look
d3hud.box{
    id = "countdown", title = "Convention of Elements", x = 0.45, y = 0.24,
    draw = function(x, y)
        if not cur or not d3hud.get("hud") then return end
        local text, color = countdown_text()
        if not text then return end
        local scale = d3hud.get("hud_scale")
        local w, h = d3hud.text_size(text, scale)
        draw.shadowed(x, y, color, text, scale)
        return w, h
    end,
}

d3hud.panel{
    id = "convention", title = "Convention", width = 300, height = 220,
    draw = function(ui)
        if not cur then
            ui.text_disabled("Convention of Elements is not running (ring not worn, or the reader has not found you yet)")
            return
        end
        local w = wanted()
        local text, color = countdown_text()
        if text then ui.text_colored(color, text) end
        ui.separator()
        if not lap then
            ui.text_disabled("Learning the order of the elements: it takes one lap of the ring.")
            ui.text_colored(element_color(cur.slot), string.format("now: %s, %.1f s left", element_name(cur.slot), cur.remaining))
            return
        end
        -- the elements in the order they come, the active one first
        local start_t = cur.remaining
        for i, slot in ipairs(lap) do
            local label
            if i == 1 then
                label = string.format("now      %4.1f s left", cur.remaining)
            else
                label = string.format("in %4.1f s", start_t + cur.step * (i - 2))
            end
            ui.text_colored(element_color(slot), string.format("%s    %s%s", label, element_name(slot), slot == w and "    <- yours" or ""))
            ui.same_line()
            if ui.button("mine##" .. slot) then
                mine[cycle_key(lap)] = slot
                save_mine()
            end
        end
        ui.separator()
        ui.text_disabled("Press \"mine\" next to the element you want (once per hero class).")
    end,
}
