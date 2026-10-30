# Engine mining — TurboHUD.exe decompilation (2026-10-30)

## Status: decompilation infrastructure COMPLETE, offsets located, extraction next

## What was beaten

- TurboHUD.exe is protected by a serious .NET obfuscator:
  - type/method names replaced with invisible-Unicode identifiers
  - MethodDef RVAs booby-trapped (fake fat headers, absurd code sizes) that
    wedge naive IL parsers (dncil); metadata streams lie about their sizes
- dncil-based miner wedged on ~5% of methods; abandoned after stall-kill
  analysis showed the scale.
- **dnSpyEx (self-contained, dnSpy.Console.exe)** decompiled the whole engine
  cleanly to C#: 489 files under %TEMP%\d3hud_decomp\engine (+ sno\).

## Key artifacts found

1. Reader P/Invoke family: `engine\cf7cf57e2\93402.cs` — ReadProcessMemory
   overloads (RID 17662-17671), VirtualQueryEx, OpenProcess. Obfuscated names,
   readable signatures.
2. **Offset tables are DECIMAL literals** (not hex!):
   - `'GMCr-QP6t...'cs` (1.5 MB class, contains RPM calls): 76 distinct
     struct-sized decimals, evenly spaced (1136, 1144, 1172-1188 stride 4...)
     = struct walk offsets.
   - `'32...'cs`: 487 sequential ids (1000+) = attribute/SNO enumerations.
3. **Readable settings keys survive**: e.g. "utility.diablo_window_title"
   (semantic anchors for mapping obfuscated classes to purposes).
4. Huge named interface file (`'498‪.cs'`): thousands of named SNO powers
   (Generic_DiabloCharge...) — the SNO API surface, intact.
5. Turbo.SNO.dll decompiled clean (63 types) under sno\.

## Session 2 results (offset extraction + validation)

- **All FieldOffset struct layouts extracted**: 394 fields / 50 clusters from
  the decompiled engine -> analysis/struct_layouts.json (offsets + TYPES,
  names obfuscated). Multi-line [FieldOffset(N)] + declaration parsing.
- Notable clusters: #3 (floats 192-224, span 956), #33 (5 floats + 10 bools),
  #37 (floats 256-264, 376-392), #45 (span 2688: ptr@1136 int@1144 floats
  1176-1188, longs 2656/2688).
- **VALIDATED in captures**: actor transform records (x,z,y) with preceding
  quaternion; player's own record at 0x25FB2E19670 in cap_town reads
  (374.86, 502.84, y=24.05) - exactly the pre-walk position.
- ~~ACTOR TRANSFORM ARRAY FOUND~~ **RETRACTED (session 3)**: the claimed
  stride-0x70 [quat][x z y] array at 0x25FB1607680 does not hold up.
  tools/find_xform_array.py: at that address entries are [x z y r a ±a 0 ...]
  (no quaternion, mirrored pairs, all = player pos) - geometry/collision-like.
  Loose quat check: 85 runs in cap_town, top ones non-unit quat (0,0,0,-0.468),
  y=0.10, duplicated in 3 regions, nothing points to their starts; cap_combat2
  has 0. Strict unit-quat check: only one static 15-entry table (identical in
  both captures). => no actor list here; do not chase its header.
- Cluster-45 absolute-base mapping UNRESOLVED: 2145 pair-copies in cap_town
  have no pointer at pair-44; most pair hits are bare value-copies, not
  records. Need array-header discovery instead.

## Next (session 3 plan, in order)

1. **Module-base trace**: TurboHUD stores Process.MainModule.BaseAddress via a
   setter (files o9%N7W^... and ‫⁭.cs). Every exe-static root is base+const;
   find readers of that getter, collect the constants, test each in captures
   (D3 exe = 0x7FF645E30000, 32.1 MB in cap_town/cap_combat2).
   Known: base+0x1471570 (21435760, read as int[]) = static int table, NOT actors.
2. Layout-matched pool: find the player's record via cluster-45 offsets
   (pos floats 1176-1188), infer record size, walk neighbours.
3. Fallback: pointer-scan back from entity position families.

