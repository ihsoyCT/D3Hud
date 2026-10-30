"""D3 allocator block map from a capture.

Blocks have a 32-byte header; the uint at header+24 is (size << 1) | inUse; data starts at header+32
and the next header is at header + 32 + size (layout per TurboHUD's "daf" heap walk, still used by
the current client). We find blocks offset-free: any 16-aligned slot that starts a >= MIN_HOPS chain
of plausible headers, then propagate along each chain.

  python tools/heapwalk.py build <cap>        -> recordings/<cap>/blocks.npz (addr, size, used)
  python tools/heapwalk.py om <cap>           -> search ObjectManager-style container shape
"""
import collections
import sys
import numpy as np

from ptrscan import Cap, MEM_IMAGE, BASE

MIN_HOPS = 12


def region_blocks(c, r):
    d = c.data(r)
    n = r["size"] // 16
    if n < 4:
        return None
    u4 = d[:n * 16].view(np.uint32).reshape(n, 4)
    h = np.zeros(n, np.uint32)
    h[:-1] = u4[1:, 2]                       # uint at slot*16 + 24
    s = (h >> 1).astype(np.int64)
    nxt = np.arange(n) + 2 + s // 16
    hdr = (s > 0) & (s % 16 == 0) & (nxt <= n)
    nxt = np.where(hdr, nxt, n)
    # chain length (capped) from every slot
    L = np.zeros(n, np.int32)
    pos = np.flatnonzero(hdr)
    cur = pos.copy()
    for _ in range(MIN_HOPS):
        cur = nxt[cur]
        ok = cur < n
        ok[ok] &= hdr[cur[ok]]
        L[pos[ok]] += 1
        cur = np.where(ok, cur, 0)
        pos, cur = pos[ok], cur[ok]
    start = np.flatnonzero(L >= MIN_HOPS)
    if start.size == 0:
        return None
    seen = np.zeros(n, bool)
    front = start
    while front.size:
        front = front[~seen[front]]
        seen[front] = True
        nx = nxt[front]
        front = np.unique(nx[(nx < n)])
        front = front[hdr[front]]
    k = np.flatnonzero(seen)
    addr = np.uint64(r["base"]) + k.astype(np.uint64) * np.uint64(16) + np.uint64(32)
    return addr, s[k], (h[k] & 1).astype(bool)


def build(cap):
    c = Cap(cap)
    A, S, U = [], [], []
    for r in c.regions:
        if r["type"] == MEM_IMAGE or r["size"] < 0x1000:
            continue
        b = region_blocks(c, r)
        if b:
            A.append(b[0]); S.append(b[1]); U.append(b[2])
    A, S, U = np.concatenate(A), np.concatenate(S), np.concatenate(U)
    o = np.argsort(A)
    A, S, U = A[o], S[o], U[o]
    np.savez(f"{BASE}/{cap}/blocks.npz", addr=A, size=S, used=U)
    print(f"{cap}: {A.size:,} blocks ({U.sum():,} in use), {S[U].sum() / 1e6:.0f} MB in use")
    print("  top in-use sizes:", collections.Counter(S[U].tolist()).most_common(10))


def load_blocks(cap):
    z = np.load(f"{BASE}/{cap}/blocks.npz")
    return z["addr"], z["size"], z["used"]


class BlockMap:
    def __init__(self, cap):
        self.addr, self.size, self.used = load_blocks(cap)
        self.addr_i = self.addr.astype(np.int64)

    def size_of(self, a):
        """Size of the in-use block whose data starts exactly at a, else 0 (vectorised)."""
        a = np.asarray(a, dtype=np.int64)
        i = np.searchsorted(self.addr_i, a)
        i = np.clip(i, 0, self.addr_i.size - 1)
        hit = (self.addr_i[i] == a) & self.used[i]
        return np.where(hit, self.size[i], 0)


def om(cap):
    """Find container headers H (any block) with: int@H+cnt_off > 0, ptr@H+arr_off -> block of size k*count
    (k = element size), for a range of offsets; report the most consistent (cnt_off, arr_off, k) combos."""
    c = Cap(cap)
    bm = BlockMap(cap)
    used = bm.used
    A, S = bm.addr[used], bm.size[used]
    print(f"{A.size:,} in-use blocks")
    stats = collections.Counter()
    examples = {}
    # restrict to header-sized blocks (old: 320 / 352) - scan 128..1024 to allow layout drift
    sel = np.flatnonzero((S >= 128) & (S <= 1024))
    for i in sel:
        a, sz = int(A[i]), int(S[i])
        b = c.read(a, sz)
        if not b:
            continue
        q = np.frombuffer(b[: sz // 8 * 8], np.uint64)
        ints = np.frombuffer(b[: sz // 4 * 4], np.int32)
        tgt_sizes = bm.size_of(q.astype(np.int64))
        for ao in np.flatnonzero(tgt_sizes > 0):
            tsz = int(tgt_sizes[ao])
            # count field: some int in the header with tsz == k * count (or +32) for k in 8..512
            for ci in range(ints.size):
                cnt = int(ints[ci])
                if 1 <= cnt <= 100000:
                    for t in (tsz, tsz - 32):
                        if t > 0 and t % cnt == 0 and 8 <= t // cnt <= 1024 and (t // cnt) % 4 == 0:
                            key = (sz, ci * 4, int(ao) * 8, t // cnt)
                            stats[key] += 1
                            examples.setdefault(key, a)
    print("most frequent (hdr_size, count_off, array_ptr_off, elem_size) combos:")
    for k, n in stats.most_common(25):
        print(f"  {k}  x{n}  e.g. 0x{examples[k]:X}")


if __name__ == "__main__":
    {"build": build, "om": om}[sys.argv[1]](sys.argv[2])
