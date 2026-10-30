"""Convert TurboHUD's scene layout data into D3Hud's compact scene_cells.bin.

  python tools/extract_scene_cells.py <TurboHUD dir>/data analysis/scene_cells.bin

Input (TurboHUD 2.6.10, format per decompiled Turbo.SNO Subs/Scenes.cs + SnoScene.cs):
  scene.bin       per scene: u32 sno, varint cellPos, varint cellCount, varint sqW, sqH,
                  sqCount, varint sqPos, varint-length string code
  scene_cell.bin  28-byte nav cells: f32 minX, minY, -, maxX, maxY, -, u32 flags
                  (scene-relative, 0..240; flag 1 = walkable)
Output (little endian):
  "D3SC" u32 version=2, u32 sceneCount, then per scene: u32 sno, u32 flags, u32 n,
  n * f32[4] (minX, minY, maxX, maxY) - walkable cells only.
  flags (from the scene code): 1 = level entrance tile ("*_entrance_*"),
  2 = exit tile ("*_exit_*", incl. rift "*_dungeonstone_exit_*").
"""
import os
import struct
import sys


def varint(b, p):
    n = s = 0
    while True:
        c = b[p]
        p += 1
        n |= (c & 127) << s
        s += 7
        if not c & 128:
            return n, p


def main(data_dir, out_path):
    idx = open(os.path.join(data_dir, "scene.bin"), "rb").read()
    cells = open(os.path.join(data_dir, "scene_cell.bin"), "rb").read()
    scenes, p = [], 0
    while p < len(idx):
        sno, = struct.unpack_from("<I", idx, p)
        p += 4
        pos, p = varint(idx, p)
        cnt, p = varint(idx, p)
        for _ in range(4):  # nav square w, h, count, position
            _, p = varint(idx, p)
        n, p = varint(idx, p)
        code = idx[p:p + n].decode("utf-8", "replace").lower()
        p += n
        flags = (1 if "_entrance" in code else 0) | (2 if "_exit" in code else 0)
        rects = []
        for i in range(cnt):
            x0, y0, _, x1, y1, _, cellFlags = struct.unpack_from("<6fI", cells, pos + i * 28)
            if cellFlags & 1:
                rects.append((x0, y0, x1, y1))
        if rects:
            scenes.append((sno, flags, rects))
    with open(out_path, "wb") as f:
        f.write(b"D3SC" + struct.pack("<II", 2, len(scenes)))
        for sno, flags, rects in scenes:
            f.write(struct.pack("<III", sno, flags, len(rects)))
            for r in rects:
                f.write(struct.pack("<4f", *r))
    print(f"{len(scenes)} scenes ({sum(1 for s in scenes if s[1] & 1)} entrance, "
          f"{sum(1 for s in scenes if s[1] & 2)} exit), {sum(len(s[2]) for s in scenes)} walkable cells "
          f"-> {out_path}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