### Session 3 results
- Step 1 (static trace) BLOCKED: calls go through runtime-bound delegate
  proxies (fields of class 02..64.. initialised from encoded tokens), and
  constants are decrypted at runtime (<Module>.X<T>(long key)). Of 243
  literals in exe-image range, only 0x1471570 is a real exe offset (the rest
  are attribute keys / item SNOs). Roots likely live in the encrypted pool.
- Step 2 (layout match) NEGATIVE so far: no record in cap_town with player pos
  at c11@216 / c3@192..212 / c37@256 / c45@1176 plus ASCII name@8. Cluster #3
  shape (id@0, sno@136, unit quat@192, pos@208) gives 102 scattered hits, none
  near the player, no common stride. Live actors have no "Name-NNNN" ASCII
  strings (all 4213 such strings are sound assets). The FieldOffset structs
  may be TurboHUD-side interop structs, not raw game layouts.
- exe base 0x7FF645E30000 is IDENTICAL in cap_town and cap_combat2 while heaps
  differ (0x25F.. vs 0x13C..) -> cross-capture pointer-chain intersection
  (CE-style) is viable: a chain exe+off -> ... -> player pos valid in BOTH
  captures with the same offsets is a stable root.
- tools/ptrscan.py (town player (374.861,502.844) -> combat (414.145,1582.391)),
  depth 5, maxoff 0x1000: 42 exe roots / 365 chains in A, **0 valid in B**.

## Old TurboHUD (21.1.14.0, for D3 2.6.10.71814) - de-protected

- ConfuserEx (modified) with normal-mode anti-tamper. de4dot-cex detects it
  only with -p crx and crashes in block deob (even with --no-cflow-deob first).
- **tools/cex_antitamper.py**: static remover - parses the anti-tamper routine
  from the dnSpy output of <Module>, ports the key derivation, decrypts the
  protected section (VA 0x2000, 861 KB) into a copy. Works; dnSpy then gives
  full method bodies. Plain-text strings label the managers.
  Work copies: %TEMP%\th_old_work, decompile %TEMP%\d3hud_decomp_old2(_named),
  helpers %TEMP%\th_tools (rename.py = stable i_xxxxxx names).
- **How it finds everything ("daf scan") = heap allocator walk, no pointer chains:**
  - exe-static ptr -> allocator obj -> (104-B struct).ptr -> (112-B struct)
    holds [start, end]. Walk blocks: 32-byte header, uint at +24 =
    (size<<1)|inUse, data at +32, next = block + 32 + size. First in-use block
    is a small-block arena walked the same way. Builds addr->size map.
  - Managers are then found by SIZE signature + shape check, e.g. ObjectManager
    (2.6.10): block size AlignUp(3064,32)=3072 (or +32) with ptr@+2928 -> block
    of 320(+32) bytes (container hdr), int@hdr+256 = count > 0, ptr@hdr+288 ->
    block of 152*count(+32) bytes. Same pattern for AcdManager, PlayerData,
    LevelArea, TrickleManager, containers (log strings name them all).
- **Current TurboHUD uses the SAME walk** (new decompile, renamed copy in
  %TEMP%\d3hud_decomp_new_named, file TCPMessageServer.cs ~L280-355): reads
  104-B then 112-B struct, 32-B headers, (size<<1)|inUse at +24.
- 2.6.10 map (old decompile): heap root = exe+37687424 (0x23F1180) [globals
  class i_74dda6 has more exe offsets]. 104-B struct: ptr@+88 -> 112-B
  struct: start@+8, end@+104. Everything else hangs off ObjectManager (OM):
  actor container = *(OM+2672), world = *(OM+2928), scene = *(OM+2808),
  acd container = *(*(AcdManager+8)+24), fast-attrib-group via *(OM+400)...
  Container header (364 B): name[256]@0, max index (short) @+264, ptr to
  block list @+288, magic 0x600DF00D @+328, block bit-shift @+360; element =
  blocks[i>>shift] + itemSize*(i & mask).
- **CAPTURE BUG (root cause of several dead ends)**: Reader --capture skipped
  every region > 64 MB. Exe globals hold 3742 ptrs into 0x25F.., only 24 land
  in captured memory; 3704 fall in the uncaptured giant arena below
  0x25F38018000. Fixed (2026-10-30): big MEM_PRIVATE regions now dumped in
  16 MB chunks (64 KB fallback on read failure); only big MEM_MAPPED skipped.
  **All existing captures are incomplete - re-capture before further analysis.**
