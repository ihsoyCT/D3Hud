-- Small formatting helpers shared by plugins.
local fmt = {}

-- 3725 -> "1:02:05"
function fmt.hms(seconds)
    local s = math.floor(seconds)
    return string.format("%d:%02d:%02d", s // 3600, s % 3600 // 60, s % 60)
end

-- 0.427 -> "43%"
function fmt.pct(fraction) return string.format("%d%%", math.floor(fraction * 100 + 0.5)) end

-- the game's internal names, readable: "x1_Fallen_Champion" -> "Fallen Champion", "FallenHound" -> "Fallen Hound"
function fmt.pretty(name)
    name = string.gsub(name, "^%l%d+_", "")
    name = string.gsub(name, "_", " ")
    return (string.gsub(name, "(%l)(%u)", "%1 %2"))
end

return fmt
