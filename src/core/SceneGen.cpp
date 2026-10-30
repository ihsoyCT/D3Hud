#include "SceneGen.h"

#include <cmath>
#include <cstring>

namespace d3hud {
namespace {

void SetName(char* dst, const char* src) {
    strncpy_s(dst, kNameLen, src, _TRUNCATE);
}

constexpr float kPi = 3.14159265358979f;

// Deterministic hash-derived pseudo-random in [0,1) from an integer.
float Hash01(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return static_cast<float>(x) / 4294967296.0f;
}

}  // namespace

SceneGen::SceneGen(uint32_t seed, double frameIntervalMs) : seed_(seed), intervalMs_(frameIntervalMs) {}

void SceneGen::Generate(WorldSnapshot* out) {
    std::memset(out, 0, sizeof(WorldSnapshot));
    out->magic = 0x44573348;
    out->version = kSnapshotVersion;
    out->frameIndex = frame_;
    out->timestampMs = static_cast<double>(frame_) * intervalMs_;
    out->worldSnoId = 123456;

    const float t = static_cast<float>(out->timestampMs) / 1000.0f;

    // Player walks a circle of radius 40 world units, one lap per 40 s.
    const float pAng = t * (2.0f * kPi / 40.0f);
    out->playerPos = {40.0f * std::cos(pAng), 40.0f * std::sin(pAng)};
    out->playerHeading = pAng + kPi / 2.0f;  // tangent direction

    auto add = [&](ActorKind kind, Vec2 pos, float heading, const char* name, float hp = 1.0f,
                   uint8_t affixes = 0) {
        if (out->actorCount >= kMaxActors) return;
        ActorSnapshot& a = out->actors[out->actorCount++];
        a.id = 0x1000 + out->actorCount;
        a.world = pos;
        a.heading = heading;
        a.currentHp = hp;
        a.maxHp = 1.0f;
        a.kind = kind;
        a.affixCount = affixes;
        SetName(a.name, name);
    };

    // 12 trash mobs wandering on individual Lissajous loops.
    for (int i = 0; i < 12; ++i) {
        const float h1 = Hash01(seed_ + 100u + static_cast<uint32_t>(i) * 7u);
        const float h2 = Hash01(seed_ + 200u + static_cast<uint32_t>(i) * 13u);
        const float cx = -80.f + 160.f * h1;
        const float cy = -80.f + 160.f * h2;
        const float w1 = 0.3f + 0.5f * h1;
        const float w2 = 0.3f + 0.5f * h2;
        const Vec2 pos = {cx + 12.f * std::cos(t * w1 + h1 * 6.0f),
                          cy + 12.f * std::sin(t * w2 + h2 * 6.0f)};
        add(ActorKind::Trash, pos, 0.f, "Fallen One", 0.8f);
    }

    // 2 champion packs of 4 blues each, circling their pack anchor.
    for (int pack = 0; pack < 2; ++pack) {
        const float px = pack == 0 ? -60.f : 70.f;
        const float py = pack == 0 ? 55.f : -65.f;
        const float pa = t * (0.4f + 0.2f * static_cast<float>(pack)) + pack * 2.0f;
        const Vec2 anchor = {px + 8.f * std::cos(pa), py + 8.f * std::sin(pa)};
        for (int m = 0; m < 4; ++m) {
            const float ma = pa + static_cast<float>(m) * (kPi / 2.0f);
            const Vec2 pos = {anchor.x + 5.0f * std::cos(ma), anchor.y + 5.0f * std::sin(ma)};
            add(ActorKind::Champion, pos, ma, "Champion", 1.0f, 4);
        }
    }

    // 1 yellow rare with 3 minions orbiting it.
    const float ra = t * 0.25f;
    const Vec2 rarePos = {20.0f * std::cos(ra + 1.0f), 90.0f + 8.0f * std::sin(ra * 2.0f)};
    add(ActorKind::Rare, rarePos, ra, "Rare", 1.0f, 5);
    for (int m = 0; m < 3; ++m) {
        const float ma = ra * 2.0f + static_cast<float>(m) * (2.0f * kPi / 3.0f);
        const Vec2 pos = {rarePos.x + 6.0f * std::cos(ma), rarePos.y + 6.0f * std::sin(ma)};
        add(ActorKind::Minion, pos, ma, "Minion", 0.9f);
    }

    // A stationary purple boss.
    add(ActorKind::Boss, {-110.f, -100.f}, 0.f, "Boss", 1.0f, 6);

    // 2 pylons + 1 shrine, static.
    add(ActorKind::Pylon, {120.f, 30.f}, 0.f, "Pylon");
    add(ActorKind::Pylon, {-40.f, 120.f}, 0.f, "Pylon");
    add(ActorKind::Shrine, {90.f, 100.f}, 0.f, "Shrine");

    // Inventory items: a mix of identified and unided, ancients and primals —
    // the fake data the salvage panel and unided-reveal render against.
    auto item = [&](const char* name, const char* type, ItemRarity r, bool ided, bool ancient,
                    bool primal, SalvageVerdict v, uint32_t sno) {
        if (out->itemCount >= kMaxItems) return;
        ItemSnapshot& it = out->items[out->itemCount++];
        it.id = 0x2000 + out->itemCount;
        it.snoId = sno;
        it.rarity = r;
        it.identified = ided;
        it.ancient = ancient;
        it.primal = primal;
        it.verdict = v;
        if (ided) SetName(it.name, name);
        SetName(it.typeName, type);
    };
    item("Pox Faux", "Fist Weapon", ItemRarity::Legendary, true, true, false, SalvageVerdict::Keep, 1001);
    item("", "Ring", ItemRarity::Legendary, false, true, false, SalvageVerdict::Keep, 1002);
    item("", "Amulet", ItemRarity::Legendary, false, false, false, SalvageVerdict::Check, 1003);
    item("Fellshard Coy", "Source", ItemRarity::Legendary, true, false, false, SalvageVerdict::Junk, 1004);
    item("", "Mighty Weapon", ItemRarity::Legendary, false, false, true, SalvageVerdict::Keep, 1005);
    item("Grim Wagon", "Cloak", ItemRarity::Rare, true, false, false, SalvageVerdict::Junk, 1006);
    item("", "Ring", ItemRarity::Rare, false, false, false, SalvageVerdict::Junk, 1007);
    item("Sage's Journey", "Boots", ItemRarity::Set, true, false, false, SalvageVerdict::Junk, 1008);

    frame_++;
}

}  // namespace d3hud
