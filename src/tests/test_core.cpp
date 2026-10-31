// Core unit tests — run via CTest. No game, no graphics: pure logic.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <print>
#include <vector>

#include "AcdTable.h"
#include "AttribTable.h"
#include "Effects.h"
#include "ActorList.h"
#include "AobScanner.h"
#include "PlayerTracker.h"
#include "ReplayIO.h"
#include "SceneGen.h"
#include "SharedRing.h"
#include "Snapshot.h"
#include "BasesCache.h"
#include "ItemSearch.h"
#include "Paths.h"
#include "Salvage.h"
#include "SceneCells.h"
#include "SceneTable.h"
#include "HostApi.h"
#include "MonsterAffixes.h"
#include "LuaPlugin.h"
#include "PluginSettings.h"
#include "Settings.h"
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
    // town vendors contain "Unique" in their name but are not unique monsters
    CHECK(ClassifyActor("Monster", "A2_UniqueVendor_Miner_InTown_01", true, kAlive, &k) && k == ActorKind::Trash);
    CHECK(ClassifyActor("Monster", "Skeleton_Unique_Foo", true, kAlive, &k) && k == ActorKind::Boss);
    // rift progress orbs: their own kinds, with or without an ACD; the flippy effects are not actors we show
    CHECK(ClassifyActor("Item", "p1_normal_rifts_Orb", false, kAlive, &k) && k == ActorKind::RiftOrb);
    CHECK(ClassifyActor("Item", "Challenge_Rifts_Orb", true, kAlive, &k) && k == ActorKind::RiftOrb);
    CHECK(ClassifyActor("Item", "p1_tiered_rifts_Orb", false, kAlive, &k) && k == ActorKind::RiftOrbTiered);
    CHECK(!ClassifyActor("ClientEffect", "p1_tiered_rifts_Orb_flippy", true, kAlive, &k));
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

    // rarity mask, ancient/primal keeps, never-salvage families
    ItemSnapshot g;
    g.location = kItemLocationBackpack;
    g.gridX = 5;  // outside the safe cells above
    g.rarity = ItemRarity::Rare;
    strcpy_s(g.typeName, "Boots");
    strcpy_s(g.name, "Plain Boots");
    SalvageRules r2;
    CHECK(IsSalvageCandidate(g, safe, r2, nullptr, 0));
    r2.rarityMask &= ~(1u << static_cast<int>(ItemRarity::Rare));
    CHECK(!IsSalvageCandidate(g, safe, r2, nullptr, 0));  // rares excluded
    g.rarity = ItemRarity::Legendary;
    CHECK(IsSalvageCandidate(g, safe, r2, nullptr, 0));
    r2 = SalvageRules();
    r2.keepAncient = true;
    g.ancient = true;
    CHECK(!IsSalvageCandidate(g, safe, r2, nullptr, 0));
    g.ancient = false;
    r2 = SalvageRules();
    r2.keepPrimal = true;
    g.primal = true;
    CHECK(!IsSalvageCandidate(g, safe, r2, nullptr, 0));
    g.primal = false;
    CHECK(IsSalvageCandidate(g, safe, r2, nullptr, 0));
    r2 = SalvageRules();
    r2.neverFamilies = 1u << GearFamily("Boots");
    CHECK(!IsSalvageCandidate(g, safe, r2, nullptr, 0));
    strcpy_s(g.typeName, "Ring");
    CHECK(IsSalvageCandidate(g, safe, r2, nullptr, 0));  // only boots are protected
    CHECK(GearFamily("Helm_Necromancer") == GearFamily("Helm") && GearFamily("Ring") != GearFamily("Amulet"));
    CHECK(GearFamily("Axe2H") == GearFamily("Sword") && GearFamily("Axe2H") == kGearFamilyCount - 1);
}

extern "C" int HostApiCCheck(void);

// The C API mirrors a few engine enums by number; plugins depend on those values.
static void TestMonsterAffixes() {
    static_assert(kMonsterAffixCount == 30, "the game's MonsterAffixes table has 30 entries");
    for (size_t i = 0; i < kMonsterAffixCount; ++i) {
        const MonsterAffixInfo& a = kMonsterAffixes[i];
        CHECK(a.gbid != 0 && a.key[0] != '\0' && a.name[0] != '\0');
        CHECK(FindMonsterAffix(a.gbid) == &a);
        for (size_t j = 0; j < i; ++j) CHECK(kMonsterAffixes[j].gbid != a.gbid && strcmp(kMonsterAffixes[j].key, a.key) != 0);
    }
    CHECK(FindMonsterAffix(1799201764) != nullptr && strcmp(FindMonsterAffix(1799201764)->name, "Health Link") == 0);
    CHECK(FindMonsterAffix(0) == nullptr && FindMonsterAffix(12345) == nullptr);
}

static void TestAffixMask() {
    // an ACD record of a champion as measured live: the list starts at +0x1E4 (an elite marker that is not one of the
    // 30, then four affixes, then empty slots)
    uint32_t words[0x3C0 / 4] = {};
    words[0x1E4 / 4] = 0x371E75AA;
    words[0x1E8 / 4] = 1905614711;  // Orbiter
    words[0x1EC / 4] = 3855260060u;  // Fire Chains
    words[0x1F0 / 4] = 2206426855u;  // Knockback
    words[0x1F4 / 4] = 4131130388u;  // Frozen
    words[0x1F8 / 4] = words[0x1FC / 4] = words[0x200 / 4] = 0xFFFFFFFFu;
    const uint32_t mask = AffixMaskFromAcdWords(words, std::size(words));
    const uint32_t expected = (1u << FindMonsterAffixIndex(1905614711)) | (1u << FindMonsterAffixIndex(3855260060u)) |
                              (1u << FindMonsterAffixIndex(2206426855u)) | (1u << FindMonsterAffixIndex(4131130388u));
    CHECK(mask == expected);
    CHECK(AffixMaskFromAcdWords(words, 4) == 0);  // a record too short to hold the list
    words[0x1E8 / 4] = 0;
    CHECK(AffixMaskFromAcdWords(words, std::size(words)) != expected);
    // ids elsewhere in the record do not count
    uint32_t other[0x3C0 / 4] = {};
    other[0x100 / 4] = 1905614711;
    CHECK(AffixMaskFromAcdWords(other, std::size(other)) == 0);
    CHECK(FindMonsterAffixIndex(0xFFFFFFFFu) == -1 && FindMonsterAffixIndex(3775118) == 0);
}

static void TestHostApi() {
    static_assert(D3H_ACTOR_PLAYER == static_cast<int>(ActorKind::Player));
    static_assert(D3H_ACTOR_PORTAL == static_cast<int>(ActorKind::Portal));
    static_assert(D3H_ACTOR_RIFT_ORB_TIERED == static_cast<int>(ActorKind::RiftOrbTiered));
    static_assert(D3H_RARITY_LEGENDARY == static_cast<int>(ItemRarity::Legendary));
    static_assert(D3H_RARITY_SET == static_cast<int>(ItemRarity::Set));
    static_assert(D3H_VERDICT_JUNK == static_cast<int>(SalvageVerdict::Junk));
    static_assert(D3H_VERDICT_CHECK == static_cast<int>(SalvageVerdict::Check));
    static_assert(D3H_NAME_LEN == kNameLen);
    static_assert(D3H_FLAG_ANCIENT == kItemFlagAncient && D3H_FLAG_PRIMAL == kItemFlagPrimal &&
                  D3H_FLAG_BONUS == kItemFlagBonus && D3H_FLAG_UNIDENTIFIED == kItemFlagUnidentified &&
                  D3H_FLAG_USED == kActorFlagUsed);
    CHECK(HostApiCCheck() == D3H_API_VERSION);  // the header compiles as C and D3H_HOST_HAS works

    // an older host without the last function is detected
    d3h_host old{};
    old.size = static_cast<uint32_t>(offsetof(d3h_host, set_salvage_policy));
    CHECK(D3H_HOST_HAS(&old, register_hotkey) && !D3H_HOST_HAS(&old, set_salvage_policy));
}

