#!/usr/bin/env python3
"""analyze_cap.py — single/differential snapshot analysis of D3Hud.Reader captures.

Single-capture mode (default): locate game-data regions by scanning for plausible
world-coordinate float triples and summarize candidate clusters.

Differential mode (--diff A B): find float triples that look like world positions
and CHANGED between two captures -> moving entities (player among them).
"""
import argparse
import json
import os
import re
import struct
import sys

import numpy as np

LIMIT = 4000.0   # |x|,|z| bound for world coords
Y_LIMIT = 500.0  # |y| bound


def load_index(cap_dir):
    regions = []
    with open(os.path.join(cap_dir, "index.txt")) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = re.match(r"(\d+) (0x[0-9A-Fa-f]+) (\d+) (0x[0-9A-Fa-f]+) (0x[0-9A-Fa-f]+) (0x[0-9A-Fa-f]+)", line)
            if not m:
                continue
            rid, base, size, prot, typ, foff = m.groups()
            regions.append({
                "id": int(rid), "base": int(base, 16), "size": int(size),
                "protect": int(prot, 16), "type": int(typ, 16), "off": int(foff, 16),
            })
    return regions


def region_view(blob, region):
    return np.memmap(blob, dtype=np.uint8, mode="r", offset=region["off"], shape=(region["size"],))


def triple_mask(a):
    """a: float32 array; True where (v0,v1,v2) at i looks like (x,y,z) world coords."""
    n = a.size - 2
    if n <= 0:
        return None
    v = a[:n]
    x, y, z = v, a[1:n + 1], a[2:n + 2]
    m = (np.abs(x) <= LIMIT) & (np.abs(y) <= Y_LIMIT) & (np.abs(z) <= LIMIT)
    m &= (np.abs(x) > 1.0) | (np.abs(z) > 1.0)  # not an all-zero cluster
    # filter out ints-stored-as-float noise: real coords have fractional parts
    frac = np.abs(v - np.round(v)) > 0.0005
    m &= frac
    return m


def scan_single(cap_dir, top=25):
    regions = load_index(cap_dir)
    blob = os.path.join(cap_dir, "regions.bin")
    scored = []
    for r in regions:
        if r["size"] < 0x10000:
            continue
        try:
            view = region_view(blob, r)
            a = view.view(np.float32) if r["size"] % 4 == 0 else None
            if a is None:
                b = np.asarray(view[: r["size"] // 4 * 4]).view(np.float32)
                a = b
            m = triple_mask(a)
            if m is not None and m.any():
                scored.append((int(m.sum()), r))
        except Exception as e:
            print(f"  region {r['id']} error: {e}", file=sys.stderr)
    scored.sort(key=lambda t: -t[0])
    total = sum(c for c, _ in scored)
    print(f"{len(regions)} regions, {total} candidate coord-triples total")
    print(f"top regions by density:")
    out = []
    for count, r in scored[:top]:
        kb = r["size"] >> 10
        print(f"  region {r['id']:5d} base=0x{r['base']:X} {kb:7d} KB type=0x{r['type']:X} prot=0x{r['protect']:X} triples={count}")
        out.append({"id": r["id"], "base": r["base"], "size": r["size"], "type": r["type"], "triples": count})
    return out, regions


def strings_of(cap_dir, region, min_len=8, cap=60):
    view = region_view(os.path.join(cap_dir, "regions.bin"), region)
    data = bytes(view)
    seen = []
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % min_len, data):
        s = m.group().decode("ascii", "replace")
        seen.append(s)
        if len(seen) >= 4000:
            break
    # heuristically game-y strings first (snake_case / mixed with underscores)
    gamey = [s for s in seen if re.search(r"^[A-Za-z0-9_]+$", s) and "_" in s]
    uniq = []
    for s in gamey:
        if s not in uniq:
            uniq.append(s)
    return uniq[:cap]


def cluster_scan(cap_dir, min_bin=40, bin_size=64.0, top=15):
    """Find regions where many candidate (x,z) pairs concentrate in one spot —
    the signature of actor arrays. Uniform float noise never concentrates."""
    regions = load_index(cap_dir)
    blob = os.path.join(cap_dir, "regions.bin")
    nbins = int((2 * LIMIT) / bin_size)
    found = []
    for r in regions:
        if r["size"] < 0x100000:  # need volume for a cluster
            continue
        try:
            view = region_view(blob, r)
            usable = r["size"] // 4 * 4
            a = np.asarray(view[:usable]).view(np.float32)
            n = a.size - 2
            if n <= 0:
                continue
            v = a[:n]
            x, y, z = v, a[1:n + 1], a[2:n + 2]
            m = (np.abs(x) <= LIMIT) & (np.abs(y) <= Y_LIMIT) & (np.abs(z) <= LIMIT)
            m &= (np.abs(x) > 100.0) & (np.abs(z) > 100.0)  # real zones live far from origin
            m &= np.abs(v - np.round(v)) > 0.0005
            if not m.any():
                continue
            xs, zs = x[m], z[m]
            if xs.size < min_bin * 4:
                continue
            hi = np.histogram2d(xs, zs, bins=nbins, range=((-LIMIT, LIMIT), (-LIMIT, LIMIT)))[0]
            best = int(hi.max())
            if best >= min_bin:
                iy, ix = np.unravel_index(hi.argmax(), hi.shape)
                cx = -LIMIT + (ix + 0.5) * bin_size
                cz = -LIMIT + (iy + 0.5) * bin_size
                found.append({"region": r["id"], "base": r["base"], "size": r["size"],
                              "peak": best, "cx": cx, "cz": cz, "cands": int(xs.size)})
        except Exception as e:
            print(f"  region {r['id']} error: {e}", file=sys.stderr)
    found.sort(key=lambda d: -d["peak"])
    print(f"clustered coordinate regions (bin={bin_size:.0f} units, peak>={min_bin}):")
    for d in found[:top]:
        print(f"  region {d['region']:5d} base=0x{d['base']:X} {d['size']>>10:7d} KB  "
              f"peak={d['peak']:5d} at ({d['cx']:.0f},{d['cz']:.0f})  cands={d['cands']}")
    # consensus: a real world location lights up in several independent regions
    cons = {}
    for d in found:
        key = (round(d["cx"] / bin_size), round(d["cz"] / bin_size))
        cons.setdefault(key, []).append(d["region"])
    agree = sorted(cons.items(), key=lambda kv: -len(kv[1]))
    print("\ncross-region consensus (same world bin in N regions):")
    for (kx, kz), regs in agree[:8]:
        print(f"  bin center ({kx * bin_size:.0f},{kz * bin_size:.0f}): {len(regs)} regions {regs[:10]}")
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cap")
    ap.add_argument("--strings-region", type=int, default=None)
    ap.add_argument("--cluster", action="store_true")
    ap.add_argument("--top", type=int, default=25)
    args = ap.parse_args()
    if args.cluster:
        cluster_scan(args.cap, top=args.top)
        return
    out, regions = scan_single(args.cap, args.top)
    if args.strings_region is not None:
        for r in regions:
            if r["id"] == args.strings_region:
                print(f"\nsample strings from region {args.strings_region}:")
                for s in strings_of(args.cap, r):
                    print("   ", s)
                break


if __name__ == "__main__":
    main()
