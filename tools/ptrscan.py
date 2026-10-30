"""Two-capture pointer scan: find exe-static chains that reach the player position in BOTH captures.

  python tools/ptrscan.py top  <cap> [<cap> ...]       # top exact (x,z) pairs (player candidates)
  python tools/ptrscan.py scan <capA> xA zA <capB> xB zB [--depth 5] [--maxoff 0x1000]

Chain semantics: a = exe_base + root; then for each off: a = qword[a] + off. Final a holds (x, z) floats.
"""
import argparse
import bisect
import re
import sys
import time
import numpy as np

BASE = "build/Release/recordings"
MEM_IMAGE = 0x1000000


class Cap:
    def __init__(self, name):
        self.name = name
        rs = []
        for line in open(f"{BASE}/{name}/index.txt"):
            m = re.match(r"(\d+) (0x\w+) (\d+) (0x\w+) (0x\w+) (0x\w+)", line.strip())
            if m:
                rs.append(dict(rid=int(m[1]), base=int(m[2], 16), size=int(m[3]), prot=int(m[4], 16),
                               type=int(m[5], 16), off=int(m[6], 16)))
        rs.sort(key=lambda r: r["base"])
        self.regions = rs
        self.bases = [r["base"] for r in rs]
        self.blob = np.memmap(f"{BASE}/{name}/regions.bin", dtype=np.uint8, mode="r")
        self.exe_base, self.exe_size = self._find_exe()

    def _find_exe(self):
        # largest contiguous MEM_IMAGE run in the 0x7FF0..0x7FFE range (D3 is ~32 MB)
        mods, cur = [], None
        for r in self.regions:
            if r["type"] != MEM_IMAGE:
                cur = None
                continue
            if cur and r["base"] == cur[0] + cur[1]:
                cur[1] += r["size"]
            else:
                cur = [r["base"], r["size"]]
                mods.append(cur)
        exe = [m for m in mods if 0x7FF000000000 <= m[0] < 0x7FFE00000000 and m[1] > 20_000_000]
        return tuple(max(exe, key=lambda m: m[1])) if exe else (0, 0)

    def data(self, r):
        return self.blob[r["off"]:r["off"] + r["size"]]

    def read(self, addr, n):
        i = bisect.bisect_right(self.bases, addr) - 1
        if i < 0:
            return None
        r = self.regions[i]
        if addr + n > r["base"] + r["size"]:
            return None
        o = r["off"] + addr - r["base"]
        return bytes(self.blob[o:o + n])

    def q(self, addr):
        b = self.read(addr, 8)
        return int.from_bytes(b, "little") if b else None

    def pair_counts(self):
        """Exact (x, z) float pairs at any 4-byte alignment in private memory -> (keys, counts)."""
        keys = []
        for r in self.regions:
            if r["type"] == MEM_IMAGE:
                continue
            raw = self.data(r)[: r["size"] // 8 * 8]
            for sh in (0, 4):
                b = raw[sh: sh + (len(raw) - sh) // 8 * 8]
                f = b.view(np.float32)
                x, z = f[0::2], f[1::2]
                with np.errstate(all="ignore"):
                    m = (np.abs(x) > 20) & (np.abs(x) < 4000) & (np.abs(z) > 20) & (np.abs(z) < 4000) \
                        & (np.abs(x - z) > 4) & (x != np.round(x)) & (z != np.round(z))
                keys.append(b.view(np.uint64)[m])
        k, c = np.unique(np.concatenate(keys), return_counts=True)
        return k, c

    def pair_addrs(self, x, z):
        """Addresses (4-aligned) of float pairs exactly equal to (x, z) as float32."""
        out = []
        fx, fz = np.float32(x), np.float32(z)
        for r in self.regions:
            if r["type"] == MEM_IMAGE:
                continue
            f = self.data(r)[: r["size"] // 4 * 4].view(np.float32)
            m = (f[:-1] == fx) & (f[1:] == fz)
            out.append(r["base"] + np.flatnonzero(m).astype(np.uint64) * 4)
        return np.unique(np.concatenate(out))

    def pointers(self):
        """All 8-aligned qwords whose value lands in a committed region -> (vals, addrs) sorted by val."""
        starts = np.array([r["base"] for r in self.regions], dtype=np.uint64)
        ends = np.array([r["base"] + r["size"] for r in self.regions], dtype=np.uint64)
        lo, hi = starts.min(), ends.max()
        V, A = [], []
        for r in self.regions:
            u = self.data(r)[: r["size"] // 8 * 8].view(np.uint64)
            idx = np.flatnonzero((u >= lo) & (u < hi))
            if idx.size == 0:
                continue
            v = u[idx]
            j = np.searchsorted(starts, v, side="right") - 1
            ok = v < ends[j]
            V.append(v[ok])
            A.append(np.uint64(r["base"]) + idx[ok].astype(np.uint64) * np.uint64(8))
        V, A = np.concatenate(V), np.concatenate(A)
        o = np.argsort(V, kind="stable")
        return V[o], A[o]


def f2(k):
    a = np.array([k], dtype=np.uint64).view(np.float32)
    return float(a[0]), float(a[1])


def cmd_top(cap):
    c = Cap(cap)
    print(f"{cap}: exe 0x{c.exe_base:X} size {c.exe_size / 1e6:.1f} MB")
    k, n = c.pair_counts()
    for i in np.argsort(-n)[:15]:
        x, z = f2(k[i])
        print(f"  ({x!r}, {z!r}) x{n[i]}")


def reverse_bfs(c, targets, depth, maxoff, links_per_node=4):
    """Level-by-level reverse pointer search. Returns (statics, links): links[addr] = [(child, off)]."""
    t0 = time.time()
    V, A = c.pointers()
    print(f"  {c.name}: {V.size:,} pointers indexed ({time.time() - t0:.0f}s)")
    exe_lo, exe_hi = c.exe_base, c.exe_base + c.exe_size
    frontier = np.unique(targets.astype(np.uint64))
    seen = set(int(x) for x in frontier)
    links, statics = {}, []
    Vmax = V + np.uint64(maxoff)
    for lvl in range(1, depth + 1):
        # pointers p (value v) such that some frontier t has v <= t <= v + maxoff
        lo = np.searchsorted(frontier, V, side="left")
        hi = np.searchsorted(frontier, Vmax, side="right")
        hit = np.flatnonzero(hi > lo)
        new = []
        for i in hit:
            a = int(A[i]); v = int(V[i]); l = int(lo[i]); h = min(int(hi[i]), l + links_per_node)
            lk = links.setdefault(a, [])
            for t in frontier[l:h]:
                lk.append((int(t), int(t) - v))
            if a not in seen:
                seen.add(a)
                new.append(a)
                if exe_lo <= a < exe_hi:
                    statics.append((lvl, a))
        frontier = np.unique(np.array(new, dtype=np.uint64))
        ns = sum(1 for l, _ in statics if l == lvl)
        print(f"  level {lvl}: {hit.size:,} pointer hits, {frontier.size:,} new nodes, {ns} in exe static "
              f"({time.time() - t0:.0f}s)")
        if frontier.size == 0:
            break
    return statics, links


def chains_from(static_addr, links, targets_set, depth, limit=200000):
    out = []

    def dfs(a, offs, d):
        if len(out) >= limit:
            return
        for child, off in links.get(a, ()):
            if child in targets_set:
                out.append(offs + [off])
            elif d > 1 and child in links:
                dfs(child, offs + [off], d - 1)
    dfs(static_addr, [], depth)
    return out


def follow(c, root, offs):
    a = c.exe_base + root
    for off in offs:
        p = c.q(a)
        if p is None:
            return None
        a = p + off
    return a


def cmd_scan(a, xa, za, b, xb, zb, depth, maxoff):
    A, B = Cap(a), Cap(b)
    print(f"{a}: exe 0x{A.exe_base:X} | {b}: exe 0x{B.exe_base:X}")
    tA = A.pair_addrs(xa, za)
    print(f"  {a}: {tA.size} copies of player pair ({xa}, {za})")
    statics, links = reverse_bfs(A, tA, depth, maxoff)
    tset = set(int(x) for x in tA)
    chains = []
    for lvl, s in statics:
        for offs in chains_from(s, links, tset, depth):
            chains.append((s - A.exe_base, offs))
    print(f"  {len(chains):,} candidate chains from {len(statics)} static roots")
    good = []
    for root, offs in chains:
        fa = follow(B, root, offs)
        if fa is None:
            continue
        bts = B.read(fa, 8)
        if not bts:
            continue
        x, z = np.frombuffer(bts, np.float32)
        if abs(x - xb) < 3 and abs(z - zb) < 3:
            good.append((len(offs), root, offs, float(x), float(z)))
    good.sort()
    print(f"=== {len(good)} chains valid in BOTH captures")
    for n, root, offs, x, z in good[:60]:
        print(f"  [exe+0x{root:X}] " + " ".join(f"-> +0x{o:X}" for o in offs) + f"   B=({x:.2f},{z:.2f})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd")
    ap.add_argument("args", nargs="*")
    ap.add_argument("--depth", type=int, default=5)
    ap.add_argument("--maxoff", type=lambda s: int(s, 0), default=0x1000)
    o = ap.parse_args()
    if o.cmd == "top":
        for cap in o.args:
            cmd_top(cap)
    elif o.cmd == "scan":
        a, xa, za, b, xb, zb = o.args
        cmd_scan(a, float(xa), float(za), b, float(xb), float(zb), o.depth, o.maxoff)


if __name__ == "__main__":
    sys.exit(main())
