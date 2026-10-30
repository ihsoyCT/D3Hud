"""Locate and dump the actor array in a capture (offset-free signature, current client).

Actor record (stride 0x4F8): +0x00 u32 id (low 16 bits = slot index, -1 = free), +0x08 name[128]
(empty in this client), +0x88 u32 acd id (-1 = client-only), +0x8C u32 actor SNO, +0x90 u32 -1,
+0xA8 rotation (2 floats), +0xB0 position (x, z, y).

  python tools/actors.py <cap> [kind ...]     kinds e.g. Monster Gizmo Item Player
"""
import collections
import sys
import numpy as np

from ptrscan import Cap, MEM_IMAGE

STRIDE = 0x4F8


def load_sno(path="analysis/sno_actors.tsv"):
    out = {}
    for line in open(path, encoding="utf-8").read().splitlines()[1:]:
        sno, name, sub, mob, kind = line.split("\t")
        out[int(sno)] = (name, kind, sub)
    return out


def find_arrays(c):
    """Group records by implied array base (addr - index*STRIDE); return {base: n_records}."""
    bases = collections.Counter()
    for r in c.regions:
        if r["type"] == MEM_IMAGE or r["size"] < STRIDE * 4:
            continue
        u = c.data(r)[: r["size"] // 4 * 4].view(np.uint32)
        n = u.size - STRIDE // 4 - 0x40
        if n <= 0:
            continue
        i = np.arange(0, n, 2)  # records are 8-aligned
        nxt = i + STRIDE // 4
        m = (u[i] != 0xFFFFFFFF) & (u[i + 0x90 // 4] == 0xFFFFFFFF) & (u[i + 2] == 0) & (u[i + 3] == 0)
        m &= ((u[nxt] & 0xFFFF) == ((u[i] & 0xFFFF) + 1)) & (u[nxt + 0x90 // 4] == 0xFFFFFFFF)
        for k in i[m]:
            a = r["base"] + int(k) * 4
            bases[a - int(u[k] & 0xFFFF) * STRIDE] += 1
    return bases


def walk(c, base, max_slots=8192):
    out = []
    misses = 0
    for slot in range(max_slots):
        b = c.read(base + slot * STRIDE, 0xC0)
        if b is None:
            break
        u = np.frombuffer(b, np.uint32)
        f = np.frombuffer(b, np.float32)
        aid = int(u[0])
        if aid == 0xFFFFFFFF or (aid & 0xFFFF) != slot:
            misses += 1
            if misses > 256:
                break
            continue
        misses = 0
        out.append(dict(slot=slot, id=aid, acd=int(u[0x88 // 4]), sno=int(u[0x8C // 4]),
                        x=float(f[0xB0 // 4]), z=float(f[0xB4 // 4]), y=float(f[0xB8 // 4]),
                        addr=base + slot * STRIDE))
    return out


def main(cap, kinds):
    sno = load_sno()
    c = Cap(cap)
    bases = find_arrays(c)
    base, votes = bases.most_common(1)[0]
    acts = walk(c, base)
    print(f"{cap}: actor array @0x{base:X} ({votes} signature votes), {len(acts)} live actors")
    by_kind = collections.Counter(sno.get(a["sno"], ("?", "?", ""))[1] for a in acts)
    print("  by kind:", by_kind.most_common())
    for a in acts:
        name, kind, sub = sno.get(a["sno"], (f"sno{a['sno']}", "?", ""))
        if kinds and kind not in kinds:
            continue
        acd = "-" if a["acd"] == 0xFFFFFFFF else f"{a['acd']:08X}"
        print(f"  [{a['slot']:4d}] {kind:11s} {name:45s} acd={acd:8s} pos=({a['x']:8.2f},{a['z']:8.2f},{a['y']:6.2f})")


if __name__ == "__main__":
    main(sys.argv[1], set(sys.argv[2:]))
