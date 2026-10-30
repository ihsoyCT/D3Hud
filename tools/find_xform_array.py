"""Find stride-0x70 actor transform arrays in captures, then pointer-scan for their headers.

Entry shape: [quat f32x4][x f32][z f32][y f32] ... (0x70 bytes total).
Usage: python tools/find_xform_array.py [cap ...]   (default: cap_town cap_combat2)
"""
import re
import sys
import numpy as np

BASE = "build/Release/recordings"
STRIDE = 0x70
SF = STRIDE // 4            # stride in floats
MIN_RUN = 3
TOP = 5
MEM_IMAGE = 0x1000000
KNOWN = {"cap_town": 0x25FB1607680}   # validated array start (ENGINE_MINING.md)


def load(cap):
    regions = []
    for line in open(f"{BASE}/{cap}/index.txt"):
        m = re.match(r"(\d+) (0x[0-9A-Fa-f]+) (\d+) (0x[0-9A-Fa-f]+) (0x[0-9A-Fa-f]+) (0x[0-9A-Fa-f]+)", line.strip())
        if m:
            rid, base, size, prot, typ, foff = m.groups()
            regions.append(dict(rid=int(rid), base=int(base, 16), size=int(size),
                                off=int(foff, 16), type=int(typ, 16)))
    regions.sort(key=lambda r: r["base"])
    blob = np.memmap(f"{BASE}/{cap}/regions.bin", dtype=np.uint8, mode="r")
    return regions, blob


def region_bytes(blob, r):
    return blob[r["off"]:r["off"] + r["size"]]


def entry_mask(a):
    """bool per float index i: a[i..i+6] looks like quat + (x, z, y)."""
    n = a.size - 6
    if n <= 0:
        return np.zeros(0, bool)
    with np.errstate(all="ignore"):
        q0, q1, q2, q3 = a[0:n], a[1:n + 1], a[2:n + 2], a[3:n + 3]
        x, z, y = a[4:n + 4], a[5:n + 5], a[6:n + 6]
        m = (np.abs(q0) <= 1.05) & (np.abs(q1) <= 1.05) & (np.abs(q2) <= 1.05) & (np.abs(q3) <= 1.05)
        m &= (q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3) > 0.04
        ax, az = np.abs(x), np.abs(z)
        m &= (ax > 20) & (ax < 4000) & (az > 20) & (az < 4000) & (y > 0) & (y < 300)
    return m


def find_runs(regions, blob):
    """Runs of >= MIN_RUN entries spaced exactly STRIDE apart -> [(count, start_addr, rid)]."""
    runs = []
    for r in regions:
        if r["type"] == MEM_IMAGE:
            continue
        a = region_bytes(blob, r)[: r["size"] // 4 * 4].view(np.float32)
        m = entry_mask(a)
        if not m.any():
            continue
        # each residue class mod SF is an independent chain of candidate entry slots
        for res in range(SF):
            sub = m[res::SF].astype(np.int8)
            if sub.sum() < MIN_RUN:
                continue
            d = np.diff(np.concatenate(([0], sub, [0])))
            starts, ends = np.flatnonzero(d == 1), np.flatnonzero(d == -1)
            for s, e in zip(starts, ends):
                if e - s >= MIN_RUN:
                    runs.append((int(e - s), r["base"] + (res + s * SF) * 4, r["rid"]))
    runs.sort(reverse=True)
    return runs


def read(regions, blob, addr, n):
    for r in regions:
        if r["base"] <= addr and addr + n <= r["base"] + r["size"]:
            o = r["off"] + addr - r["base"]
            return bytes(blob[o:o + n])
    return None


def pointer_scan(regions, blob, runs):
    """All 8-byte-aligned qwords pointing into [start-0x100, end+0x100] of each run."""
    hits = {start: [] for _, start, _ in runs}
    spans = [(start - 0x100, start + cnt * STRIDE + 0x100, start) for cnt, start, _ in runs]
    for r in regions:
        u = region_bytes(blob, r)[: r["size"] // 8 * 8].view(np.uint64)
        for lo, hi, start in spans:
            idx = np.flatnonzero((u >= lo) & (u < hi))
            for i in idx:
                hits[start].append((r["base"] + int(i) * 8, int(u[i]), r["rid"], r["type"] == MEM_IMAGE, u, int(i)))
    return hits


def fmt_ctx(u, i, start, cnt):
    """Qwords around a pointer hit, annotated (S=start, E=end, N=count)."""
    out = []
    for j in range(max(0, i - 2), min(u.size, i + 4)):
        v = int(u[j])
        tag = ""
        if v == start:
            tag = "=S"
        elif v == start + cnt * STRIDE:
            tag = "=E"
        elif start < v < start + cnt * STRIDE + 0x1000 and (v - start) % STRIDE == 0:
            tag = f"=S+{(v - start) // STRIDE}*0x70"
        elif v & 0xFFFFFFFF in (cnt, cnt + 1) or (v >> 32) in (cnt, cnt + 1):
            tag = "~N"
        out.append(("*" if j == i else " ") + f"{v:016X}{tag}")
    return " ".join(out)


def main():
    caps = sys.argv[1:] or ["cap_town", "cap_combat2"]
    for cap in caps:
        regions, blob = load(cap)
        runs = find_runs(regions, blob)
        print(f"=== {cap}: {len(runs)} runs of >= {MIN_RUN} stride-0x70 entries")
        if cap in KNOWN:
            k = KNOWN[cap]
            got = [(c, s) for c, s, _ in runs if s <= k < s + c * STRIDE and (k - s) % STRIDE == 0]
            print(f"  sanity: known array 0x{k:X} -> " + (f"run @0x{got[0][1]:X} x{got[0][0]}" if got else "NOT FOUND"))
        top = runs[:TOP]
        for cnt, start, rid in top:
            e = read(regions, blob, start, 0x1C)
            q = np.frombuffer(e, np.float32)
            print(f"  run @0x{start:X} x{cnt} (region {rid})  first: quat({q[0]:.3f},{q[1]:.3f},{q[2]:.3f},{q[3]:.3f}) "
                  f"pos({q[4]:.2f},{q[5]:.2f},{q[6]:.2f})")
        print("  -- pointers into top runs (S=start E=end N~count, *=hit qword)")
        for cnt, start, _ in top:
            hs = pointer_scan(regions, blob, [(cnt, start, 0)])[start]
            exact = sum(1 for h in hs if h[1] == start)
            print(f"  run @0x{start:X}: {len(hs)} pointers ({exact} exact to start)")
            # exact-start and image-region hits first, then nearest to start
            hs.sort(key=lambda h: (h[1] != start, not h[3], abs(h[1] - start)))
            for addr, val, rid, img, u, i in hs[:12]:
                print(f"    @0x{addr:X} r{rid}{' IMG' if img else ''} -> S{val - start:+#x}  | {fmt_ctx(u, i, start, cnt)}")
        del blob


if __name__ == "__main__":
    main()