static void TestSettingsTable() {
    enum class Shape : uint8_t { A, B, C };
    bool on = true;
    int n = 5;
    float f = 1.5f;
    uint32_t rgb = 0x102030;
    uint64_t mask = 0;
    Shape shape = Shape::A;
    SettingsTable t;
    t.Comment("first section");
    t.Add("on", on);
    t.Add("n", n, 0, 10);
    t.Add("f", f, 0.f, 2.f, 3);
    t.Add("rgb", rgb, 0u, 0xFFFFFFu);
    t.AddHex64("mask", mask, 0xFFull);
    t.Add("shape", shape, Shape::A, Shape::C);
    t.Alias("oldOn", "on");
    CHECK(t.size() == 6);

    // text round trip
    const std::string saved = t.SaveText();
    CHECK(saved.starts_with("# first section\non=1\n"));
    on = false; n = 0; f = 0.f; rgb = 0; mask = 0; shape = Shape::C;
    CHECK(t.LoadText(saved) == 6);
    CHECK(on && n == 5 && f == 1.5f && rgb == 0x102030 && shape == Shape::A);

    // clamping, spaces, comments, CRLF, unknown keys, junk values, aliases, hex masks
    CHECK(t.LoadText("# n=3\r\n n = 99 \r\nf=-4\nmissing=1\nshape=text\nrgb=1e30\nmask=1ff\noldOn=0\nnoequals\n") == 5);
    CHECK(n == 10 && f == 0.f && rgb == 0xFFFFFFu && mask == 0xFF && !on && shape == Shape::A);
    CHECK(t.LoadText("shape=7") == 1 && shape == Shape::C);
    CHECK(!t.Has("oldOn") || t.Has("on"));

    // file round trip, unicode folder
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / L"d3hud_settings_é日";
    fs::create_directories(dir);
    n = 7;
    CHECK(t.Save(dir / L"s.ini"));
    n = 0;
    CHECK(t.Load(dir / L"s.ini") && n == 7);
    CHECK(!t.Load(dir / L"missing.ini"));
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ---- plugins: settings store, Lua sandbox, the time tracker plugin ----------------------------------

namespace fake {
struct Sub { d3h_event_fn fn; void* user; };
struct Page { std::string id, title, group; d3h_draw_fn draw; void* user; };
struct State {
    PluginSettings settings;
    std::vector<std::pair<int32_t, Sub>> subs;
    std::vector<Page> pages;
    std::vector<std::string> logs, drawn, lines, tips;
    std::vector<d3h_actor> actors;
    std::vector<d3h_effect> effects;
    std::vector<d3h_rect> rects;
    std::vector<Page> panels;
    size_t checkboxes = 0, swatches = 0;  // widgets a page drew
    d3h_frame frame{};
    int comboPick = 0;  // 0 = leave the combo alone, else the 1-based index to choose
    void Reset() { *this = State(); frame.size = sizeof(frame); frame.rift_timer_fraction = -1.f; frame.hovered_item = -1; }
};
State g;

std::string Key(const char* k) { return *CurrentPluginName() + "." + k; }

void Log(int32_t, const char* m) { g.logs.emplace_back(m); }
void GetFrame(d3h_frame* out) {
    *out = g.frame;
    out->actor_count = static_cast<uint32_t>(g.actors.size());
    out->effect_count = static_cast<uint32_t>(g.effects.size());
}
d3h_bool GetEffect(uint32_t i, d3h_effect* out) {
    if (i >= g.effects.size()) return 0;
    *out = g.effects[i];
    return 1;
}
d3h_bool GetActor(uint32_t i, d3h_actor* out) {
    if (i >= g.actors.size()) return 0;
    *out = g.actors[i];
    return 1;
}
d3h_bool WorldToScreen(d3h_vec2 w, float, d3h_vec2* out) {
    if (w.x < 0) return 0;  // "off screen"
    *out = w;
    return 1;
}
void DrawRect(int32_t, d3h_rect r, d3h_color, d3h_bool, float) { g.rects.push_back(r); }
d3h_vec2 TextSize(const char* t, float scale) { return {7.f * scale * static_cast<float>(strlen(t)), 13.f * scale}; }
d3h_bool RegisterPanel(const d3h_panel_desc* d) {
    g.panels.push_back({d->id, d->title, "", d->draw, d->user});
    return 1;
}
void Subscribe(int32_t type, d3h_event_fn fn, void* user) { g.subs.push_back({type, {fn, user}}); }
void DrawText(int32_t, d3h_vec2, d3h_color, const char* t, float) { g.drawn.emplace_back(t); }
d3h_bool RegisterPage(const d3h_page_desc* d) {
    g.pages.push_back({d->id, d->title, d->group, d->draw, d->user});
    return 1;
}
d3h_bool Define(const d3h_setting_desc* d) { return g.settings.Define(*CurrentPluginName(), *d); }
d3h_bool GetBool(const char* k, d3h_bool* o) { double v; if (!g.settings.GetNumber(Key(k), v)) return 0; *o = v != 0; return 1; }
d3h_bool GetInt(const char* k, int32_t* o) { double v; if (!g.settings.GetNumber(Key(k), v)) return 0; *o = static_cast<int32_t>(v); return 1; }
d3h_bool GetFloat(const char* k, float* o) { double v; if (!g.settings.GetNumber(Key(k), v)) return 0; *o = static_cast<float>(v); return 1; }
d3h_bool GetColor(const char* k, d3h_color* o) { double v; if (!g.settings.GetNumber(Key(k), v)) return 0; *o = static_cast<d3h_color>(v); return 1; }
d3h_bool GetString(const char* k, char* o, size_t n) {
    std::string v;
    if (!g.settings.GetString(Key(k), v) || n == 0) return 0;
    strncpy_s(o, n, v.c_str(), _TRUNCATE);
    return 1;
}
d3h_bool SetValue(const char* k, double v) { (void)g.settings.SetNumber(Key(k), v); return 1; }
d3h_bool SetString(const char* k, const char* v) { (void)g.settings.SetString(Key(k), v); return 1; }

d3h_host MakeHost() {
    d3h_host h{};
    h.size = sizeof(h);
    h.api_version = D3H_API_VERSION;
    h.log = Log; h.get_frame = GetFrame; h.get_actor = GetActor; h.get_effect = GetEffect; h.subscribe = Subscribe; h.draw_text = DrawText;
    h.world_to_screen = WorldToScreen; h.draw_rect = DrawRect; h.register_panel = RegisterPanel;
    h.text_size = TextSize;
    h.register_page = RegisterPage; h.define_setting = Define; h.get_bool = GetBool; h.get_int = GetInt;
    h.get_float = GetFloat; h.get_color = GetColor; h.get_string = GetString; h.set_value = SetValue; h.set_string = SetString;
    return h;
}

// A ui that records the text and answers the combo with g.comboPick.
void UText(const char* t) { g.lines.emplace_back(t); }
void UTextColored(d3h_color, const char* t) { g.lines.emplace_back(t); }
d3h_bool UHovered() { return 1; }
void UTooltip(const char* t) { g.tips.emplace_back(t); }
void UNone() {}
d3h_bool UCombo(const char*, int32_t* idx, const char*) {
    if (g.comboPick == 0) return 0;
    *idx = g.comboPick - 1;
    return 1;
}
d3h_bool UInput(const char*, char*, size_t) { return 0; }
d3h_bool UButton(const char*) { return 0; }
d3h_bool UCheckbox(const char*, d3h_bool*) { ++g.checkboxes; return 0; }
d3h_bool UColor(const char*, d3h_color*) { ++g.swatches; return 0; }
d3h_ui MakeUi() {
    d3h_ui u{};
    u.size = sizeof(u);
    u.text = UText; u.text_colored = UTextColored; u.is_item_hovered = UHovered; u.tooltip = UTooltip; u.text_disabled = UText; u.separator = UNone; u.separator_text = UText;
    u.same_line = UNone; u.spacing = UNone; u.button = UButton; u.checkbox = UCheckbox; u.combo = UCombo;
    u.input_text = UInput; u.color_edit = UColor;
    return u;
}

void Fire(int32_t type, int32_t index = -1, int64_t value = 0, const char* text = nullptr) {
    d3h_event ev{sizeof(ev), type, index, value, text};
    for (const auto& s : std::vector<std::pair<int32_t, Sub>>(g.subs))
        if (s.first == type) s.second.fn(&ev, s.second.user);
}
void DrawPages() {
    const d3h_ui ui = MakeUi();
    for (const Page& p : g.pages) p.draw(&ui, p.user);
}
void DrawPanels() {
    const d3h_ui ui = MakeUi();
    for (const Page& p : g.panels) p.draw(&ui, p.user);
}
d3h_actor Actor(int32_t kind, float x, float hp, const char* name, uint32_t affixes = 0) {
    d3h_actor a{};
    a.affix_mask = affixes;
    a.size = sizeof(a);
    a.kind = kind;
    a.pos = {x, 5.f};
    a.hp_fraction = hp;
    a.hp_current = hp > 0 ? hp * 1000.f : 0.f;
    a.hp_max = hp >= 0 ? 1000.f : 0.f;
    strncpy_s(a.name, sizeof(a.name), name, _TRUNCATE);
    return a;
}
}  // namespace fake

static std::filesystem::path WritePlugin(const std::filesystem::path& root, const char* folder, const std::string& mainLua,
                                         const char* manifest = "return { name = 'testplugin', version = '1', api = 1 }") {
    namespace fs = std::filesystem;
    const fs::path dir = root / folder;
    fs::create_directories(dir);
    File m = OpenFile(dir / L"plugin.lua", L"wb");
    fwrite(manifest, 1, strlen(manifest), m);
    File s = OpenFile(dir / L"main.lua", L"wb");
    fwrite(mainLua.data(), 1, mainLua.size(), s);
    return dir;
}

// ---- effects: buffs and cooldowns from the player's attributes ---------------------------------------

static AttribEntry Attr(uint32_t index, uint32_t sno, uint32_t value) { return {index, sno, value}; }

static void TestEffects() {
    // channeling pylon in slot 0 (60 s), cheat death cooldown buff: slot 1 expired (zeroed) and slot 3 running,
    // a potion on cooldown, a skill whose cooldown is idle (sentinels), and noise
    std::vector<AttribEntry> a = {
        Attr(kAttrBuffStartTick, 266258, 74518), Attr(kAttrBuffEndTick, 266258, 78118), Attr(kAttrBuffCount, 266258, 1),
        Attr(kAttrBuffStartTick + 1, 485530, 0), Attr(kAttrBuffEndTick + 1, 485530, 0), Attr(kAttrBuffCount + 1, 485530, 0),
        Attr(kAttrBuffStartTick + 3, 485530, 58254), Attr(kAttrBuffEndTick + 3, 485530, 58344), Attr(kAttrBuffCount + 3, 485530, 2),
        // Vengeance-like: the same buff in three more icon slots (identical times = one effect), and a power with a
        // second, different buff in another slot (two effects)
        Attr(kAttrBuffStartTick + 1, 266258, 74518), Attr(kAttrBuffEndTick + 1, 266258, 78118), Attr(kAttrBuffCount + 1, 266258, 1),
        Attr(kAttrBuffStartTick + 2, 266258, 74518), Attr(kAttrBuffEndTick + 2, 266258, 78118), Attr(kAttrBuffCount + 2, 266258, 1),
        Attr(kAttrBuffStartTick + 5, 485530, 58300), Attr(kAttrBuffEndTick + 5, 485530, 65500), Attr(kAttrBuffCount + 5, 485530, 1),
        Attr(kAttrPowerCooldown, 30211, 77320), Attr(kAttrPowerCooldownStart, 30211, 75520),
        Attr(kAttrPowerCooldown, 130695, kCooldownIdleFinish), Attr(kAttrPowerCooldownStart, 130695, kCooldownIdleStart),
        {115, kAttribNoModifier, 1234},  // an attribute without a modifier
        Attr(100, 130695, 5),            // another attribute of a power
    };
    std::vector<EffectSnapshot> fx;
    ExtractEffects(a, &fx);
    CHECK(fx.size() == 4);
    CHECK(fx[0].sno == 266258 && fx[0].kind == EffectKind::Buff && fx[0].slot == 0 && fx[0].count == 1);  // slots 1, 2 merged
    CHECK(fx[0].startTick == 74518 && fx[0].endTick == 78118);
    CHECK(fx[1].sno == 485530 && fx[1].slot == 3 && fx[1].count == 2);  // the zeroed slot 1 is gone
    CHECK(fx[2].sno == 485530 && fx[2].slot == 5 && fx[2].endTick == 65500);  // different times: its own effect
    CHECK(fx[3].sno == 30211 && fx[3].kind == EffectKind::Cooldown && fx[3].startTick == 75520 && fx[3].endTick == 77320);

    // times as of a tick
    const EffectTimes t = TimesOf(fx[0], 74518 + 600);
    CHECK(t.duration == 60.f && std::fabs(t.elapsed - 10.f) < 1e-3f && std::fabs(t.remaining - 50.f) < 1e-3f);
    const EffectTimes late = TimesOf(fx[0], 90000);
    CHECK(late.remaining == 0.f && late.elapsed == 60.f);

    // the tick of a frame: extrapolated from a recent snapshot only
    CHECK(TickAtTime(-1, 0, 100) < 0);
    CHECK(TickAtTime(1000, 5000, 5100) == 1006.0);
    CHECK(TickAtTime(1000, 5000, 9000) == 1000.0);  // stale snapshot: no extrapolation
    CHECK(TickAtTime(1000, 5000, 100) == 1000.0);   // other clock (a replay)

    // the cap
    std::vector<AttribEntry> many;
    for (uint32_t i = 0; i < 300; ++i) {
        many.push_back(Attr(kAttrBuffStartTick, 1000 + i, 1));
        many.push_back(Attr(kAttrBuffEndTick, 1000 + i, 100));
        many.push_back(Attr(kAttrBuffCount, 1000 + i, 1));
    }
    ExtractEffects(many, &fx);
    CHECK(fx.size() == static_cast<size_t>(kMaxEffects));
}

static void TestGameClock() {
    auto buff = [](uint32_t sno, int32_t start, int32_t end) {
        EffectSnapshot e;
        e.sno = sno;
        e.startTick = start;
        e.endTick = end;
        return e;
    };
    GameClock c;
    CHECK(!c.valid() && c.Tick(0) < 0);
    // first poll: the newest start is the best lower bound there is
    std::vector<EffectSnapshot> fx = {buff(1, 1000, 5000), buff(2, 1200, 9000)};
    c.Update(0, fx.data(), fx.size());
    CHECK(c.valid() && c.Tick(0) == 1200);
    CHECK(c.Tick(1000) == 1260);  // 60 ticks per second
    // nothing changed: the estimate just runs on
    c.Update(1000, fx.data(), fx.size());
    CHECK(c.Tick(1000) == 1260);
    // a passive refreshed: its start is "now" (ahead of the estimate) and becomes the anchor
    fx[0].startTick = 1500;
    fx[0].endTick = 1590;
    c.Update(2000, fx.data(), fx.size());
    CHECK(c.Tick(2000) == 1500 && c.Tick(3000) == 1560);
    // a refresh that lands close to the extrapolation does not make the clock jitter backwards
    fx[0].startTick = 1560 - 3;
    c.Update(3000, fx.data(), fx.size());
    CHECK(c.Tick(3000) == 1560);
    // a stack decrement keeps an old start tick: not "now"
    fx[1].startTick = 900;
    c.Update(3100, fx.data(), fx.size());
    CHECK(c.Tick(3100) == 1566);
    // an effect still listed long after its end: the estimate was ahead, pull it back
    GameClock d;
    std::vector<EffectSnapshot> g2 = {buff(1, 2000, 2400)};
    d.Update(0, g2.data(), g2.size());
    d.Update(60000, g2.data(), g2.size());  // 60 s later the estimate is 5600, the buff claims to end at 2400
    CHECK(d.Tick(60000) <= 2430);
}

static void TestPowerNames() {
    CHECK(PowerNames::Prettify("Generic_EnchantressCheatDeathPassive") == "Enchantress Cheat Death Passive");
    CHECK(PowerNames::Prettify("DemonHunter_SmokeScreen") == "Smoke Screen");
    CHECK(PowerNames::Prettify("Generic_ItemPassiveUniqueRing735x1") == "Item Passive Unique Ring 735x1");
    CHECK(PowerNames::Prettify("Generic_DOTSomething") == "DOT Something");
    CHECK(PowerNames::Prettify("NoPrefix") == "No Prefix");

    const char* path = "test_sno_powers.tsv";
    {
        File f = OpenFile(path, L"wb");
        const char* text = "sno\tname\r\n266258\tGeneric_PagesBuffInfiniteCasting\r\n30211\tGeneric_DrinkHealthPotion\r\n";
        fwrite(text, 1, strlen(text), f);
    }
    PowerNames n;
    CHECK(n.Load(path) && n.size() == 2);
    CHECK(n.Code(30211) != nullptr && *n.Code(30211) == "Generic_DrinkHealthPotion");
    CHECK(n.Readable(266258) == "Pages Buff Infinite Casting");
    CHECK(n.Code(1) == nullptr && n.Readable(1) == "Power 1");
    CHECK(!n.Load("no_such_file.tsv"));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static void TestPluginSettings() {
    PluginSettings s;
    d3h_setting_desc d{};
    d.size = sizeof(d);
    d.key = "level"; d.type = D3H_SETTING_INT; d.min = 1; d.max = 5; d.def = 3;
    CHECK(s.Define("p", d));
    double v = 0;
    CHECK(s.GetNumber("p.level", v) && v == 3);
    CHECK(s.SetNumber("p.level", 99) && s.GetNumber("p.level", v) && v == 5);  // clamped
    CHECK(!s.SetNumber("p.unknown", 1) && !s.GetNumber("q.level", v));
    d.key = "name"; d.type = D3H_SETTING_STRING; d.def_string = "x";
    CHECK(s.Define("p", d));
    CHECK(s.SetString("p.name", "line1\nline2\\end"));

    // a value stored before the plugin defines the key survives and is applied (within the limits)
    PluginSettings t;
    t.LoadText(s.SaveText() + "p.later=7\n");
    d.key = "level"; d.type = D3H_SETTING_INT;
    CHECK(t.Define("p", d) && t.GetNumber("p.level", v) && v == 5);
    d.key = "name"; d.type = D3H_SETTING_STRING;
    CHECK(t.Define("p", d));
    std::string str;
    CHECK(t.GetString("p.name", str) && str == "line1\nline2\\end");
    d.key = "later"; d.type = D3H_SETTING_FLOAT; d.min = 0; d.max = 10; d.def = 1;
    CHECK(t.Define("p", d) && t.GetNumber("p.later", v) && v == 7);
    d.type = D3H_SETTING_BOOL;
    CHECK(!t.Define("p", d));  // same key, other type
    CHECK(t.Of("p").size() == 3);
}

static void TestLuaSandbox() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / L"d3hud_plugins_é";
    std::error_code ec;
    fs::remove_all(root, ec);
    fake::g.Reset();
    const d3h_host host = fake::MakeHost();
    std::string err;
    auto load = [&](const char* folder, const std::string& code, const char* manifest = nullptr) {
        const fs::path dir = manifest ? WritePlugin(root, folder, code, manifest) : WritePlugin(root, folder, code);
        err.clear();
        return LuaPlugin::Load(dir, host, err);
    };

    CHECK(load("ok", "assert(os == nil and io == nil and require == nil and dofile == nil and loadfile == nil and load == nil "
                     "and debug == nil and package == nil and collectgarbage == nil and print == nil)\n"
                     "assert(string.dump == nil and d3hud.api == 2)\n"
                     "assert(string.format('%d', 3) == '3' and math.floor(2.5) == 2 and #table.concat({'a','b'}) == 2)") != nullptr);
    CHECK(err.empty());

    CHECK(load("syntax", "this is not lua") == nullptr && !err.empty());
    CHECK(load("loop", "while true do end") == nullptr && err.find("instructions") != std::string::npos);
    CHECK(load("memory", "local t = {} for i = 1, 1e7 do t[i] = string.rep('x', 4096) .. i end") == nullptr);
    CHECK(load("global", "x = 1\nassert(x == 1)") != nullptr);  // globals live in the plugin's own environment
    CHECK(load("badname", "", "return { name = '../evil', api = 1 }") == nullptr);
    CHECK(load("badapi", "", "return { name = 'x', api = 99 }") == nullptr && err.find("api") != std::string::npos);
    CHECK(load("manifestglobals", "", "return { name = os.getenv('PATH'), api = 1 }") == nullptr);
    CHECK(load("nomanifest", "", "this is not a table") == nullptr);
    (void)WritePlugin(root, "nomain", "");
    fs::remove(root / L"nomain" / L"main.lua");
    CHECK(LuaPlugin::Load(root / L"nomain", host, err) == nullptr && err.find("main.lua") != std::string::npos);

    // errors are counted, logged with the plugin's name, and switch the plugin off after kMaxErrors
    fake::g.Reset();
    auto bad = load("bad", "d3hud.on('frame', function() error('boom') end)");
    CHECK(bad != nullptr);
    for (int i = 0; i < LuaPlugin::kMaxErrors + 3; ++i) fake::Fire(D3H_EVENT_FRAME);
    CHECK(bad->disabled() && bad->errorCount() == LuaPlugin::kMaxErrors);
    CHECK(!fake::g.logs.empty() && fake::g.logs[0].find("[testplugin]") == 0 && fake::g.logs[0].find("boom") != std::string::npos);

    // time spent in plugin code is measured per frame and published once per window
    fake::g.Reset();
    auto busy = load("busy", "d3hud.on('frame', function() local n = 0 for i = 1, 300000 do n = n + i end end)");
    CHECK(busy != nullptr && busy->timing().avgUs == 0.0);
    auto clock = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        fake::Fire(D3H_EVENT_FRAME);
        busy->EndFrame(clock + std::chrono::milliseconds(i * 10));  // 40 ms: still inside the first window
    }
    CHECK(busy->timing().avgUs == 0.0);  // nothing published yet, so a display does not flicker
    fake::Fire(D3H_EVENT_FRAME);
    busy->EndFrame(clock + std::chrono::milliseconds(600));
    CHECK(busy->timing().avgUs > 0.0 && busy->timing().peakUs >= busy->timing().avgUs);
    CHECK(busy->timing().calls == 1.0);
    const double published = busy->timing().avgUs;
    fake::Fire(D3H_EVENT_FRAME);
    busy->EndFrame(clock + std::chrono::milliseconds(700));
    CHECK(busy->timing().avgUs == published);  // unchanged until the next window ends

    // events carry their data; settings round trip through the host
    fake::g.Reset();
    auto ok = load("events", "d3hud.setting{ key = 'n', type = 'int', default = 2, min = 0, max = 9 }\n"
                             "d3hud.setting{ key = 'c', type = 'choice', default = 2, choices = {'a','b','c'} }\n"
                             "d3hud.on('world_change', function(ev) d3hud.set('n', ev.value) end)\n"
                             "d3hud.on('hotkey', function(ev) d3hud.set('c', 3); d3hud.log(ev.text .. d3hud.get('c') .. d3hud.get('n')) end)");
    CHECK(ok != nullptr);
    double v = 0;
    CHECK(fake::g.settings.GetNumber("testplugin.c", v) && v == 1);  // Lua default 2 is index 1
    fake::Fire(D3H_EVENT_WORLD_CHANGE, -1, 42);
    CHECK(fake::g.settings.GetNumber("testplugin.n", v) && v == 9);  // clamped by the host
    fake::Fire(D3H_EVENT_HOTKEY, -1, 0, "go");
    CHECK(fake::g.logs.size() == 1 && fake::g.logs[0] == "go39");

    // d3hud.actors(): everything the snapshot knows, with names and the distance to the player
    fake::g.Reset();
    fake::g.frame.player_pos = {0.f, 5.f};
    fake::g.actors = {fake::Actor(D3H_ACTOR_RARE, 30.f, 0.25f, "Skeleton_Rare"), fake::Actor(D3H_ACTOR_TRASH, 4.f, -1.f, "Zombie")};
    auto lister = load("lister", "d3hud.on('frame', function()\n"
                                 "  local all = d3hud.actors()\n"
                                 "  local r = all[1]\n"
                                 "  d3hud.log(#all .. ' ' .. r.kind_name .. ' ' .. r.name .. ' ' .. r.dist .. ' ' .. r.hp .. ' ' .. r.hp_current .. '/' .. r.hp_max .. ' ' .. all[2].hp .. ' ' .. d3hud.actor(2).name .. ' ' .. tostring((d3hud.actor(3))))\n"
                                 "end)");
    CHECK(lister != nullptr);
    fake::Fire(D3H_EVENT_FRAME);
    CHECK(fake::g.logs.size() == 1 && fake::g.logs[0] == "2 rare Skeleton_Rare 30.0 0.25 250.0/1000.0 -1.0 Zombie nil");

    // affixes: keys and names in the order of d3hud.affixes
    fake::g.Reset();
    fake::g.actors = {fake::Actor(D3H_ACTOR_CHAMPION, 10.f, 0.5f, "Champ",
                                  (1u << FindMonsterAffixIndex(4131130388u)) | (1u << FindMonsterAffixIndex(106438735)))};
    auto affixes = load("affixes", "d3hud.on('frame', function()\n"
                                   "  local a = d3hud.actor(1)\n"
                                   "  d3hud.log(#d3hud.affixes .. ' ' .. d3hud.affixes[2].key .. ' ' .. #a.affixes .. ' ' .. a.affixes[1] .. ' ' .. a.affix_names[2])\n"
                                   "end)");
    CHECK(affixes != nullptr);
    fake::Fire(D3H_EVENT_FRAME);
    CHECK(fake::g.logs.size() == 1 && fake::g.logs[0] == "30 molten 2 molten Frozen");
    fs::remove_all(root, ec);
}

