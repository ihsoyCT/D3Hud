-- Player stats: a test view of what the reader publishes about the character (d3hud.player), its equipped skills
-- (d3hud.skills) and how well the reader sees the game (d3hud.health). Paragon experience per hour is counted from the
-- paragon experience still missing for the next level, which falls while you play and jumps up when you level up.

local fmt = d3hud.require("fmt")

if not d3hud.player then
    d3hud.log("this host is too old for the player data (needs API 7)", "error")
    return
end

local RUNES = { [0] = "-", "A", "B", "C", "D", "E" }
local start = nil  -- { clock, paragon, xp }: the first reading
local last_xp, gained = nil, 0  -- xp gained so far, adding up the drops of the counter

local function track(p)
    local xp = p.paragon_xp_to_next
    if not start then start = { time = d3hud.frame().time_ms, paragon = p.paragon } end
    if last_xp and xp < last_xp then gained = gained + (last_xp - xp) end
    last_xp = xp
    return gained
end


d3hud.panel{
    id = "playerstats", title = "Player", width = 360, height = 520,
    draw = function(ui)
        local h = d3hud.health()
        if h then
            ui.separator_text("Reader health")
            local line = {}
            for _, k in ipairs({ "actors", "records", "attributes", "inventory", "player", "ui", "scenes", "wallet" }) do
                line[#line + 1] = k .. " " .. h[k]
            end
            ui.text_colored(h.ok and 0xFF70E070 or 0xFFFFB040, table.concat(line, ", "))
            ui.text(string.format("%.0f ticks/s | actors seen %d, over the limit %d | unresolved: %d monsters, %d items | %d ui controls missing",
                h.ticks_per_second, h.actors_seen, h.actors_dropped, h.monsters_unresolved, h.items_unresolved, h.ui_missing))
        end
        local p = d3hud.player()
        if not p then
            ui.text_disabled("the reader has not read the character yet")
            return
        end
        ui.separator_text(string.format("%s, level %d, paragon %d (%d points)", p.class, p.level, p.paragon, p.paragon_points))
        ui.text(string.format("Life %s / %s   regen %s/s", fmt.big(p.life), fmt.big(p.life_max), fmt.big(p.life_regen)))
        for _, r in ipairs(p.resources) do
            ui.text(string.format("%s %.0f / %.0f   regen %.1f/s", r.name, r.current, r.max, r.regen))
        end
        ui.text(string.format("Str %s  Dex %s  Int %s  Vit %s", fmt.big(p.strength), fmt.big(p.dexterity), fmt.big(p.intelligence), fmt.big(p.vitality)))
        ui.text(string.format("Armor %s   All resist %s", fmt.big(p.armor), fmt.big(p.resist_all)))
        ui.text(string.format("Resist  phys %s  fire %s  light %s  cold %s  poison %s  arcane %s",
            fmt.big(p.resist.physical), fmt.big(p.resist.fire), fmt.big(p.resist.lightning), fmt.big(p.resist.cold),
            fmt.big(p.resist.poison), fmt.big(p.resist.arcane)))
        ui.text(string.format("Attack speed %.2f   Move speed +%.0f%%   Cooldown reduction %.0f%%", p.attack_speed,
            (p.move_speed - 1) * 100, p.cooldown_reduction * 100))
        ui.text(string.format("Gold find %.0f%%   Magic find %.0f%%", p.gold_find, p.magic_find))
        ui.text(string.format("Rifts: highest solo %d, highest unlocked %d, last started %d", p.highest_solo_rift,
            p.highest_unlocked_rift, p.last_rift_level))
        ui.separator_text("Experience")
        local g = track(p)
        local hours = (d3hud.frame().time_ms - start.time) / 3600000
        ui.text(string.format("Paragon xp missing for the next level: %s", fmt.big(p.paragon_xp_to_next)))
        ui.text(string.format("gained since the plugin started: %s  (%s per hour)", fmt.big(g), hours > 0.0005 and fmt.big(g / hours) or "-"))
        if p.xp_to_next > 0 then ui.text(string.format("Normal xp missing: %s", fmt.big(p.xp_to_next))) end
        ui.separator_text("Skills")
        for _, s in ipairs(d3hud.skills()) do
            local cd = s.on_cooldown and string.format("  cooldown %.1f / %.1f s", s.cooldown_remaining, s.cooldown_duration) or ""
            ui.text(string.format("%s%s %s%s", s.passive and "[P] " or "", s.name, s.passive and "" or ("(rune " .. RUNES[s.rune] .. ")"), cd))
        end
        if ui.button("count the experience from now") then start = nil; last_xp = nil; gained = 0 end
    end,
}
