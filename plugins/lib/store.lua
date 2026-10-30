-- Packing small tables into the text of a hidden string setting (settings are the only thing a plugin can
-- persist) and back. Names must not contain the separator or "=".
local store = {}

local function esc(sep) return "%" .. sep end

-- { "a", "b" } <-> "a,b"
function store.pack_list(list, sep) return table.concat(list, sep or ",") end

function store.unpack_list(text, sep)
    local list = {}
    for item in string.gmatch(text, "[^" .. esc(sep or ",") .. "]+") do list[#list + 1] = item end
    return list
end

-- { b = 2, a = 1 } -> "a=1;b=2" (sorted, so the text is stable). opts: sep (";"), format (value -> text)
function store.pack_map(map, opts)
    opts = opts or {}
    local parts = {}
    for name, value in pairs(map) do
        parts[#parts + 1] = name .. "=" .. (opts.format and opts.format(value) or tostring(value))
    end
    table.sort(parts)
    return table.concat(parts, opts.sep or ";")
end

-- "a=1;b=2" -> { a = "1", b = "2" }; opts: sep (";"), number (true: the values become numbers)
function store.unpack_map(text, opts)
    opts = opts or {}
    local sep = esc(opts.sep or ";")
    local map = {}
    for name, value in string.gmatch(text, "([^=" .. sep .. "]+)=([^" .. sep .. "]+)") do
        map[name] = opts.number and (tonumber(value) or 0) or value
    end
    return map
end

return store