// Runs plugins/timetracker against the fake host: classification, percentages, persistence.
static void TestLuaEffects() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / L"d3hud_plugins_effects";
    std::error_code ec;
    fs::remove_all(root, ec);
    fake::g.Reset();
    d3h_effect e{};
    e.size = sizeof(e);
    e.sno = 266258;
    e.kind = D3H_EFFECT_BUFF;
    e.stacks = 1;
    e.duration = 60.f;
    e.remaining = 15.f;
    e.elapsed = 45.f;
    e.timed = 1;
    strncpy_s(e.name, sizeof(e.name), "Pages Buff Infinite Casting", _TRUNCATE);
    strncpy_s(e.code, sizeof(e.code), "Generic_PagesBuffInfiniteCasting", _TRUNCATE);
    fake::g.effects.push_back(e);
    e.sno = 30211;
    e.kind = D3H_EFFECT_COOLDOWN;
    e.timed = 0;
    e.remaining = 0.f;
    fake::g.effects.push_back(e);
    const d3h_host host = fake::MakeHost();
    std::string err;
    const fs::path dir = WritePlugin(root, "fx",
        "local fx = d3hud.effects()\n"
        "assert(#fx == 2 and d3hud.frame().effect_count == 2)\n"
        "assert(fx[1].sno == 266258 and fx[1].kind == 'buff' and fx[1].stacks == 1)\n"
        "assert(fx[1].name == 'Pages Buff Infinite Casting' and fx[1].code == 'Generic_PagesBuffInfiniteCasting')\n"
        "assert(fx[1].duration == 60 and fx[1].remaining == 15 and fx[1].elapsed == 45 and fx[1].fraction == 0.25 and fx[1].timed)\n"
        "assert(fx[2].kind == 'cooldown' and not fx[2].timed and fx[2].fraction == 0)\n");
    CHECK(LuaPlugin::Load(dir, host, err) != nullptr);
    CHECK(err.empty());
    // a host without the function: the binding is simply absent
    d3h_host old = fake::MakeHost();
    old.size = static_cast<uint32_t>(offsetof(d3h_host, get_effect));
    const fs::path dir2 = WritePlugin(root, "nofx", "assert(d3hud.effects == nil)");
    err.clear();
    CHECK(LuaPlugin::Load(dir2, old, err) != nullptr);
    fs::remove_all(root, ec);
}

