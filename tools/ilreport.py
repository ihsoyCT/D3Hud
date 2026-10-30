#!/usr/bin/env python3
"""ilreport.py — analyze the mined IL index: find the memory-reader class,
its callers, offset-rich types, and interesting strings."""
import json
import re
import sys
from collections import Counter, defaultdict

IDX = r"C:\Users\arne\Projects\D3Hud\analysis\il_index.json"

methods = json.load(open(IDX, encoding="utf-8"))

# 1. P/Invoke prims (the ReadProcessMemory family)
prims = [m for m in methods if m["pinvoke"]]
print(f"== {len(prims)} P/Invoke methods; RPM-related:")
for m in prims:
    if re.search(r"read|write|process|memory|virtual", m["name"], re.I):
        print(f"   [{m['type']}] {m['name']}")

# 2. reader wrappers = methods calling pinvoke prims by name
prim_names = {m["name"] for m in prims}
callers = Counter()
for m in methods:
    for c in m["calls"]:
        if any(p in c for p in prim_names if re.search(r"read|memory|process|virtual", p, re.I)):
            callers[m["type"]] += 1

print("\n== types calling reader prims (top 15):")
for t, c in callers.most_common(15):
    print(f"   {c:5d} calls  {t}")

# 3. offset-rich methods in the top reader types
top_types = {t for t, _ in callers.most_common(8)}
rich = defaultdict(list)
for m in methods:
    if m["type"] in top_types and len(m["consts"]) >= 4:
        rich[m["type"]].append(m)
print("\n== offset-rich methods per top type:")
for t, ms in rich.items():
    print(f"   -- {t}: {len(ms)} methods")
    for m in sorted(ms, key=lambda x: -len(x["consts"]))[:5]:
        cs = sorted(set(m["consts"]))
        print(f"      {m['name'][:40]:42s} consts({len(cs)}): {[hex(x) for x in cs[:12]]}")

# 4. interesting strings
print("\n== selected string literals:")
seen = set()
pat = re.compile(r"diablo|actor|world|object.?manager|offset|sno|base.?addr|process", re.I)
for m in methods:
    for s in m["strs"]:
        if pat.search(s) and s not in seen and len(s) < 80:
            seen.add(s)
            if len(seen) <= 30:
                print(f"   [{m['type'][:35]}] {s!r}")