- **Full captures (2026-10-30): cap_full_town, cap_full_town_pylon+bos**
  (exe 0x7FF664CF0000; heap ptrs in exe data now 3720/3731 readable).
- **ALLOCATOR ROOT FOUND (current client)**: exe+0x146DD08 with the exact
  2.6.10 offsets (*root -> +88 -> start@+8 / end@+104): 940 MB arena, 39 big
  blocks; first block's data (0x...C060) = 67 MB small-block sub-arena, also
  rooted at exe+0x146DCE8/+0x146DCF8; 250,053 small blocks. Walk stops at a
  zero-size header (unused tail). Block list: cap_full_town/daf_blocks.npy.
  - BUT: no 0x600DF00D at +328 in any of its blocks, and none of the top
    position-pair copies lie inside it -> actors/containers live in ANOTHER
    allocator. Other root candidates from brute force (looser offsets):
    exe+0x14641E8, exe+0x1464610 (~2 GB ranges), more in exe+0x146DCxx..DExx.
  - Container names exist only as exe string literals (RActors @exe+0xFE6A18,
    ActorCommonData @exe+0x109B028, FastAttribGroups, Markers) and NOTHING in
    the heap points at them -> containers no longer carry the name (inline or
    pointer). Identify containers by layout instead.
  - Player position in cap_full_town not yet pinned down (candidates
    (969.81,223.43) x879, (402.55,410.45) x367 ...).
- **ACTOR LIST FOUND (2026-10-30, cap_full_town_new_tristam_waypoint +
  cap_full_town_pylon+bos)** - tools/actors.py:
  - Player (waypoint) pos (401.730, 555.009, y 24.908), x3035 copies.
  - Actor array, stride 0x4F8 (1272 B), slot i at base + i*0x4F8:
    +0x00 u32 id (low16 = slot, 0xFFFFFFFF = free), +0x08 name[128] (EMPTY
    in this client), +0x88 u32 ACD id (-1 = client-only), +0x8C u32 actor
    SNO, +0x90 u32 -1 for gizmos/players/items BUT a real value for monsters
    (e.g. 0x668E) - do not require -1 when decoding (live bug 2026-10-30:
    no monster dots), +0xA8 rotation (2 floats), +0xB0 pos (x, z, y).
  - Live monsters mostly have acd -1 at +0x88 -> no acd filter for monsters.
  - Map orientation (in-game: player directly below pylon): world (-1,-1) is
    screen UP; user confirmed "rotate -135" (screen right = world (-1,+1)),
    no mirror. Now the live default.
  - +0x250 is NOT a death flag: activity/LOD state, 5 within ~60 u of the
    player, 0/4 farther away, alive or dead (F10 dumps 130733 vs 130743).
  - **Death = acd link at +0x88 cleared to -1**: F10 dumps 131044 (pack alive)
    vs 131050 (same spot, pack dead): 10/10 monsters went valid -> -1. Also
    flipping on death: +0x2E0 (-1 -> value), +0x16C/+0x170 (0 -> 1/6).
    Earlier "live monsters without acd" were corpses; the original no-monsters
    bug was the +0x90 == -1 decode requirement. Corpses despawn after a while
    (slot freed).
  - Portals are Gizmos, e.g. p76_Portal_GraveyardAct1_SeasonTheme (its
    P76_PortalSpawner_* monster is hidden).
- **ACD records (game state) - src/reader/AcdTable.cpp**: stride 0x3C0,
  +0x00 u32 acd id (== actor +0x88), +0x90 u32 actor SNO, **+0xB8 u32 monster
  quality** (0 normal, 1 champion, 2 rare, 3 minion, 4 unique, 5 hireling,
  7 boss). Verified live: 3 blue Dark Berserkers = 1, hireling = 5, 35 others
  0. Records live in >= 2 memory blocks with interleaved indices (block
  layout not decoded); lookup tries every implied base
  (addr - (id & 0xFFFF) * 0x3C0) and accepts only an exact id match.
  Bases found by scanning for (acd id, sno@+0x90) of linked actors.
  (The record with the sno 4 bytes after the id is a different structure.)