static void TestTimeTracker() {
    namespace fs = std::filesystem;
    fake::g.Reset();
    const d3h_host host = fake::MakeHost();
    std::string err;
    auto p = LuaPlugin::Load(fs::path(D3HUD_SOURCE_DIR) / L"plugins" / L"timetracker", host, err);
    CHECK(p != nullptr);
    if (p == nullptr) {
        fprintf(stderr, "timetracker: %s\n", err.c_str());
        return;
    }
    CHECK(p->manifest().name == "timetracker" && fake::g.pages.size() == 1 && fake::g.panels.size() == 1);

    auto tick = [&](double ms, uint32_t world, uint32_t uiFlags = 0) {
        fake::g.frame.time_ms = ms;
        fake::g.frame.world_sno = world;
        fake::g.frame.ui_flags = uiFlags;
        fake::Fire(D3H_EVENT_SNAPSHOT);
    };
    // 10 s in an unassigned world, then assign it to Town and stay 20 s, 30 s in a rift, a stall that is not
    // counted, 10 s in a greater rift
    double t = 0;
    for (int i = 0; i <= 10; ++i, t += 1000) tick(t, 1000);
    fake::g.comboPick = 1;  // "Town" is the first area
    fake::DrawPages();
    fake::g.comboPick = 0;
    for (int i = 0; i < 20; ++i, t += 1000) tick(t, 1000);
    for (int i = 0; i < 30; ++i, t += 1000) tick(t, 2000, D3H_UI_RIFT);
    t += 20000;
    tick(t, 2000);  // after the stall only the reference time moves
    for (int i = 0; i < 10; ++i) {
        t += 1000;
        tick(t, 2000, D3H_UI_RIFT | D3H_UI_GREATER_RIFT);
    }

    std::string totals;
    CHECK(fake::g.settings.GetString("timetracker.totals", totals));
    CHECK(totals.find("Town=") != std::string::npos && totals.find("Rift=") != std::string::npos);  // saved every 30 s

    fake::g.lines.clear();
    fake::DrawPages();
    std::string page;
    for (const std::string& l : fake::g.lines) page += l + "\n";
    CHECK(page.find("This session") != std::string::npos && page.find("All time") != std::string::npos);
    CHECK(page.find("Town") != std::string::npos && page.find("Greater Rift") != std::string::npos);
    // session: Other 10 s, Town 20 s, Rift 30 s, Greater Rift 10 s, together 70 s
    CHECK(page.find("42.9%") != std::string::npos);  // Rift 30 / 70
    CHECK(page.find("total 0:01:10") != std::string::npos);
    CHECK(!p->disabled());

    // the dock panel shows the same split
    fake::g.lines.clear();
    fake::DrawPanels();
    page.clear();
    for (const std::string& l : fake::g.lines) page += l + "\n";
    CHECK(page.find("This session") != std::string::npos && page.find("42.9%") != std::string::npos);

    // the totals survive a restart: a new plugin instance reads them from the stored setting
    fake::g.subs.clear();
    fake::g.pages.clear();
    fake::g.panels.clear();
    p.reset();
    p = LuaPlugin::Load(fs::path(D3HUD_SOURCE_DIR) / L"plugins" / L"timetracker", host, err);
    CHECK(p != nullptr);
    fake::g.lines.clear();
    fake::DrawPages();
    page.clear();
    for (const std::string& l : fake::g.lines) page += l + "\n";
    CHECK(page.find("nothing recorded yet") != std::string::npos);  // this session is empty again ...
    CHECK(page.find("Town") != std::string::npos);                  // ... but the all-time table is not
}

