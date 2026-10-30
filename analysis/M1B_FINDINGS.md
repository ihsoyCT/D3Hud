# M1b/M2a analysis — findings (all captures + live check 2026-10-30)

## STATUS: live player tracking WORKS (M2a done)

- `D3Hud.Reader --find-player`: attach -> position in ~2.5 s live.
- Validated: player (415.45, 561.82) with top=3755 vs NPC crowd 861.
- Method: see "standing-cluster heuristic" below.

## IMPORTANT REVERSAL: sentinel signature is DEAD

The `FF 7F 80 00 FF FF FF ?? ...` sentinel (M1b's find) had 1088/2742 hits in
the original captures but ZERO in cap_live_check and live scans — it was a
state-dependent artifact (ring poison markers present only in that moment),
not a stable signature. Removed from the tracker.

## Standing-cluster heuristic (current method)

- Player position = most-repeated EXACT (x,z) float pair in a dense
  neighborhood, subject to: total >= 400, distinct >= 3, mono < 0.5
  (excludes static anchors like (216.271,841.239) x5051 identical),
  |x - z| > 4 (excludes mesh diagonals).
- Works when the player has been STANDING ~2 s (top value concentrates).
  Just-moved players splinter across interpolation variants and can lose to
  NPC crowds (~400-900 top counts). UX rule: stand still before find.
- Cluster shapes measured: player standing top=1104 (cap_town) / 3755 (live);
  NPC crowd top=428..898; static anchors mono=1.0; mesh mono~0.6 diagonal.

## M2b state (2026-10-30, combat captures)

- cap_combat2/3/4 are BYTE-IDENTICAL (user captured after the fights, same spot).
  Only cap_combat (pylon area, player 414.1/1582.4) is distinct.
- Entity families confirmed: position pairs with >=15 copies, NMS-consolidated
  at ~3u (each entity's interpolation variants). cap_combat2 shows ~5-8 entity
  families at 40-50u from the player.
- Player standing alone = only its own copy family nearby (cap_combat).
- Pylons do NOT emit position pairs (gizmos need a separate discovery path).
- NEED: labeled captures taken AT the moment (fight -> stand 2s -> capture ->
  note what kind of pack is on screen). One blue, one rare minimum.

## Next (M2b)

- Actor list: decode actor records (region-408-style transform records with
  rotation quaternions) into the minimap actor set; find their container via
  the same cluster scan repeated over ALL entity positions, not just the
  densest one.


## [OBSOLETE - sentinel signature is dead, see top] local player world position, offset-free

**Signature** (byte-exact, validated across two snapshots):

    record:  [float x][float z][float y]  FF 7F 80 00 FF FF FF ??  00 00 00 FF  ?? 80 00 (80|82)
              ^ 12 bytes before the sentinel                          ^ ~0x0C..0x3C stride

- Selectivity: cap_town_walk 1088/1088 hits valid; cap_town 2741/2742 valid.
- The densest position cluster among hits = local player (most-cached value).
  - cap_town (still): player at ~(373..376, 500..503)  [404 copies of top value]
  - cap_town_walk:    player at ~(411..413, 520..523)  [walk delta ~ (+37, +19) ~ 42 yd]

**Runtime algorithm for the reader** (M2 wiring):
1. Scan whole process for sentinel AOB (wildcards: ?? in bytes 7, 11, 12; last byte 80|82).
2. Read (x,z,y) at hit-12; keep plausible (|x|,|z| in 20..4000, y in 0..300, fractional).
3. Bin to 1-unit grid; densest cluster = player position. Track across frames.

## Data model discoveries

- Vector layout in these records: (x, z, y) adjacent floats, y LAST, height ~24-33 in town.
  (The earlier "[~0, x, z]" pattern seen in caches is a different container.)
- Static level anchor: (216.271, 841.239) with ~5050 exact copies, identical in all
  three captures = scene/hub origin, NOT live position. Do not confuse with player.
- Region 408 (cap) contains full actor transform records: rotation quaternion
  (4 unit floats) + (x, z, y) + more unit vectors. This is the actor-transform
  array — the M2 minimap data source candidate.
- Region 1007-family: position ring with sentinels — the "position cache" array.

## Pointer-chain status (parked)

- Level-1: exactly one heap pointer (0x26037CDB1D0 -> 0x25FED8E2000 player container).
- Levels 2-4 fan out (11 -> 42 -> 266) through hash/list structures, no exe-static root.
- 26+ exe-static globals point into heap neighborhood 0x2602xxxxxxx but BFS from
  the first 10 roots did not reach the player container within visit limits.
- PARKED: signature scanning replaces the need. Revisit only if signature breaks.

## Tooling

- analyze_cap.py + ad-hoc probes in bash history; np.histogram2d axis bug fixed.
- Reader: --capture now refuses overwrites; EnsureDir creates nested paths.
