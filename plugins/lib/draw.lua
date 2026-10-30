-- The standard look of everything plugins draw on the screen. Colours are plain 0xRRGGBB numbers (like the
-- colour settings); the functions add the alpha themselves, so plugins stay consistent:
--   * bars: black border, dark background, the colour as the fill
--   * tags (affixes, labels): a box with a border in the colour, a translucent fill of the same colour, and
--     black or white text, whichever reads better on that fill
--   * plain text: white with a black drop shadow
-- Everything draws in screen pixels, from a `frame` event handler.
local draw = {}

draw.WHITE, draw.BLACK = 0xFFFFFFFF, 0xFF000000
draw.PANEL = 0x90000000        -- the dark box behind a list
local BAR_BACK = 0xC0101010
draw.TAG_FILL = 112            -- default opacity (0..255) of a tag's fill

-- 0xRRGGBB and an opacity 0..255 -> 0xAARRGGBB
function draw.argb(rgb, alpha) return ((alpha or 255) & 0xFF) << 24 | (rgb & 0xFFFFFF) end

function draw.luminance(rgb)
    local r, g, b = (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255
    return (0.299 * r + 0.587 * g + 0.114 * b) / 255
end

-- Black or white text for a fill of this colour and opacity, as it looks over the (mostly dark) game.
function draw.contrast(rgb, alpha)
    local a = (alpha or draw.TAG_FILL) / 255
    return draw.luminance(rgb) * a + 0.12 * (1 - a) > 0.45 and draw.BLACK or draw.WHITE
end

-- text with a drop shadow
function draw.shadowed(x, y, color, text, scale)
    d3hud.draw_text(x + 1, y + 1, draw.BLACK, text, scale)
    d3hud.draw_text(x, y, color, text, scale)
end

-- shortens text so it is at most `maxw` pixels wide (".." at the end when it was cut)
function draw.fit(text, maxw, scale)
    if d3hud.text_size(text, scale) <= maxw then return text end
    while #text > 1 do
        text = string.sub(text, 1, -2)
        if d3hud.text_size(text .. "..", scale) <= maxw then return text .. ".." end
    end
    return text
end

-- A bar filled to `fraction` (0..1) in `rgb`.
function draw.bar(x0, y0, w, h, fraction, rgb)
    d3hud.draw_rect(x0 - 1, y0 - 1, x0 + w + 1, y0 + h + 1, draw.BLACK)
    d3hud.draw_rect(x0, y0, x0 + w, y0 + h, BAR_BACK)
    d3hud.draw_rect(x0, y0, x0 + w * math.max(0, math.min(fraction, 1)), y0 + h, draw.argb(rgb))
end

-- width, height of a tag with this text
function draw.tag_size(text, scale, opts)
    local tw, th = d3hud.text_size(text, scale)
    local pad = (opts and opts.padding) or math.max(2, math.floor(th * 0.25 + 0.5))
    return tw + 2 * pad, th + 2 * pad, pad
end

-- A tag at x, y (its top left corner): border and translucent fill in `rgb`, black or white text.
-- opts: alpha (fill opacity 0..255), padding (px), text (0xAARRGGBB to force the text colour).
-- Returns its width and height.
function draw.tag(x, y, text, rgb, scale, opts)
    opts = opts or {}
    local alpha = opts.alpha or draw.TAG_FILL
    local w, h, pad = draw.tag_size(text, scale, opts)
    d3hud.draw_rect(x, y, x + w, y + h, draw.argb(rgb, alpha))
    d3hud.draw_rect(x, y, x + w, y + h, draw.argb(rgb), false, 1)
    d3hud.draw_text(x + pad, y + pad, opts.text or draw.contrast(rgb, alpha), text, scale)
    return w, h
end

-- A row of tags, items = { {text = "Molten", color = 0xFF6A3D}, ... }, left to right. Tags that do not fit
-- into `maxw` are left out. opts: anchor = "center" (x is the middle of the row), gap (px), plus the tag options.
-- Returns the width and height of the row.
function draw.tags(x, y, items, scale, maxw, opts)
    opts = opts or {}
    local gap = opts.gap or 3
    local widths, h = {}, 0
    for i, item in ipairs(items) do widths[i], h = draw.tag_size(item.text, scale, opts) end
    local n, total = #items, 0
    for i = 1, n do total = total + widths[i] + (i > 1 and gap or 0) end
    while n > 1 and total > maxw do
        total = total - widths[n] - gap
        n = n - 1
    end
    if opts.anchor == "center" then x = x - total / 2 end
    for i = 1, n do
        draw.tag(x, y, items[i].text, items[i].color, scale, opts)
        x = x + widths[i] + gap
    end
    return total, h
end

return draw