// The shared Lua libraries (plugins/lib): d3hud.require, and the standard way to draw.
static void TestLuaLibraries() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / L"d3hud_libs_\u00e9";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    fs::copy(fs::path(D3HUD_SOURCE_DIR) / L"plugins" / L"lib", root / L"lib", fs::copy_options::recursive, ec);
    CHECK(!ec);
    {
        File f = OpenFile(root / L"lib" / L"selfref.lua", L"wb");
        const char* src = "return d3hud.require('selfref')";
        fwrite(src, 1, strlen(src), f);
    }
    fake::g.Reset();
    const d3h_host host = fake::MakeHost();
    std::string err;
    auto load = [&](const char* folder, const std::string& code) {
        const fs::path dir = WritePlugin(root, folder, code);
        err.clear();
        return LuaPlugin::Load(dir, host, err);
    };

    // pure helpers
    auto pure = load("pure", R"lua(
local draw, fmt, store = d3hud.require("draw"), d3hud.require("fmt"), d3hud.require("store")
assert(draw.contrast(0xFFC040, 112) == draw.WHITE, "amber on a translucent fill: white text")
assert(draw.contrast(0xFFFFFF, 255) == draw.BLACK, "a solid white fill: black text")
assert(draw.contrast(0x000000, 112) == draw.WHITE)
assert(draw.argb(0x102030, 0x80) == 0x80102030 and draw.argb(0x102030) == 0xFF102030)
assert(fmt.hms(3725) == "1:02:05" and fmt.pct(0.427) == "43%")
assert(fmt.big(999) == "999" and fmt.big(1000) == "1.00K" and fmt.big(45600) == "45.6K" and fmt.big(1234567) == "1.23M")
assert(fmt.big(1.5e12) == "1.50T" and fmt.big(0) == "0" and fmt.dur(3.14) == "3.1s" and fmt.dur(75) == "1:15")
assert(fmt.pretty("x1_Fallen_Champion") == "Fallen Champion" and fmt.pretty("FallenHound") == "Fallen Hound")
assert(store.pack_map({ b = 2, a = 1 }) == "a=1;b=2")
assert(store.unpack_map("a=1;b=2", { number = true }).b == 2 and store.unpack_map("a=x;b=y").a == "x")
assert(store.pack_map({ k = 1.5 }, { sep = ",", format = function(v) return string.format("%.1f", v) end }) == "k=1.5")
local m = store.unpack_map("molten=1,frozen=2", { sep = ",", number = true })
assert(m.molten == 1 and m.frozen == 2)
local l = store.unpack_list("a,b,c")
assert(#l == 3 and store.pack_list(l) == "a,b,c" and #store.unpack_list("") == 0)
assert(d3hud.require("draw") == draw, "a library is loaded once")
assert(not pcall(d3hud.require, "../evil") and not pcall(d3hud.require, "C:/x") and not pcall(d3hud.require, "nosuchlib"))
assert(not pcall(d3hud.require, "selfref"), "a library that requires itself is an error, not an endless loop")
)lua");
    CHECK(pure != nullptr);
    if (pure == nullptr) fprintf(stderr, "libs: %s\n", err.c_str());

    // tags: a translucent fill and a border in the colour, black / white text; what does not fit is left out
    fake::g.Reset();
    auto tags = load("tags", R"lua(
local draw = d3hud.require("draw")
d3hud.on("frame", function()
    draw.tag(10, 10, "Molten", 0xFF6A3D, 1)
    local w, h = draw.tags(100, 50, { {text = "AAAA", color = 0xFF0000}, {text = "BBBB", color = 0x00FF00}, {text = "CCCC", color = 0x0000FF} }, 1, 80)
    d3hud.log(w .. " " .. h)
end)
)lua");
    CHECK(tags != nullptr);
    fake::Fire(D3H_EVENT_FRAME);
    CHECK(!tags->disabled());
    // Molten: fill + border; two of the three AAAA/BBBB/CCCC tags fit into 80 px: two rects and one text each
    CHECK(fake::g.rects.size() == 2 + 2 * 2);
    CHECK(fake::g.drawn.size() == 3 && fake::g.drawn[0] == "Molten" && fake::g.drawn[1] == "AAAA" && fake::g.drawn[2] == "BBBB");
    CHECK(fake::g.logs.size() == 1 && fake::g.logs[0] == "71.0 19.0");  // 34 + 3 + 34 wide, 13 + 6 tall
    fs::remove_all(root, ec);
}

