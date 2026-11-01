-- Materials: gold, blood shards, rift keystones and the crafting materials (what the game's material panel shows), with
-- what you gained since the plugin started. A dock panel lists them; while the inventory is open a table on the screen
-- shows them all at a glance, in four rows.
--
-- The reader finds the counters in the game's memory by shape (core/Wallet.h); they read the same numbers as the
-- game's own panel and update as you pick things up.

local draw = d3hud.require("draw")
local fmt = d3hud.require("fmt")

if not d3hud.wallet then
    d3hud.log("this host is too old for the wallet (needs API 6)", "error")
    return
end

d3hud.setting{ key = "inv_box", type = "bool", default = true, label = "Show the table while the inventory is open",
    tooltip = "Move it and change its colours with the buttons below (the box on the screen)." }
d3hud.setting{ key = "inv_scale", type = "float", default = 1.3, min = 0.7, max = 3.0, label = "Table size" }

-- the table's rows: a title and the cells (key, short label), in the game's order
local TABLE = {
    { "Other", { { "blood_shards", "Blood Shards" }, { "greater_rift_keystone", "Keystones" },
                 { "primordial_ashes", "Ashes" }, { "gold", "Gold" } } },
    { "Bounty", { { "khanduran_rune", "Khanduran" }, { "caldeum_nightshade", "Nightshade" }, { "arreat_war_tapestry", "Tapestry" },
                  { "corrupted_angel_flesh", "Angel Flesh" }, { "westmarch_holy_water", "Holy Water" } } },
    { "Crafting", { { "reusable_parts", "Parts" }, { "arcane_dust", "Dust" }, { "veiled_crystal", "Crystal" },
                    { "deaths_breath", "Breath" }, { "forgotten_soul", "Soul" } } },
    { "Hellfire", { { "leorics_regret", "Leoric's" }, { "vial_of_putridness", "Vial" }, { "idol_of_terror", "Idol" },
                    { "heart_of_fright", "Heart" } } },
}

-- the panel's list: the full names, in the game's order
local ROWS = {
    { "blood_shards", "Blood Shards" }, { "greater_rift_keystone", "Greater Rift Keystones" },
    { "khanduran_rune", "Khanduran Rune" }, { "caldeum_nightshade", "Caldeum Nightshade" },
    { "arreat_war_tapestry", "Arreat War Tapestry" }, { "corrupted_angel_flesh", "Corrupted Angel Flesh" },
    { "westmarch_holy_water", "Westmarch Holy Water" },
    { "reusable_parts", "Reusable Parts" }, { "arcane_dust", "Arcane Dust" }, { "veiled_crystal", "Veiled Crystal" },
    { "deaths_breath", "Death's Breath" }, { "forgotten_soul", "Forgotten Soul" }, { "primordial_ashes", "Primordial Ashes" },
    { "leorics_regret", "Leoric's Regret" }, { "vial_of_putridness", "Vial of Putridness" }, { "idol_of_terror", "Idol of Terror" },
    { "heart_of_fright", "Heart of Fright" },
}

local start = nil  -- the counts when the first reading came in

-- 1234567 -> "1,234,567"
local function grouped(n)
    local s = tostring(math.floor(n))
    local out = s:reverse():gsub("(%d%d%d)", "%1,"):reverse()
    return (out:gsub("^,", ""))
end

local function wallet()
    local w = d3hud.wallet()
    if w and not start then
        start = {}
        for k, v in pairs(w) do start[k] = v end
    end
    return w
end

local function gained(w, key)
    local d = w[key] - (start and start[key] or w[key])
    if d == 0 then return "" end
    return string.format("   %s%s", d > 0 and "+" or "-", grouped(math.abs(d)))
end

local LABEL_RGB, NUMBER_RGB, ZERO_RGB, TITLE_RGB = 0xB8B0A0, 0xFFE070, 0x807868, 0xE8C070
local SHARD_RGB, KEY_RGB = 0xFF7070, 0x80D0FF

-- the table: row titles on the left, each cell a small label over its number; the columns line up across the rows
d3hud.box{
    id = "inventory", title = "Materials (inventory)", x = 0.03, y = 0.22,
    draw = function(x, y)
        if not d3hud.get("inv_box") or not d3hud.frame().ui.inventory then return end
        local w = wallet()
        if not w then return end
        local scale = d3hud.get("inv_scale")
        local small = scale * 0.75
        local title_w, cell_w = 0, 0
        local rows = {}
        for _, row in ipairs(TABLE) do
            local cells = {}
            local tw = d3hud.text_size(row[1], small)
            title_w = math.max(title_w, tw)
            for _, c in ipairs(row[2]) do
                local n = w[c[1]] or 0
                local number = c[1] == "gold" and fmt.big(n) or grouped(n)
                cells[#cells + 1] = { label = c[2], number = number, n = n, key = c[1] }
                cell_w = math.max(cell_w, d3hud.text_size(c[2], small), d3hud.text_size(number, scale))
            end
            rows[#rows + 1] = { title = row[1], cells = cells }
        end
        local _, label_h = d3hud.text_size("Ag", small)
        local _, number_h = d3hud.text_size("0", scale)
        local row_h = label_h + number_h + 6
        local pad = 14 * scale
        local top = y
        local widest = 0
        for _, row in ipairs(rows) do
            draw.shadowed(x, y + label_h, draw.argb(TITLE_RGB, 255), row.title, small)
            local cx = x + title_w + pad
            for _, c in ipairs(row.cells) do
                draw.shadowed(cx, y, draw.argb(LABEL_RGB, 255), c.label, small)
                local rgb = c.n == 0 and ZERO_RGB or c.key == "blood_shards" and SHARD_RGB
                    or c.key == "greater_rift_keystone" and KEY_RGB or NUMBER_RGB
                draw.shadowed(cx, y + label_h + 2, draw.argb(rgb, 255), c.number, scale)
                cx = cx + cell_w + pad
            end
            widest = math.max(widest, cx - pad - x)
            y = y + row_h + 4
        end
        return widest, y - top - 4
    end,
}

d3hud.panel{
    id = "materials", title = "Materials", width = 330, height = 360,
    draw = function(ui)
        local w = wallet()
        if not w then
            ui.text_disabled("the reader has not found the counters yet (is a character in the world?)")
            return
        end
        ui.text(string.format("Gold          %s%s", grouped(w.gold), gained(w, "gold")))
        ui.separator()
        for _, row in ipairs(ROWS) do
            ui.text(string.format("%-24s %s%s", row[2], grouped(w[row[1]]), gained(w, row[1])))
        end
        ui.separator()
        if ui.button("count the gains from now") then start = nil end
    end,
}
