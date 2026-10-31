-- Small formatting helpers shared by plugins.
local fmt = {}

-- 3725 -> "1:02:05"
function fmt.hms(seconds)
    local s = math.floor(seconds)
    return string.format("%d:%02d:%02d", s // 3600, s % 3600 // 60, s % 60)
end

-- 1234567 -> "1.23M" (K, M, B, T, Qa); big life and damage numbers
function fmt.big(n)
    local units = { "", "K", "M", "B", "T", "Qa" }
    local i = 1
    while math.abs(n) >= 1000 and i < #units do
        n = n / 1000
        i = i + 1
    end
    local digits = (math.abs(n) >= 100 or i == 1) and 0 or math.abs(n) >= 10 and 1 or 2
    return string.format("%." .. digits .. "f%s", n, units[i])
end

-- 3.14 -> "3.1s", 75 -> "1:15"
function fmt.dur(seconds)
    if seconds < 60 then return string.format("%.1fs", seconds) end
    local s = math.floor(seconds)
    return string.format("%d:%02d", s // 60, s % 60)
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