// plugins/damagemeter: damage from how the life of elites falls
static d3h_effect FxEntry(uint32_t sno, int32_t kind, float duration, float remaining, const char* name, const char* code,
                          int32_t stacks = 1, bool timed = true) {
    d3h_effect e{};
    e.size = sizeof(e);
    e.sno = sno;
    e.kind = kind;
    e.stacks = stacks;
    e.duration = duration;
    e.remaining = timed ? remaining : 0.f;
    e.elapsed = timed ? duration - remaining : 0.f;
    e.timed = timed ? 1 : 0;
    strncpy_s(e.name, sizeof(e.name), name, _TRUNCATE);
    strncpy_s(e.code, sizeof(e.code), code, _TRUNCATE);
    return e;
}

static void TestEffectsPlugin() {
    namespace fs = std::filesystem;
    fake::g.Reset();
    const d3h_host host = fake::MakeHost();
    std::string err;
    auto p = LuaPlugin::Load(fs::path(D3HUD_SOURCE_DIR) / L"plugins" / L"effects", host, err);
    CHECK(p != nullptr);
    if (p == nullptr) {
        fprintf(stderr, "effects: %s\n", err.c_str());
        return;
    }
    CHECK(fake::g.panels.size() == 1);
    fake::g.frame.screen_w = 1920.f;
    fake::g.frame.screen_h = 1080.f;
    auto panel = [&] {
        fake::Fire(D3H_EVENT_FRAME);  // the list is worked out once per drawn frame
        fake::g.lines.clear();
        fake::DrawPanels();
        std::string text;
        for (const std::string& l : fake::g.lines) text += l + "\n";
        return text;
    };
    auto at = [](const std::string& text, const char* needle) { return text.find(needle); };

    CHECK(panel().find("nothing running") != std::string::npos);

    fake::g.effects = {
        FxEntry(266258, D3H_EFFECT_BUFF, 60.f, 45.3f, "Pages Buff Infinite Casting", "Generic_PagesBuffInfiniteCasting"),
        FxEntry(485530, D3H_EFFECT_BUFF, 120.f, 75.f, "Enchantress Cheat Death Passive", "Generic_EnchantressCheatDeathPassive"),
        FxEntry(485530, D3H_EFFECT_BUFF, 1.5f, 0.5f, "Enchantress Cheat Death Passive", "Generic_EnchantressCheatDeathPassive"),
        FxEntry(30211, D3H_EFFECT_COOLDOWN, 30.f, 12.f, "Drink Health Potion", "Generic_DrinkHealthPotion"),
        FxEntry(90314, D3H_EFFECT_BUFF, 10.f, 4.f, "Monster Affix Molten", "Generic_MonsterAffixMolten"),
        FxEntry(134334, D3H_EFFECT_BUFF, 36000.f, 30000.f, "Banter Cooldown", "Generic_BanterCooldown"),
        FxEntry(318383, D3H_EFFECT_BUFF, 8.f, 3.f, "Item Passive Unique Ring 521x1", "Generic_ItemPassiveUniqueRing521x1", 3),
    };
    // the default: only the important ones. Cooldowns first (the potion 12 s, cheat death 1:15), then buffs (the pylon)
    std::string text = panel();
    CHECK(at(text, "Cooldowns") < at(text, "Health Potion") && at(text, "Health Potion") < at(text, "Cheat Death"));
    CHECK(at(text, "Cheat Death") < at(text, "Buffs") && at(text, "Buffs") < at(text, "Channeling Pylon"));
    CHECK(text.find("12.0s   Health Potion") != std::string::npos);
    CHECK(text.find("1:15   Cheat Death") != std::string::npos);
    CHECK(text.find("45.3s   Channeling Pylon") != std::string::npos);
    CHECK(text.find("Molten") == std::string::npos && text.find("Banter") == std::string::npos &&
          text.find("Item Passive") == std::string::npos);
    // the short cheat death renewal, the affix, the 10 hour cooldown and the ring passive
    CHECK(text.find("4 more hidden") != std::string::npos);
    CHECK(!fake::g.tips.empty());
    bool named = false;
    for (const std::string& t : fake::g.tips) named |= t.find("Generic_PagesBuffInfiniteCasting\npower 266258") != std::string::npos;
    CHECK(named);

    // a power of your own on the list of important ones
    CHECK(fake::g.settings.SetString("effects.extra", "1, 318383"));
    text = panel();
    CHECK(text.find("Item Passive Unique Ring 521x1  x3") != std::string::npos);
    CHECK(text.find("3 more hidden") != std::string::npos);
    CHECK(fake::g.settings.SetString("effects.extra", ""));

    // without noise: the filter by duration and by name
    CHECK(fake::g.settings.SetNumber("effects.mode", 1));  // stored 0-based: Without noise
    text = panel();
    CHECK(at(text, "Buffs") < at(text, "Item Passive Unique Ring 521x1  x3") &&
          at(text, "Item Passive Unique Ring 521x1  x3") < at(text, "Channeling Pylon"));  // soonest first
    CHECK(text.find("Molten") == std::string::npos && text.find("Banter") == std::string::npos);
    CHECK(text.find("3 more hidden") != std::string::npos);

    // everything
    CHECK(fake::g.settings.SetNumber("effects.mode", 2));  // Everything
    text = panel();
    CHECK(text.find("Monster Affix Molten") != std::string::npos);
    CHECK(text.find("Banter Cooldown") != std::string::npos);
    CHECK(text.find("hidden") == std::string::npos);
    CHECK(fake::g.settings.SetNumber("effects.mode", 0));  // Important only

    // the game clock is not known yet: no times, nothing broken
    fake::g.effects = {FxEntry(266258, D3H_EFFECT_BUFF, 60.f, 0.f, "Pages Buff Infinite Casting", "Generic_PagesBuffInfiniteCasting", 1, false)};
    CHECK(panel().find("?   Channeling Pylon") != std::string::npos);

    // the same list as bars on the screen
    fake::g.effects = {
        FxEntry(266258, D3H_EFFECT_BUFF, 60.f, 45.f, "Pages Buff Infinite Casting", "Generic_PagesBuffInfiniteCasting"),
        FxEntry(30211, D3H_EFFECT_COOLDOWN, 30.f, 12.f, "Drink Health Potion", "Generic_DrinkHealthPotion"),
    };
    fake::g.rects.clear();
    fake::g.drawn.clear();
    fake::Fire(D3H_EVENT_FRAME);
    CHECK(fake::g.rects.empty());  // off by default
    CHECK(fake::g.settings.SetNumber("effects.hud", 1));
    fake::Fire(D3H_EVENT_FRAME);
    CHECK(fake::g.rects.size() == 6);  // three rectangles per bar, two bars
    bool potion = false, pylon = false;
    for (const std::string& d : fake::g.drawn) {
        potion |= d.find("Health Potion") != std::string::npos;
        pylon |= d.find("Channeling Pylon") != std::string::npos;
    }
    CHECK(potion && pylon);
    CHECK(!p->disabled());
    if (p->disabled()) fprintf(stderr, "effects: %s", p->lastError().c_str());
}