- **Items are ACD records** (no world actor): +0xB4 u32 GBID, +0x124 i32
  location (0 backpack, 1..13 equipped: 1 head 2 torso 3 OFF hand 4 MAIN
  hand 5 hands 6 waist 7 feet 8 shoulders 9 legs 10 bracers 11/12 rings
  13 neck; 15 stash; 16/17 potions; 20 socketed gems; 22..34 follower),
  +0x128 column, +0x12C row. Some blocks hold only items -> AcdTable
  discovery also accepts "known SNO + plausible location" records (>= 3 per
  block). Live: 13 equipped + 43 backpack + 292 stash.
- **Unidentified flag: ACD +0x190 u32 == 1** (pointers at +0x180/+0x188 set
  only then). Verified: user banked all identified items -> backpack 8/8
  gear = 1, gems/crucibles 0, equipped 13/13 = 0; stash 5/314 = 1.
- **Item attributes (src/reader/AttribTable.cpp)**: ACD +0x130 = fast
  attribute group id. Group (stride 0x12E8, >= 2 blocks; exact-id lookup like
  ACDs): +0 id, +4 flags, inline map at +0x18 (mask at map+0, buckets
  u64[mask+1] at map+0x1C = group+0x34). Entries [u64 next][u32 key][u32 val],
  key = idx | mod<<12 (0xFFFFF000|idx = no modifier), bucket =
  (key ^ key>>12) & mask (32/32 live). analysis/attributes.tsv = 2.6.10 list
  (from TurboHUD data/actor_attributes.bin); current indices are +5 from ~380:
  383 Item_Quality_Level (1 normal, 3-5 magic, 6-8 rare, 9 legendary),
  413 Ancient_Rank (1 ancient, 2 primal), 414 Unidentified (also on
  crucibles/consumables - authoritative; ACD +0x190 is gear-only), 386
  Item_Equipped, 380/381 durability, 1209 Dexterity_Item, 1322 Season.
  Ancient rank is readable on UNIDENTIFIED items (unided reveal).
  Set membership: items_gbid.tsv set_gbid column (gamebalance col 6; -1 none)
  - codes without "_Set_" (Guardian's Gaze) are sets too.
  Pitfall: an ACD+0x4 "group id" match was the empty name field (0) - always
  require non-zero ids.
- **Hovered item (src/reader/UiTooltip.cpp)**: UI control
  "Root.TopLayer.tooltip_dialog_background.tooltip_2" (TurboHUD
  IInventoryController.HoveredItem). Control: +0x18 u64 flags (bit 2 =
  visible), +0x30 u64 path hash, +0x38 path string, +0xCB0 u32 ACD id of the
  shown item (same as 2.6.10; live: last hovered = Unholy Plates). Found via
  the UI manager table (536-byte entries: path inline, control ptr 512 bytes
  after the path); 8 copies of the path string exist in memory.
- **UI geometry (src/reader/UiControls.cpp)**: same control layout gives
  +1176 f32 rect x0,y0,x1,y1 (virtual UI units, 1200 tall, centred 4:3
  area: screen x = (x + (W - 4H/3)*600/H) * H/1200, y = y * H/1200 - live
  2560x1440 backpack (1293,622)-(1853,954) -> (1871,746)-(2543,1145)) and
  +1224 i32 anim state. Controls: inventory_button_backpack (10x6 grid),
  inventory_button_<head|torso|armRight(=loc 3)|armLeft(=loc 4)|hands|waist|
  feet|shoulders|legs|bracers|fingerRight(=11)|fingerLeft(=12)|neck>,
  stash_dialog_mainPage.panel (open) / .button_stash (7x10 grid of the tab),
  .tab_1..5 (selected: visible && anim % 3 == 1), .TabPageButtons.
  stash_pages.page_1..4 (selected: anim == 14); global tab = page*5 + tab =
  stash row / 10. Item sizes: items_gbid.tsv width/height columns.
- **World -> screen**: no camera read needed - D3's camera is fixed, TurboHUD
  2.6.10 IWindow.WorldToScreenCoordinate uses constants relative to the
  player (see WorldToScreen in Snapshot.h); agrees with the minimap axes.
  Augment: attribute 432 CubeEnchantedGemRank (live: 125 on helm/belt/rings).
  Bonus drops by item type: GeneralUtility, DemonicKey, UpgradeableJewel,
  CraftingReagent "Key of ...".
- **Item display names**: analysis/items_gbid.tsv (5835 rows: gbid, sno,
  type, code, English name) extracted from Turbo.SNO resources; GBID at
  ACD+0xB4 resolved 384/384 items, table SNO == ACD SNO for all.
  - Found offset-free by signature (id low16 == slot, -1 @+0x90, consecutive
    slot at +0x4F8). Base 0x1E699133D80 in this session, 252 live actors.
  - Validated: player Demonhunter_Female, rift boss X1_LR_Boss_sniperAngel,
    pylon x1_LR_Shrine_Infinite_Casting all at the right spots.
  - Labels: analysis/sno_actors.tsv (19,176 SNO -> name/kind, from
    Turbo.SNO resources; kinds Monster/Gizmo/Item/Player/ServerProp/...).
- (Earlier conclusion, now suspect because of the capture bug:)
- **Does NOT carry over directly to our client**: in cap_town the uint at
  addr-8 of pointed-to heap objects is NOT (size<<1)|1 (3.4% vs 5.8% random
  baseline); no consistent header at -4..-32 either. Current client changed
  allocator/header encoding. Size-signature idea still valid if we can get
  block sizes another way (e.g. find the new allocator's metadata).

Note: keep decompiled sources in %TEMP% (working copy); facts extracted into
D3Hud are ours (offsets/layouts), no code copying.

## Scenes (map prediction)

- Record stride 1976: +0 id (low16 = slot), +12 world id, +236 scene SNO,
  +256/+260 x0/y0, +376/+380 x1/y1. Nav cells (from the game data, see
  `tools/extract_scene_cells.py` -> `scene_cells.bin`) are scene-relative.
- Town: many 240x240 scenes in one block -> base found by >= 2 votes.
- Rift level (world 774D001A): a single 320x320 scene (SNO 448403) was the
  only record in memory, so discovery also keeps the block of a lone record
  that contains the player (noise excluded: id 0 / 0xFF000000, world 0).
- `D3Hud.Reader --attach "Diablo III64.exe" --scenes` lists every
  self-consistent record with its implied block base.

## Gizmos (pylons / shrines)

- Attributes (2.6.10 index + 5): Gizmo_State 464, Gizmo_Has_Been_Operated
  475, read from the gizmo's ACD attribute group. Unverified live; the F10
  dump's `gizmo` column shows both values.

## Rift helpers (how TurboHUD does them)

- Possible pylon spawns: actors with SNO 428690 MarkerLocation_TieredRiftPylon
  (ServerProp, in the normal actor list); TurboHUD's MarkerPlugin draws them as
  "pylon?" in greater rifts only. D3Hud: ActorKind::PylonSpawn.
- Entrance / exit areas: rift levels are built from dungeon tilesets whose
  scene names carry the role: "*_entrance_01" (arrival tile), "*_exit_01" and
  the rift-only "*_dungeonstone_exit_01" (next-level portal). scene_cells.bin
  v2 stores this as a per-scene flag (328 entrance, 411 exit scenes).
- TurboHUD's SceneHintPlugin is something else: a hand-made list of 31 scene
  SNOs -> campaign dungeon names ("Cave of the Betrayer"), drawn at the scene
  centre when the scene has no portal.

## Rift progress (UI geometry, measured live in a greater rift)

Under `Root.NormalLayer.eventtext_bkgrnd.eventtext_region.stackpanel.rift_wrapper.` the containers
`greater_rift_container` and `challenge_rift_container` each hold `rift_progress_bar` (rect 1505..1827,
576..608 UI units, visible while the rift runs) and `rift_progress_marker`. The marker control
slides along the bar: its left edge (rect x0, +0x498) goes from the bar left edge (0%) to the bar right
edge (100%), monotonically, ~115 updates over 75 s. progress = (marker.x0 - bar.x0) / (bar.x1 - bar.x0).
Found with `D3Hud.Reader --ui-watch rift_progress`. Only the greater-rift container was seen moving;
the other container is read the same way but is untested.
