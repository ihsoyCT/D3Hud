// Core unit tests — run via CTest. No game, no graphics: pure logic.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <print>
#include <vector>

#include "AcdTable.h"
#include "AttribTable.h"
#include "ActorList.h"
#include "AobScanner.h"
#include "PlayerTracker.h"
#include "ReplayIO.h"
#include "SceneGen.h"
#include "SharedRing.h"
#include "Snapshot.h"
#include "Salvage.h"
#include "SceneCells.h"
#include "SceneTable.h"
#include "SnapshotInterp.h"
#include "SnoTable.h"

using namespace d3hud;

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::print(stderr, "FAIL {}:{}  {}\n", __FILE__, __LINE__, #cond);      \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

static void TestSceneDeterminism() {
    SceneGen a(7), b(7);
    WorldSnapshot sa, sb;
    for (int i = 0; i < 50; ++i) {
        a.Generate(&sa);
        b.Generate(&sb);
        CHECK(memcmp(&sa, &sb, sizeof(WorldSnapshot)) == 0);
    }
    SceneGen c(8);
    c.Generate(&sa);
    CHECK(sa.frameIndex == 0);
}

static void TestSceneContents() {
    SceneGen s(1);
    WorldSnapshot snap;
    s.Generate(&snap);  // frame 0
    CHECK(snap.actorCount > 20);   // 12 trash + 8 champions + rare + 3 minions + boss + 3 static
    CHECK(snap.itemCount == 8);
    int unided = 0, ancient = 0;
    for (int i = 0; i < snap.itemCount; ++i) {
        if (!snap.items[i].identified) unided++;
        if (snap.items[i].ancient) ancient++;
    }
    CHECK(unided == 4);
    CHECK(ancient == 2);
    CHECK(snap.items[4].primal);
    CHECK(strcmp(snap.items[1].typeName, "Ring") == 0);
}

static void TestReplayRoundtrip() {
    const char* path = "test_replay.bin";
    SceneGen gen(42);
    {
        ReplayWriter w;
        CHECK(w.Open(path, 66.0f));
        WorldSnapshot snap;
        for (int i = 0; i < 60; ++i) {
            gen.Generate(&snap);
            w.Append(snap);
        }
        CHECK(w.count() == 60);
    }
    {
        ReplayReader r;
        CHECK(r.Open(path));
        CHECK(r.frameCount() == 60);
        CHECK(r.frameIntervalMs() == 66.0f);
        SceneGen gen2(42);
        WorldSnapshot snap, ref;
        for (uint32_t i = 0; i < r.frameCount(); ++i) {
            CHECK(r.Read(i, &snap));
            gen2.Generate(&ref);
            CHECK(memcmp(&snap, &ref, sizeof(WorldSnapshot)) == 0);
        }
        CHECK(!r.Read(60, &snap));  // out of range
    }
    remove(path);
}

static void TestWorldToMinimap() {
    Vec2 out, edge;
    const Vec2 player{0, 0};
    const Vec2 center{100, 100};

    CHECK(WorldToMinimap({10, 0}, player, 1.0f, center, 80.0f, &out));
    CHECK(out.x == 110.f && out.y == 100.f);

    CHECK(WorldToMinimap({0, 10}, player, 1.0f, center, 80.0f, &out));
    CHECK(out.x == 100.f && out.y == 90.f);   // north = up

    CHECK(WorldToMinimap({20, 0}, player, 2.0f, center, 80.0f, &out));  // scale
    CHECK(out.x == 110.f);

    CHECK(!WorldToMinimap({100, 0}, player, 1.0f, center, 80.0f, &out, &edge));
    CHECK(edge.x > 170.f && edge.x <= 176.f);  // clamped on the rim, east side
    CHECK(edge.y == 100.f);
}

static void TestAobScanner() {
    d3hud::Pattern p;
    CHECK(d3hud::Pattern::Parse("48 8B ?? ?? 05 C0", &p));
    CHECK(p.size() == 6);
    CHECK(p.mask[0] == 1 && p.mask[2] == 0 && p.mask[3] == 0 && p.mask[5] == 1);
    CHECK(!d3hud::Pattern::Parse("xyz", &p));
    CHECK(!d3hud::Pattern::Parse("", &p));

    const uint8_t buf[] = {0x00, 0x48, 0x8B, 0x11, 0x22, 0x05, 0xC0, 0x00,
                           0xFF, 0x48, 0x8B, 0xAB, 0xCD, 0x05, 0xC0};
    auto hits = d3hud::ScanBuffer(p, buf, sizeof(buf), 0x1000, 8);
    CHECK(hits.size() == 2);
    CHECK(hits[0] == 0x1001 && hits[1] == 0x1009);

    const uint8_t nearMiss[] = {0x48, 0x8B, 0x11, 0x22, 0x05, 0xC1};
    CHECK(d3hud::ScanBuffer(p, nearMiss, sizeof(nearMiss), 0, 4).empty());

    const char* path = "test_aob.bin";
    FILE* f = nullptr;
    fopen_s(&f, path, "wb");
    uint8_t pad[32] = {};
    fwrite(pad, 1, sizeof(pad), f);
    fwrite(buf, 1, sizeof(buf), f);
    fclose(f);
    auto fh = d3hud::ScanFile(p, path, 0x2000, 8);
    CHECK(fh.size() == 2);
    CHECK(fh[0] == 0x2000 + 33 && fh[1] == 0x2000 + 41);
    remove(path);
}

static void TestPickPlayerCluster() {
    using namespace d3hud;
    std::vector<ClusterInfo> clusters = {
        {362, 354, 362.89f, 353.08f, 11260, 238, 428, 0.04f},   // NPC crowd
        {374, 502, 374.86f, 502.84f, 15711, 2865, 1104, 0.07f}, // standing player
        {216, 840, 216.271f, 841.239f, 5051, 1, 5051, 1.0f},    // static anchor (excluded upstream)
        {786, 786, 786.1f, 786.1f, 1066, 142, 631, 0.59f},      // mesh diagonal (excluded upstream)
    };
    PlayerFindResult r = PickPlayerCluster(clusters);
    CHECK(r.clusterVotes == 1104);
    CHECK(r.pos.x == 374.86f && r.pos.z == 502.84f);

    PlayerFindResult empty = PickPlayerCluster({});
    CHECK(empty.clusterVotes == 0);
    CHECK(empty.recordAddr == 0);
}

static void TestSharedRing() {
    using namespace d3hud;
    SharedRing ring;
    CHECK(ring.Create());
    WorldSnapshot probe;
    CHECK(!ring.Poll(&probe));  // empty ring

    for (uint64_t i = 0; i < 40; ++i) {
        WorldSnapshot w;
        memset(&w, 0, sizeof(w));
        w.magic = 0x44573348;
        w.frameIndex = i;
        w.playerPos = {static_cast<float>(i), static_cast<float>(100 + i)};
        ring.Publish(w);
    }
    CHECK(ring.latestIndex() == 40);
    // catch-up contract: a late reader gets only the most recent kCapacity frames
    int seen = 0;
    uint64_t expect = 40 - SharedRing::kCapacity;
    WorldSnapshot out;
    while (ring.Poll(&out)) {
        CHECK(out.frameIndex == expect);  // in-order delivery after catch-up
        expect++;
        seen++;
    }
    CHECK(seen == static_cast<int>(SharedRing::kCapacity));
    CHECK(!ring.Poll(&out));  // drained
    ring.Close();
}

// Write an actor record as the current client lays it out (see ActorList.h).
static void PutActor(uint8_t* rec, uint32_t slot, uint32_t sno, float x, float z) {
    memset(rec, 0, kActorStride);
    const uint32_t id = 0xA8BB0000u | slot, acd = 0x8C000000u | slot, minus1 = 0xFFFFFFFF;
    const float rot[4] = {0.f, 0.f, 0.f, 1.f}, pos[3] = {x, z, 24.f};
    memcpy(rec + 0x00, &id, 4);
    memcpy(rec + 0x88, &acd, 4);
    memcpy(rec + 0x8C, &sno, 4);
    memcpy(rec + 0x90, &minus1, 4);
    memcpy(rec + 0xA0, rot, sizeof(rot));
    memcpy(rec + 0xB0, pos, sizeof(pos));
}

static void TestActorRecords() {
    std::vector<uint8_t> buf(4 * kActorStride + kActorHeaderBytes);
    PutActor(&buf[0], 8, 74706, 401.73f, 555.01f);
    PutActor(&buf[kActorStride], 9, 221524, 401.9f, 555.2f);
    const uint32_t freeId = 0xFFFFFFFF;
    memset(&buf[2 * kActorStride], 0, kActorStride);
    memcpy(&buf[2 * kActorStride], &freeId, 4);  // free slot 10

    RawActor a;
    CHECK(DecodeActor(&buf[0], 8, &a));
    CHECK(a.sno == 74706 && a.x == 401.73f && a.z == 555.01f && a.hasAcd());
    CHECK(std::fabs(a.heading) < 1e-6f);  // identity rotation
    CHECK(!DecodeActor(&buf[0], 7, &a));  // id index must match the slot
    CHECK(!DecodeActor(&buf[2 * kActorStride], 10, &a));  // free slot

    uint32_t slot = 0;
    CHECK(MatchesArraySignature(&buf[0], &slot) && slot == 8);  // slot 8 followed by 9
    CHECK(!MatchesArraySignature(&buf[kActorStride], &slot));    // 9 followed by free

    // monsters: real value at +0x90 - still a live actor (in-game regression)
    const uint32_t monsterField = 0x668E;
    memcpy(&buf[0x90], &monsterField, 4);
    CHECK(DecodeActor(&buf[0], 8, &a));
    CHECK(!MatchesArraySignature(&buf[0], &slot));  // but not a scan anchor
    // +0x250 is an activity/LOD state (5 = near the player), not a death flag:
    // a live monster next to the player must stay visible (in-game regression)
    const uint32_t nearState = 5;
    memcpy(&buf[0x250], &nearState, 4);
    CHECK(DecodeActor(&buf[0], 8, &a) && a.state == 5 && !a.dead);
    // death clears the acd link (alive -> dead F10 dumps, 10/10 monsters)
    const uint32_t noAcd = 0xFFFFFFFF;
    memcpy(&buf[0x88], &noAcd, 4);
    CHECK(DecodeActor(&buf[0], 8, &a) && a.dead);

    buf[0x0C] = 'X';  // non-empty name field -> not an actor record
    CHECK(!DecodeActor(&buf[0], 8, &a));
}

static void TestClassifyActor() {
    ActorKind k;
    const bool kAlive = false, kDead = true;
    CHECK(ClassifyActor("Player", "Demonhunter_Female", true, kAlive, &k) && k == ActorKind::Player);
    CHECK(ClassifyActor("Monster", "X1_LR_Boss_sniperAngel", true, kAlive, &k) && k == ActorKind::Boss);
    CHECK(ClassifyActor("Monster", "Goatman_Ranged_A", true, kAlive, &k) && k == ActorKind::Trash);
    CHECK(!ClassifyActor("Monster", "Skeleton_A", false, kDead, &k));  // corpse
    CHECK(!ClassifyActor("Monster", "Generic_Proxy", true, kAlive, &k));
    CHECK(!ClassifyActor("Monster", "Hireling_Enchantress", true, kAlive, &k));
    CHECK(!ClassifyActor("Monster", "p75_cos_pet_AngelicDragon", true, kAlive, &k));
    CHECK(!ClassifyActor("Monster", "x1_MonsterAffix_frozenPulse_monster", false, kAlive, &k));
    CHECK(!ClassifyActor("Monster", "Voiceover_FreedPrisoner", false, kAlive, &k));
    CHECK(!ClassifyActor("Monster", "P76_PortalSpawner_Graveyard_Act1", false, kAlive, &k));
    CHECK(ClassifyActor("Monster", "Unique_CaptainDaltyn_AdventureMode", false, kAlive, &k) && k == ActorKind::Boss);
    CHECK(ClassifyActor("Monster", "SkeletonArcher_A", false, kAlive, &k) && k == ActorKind::Trash);
    CHECK(ClassifyActor("Gizmo", "x1_LR_Shrine_Infinite_Casting", false, kAlive, &k) && k == ActorKind::Pylon);
    CHECK(ClassifyActor("Gizmo", "Shrine_Global_Blessed", false, kAlive, &k) && k == ActorKind::Shrine);
    CHECK(ClassifyActor("Gizmo", "a3dunRmpt_Chest_Rare", false, kAlive, &k) && k == ActorKind::Chest);
    CHECK(ClassifyActor("Gizmo", "p76_Portal_GraveyardAct1_SeasonTheme", false, kAlive, &k) && k == ActorKind::Portal);
    CHECK(!ClassifyActor("Gizmo", "x1_westm_Barrel_B", false, kAlive, &k));
    CHECK(!ClassifyActor("Gizmo", "a3dun_crater_st_Demon_ChainPylon", false, kAlive, &k));
    CHECK(ClassifyActor("ServerProp", "MarkerLocation_TieredRiftPylon", false, kAlive, &k) &&
          k == ActorKind::PylonSpawn);
    CHECK(ClassifyActor("Item", "Helm_DHF_norm_set_01", true, kAlive, &k) && k == ActorKind::GroundItem);
    CHECK(!ClassifyActor("ClientEffect", "anything", true, kAlive, &k));
}

static void TestAcdItemDecode() {
    std::vector<uint8_t> rec(kAcdStride, 0);
    auto put = [&rec](uint32_t off, uint32_t v) { memcpy(&rec[off], &v, 4); };
    put(0x00, 0x78920192);   // acd id, index 0x192 in its block
    put(0x90, 210851);       // actor SNO
    put(0xB4, 1638662964u);  // GBID
    put(0x124, 0);           // backpack
    put(0x128, 3);           // column
    put(0x12C, 2);           // row
    put(0x190, 1);           // unidentified (in-game: the unided dagger)
    AcdItemRecord r;
    CHECK(DecodeAcdItem(rec.data(), 0x192, &r));
    CHECK(r.sno == 210851 && r.gbid == 1638662964u && r.location == 0 && r.x == 3 && r.y == 2);
    CHECK(r.unidentified);
    put(0x190, 0);
    CHECK(DecodeAcdItem(rec.data(), 0x192, &r) && !r.unidentified);
    CHECK(!DecodeAcdItem(rec.data(), 0x193, &r));  // id index must match the slot
    put(0x00, 0xFFFFFFFF);
    CHECK(!DecodeAcdItem(rec.data(), 0x192, &r));  // free slot
}

static void TestSnapshotInterp() {
    static WorldSnapshot a, b, out;  // large: keep off the stack
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.playerPos = {0.f, 0.f};
    b.playerPos = {10.f, -20.f};
    a.actorCount = 1;
    a.actors[0].id = 42;
    a.actors[0].world = {100.f, 100.f};
    b.actorCount = 2;
    b.actors[0].id = 7;  // new actor: no previous position
    b.actors[0].world = {5.f, 5.f};
    b.actors[1].id = 42;  // moved actor, different index than in a
    b.actors[1].world = {110.f, 90.f};

    InterpolateSnapshot(a, b, 0.5f, &out);
    CHECK(out.playerPos.x == 5.f && out.playerPos.y == -10.f);
    CHECK(out.actorCount == 2);
    CHECK(out.actors[0].world.x == 5.f && out.actors[0].world.y == 5.f);       // taken as-is
    CHECK(out.actors[1].world.x == 105.f && out.actors[1].world.y == 95.f);    // matched by id
    InterpolateSnapshot(a, b, 3.f, &out);  // past the newest: no extrapolation
    CHECK(out.playerPos.x == 10.f && out.actors[1].world.x == 110.f);
    // heading takes the short way round (+170 deg -> -170 deg passes 180, not 0)
    a.playerHeading = 2.967f;
    b.playerHeading = -2.967f;
    InterpolateSnapshot(a, b, 0.5f, &out);
    CHECK(std::fabs(std::fabs(out.playerHeading) - 3.14159f) < 1e-3f);
}

static void TestMonsterQuality() {
    // values seen in-game: 3 blue Dark Berserkers = 1, hireling = 5, everything else 0
    CHECK(ApplyMonsterQuality(ActorKind::Trash, 0) == ActorKind::Trash);
    CHECK(ApplyMonsterQuality(ActorKind::Trash, 1) == ActorKind::Champion);
    CHECK(ApplyMonsterQuality(ActorKind::Trash, 2) == ActorKind::Rare);
    CHECK(ApplyMonsterQuality(ActorKind::Trash, 3) == ActorKind::Minion);
    CHECK(ApplyMonsterQuality(ActorKind::Trash, 4) == ActorKind::Boss);
    CHECK(ApplyMonsterQuality(ActorKind::Trash, 7) == ActorKind::Boss);
    CHECK(ApplyMonsterQuality(ActorKind::Boss, 0) == ActorKind::Boss);  // name-based boss stays
    CHECK(ApplyMonsterQuality(ActorKind::Pylon, 1) == ActorKind::Pylon);  // only monsters change
}

static void TestSalvageRules() {
    CHECK(IsGearType("Ring") && IsGearType("Helm_Necromancer") && IsGearType("FistWeapon"));
    CHECK(!IsGearType("Gem") && !IsGearType("CraftingReagent") && !IsGearType("TieredRiftKey") &&
          !IsGearType("Book") && !IsGearType("Shard") && !IsGearType("UpgradeableJewel") && !IsGearType(""));
    CHECK(!IsGearType("Ringo"));  // whole-word / class-suffix matches only

    SafeArea safe;
    safe.Toggle(0, 0);
    safe.Toggle(9, 5);
    CHECK(safe.Has(0, 0) && safe.Has(9, 5) && !safe.Has(1, 0) && safe.Count() == 2);
    safe.Toggle(9, 5);
    CHECK(!safe.Has(9, 5) && !safe.Has(10, 0) && !safe.Has(-1, 0));

    ItemSnapshot it;
    it.location = kItemLocationBackpack;
    it.rarity = ItemRarity::Legendary;
    strcpy_s(it.typeName, "Boots");
    it.gridX = 0;
    it.gridY = 1;
    it.height = 2;
    strcpy_s(it.name, "Pox Faux");
    // worn piece with the same name: ancient/primal backpack dupes of it stay
    ItemSnapshot worn[1];
    worn[0] = ItemSnapshot();
    worn[0].location = 4;  // main hand
    strcpy_s(worn[0].name, "Pox Faux");
    const uint16_t n = 1;
    SalvageRules rules;
    CHECK(IsSalvageCandidate(it, safe, rules, worn, n));
    safe.Toggle(0, 2);  // lower cell of the 2-tall item
    CHECK(!IsSalvageCandidate(it, safe, rules, worn, n));
    safe.Toggle(0, 2);
    it.primal = true;
    CHECK(!IsSalvageCandidate(it, safe, rules, worn, n));  // primal of an equipped item
    it.primal = false;
    it.ancient = true;
    CHECK(!IsSalvageCandidate(it, safe, rules, worn, n));  // ancient of an equipped item
    it.ancient = false;
    it.identified = false;
    CHECK(IsSalvageCandidate(it, safe, rules, worn, n));  // unided non-ancient goes
    it.identified = false, it.ancient = true;
    strcpy_s(it.name, "Fellshard Coy");  // ancient of something NOT worn
    CHECK(IsSalvageCandidate(it, safe, rules, worn, n));
    it.primal = true;
    CHECK(IsSalvageCandidate(it, safe, rules, worn, n));  // even a primal, if unworn
    it.primal = false, it.ancient = false;
    it.identified = true;
    strcpy_s(it.name, "Pox Faux");  // non-ancient dupe of worn gear goes
    CHECK(IsSalvageCandidate(it, safe, rules, worn, n));
    it.location = 3;
    CHECK(!IsSalvageCandidate(it, safe, rules, worn, n));  // equipped
    it.location = kItemLocationBackpack;
    it.rarity = ItemRarity::Unknown;
    CHECK(!IsSalvageCandidate(it, safe, rules, worn, n));  // never the unknown
    it.rarity = ItemRarity::Rare;
    strcpy_s(it.typeName, "Gem");
    CHECK(!IsSalvageCandidate(it, safe, rules, worn, n));
    it.typeName[0] = '\0';
    CHECK(!IsSalvageCandidate(it, safe, rules, worn, n));  // unknown type
    // no equipped set at all: everything outside the safe area goes
    it.ancient = true;
    strcpy_s(it.typeName, "Ring");
    strcpy_s(it.name, "Oculus Ring");
    CHECK(IsSalvageCandidate(it, safe, rules, nullptr, 0));
    rules.protectEquippedAncients = false;
    CHECK(IsSalvageCandidate(it, safe, rules, worn, n));  // rule disabled
}

static void TestShrineLabels() {
    CHECK(!strcmp(ShrineLabel("x1_LR_Shrine_Electrified_TieredRift"), "Conduit Pylon"));
    CHECK(!strcmp(ShrineLabel("x1_LR_Shrine_Damage"), "Power Pylon"));
    CHECK(!strcmp(ShrineLabel("x1_LR_Shrine_Infinite_Casting"), "Channeling Pylon"));
    CHECK(!strcmp(ShrineLabel("a4_Heaven_Shrine_Global_Hoarder"), "Fleeting Shrine"));
    CHECK(!strcmp(ShrineLabel("p43_AD_Shrine_TreasureGoblin"), "Bandit Shrine"));
    CHECK(!strcmp(ShrineLabel("SomeNew_Shrine_X"), "Shrine"));
    CHECK(ShrineLabel("Waypoint") == nullptr);
}

static void TestScenes() {
    // shape file: one scene with two walkable cells
    const char* path = "test_scene_cells.bin";
    FILE* f = fopen(path, "wb");
    CHECK(f != nullptr);
    if (f == nullptr) return;
    const uint32_t hdr[3] = {0x43533344u /* "D3SC" */, 2, 1}, sc[3] = {141, kSceneExit, 2};
    const float cells[8] = {25.f, 0.f, 40.f, 2.5f, 0.f, 0.f, 240.f, 240.f};
    fwrite(hdr, 4, 3, f);
    fwrite(sc, 4, 3, f);
    fwrite(cells, 4, 8, f);
    fclose(f);
    SceneCells known;
    CHECK(known.Load(path) && known.sceneCount() == 1);
    CHECK(known.Find(141) != nullptr && known.Find(141)->size() == 2 && (*known.Find(141))[0].x1 == 40.f);
    CHECK(known.Find(7) == nullptr);
    CHECK(known.Flags(141) == kSceneExit && known.Flags(7) == 0);
    remove(path);

    // live town record: id 0x79460007 (slot 7), world 0x7739000B, (240,240)-(480,480)
    std::vector<uint8_t> rec(kSceneStride, 0);
    auto putU = [&rec](uint32_t off, uint32_t v) { memcpy(&rec[off], &v, 4); };
    auto putF = [&rec](uint32_t off, float v) { memcpy(&rec[off], &v, 4); };
    putU(0, 0x79460007);
    putU(12, 0x7739000B);
    putU(236, 141);
    putF(256, 240.f);
    putF(260, 240.f);
    putF(376, 480.f);
    putF(380, 480.f);
    SceneRecord s;
    CHECK(DecodeScene(rec.data(), 7, known, &s) && s.world == 0x7739000Bu && s.x1 == 480.f);
    CHECK(!DecodeScene(rec.data(), 8, known, &s));  // id index must match the slot
    putU(236, 999);
    CHECK(!DecodeScene(rec.data(), 7, known, &s));  // unknown scene SNO
    putU(236, 141);
    putF(376, 240.5f);
    CHECK(!DecodeScene(rec.data(), 7, known, &s));  // degenerate rectangle
}

static void TestBigMap() {
    const Vec2 player{500.f, 500.f};
    const int kLiveOrientation = 2;  // "rotate -135", confirmed in-game
    // the player (no pan) is the window centre
    Vec2 p = WorldToBigMap(player, player, {0.f, 0.f}, 2560.f, 1440.f, kLiveOrientation);
    CHECK(std::fabs(p.x - 1280.f) < 0.01f && std::fabs(p.y - 720.f) < 0.01f);
    // 1 world unit = H/1200 px; world (-1,-1) direction is screen up
    p = WorldToBigMap({player.x - 100.f, player.y - 100.f}, player, {0.f, 0.f}, 2560.f, 1440.f, kLiveOrientation);
    CHECK(std::fabs(p.x - 1280.f) < 0.5f && std::fabs((720.f - p.y) - 141.42f * 1.2f) < 0.5f);
    // panning the map by +d moves everything as if the player stood at player + d
    const Vec2 pan{30.f, -20.f};
    const Vec2 a = WorldToBigMap({600.f, 450.f}, player, pan, 2560.f, 1440.f, kLiveOrientation);
    const Vec2 b = WorldToBigMap({600.f, 450.f}, {player.x + pan.x, player.y + pan.y}, {0.f, 0.f}, 2560.f, 1440.f,
                                 kLiveOrientation);
    CHECK(std::fabs(a.x - b.x) < 0.01f && std::fabs(a.y - b.y) < 0.01f);
}

static void TestWorldToScreen() {
    const Vec2 player{1000.f, 900.f};
    Vec2 p;
    // the player: horizontally centred, a little above the vertical centre
    CHECK(WorldToScreen(player, 0.f, player, 2560.f, 1440.f, &p));
    CHECK(std::fabs(p.x - 1280.f) < 1.f && p.y > 0.4f * 1440.f && p.y < 0.5f * 1440.f);
    Vec2 up, right;
    // same axes as the minimap check: world (-1,-1) is screen up, (-1,+1) screen right
    CHECK(WorldToScreen({player.x - 10.f, player.y - 10.f}, 0.f, player, 2560.f, 1440.f, &up));
    CHECK(WorldToScreen({player.x - 10.f, player.y + 10.f}, 0.f, player, 2560.f, 1440.f, &right));
    CHECK(up.y < p.y - 50.f && std::fabs(up.x - p.x) < 5.f);
    CHECK(right.x > p.x + 50.f && std::fabs(right.y - p.y) < 5.f);
    CHECK(!WorldToScreen({player.x + 500.f, player.y + 500.f}, 0.f, player, 2560.f, 1440.f, &p));  // off screen

    CHECK(IsBonusDrop("GeneralUtility", "Angelic Crucible"));
    CHECK(IsBonusDrop("GeneralUtility", "Ramaladni's Gift"));
    CHECK(IsBonusDrop("DemonicKey", "Key of Terror"));
    CHECK(IsBonusDrop("UpgradeableJewel", "Bane of the Stricken"));
    CHECK(IsBonusDrop("CraftingReagent", "Key of War"));
    CHECK(!IsBonusDrop("CraftingReagent", "Death's Breath"));
    CHECK(!IsBonusDrop("Ring", "Puzzle Ring"));
}

static void TestUiGeometry() {
    auto near1 = [](float a, float b) { return std::fabs(a - b) < 1.0f; };
    // live, 2560x1440: backpack control UI rect -> measured screen rect
    const UiRect bp{1293.f, 622.f, 1853.f, 954.f};
    const UiRect s = UiToScreen(bp, 2560.f, 1440.f);
    CHECK(near1(s.x0, 1871.f) && near1(s.y0, 746.f) && near1(s.x1, 2543.f) && near1(s.y1, 1145.f));
    // 4:3 screen: no horizontal offset
    const UiRect q = UiToScreen({0.f, 0.f, 1600.f, 1200.f}, 1600.f, 1200.f);
    CHECK(near1(q.x0, 0.f) && near1(q.x1, 1600.f) && near1(q.y1, 1200.f));

    UiLayout ui;
    ui.backpack = {0.f, 0.f, 100.f, 60.f};  // 10 x 6 cells of 10 x 10
    ui.stash = {0.f, 0.f, 70.f, 100.f};     // 7 x 10 cells of 10 x 10
    ui.slots[1] = {5.f, 6.f, 7.f, 8.f};
    UiRect r;
    CHECK(!ItemScreenRect(ui, kItemLocationBackpack, 3, 2, 2, &r));  // inventory closed
    ui.inventoryOpen = 1;
    CHECK(ItemScreenRect(ui, kItemLocationBackpack, 3, 2, 2, &r) && r.x0 == 30.f && r.y0 == 20.f &&
          r.x1 == 40.f && r.y1 == 40.f);  // 2 cells tall
    CHECK(ItemScreenRect(ui, 1, 0, 0, 2, &r) && r.x0 == 5.f && r.y1 == 8.f);  // head slot rect
    CHECK(!ItemScreenRect(ui, kItemLocationStash, 1, 23, 1, &r));          // stash closed
    ui.stashOpen = 1;
    ui.stashTab = 2;
    CHECK(ItemScreenRect(ui, kItemLocationStash, 1, 23, 1, &r) && r.x0 == 10.f && r.y0 == 30.f);  // tab 2, row 3
    CHECK(!ItemScreenRect(ui, kItemLocationStash, 1, 13, 1, &r));          // tab 1: not shown
    ui.stashTab = -1;
    CHECK(!ItemScreenRect(ui, kItemLocationStash, 1, 23, 1, &r));          // tab unknown
}

static void TestItemAttributes() {
    // quality levels seen live: gems 1, Ascended Armor 7/Greaves 8 (rare), legendaries 9
    CHECK(RarityFromQuality(1, false) == ItemRarity::Common);
    CHECK(RarityFromQuality(3, false) == ItemRarity::Magic);
    CHECK(RarityFromQuality(5, false) == ItemRarity::Magic);
    CHECK(RarityFromQuality(6, false) == ItemRarity::Rare);
    CHECK(RarityFromQuality(8, false) == ItemRarity::Rare);
    CHECK(RarityFromQuality(9, false) == ItemRarity::Legendary);
    CHECK(RarityFromQuality(9, true) == ItemRarity::Set);  // e.g. Guardian's Gaze (no _Set_ in its code)
    CHECK(RarityFromQuality(7, true) == ItemRarity::Rare); // set flag only matters for legendaries
    // keys: no modifier -> 0xFFFFF000 | index; bucket = (key ^ key >> 12) & mask
    CHECK(AttribKey(kAttrItemQualityLevel) == 0xFFFFF17Fu);
    CHECK(AttribBucket(0xFFFFF17Fu, 255) == ((0xFFFFF17Fu ^ 0x000FFFFFu) & 255));
    CHECK(AttribBucket(AttribKey(kAttrAncientRank), 255) < 256);
}

static void TestSnoTable() {
    const char* path = "test_sno_actors.tsv";
    FILE* f = fopen(path, "wb");
    CHECK(f != nullptr);
    if (f == nullptr) return;
    fputs("sno\tname\tsubtype\tmobility\tkind\r\n74706\tDemonhunter_Female\tPlayer\tdynamic\tPlayer\r\n"
          "4675\tMarkerLocation\tNone\tstatic\tServerProp\r\n", f);
    fclose(f);
    SnoTable t;
    CHECK(t.Load(path) && t.size() == 2);
    const SnoInfo* i = t.Find(74706);
    CHECK(i != nullptr && i->name == "Demonhunter_Female" && i->kind == "Player");
    CHECK(t.Find(1) == nullptr);
    remove(path);
}

static void TestItemNames() {
    const char* path = "test_items_gbid.tsv";
    FILE* f = fopen(path, "wb");
    CHECK(f != nullptr);
    if (f == nullptr) return;
    // the one-handed type column is free text; names carry apostrophes
    fputs("gbid\tsno\ttype\tcode\tname\tset_gbid\r\n"
          "1638662964\t210851\tHelm\tUnique_Helm_009\tNatalya's Gaze\t269541907\r\n"
          "335491449\t221518\tHelm\tUnique_Helm_015_x1\tCoif\t4294967295\r\n"
          "12345\t1\tRing\tOld_Format\tNo Set Column\r\n", f);  // 5-column rows still load
    fclose(f);
    ItemNameTable t;
    CHECK(t.Load(path) && t.size() == 3);
    const ItemNameInfo* n = t.Find(1638662964u);  // > INT_MAX: must parse as unsigned
    CHECK(n != nullptr && n->name == "Natalya's Gaze" && n->type == "Helm" && n->inSet);
    CHECK(t.Find(335491449u) != nullptr && !t.Find(335491449u)->inSet);  // -1 = no set
    CHECK(t.Find(12345u) != nullptr && !t.Find(12345u)->inSet);
    CHECK(t.Find(1) == nullptr);
    remove(path);
    // slot names (live: quiver in 3, bow in 4)
    CHECK(!strcmp(ItemLocationName(3), "Off hand") && !strcmp(ItemLocationName(4), "Main hand"));
    CHECK(!strcmp(ItemLocationName(kItemLocationBackpack), "Backpack"));
    CHECK(!strcmp(ItemLocationName(kItemLocationUnknown), "-"));
}

static void TestMapOrientation() {
    auto approx = [](Vec2 a, float x, float y) { return std::fabs(a.x - x) < 1e-4f && std::fabs(a.y - y) < 1e-4f; };
    const float r2 = 1.41421356f;
    // in-game measurement: player directly below a pylon -> world (-1,-1) is screen up
    CHECK(approx(OrientForMap({-1.f, -1.f}, 1), 0.f, r2));
    CHECK(approx(OrientForMap({-1.f, -1.f}, 2), 0.f, r2));
    // the two candidates differ only in left/right
    CHECK(approx(OrientForMap({1.f, -1.f}, 1), r2, 0.f));
    CHECK(approx(OrientForMap({-1.f, 1.f}, 2), r2, 0.f));
    CHECK(approx(OrientForMap({3.f, 4.f}, 0), 3.f, 4.f));  // raw is identity
}

int main() {
    TestSceneDeterminism();
    TestSceneContents();
    TestReplayRoundtrip();
    TestWorldToMinimap();
    TestAobScanner();
    TestPickPlayerCluster();
    TestSharedRing();
    TestActorRecords();
    TestClassifyActor();
    TestSnoTable();
    TestItemNames();
    TestMonsterQuality();
    TestItemAttributes();
    TestUiGeometry();
    TestWorldToScreen();
    TestBigMap();
    TestScenes();
    TestShrineLabels();
    TestSalvageRules();
    TestSnapshotInterp();
    TestAcdItemDecode();
    TestMapOrientation();
    if (g_failures == 0) {
        std::print("all core tests passed\n");
        return 0;
    }
    std::print("{} test failure(s)\n", g_failures);
    return 1;
}