static void TestDamageMeter() {
    namespace fs = std::filesystem;
    fake::g.Reset();
    const d3h_host host = fake::MakeHost();
    std::string err;
    auto p = LuaPlugin::Load(fs::path(D3HUD_SOURCE_DIR) / L"plugins" / L"damagemeter", host, err);
    CHECK(p != nullptr);
    if (p == nullptr) {
        fprintf(stderr, "damagemeter: %s\n", err.c_str());
        return;
    }
    CHECK(fake::g.panels.size() == 1);
    auto tick = [&](double seconds, std::vector<d3h_actor> actors) {
        fake::g.frame.time_ms = seconds * 1000.0;
        fake::g.actors = std::move(actors);
        fake::Fire(D3H_EVENT_SNAPSHOT);
    };
    auto panel = [&] {
        fake::g.lines.clear();
        fake::DrawPanels();
        std::string text;
        for (const std::string& l : fake::g.lines) text += l + "\n";
        return text;
    };
    auto rare = [&](uint64_t id, float x, int life) {
        d3h_actor a = fake::Actor(D3H_ACTOR_RARE, x, static_cast<float>(life) / 1000.f, "Rare");
        a.id = id;
        return a;
    };

    CHECK(panel().find("no fight right now") != std::string::npos);

    // 100 life lost every 0.1 s from 1000: a rare dying within a second, next to a second one that is never hurt
    for (int i = 0; i < 10; ++i) {
        tick(i * 0.1, {rare(77, 100.f, 1000 - 100 * i), rare(99, 200.f, 1000)});
        if (i == 5) {
            fake::g.drawn.clear();
            fake::Fire(D3H_EVENT_FRAME);  // the line on the screen
            bool dps = false, ttk = false;
            for (const std::string& d : fake::g.drawn) {
                dps |= d.rfind("DPS ", 0) == 0;
                ttk |= d.find("2 elites on the screen") != std::string::npos;
            }
            CHECK(dps && ttk);
        }
    }
    tick(1.0, {rare(99, 200.f, 1000)});  // the first rare is gone at 10% life, just hurt: killed, its last life counts
    if (p->disabled()) fprintf(stderr, "damagemeter: %s", p->lastError().c_str());
    CHECK(!p->disabled());
    std::string text = panel();
    CHECK(text.find("this fight 1.00K") != std::string::npos);
    CHECK(text.find("peak 1.00K/s") != std::string::npos);

    // nothing hurt for the idle time: the fight is over and its summary is listed
    for (int i = 1; i <= 60; ++i) tick(1.0 + i * 0.1, {rare(99, 200.f, 1000)});
    text = panel();
    CHECK(text.find("no fight right now") != std::string::npos);
    CHECK(text.find("Rare   1.00K in 0.9s   1.11K/s   peak 1.00K/s   1 killed") != std::string::npos);
    CHECK(text.find("best DPS 1.11K/s") != std::string::npos);
    double best = 0;
    CHECK(fake::g.settings.GetNumber("damagemeter.best_dps", best) && std::fabs(best - 1000.0 / 0.9) < 1.0);

    // healing is not damage: no fight starts
    tick(8.0, {rare(55, 300.f, 500)});
    tick(8.1, {rare(55, 300.f, 600)});
    CHECK(panel().find("no fight right now") != std::string::npos);

    // a pause between snapshots (reader stopped) restarts the measuring instead of counting a huge jump
    tick(8.2, {rare(55, 300.f, 600)});
    tick(30.0, {rare(55, 300.f, 100)});
    CHECK(panel().find("no fight right now") != std::string::npos);
    CHECK(!p->disabled());
}

// plugins/healthbars: bars for champions, rares and bosses that are on screen and alive
static void TestHealthBars() {
    namespace fs = std::filesystem;
    fake::g.Reset();
    const d3h_host host = fake::MakeHost();
    std::string err;
    auto p = LuaPlugin::Load(fs::path(D3HUD_SOURCE_DIR) / L"plugins" / L"healthbars", host, err);
    CHECK(p != nullptr);
    if (p == nullptr) {
        fprintf(stderr, "healthbars: %s\n", err.c_str());
        return;
    }
    fake::g.frame.screen_w = 1920.f;
    fake::g.frame.screen_h = 1080.f;
    fake::g.actors = {fake::Actor(D3H_ACTOR_CHAMPION, 100.f, 0.5f, "Champion"),
                      fake::Actor(D3H_ACTOR_RARE, 200.f, 1.0f, "Rare"),
                      fake::Actor(D3H_ACTOR_BOSS, 300.f, -1.f, "Boss, life not read"),
                      fake::Actor(D3H_ACTOR_TRASH, 400.f, 1.0f, "Trash"),
                      fake::Actor(D3H_ACTOR_RARE, 500.f, 0.f, "Dead rare"),
                      fake::Actor(D3H_ACTOR_CHAMPION, -50.f, 0.7f, "Off screen")};
    CHECK(fake::g.settings.SetNumber("healthbars.list", 0));  // the bars over the monsters first
    fake::Fire(D3H_EVENT_FRAME);
    CHECK(!p->disabled());
    // two bars (border, background, fill each) and their percentages (each drawn twice: shadow and text)
    CHECK(fake::g.rects.size() == 6);
    CHECK(fake::g.drawn.size() == 4 && fake::g.drawn[0] == "50%" && fake::g.drawn[2] == "100%");
    if (fake::g.rects.size() == 6) {
        const d3h_rect fill = fake::g.rects[2];  // the champion's: half of the 80 px bar
        CHECK(std::fabs((fill.x1 - fill.x0) - 40.f) < 0.01f);
    }

    // the size option scales the bar: twice as large, so the half-filled bar is 80 px (160 * 0.5) wide
    CHECK(fake::g.settings.SetNumber("healthbars.over_scale", 2));
    fake::g.rects.clear();
    fake::g.drawn.clear();
    fake::Fire(D3H_EVENT_FRAME);
    if (fake::g.rects.size() == 6) {
        const d3h_rect fill = fake::g.rects[2];
        CHECK(std::fabs((fill.x1 - fill.x0) - 80.f) < 0.01f);
    } else {
        CHECK(false);
    }
    CHECK(fake::g.settings.SetNumber("healthbars.over_scale", 1));

    // options: no champions, no percentages; the boss's life is read now
    fake::g.rects.clear();
    fake::g.drawn.clear();
    fake::g.actors[2].hp_fraction = 0.9f;
    CHECK(fake::g.settings.SetNumber("healthbars.champion", 0) && fake::g.settings.SetNumber("healthbars.percent", 0));
    fake::Fire(D3H_EVENT_FRAME);
    CHECK(fake::g.rects.size() == 6 && fake::g.drawn.empty());  // rare and boss

    // the list on the side: a box, then one bar per elite with its name and percentage inside, nearest first
    CHECK(fake::g.settings.SetNumber("healthbars.over", 0) && fake::g.settings.SetNumber("healthbars.list", 1) &&
          fake::g.settings.SetNumber("healthbars.champion", 1));
    fake::g.actors[0].name[0] = 'x';
    strncpy_s(fake::g.actors[0].name, sizeof(fake::g.actors[0].name), "x1_Fallen_Champion", _TRUNCATE);
    fake::g.rects.clear();
    fake::g.drawn.clear();
    fake::Fire(D3H_EVENT_FRAME);
    CHECK(!p->disabled());
    CHECK(fake::g.rects.size() == 1 + 3 * 3);  // box + champion, rare, boss
    // per row: percentage (shadow, text) then name (shadow, text); the first row is the champion, 100 units away
    CHECK(fake::g.drawn.size() == 12);
    if (fake::g.drawn.size() == 12) {
        CHECK(fake::g.drawn[0] == "50%" && fake::g.drawn[2] == "Fallen Champion");  // prettified name
        CHECK(fake::g.drawn[8] == "90%" && fake::g.drawn[10] == "Boss, life not read");
    }
    // the box and the bars sit on the left (default) side, inside the screen
    CHECK(fake::g.rects[0].x0 >= 0.f && fake::g.rects[0].x1 < 400.f);

    // right side, lowest life first, no box
    CHECK(fake::g.settings.SetNumber("healthbars.list_side", 1) && fake::g.settings.SetNumber("healthbars.list_sort", 1) &&
          fake::g.settings.SetNumber("healthbars.list_box", 0));
    fake::g.rects.clear();
    fake::g.drawn.clear();
    fake::Fire(D3H_EVENT_FRAME);
    CHECK(fake::g.rects.size() == 9 && fake::g.rects[0].x0 > 1920.f / 2);
    CHECK(fake::g.drawn.size() == 12 && fake::g.drawn[0] == "50%");  // champion has the lowest life (0.5)

    // a name too long for the bar is cut short
    strncpy_s(fake::g.actors[0].name, sizeof(fake::g.actors[0].name), "AVeryLongMonsterNameThatCannotFitInTheBar", _TRUNCATE);
    fake::g.drawn.clear();
    fake::Fire(D3H_EVENT_FRAME);
    bool cut = false;
    for (const std::string& d : fake::g.drawn) cut |= d.size() > 2 && d.ends_with("..");
    CHECK(cut);

    // affixes under the bars: only the ticked ones the monster has, in the game's order
    fake::g.actors = {fake::Actor(D3H_ACTOR_CHAMPION, 100.f, 0.5f, "Champion",
                                  (1u << FindMonsterAffixIndex(4131130388u)) | (1u << FindMonsterAffixIndex(106438735)) |
                                      (1u << FindMonsterAffixIndex(1905614711)))};  // frozen, molten, orbiter
    (void)fake::g.settings.SetNumber("healthbars.champion", 1);  // some of these already have the value
    (void)fake::g.settings.SetNumber("healthbars.over", 1);
    (void)fake::g.settings.SetNumber("healthbars.list", 0);
    fake::g.drawn.clear();
    fake::Fire(D3H_EVENT_FRAME);
    bool molten = false, frozen = false, orbiter = false;
    for (const std::string& d : fake::g.drawn) {  // each affix has its own colour, so each is drawn on its own
        molten |= d == "Molten";
        frozen |= d == "Frozen";
        orbiter |= d.find("Orbiter") != std::string::npos;  // not ticked by default
    }
    CHECK(molten && frozen && !orbiter);
    CHECK(fake::g.rects.size() >= 4);  // each tag: a translucent fill and a border
    CHECK(fake::g.settings.SetNumber("healthbars.affixes_show", 0));
    fake::g.drawn.clear();
    fake::Fire(D3H_EVENT_FRAME);
    bool anyMolten = false;
    for (const std::string& d : fake::g.drawn) anyMolten |= d.find("Molten") != std::string::npos;
    CHECK(!anyMolten);
    CHECK(!p->disabled());

    // the page: ticking an affix is remembered in the hidden setting
    CHECK(fake::g.settings.SetNumber("healthbars.affixes_show", 1));
    CHECK(fake::g.pages.size() == 1);
    std::string keys;
    CHECK(fake::g.settings.GetString("healthbars.affix_keys", keys) && keys.find("molten") != std::string::npos);

    // the page shows all 30 affixes with a checkbox and a colour swatch each
    fake::g.checkboxes = fake::g.swatches = 0;
    fake::DrawPages();
    CHECK(fake::g.checkboxes == 30 && fake::g.swatches == 30);
}

