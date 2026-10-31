#!/usr/bin/env python3
"""Draws the seven element icons of the Convention of Elements plugin (plugins/convention/<element>.png).

No imaging library needed: shapes are rasterised with 4x4 supersampling and written as RGBA PNGs with zlib.
    python tools/gen_element_icons.py
"""
import math
import os
import struct
import zlib

SIZE = 48
SS = 4  # supersampling per axis
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "plugins", "convention")

ELEMENTS = {  # file name -> colour 0xRRGGBB (the same colours the plugin writes its text in)
    "arcane": 0xC76BFF, "cold": 0x6FD0FF, "fire": 0xFF7A33, "holy": 0xF5D76E,
    "lightning": 0x6F8CFF, "physical": 0xD8D8D8, "poison": 0x86DC4E,
}


def in_polygon(x, y, pts):
    inside = False
    j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


def near_segment(x, y, a, b, half):
    (ax, ay), (bx, by) = a, b
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(x - (ax + t * dx), y - (ay + t * dy)) <= half


def circle(cx, cy, r):
    return lambda x, y: math.hypot(x - cx, y - cy) <= r


def polygon(*pts):
    return lambda x, y: in_polygon(x, y, pts)


def union(*shapes):
    return lambda x, y: any(s(x, y) for s in shapes)


def line(a, b, width):
    return lambda x, y: near_segment(x, y, a, b, width / 2)


def snowflake():
    shapes = []
    for k in range(3):  # three lines through the centre: six spokes
        a = math.radians(60 * k + 90)
        dx, dy = 15 * math.cos(a), 15 * math.sin(a)
        shapes.append(line((24 - dx, 24 - dy), (24 + dx, 24 + dy), 2.6))
        for sign in (-1, 1):  # a small V near each end
            ex, ey = 24 + sign * dx * 0.62, 24 + sign * dy * 0.62
            for turn in (-0.8, 0.8):
                b = a + (0 if sign > 0 else math.pi) + turn
                shapes.append(line((ex, ey), (ex + 5 * math.cos(b), ey + 5 * math.sin(b)), 2.0))
    return union(*shapes)


SYMBOLS = {
    "fire": polygon((24, 7), (31, 18), (35, 27), (32, 36), (24, 41), (16, 36), (13, 28), (18, 22), (20, 27), (22, 17)),
    "cold": snowflake(),
    "lightning": polygon((28, 6), (14, 26), (22, 26), (19, 42), (34, 21), (26, 21)),
    "holy": union(polygon((21, 8), (27, 8), (27, 40), (21, 40)), polygon((12, 18), (36, 18), (36, 24), (12, 24))),
    "arcane": polygon((24, 7), (28.5, 19.5), (41, 24), (28.5, 28.5), (24, 41), (19.5, 28.5), (7, 24), (19.5, 19.5)),
    # two crossed blades: a long thin triangle each, with a small guard across it near the hilt
    "physical": union(polygon((10, 10), (14, 8), (38, 34), (34, 38)), polygon((38, 10), (34, 8), (10, 34), (14, 38)),
                      line((9, 18), (18, 9), 2.4), line((39, 18), (30, 9), 2.4)),
    "poison": union(circle(24, 29, 10), polygon((24, 7), (15.5, 25), (32.5, 25))),
}


def mix(under, over, a):
    return tuple(u * (1 - a) + o * a for u, o in zip(under, over))


def render(name, rgb):
    colour = ((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255)
    dark = (22, 24, 32)
    symbol = SYMBOLS[name]
    pixels = []
    for py in range(SIZE):
        row = []
        for px in range(SIZE):
            cover = {"disc": 0, "ring": 0, "sym": 0}
            for sy in range(SS):
                for sx in range(SS):
                    x, y = px + (sx + 0.5) / SS, py + (sy + 0.5) / SS
                    d = math.hypot(x - 24, y - 24)
                    if d <= 23.5:
                        cover["disc"] += 1
                        if d >= 21.0:
                            cover["ring"] += 1
                    if symbol(x, y):
                        cover["sym"] += 1
            n = SS * SS
            disc, ring, sym = cover["disc"] / n, cover["ring"] / n, cover["sym"] / n
            alpha = disc * 0.92
            col = mix(dark, colour, 0.0)
            col = mix(col, tuple(c * 0.85 for c in colour), ring / disc if disc else 0)
            bright = tuple(min(255.0, c * 1.1 + 25) for c in colour)
            col = mix(col, bright, sym)
            alpha = max(alpha, sym)
            row.append((int(col[0]), int(col[1]), int(col[2]), int(round(alpha * 255))))
        pixels.append(row)
    return pixels


def png(pixels):
    def chunk(kind, data):
        c = struct.pack(">I", len(data)) + kind + data
        return c + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + b"".join(bytes(p) for p in row) for row in pixels)
    ihdr = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    for name, rgb in ELEMENTS.items():
        path = os.path.join(OUT, name + ".png")
        with open(path, "wb") as f:
            f.write(png(render(name, rgb)))
        print(path, os.path.getsize(path), "bytes")


if __name__ == "__main__":
    main()