static void TestPaths() {
    namespace fs = std::filesystem;
    const fs::path exe = ExePath();
    CHECK(!exe.empty() && fs::exists(exe) && ExeDir() == exe.parent_path());
    // files must work in a folder whose name is not plain ASCII
    const fs::path dir = fs::temp_directory_path() / L"d3hud-t\u00ebst-\u00fcn\u00efc\u00f6de \u65e5\u672c";
    std::error_code ec;
    fs::create_directories(dir, ec);
    BasesCache c;
    c.pid = 7;
    c.created = 9;
    c.actorBase = 0x1234;
    c.acd = {1, 2};
    c.ui = {5};
    CHECK(c.Save(dir / L"bases.ini"));
    BasesCache d;
    d.pid = 7;
    d.created = 9;
    CHECK(d.Load(dir / L"bases.ini") && d.actorBase == 0x1234 && d.acd.size() == 2 && d.ui.size() == 1);
    CHECK(!Utf8(dir).empty() && Utf8(dir).find("d3hud-t") != std::string::npos);
    CHECK(OpenFile(dir / L"missing" / L"x.bin", L"rb") == nullptr);
    fs::remove_all(dir, ec);
}

static void TestRaii() {
    HANDLE raw = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    {
        UniqueHandle a(raw);
        UniqueHandle b = std::move(a);
        CHECK(!a && b && b.get() == raw);  // moved from: empty; the new owner holds it
        CHECK(!UniqueHandle(INVALID_HANDLE_VALUE));  // CreateFile's failure value counts as empty
    }
    DWORD flags = 0;
    CHECK(!GetHandleInformation(raw, &flags));  // closed when the owner went out of scope

    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / L"d3hud-raii-test.bin";
    {
        File f = OpenFile(tmp, L"wb");
        CHECK(f != nullptr);
        fputs("abc", f);
    }  // closed (and flushed) here, no fclose
    std::error_code ec;
    CHECK(fs::file_size(tmp, ec) == 3);
    File missing = OpenFile(tmp.parent_path() / L"no-such-dir" / L"x", L"rb");
    CHECK(missing == nullptr);
    fs::remove(tmp, ec);
}

static void TestRiftTimer() {
    // measured in a greater rift: bar 1505..1827 (322 units), marker moved 25 units in 70 s
    CHECK(std::fabs(RiftTimerFractionFromRects(1505.f, 1827.f, 1507.2f) - 0.0068f) < 1e-3f);
    CHECK(std::fabs(RiftTimerFractionFromRects(1505.f, 1827.f, 1666.f) - 0.5f) < 1e-4f);
    CHECK(RiftTimerFractionFromRects(1505.f, 1827.f, 454060.f) < 0.0f);  // control not laid out yet
    CHECK(RiftTimerFractionFromRects(1505.f, 1827.f, 26869.f) < 0.0f);   // recycled control after the rift
    CHECK(RiftTimerFractionFromRects(1505.f, 1505.f, 1505.f) < 0.0f);
    CHECK(RiftTimerFractionFromRects(1505.f, 1827.f, 1830.f) == 1.0f);   // slightly past the end clamps
}

static void TestSalvageAll() {
    SafeArea safe;
    SalvageRules rules;
    auto gear = [](ItemRarity r, int x, int y, const char* type = "Boots") {
        ItemSnapshot it;
        it.location = kItemLocationBackpack;
        it.rarity = r;
        it.gridX = static_cast<uint8_t>(x);
        it.gridY = static_cast<uint8_t>(y);
        strcpy_s(it.typeName, type);
        strcpy_s(it.name, "Some Item");
        return it;
    };
    ItemSnapshot items[3] = {gear(ItemRarity::Magic, 1, 0), gear(ItemRarity::Magic, 2, 0), gear(ItemRarity::Legendary, 3, 0)};
    CHECK(CanSalvageAllOfQuality(ItemRarity::Magic, items, 3, safe, rules));
    CHECK(!CanSalvageAllOfQuality(ItemRarity::Rare, items, 3, safe, rules));   // none of that quality
    safe.Toggle(2, 0);                                                         // a blue item sits in the safe area
    CHECK(!CanSalvageAllOfQuality(ItemRarity::Magic, items, 3, safe, rules));  // the game button would destroy it
    safe.Toggle(2, 0);
    rules.neverFamilies = 1u << GearFamily("Boots");
    CHECK(!CanSalvageAllOfQuality(ItemRarity::Magic, items, 3, safe, rules));  // protected item type
    rules = SalvageRules();
    rules.rarityMask &= ~(1u << static_cast<int>(ItemRarity::Magic));
    CHECK(!CanSalvageAllOfQuality(ItemRarity::Magic, items, 3, safe, rules));  // quality not allowed
    rules = SalvageRules();
    items[2].rarity = ItemRarity::Unknown;                                     // an unclassified item could be blue
    CHECK(!CanSalvageAllOfQuality(ItemRarity::Magic, items, 3, safe, rules));
    items[2].rarity = ItemRarity::Legendary;
    items[0].location = 3;                                                     // equipped items are not in the bag
    CHECK(CanSalvageAllOfQuality(ItemRarity::Magic, items, 3, safe, rules));
}

static void TestRiftProgress() {
    CHECK(RiftProgressFromRects(1505.f, 1827.f, 1504.67f) == 0.0f);    // marker at the start (slightly left): 0%
    CHECK(RiftProgressFromRects(1505.f, 1827.f, 1827.0f) == 1.0f);
    CHECK(std::fabs(RiftProgressFromRects(1505.f, 1827.f, 1666.0f) - 0.5f) < 1e-4f);
    CHECK(RiftProgressFromRects(1505.f, 1827.f, 1900.0f) == 1.0f);     // clamped
    CHECK(RiftProgressFromRects(1505.f, 1505.f, 1505.0f) < 0.0f);      // unlaid-out bar
}

static void TestItemSearch() {
    ItemSnapshot it;
    it.rarity = ItemRarity::Legendary;
    it.identified = true;
    strcpy_s(it.name, "Convention of Elements");
    strcpy_s(it.typeName, "Ring");
    CHECK(ItemMatchesQuery(it, "convention"));
    CHECK(ItemMatchesQuery(it, "ELEMENTS ring"));       // case-insensitive, all words
    CHECK(!ItemMatchesQuery(it, "convention boots"));    // every word must match
    CHECK(ItemMatchesQuery(it, "legendary"));
    CHECK(!ItemMatchesQuery(it, "ancient"));
    CHECK(!ItemMatchesQuery(it, "") && !ItemMatchesQuery(it, "   "));  // empty matches nothing
    it.ancient = true;
    CHECK(ItemMatchesQuery(it, "ancient ring") && !ItemMatchesQuery(it, "primal"));
    it.primal = true;
    CHECK(ItemMatchesQuery(it, "primal") && ItemMatchesQuery(it, "ancient"));  // primal is ancient too
    it.identified = false;
    CHECK(ItemMatchesQuery(it, "unidentified"));
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
    TestItemSearch();
    TestRiftProgress();
    TestSalvageAll();
    TestRiftTimer();
    TestPaths();
    TestSettingsTable();
    TestHostApi();
    TestMonsterAffixes();
    TestAffixMask();
    TestEffects();
    TestGameClock();
    TestPowerNames();
    TestPluginSettings();
    TestLuaSandbox();
    TestLuaEffects();
    TestTimeTracker();
    TestLuaLibraries();
    TestHealthBars();
    TestDamageMeter();
    TestEffectsPlugin();
    TestRaii();
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
