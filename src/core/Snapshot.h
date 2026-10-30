// D3Hud.Core — fixed-layout, trivially-copyable world snapshots.
// The same struct flows through every source (synthetic / replay / live reader),
// over the shared-memory ring in production, and into the recorder/replay files.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace d3hud {

constexpr uint32_t kSnapshotVersion = 5;  // 2: kMaxItems 256, item height, UiLayout; 3: big map; 4: scenes; 5: salvage UI
constexpr int kMaxScenes = 512;  // scenes of the current world (a rift has ~100-250)
constexpr int kMaxActors = 256;
constexpr int kMaxItems = 256;  // inventory + equipped + the visible stash tab (70)
constexpr int kNameLen = 48;

enum class ActorKind : uint8_t {
    Player = 0,
    Trash,
    Champion,   // blue pack member
    Rare,       // yellow rare
    Minion,     // rare/champion pack minion
    Boss,       // purple unique/boss
    Pylon,
    Shrine,
    Chest,
    Globe,
    GroundItem,  // appended: keeps existing values (and replay files) stable
    Portal,
    PylonSpawn,  // greater rift: possible pylon location (MarkerLocation_TieredRiftPylon)
};

inline const char* ActorKindName(ActorKind k) {
    switch (k) {
        case ActorKind::Player:     return "Player";
        case ActorKind::Trash:      return "Trash";
        case ActorKind::Champion:   return "Champion";
        case ActorKind::Rare:       return "Rare";
        case ActorKind::Minion:     return "Minion";
        case ActorKind::Boss:       return "Boss";
        case ActorKind::Pylon:      return "Pylon";
        case ActorKind::Shrine:     return "Shrine";
        case ActorKind::Chest:      return "Chest";
        case ActorKind::Globe:      return "Globe";
        case ActorKind::GroundItem: return "Item";
        case ActorKind::Portal:     return "Portal";
        case ActorKind::PylonSpawn: return "PylonSpawn";
    }
    return "?";
}

// Unknown: properties not read yet (attribute group not located) - appended so
// existing values (and replay files) keep their meaning.
enum class ItemRarity : uint8_t { Common = 0, Magic, Rare, Legendary, Set, Unknown };

inline const char* RarityName(ItemRarity r) {
    switch (r) {
        case ItemRarity::Common:    return "Common";
        case ItemRarity::Magic:     return "Magic";
        case ItemRarity::Rare:      return "Rare";
        case ItemRarity::Legendary: return "Legendary";
        case ItemRarity::Set:       return "Set";
        case ItemRarity::Unknown:   return "?";
    }
    return "?";
}

enum class SalvageVerdict : uint8_t { Unknown = 0, Junk, Keep, Check };

struct Vec2 {
    float x = 0.f;
    float y = 0.f;
};

struct ActorSnapshot {
    uint64_t id = 0;
    Vec2 world;
    float heading = 0.f;  // radians, 0 = east, CCW
    float currentHp = 1.f;
    float maxHp = 1.f;
    ActorKind kind = ActorKind::Trash;
    uint8_t affixCount = 0;
    char name[kNameLen] = {};  // ground items: the item's display name
    // In the former tail padding (size stays 80) - ground items only:
    ItemRarity itemRarity = ItemRarity::Unknown;
    uint8_t itemFlags = 0;     // kItemFlag* bits; pylons/shrines: kActorFlagUsed
};

constexpr uint8_t kItemFlagAncient = 1, kItemFlagPrimal = 2, kItemFlagBonus = 4, kItemFlagUnidentified = 8;
constexpr uint8_t kActorFlagUsed = 16;  // pylon/shrine already clicked (gizmo attributes)

// Player-facing name of a pylon/shrine from its SNO name (rift pylons are
// x1_LR_Shrine_*); nullptr if the actor is neither.
inline const char* ShrineLabel(const char* sno) {
    struct Entry { const char* key; const char* label; };
    static const Entry kTable[] = {
        {"LR_Shrine_Damage", "Power Pylon"},          {"LR_Shrine_Electrified", "Conduit Pylon"},
        {"LR_Shrine_Infinite_Casting", "Channeling Pylon"}, {"LR_Shrine_Invulnerable", "Shield Pylon"},
        {"LR_Shrine_Run_Speed", "Speed Pylon"},       {"TreasureGoblin", "Bandit Shrine"},
        {"Blessed", "Blessed Shrine"},                {"Enlightened", "Enlightened Shrine"},
        {"Fortune", "Fortune Shrine"},                {"Frenzied", "Frenzied Shrine"},
        {"Hoarder", "Fleeting Shrine"},               {"Reloaded", "Empowered Shrine"},
    };
    for (const Entry& e : kTable)
        if (strstr(sno, e.key) != nullptr) return e.label;
    if (strstr(sno, "LR_Shrine") != nullptr) return "Pylon";
    if (strstr(sno, "Shrine") != nullptr) return "Shrine";
    return nullptr;
}

// D3 inventory slot ids (ACD +0x124): 0 backpack, 1..13 equipped, 15 stash.
constexpr uint8_t kItemLocationBackpack = 0;
constexpr uint8_t kItemLocationStash = 15;
constexpr uint8_t kItemLocationGround = 254;  // ours: dropped item lying in the world
constexpr uint8_t kItemLocationUnknown = 255;

inline const char* ItemLocationName(uint8_t loc) {
    // 3/4: live Demon Hunter had the quiver in 3 and the bow in 4
    static const char* kEquip[] = {"Backpack", "Head",    "Torso",     "Off hand",  "Main hand",
                                   "Hands",    "Waist",   "Feet",      "Shoulders", "Legs",
                                   "Bracers",  "Ring L",  "Ring R",    "Neck"};
    if (loc <= 13) return kEquip[loc];
    if (loc == kItemLocationStash) return "Stash";
    if (loc == kItemLocationGround) return "Ground";
    if (loc == kItemLocationUnknown) return "-";
    return "Other";
}

struct ItemSnapshot {
    uint64_t id = 0;
    uint32_t snoId = 0;
    ItemRarity rarity = ItemRarity::Common;
    bool identified = true;
    bool ancient = false;
    bool primal = false;
    SalvageVerdict verdict = SalvageVerdict::Unknown;
    char name[kNameLen] = {};       // display name; live data fills it for unided items too (the reveal)
    char typeName[kNameLen] = {};   // e.g. "Ring", "Fist Weapon" — the unided reveal
    // In the former tail padding (size stays 120; old replays read 0 here):
    uint8_t location = kItemLocationUnknown;  // D3 inventory slot, see ItemLocationName
    uint8_t gridX = 0;                        // backpack/stash column
    uint8_t gridY = 0;                        // backpack/stash row (stash: tab * 10 + row)
    uint8_t height = 1;                       // grid cells tall (rings/amulets/belts/gems 1, else 2)
    uint8_t augmentRank = 0;                  // Caldesann's Despair gem rank (0 = not augmented)
    uint8_t bonus = 0;                        // 1 = bonus drop (crucible, key, Ramaladni's, legendary gem, ...)
};

// Game UI geometry, in D3's virtual UI units (1200 tall, a centred 4:3 area of
// 1600 wide); UiToScreen converts to pixels of the game window's client area.
struct UiRect {
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
};

constexpr int kEquipSlots = 14;  // index = item location 1..13 (0 unused)
constexpr int kBackpackCols = 10, kBackpackRows = 6;
constexpr int kStashCols = 7, kStashRowsPerTab = 10;

struct UiLayout {
    uint8_t inventoryOpen = 0;
    uint8_t stashOpen = 0;
    int8_t stashTab = -1;  // selected global stash tab (page * 5 + tab), -1 unknown
    uint8_t bigMapOpen = 0;  // TAB map (map_dialog_mainPage visible)
    UiRect backpack;       // the 10 x 6 backpack grid
    UiRect stash;          // the 7 x 10 grid of the selected stash tab
    UiRect slots[kEquipSlots];
    Vec2 bigMapPan;        // big map pan offset in world units (localmap +3216/+3220)
    uint8_t salvageOpen = 0;  // blacksmith's salvage tab shown
    uint8_t confirmOpen = 0;  // "are you sure?" prompt (salvaging a legendary)
    UiRect salvageButton;     // the anvil: arms the salvage cursor
    UiRect confirmOk;         // the prompt's OK button
};

// One scene (prebuilt level tile) of the current world: its SNO (shape, see
// SceneCells) and world rectangle. The whole level is loaded up front, so this
// includes unexplored parts - the map prediction.
struct SceneSnapshot {
    uint32_t sno = 0;
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
};

inline UiRect UiToScreen(UiRect r, float screenW, float screenH) {
    const float ratio = screenH / 1200.0f;
    const float offX = (screenW - screenH / 3.0f * 4.0f) * 600.0f / screenH;  // 4:3 area centred
    return {(r.x0 + offX) * ratio, r.y0 * ratio, (r.x1 + offX) * ratio, r.y1 * ratio};
}

// Where an item's icon is on screen right now (UI units); false if its panel is
// closed (or, for stash items, another tab is selected).
inline bool ItemScreenRect(const UiLayout& ui, uint8_t location, uint8_t gridX, uint8_t gridY,
                           uint8_t height, UiRect* out) {
    auto cell = [out](const UiRect& grid, int cols, int rows, int x, int y, int h) {
        const float cw = (grid.x1 - grid.x0) / cols, ch = (grid.y1 - grid.y0) / rows;
        *out = {grid.x0 + x * cw, grid.y0 + y * ch, grid.x0 + (x + 1) * cw, grid.y0 + (y + h) * ch};
        return true;
    };
    if (location == kItemLocationBackpack)
        return ui.inventoryOpen && cell(ui.backpack, kBackpackCols, kBackpackRows, gridX, gridY, height);
    if (location >= 1 && location < kEquipSlots) {
        if (!ui.inventoryOpen) return false;
        *out = ui.slots[location];
        return true;
    }
    if (location == kItemLocationStash) {
        if (!ui.stashOpen || ui.stashTab < 0 || gridY / kStashRowsPerTab != ui.stashTab) return false;
        return cell(ui.stash, kStashCols, kStashRowsPerTab, gridX, gridY % kStashRowsPerTab, height);
    }
    return false;
}

struct WorldSnapshot {
    uint32_t magic = 0x44573348;  // 'DWH3'
    uint32_t version = kSnapshotVersion;
    uint64_t frameIndex = 0;
    double timestampMs = 0.0;  // live reader: QueryPerformanceCounter ms; synthetic: frame time
    Vec2 playerPos;
    float playerHeading = 0.f;
    uint32_t worldSnoId = 0;
    uint16_t actorCount = 0;
    uint16_t itemCount = 0;
    // In the former header padding (header stays 48 bytes): index + 1 of the item
    // whose game tooltip is open, 0 = none (old replays read 0 here).
    uint16_t hoveredItemPlus1 = 0;
    ActorSnapshot actors[kMaxActors];
    ItemSnapshot items[kMaxItems];
    UiLayout ui;
    uint32_t sceneCount = 0;
    SceneSnapshot scenes[kMaxScenes];
};

static_assert(std::is_trivially_copyable_v<WorldSnapshot>, "snapshots must be memcpy-able");
// Frozen ABI for the replay format / shared-memory ring (MSVC x64), version 2:
// Actor: 8+8+4+4+4+1+1+48+2 = 80 (align 8). Item: 8+4+1*5+48+48+6 = 119 -> 120.
// UiLayout: 4 + 16 * (2 + kEquipSlots) + 8 = 268.
static_assert(sizeof(ActorSnapshot) == 80, "ActorSnapshot layout changed");
static_assert(sizeof(ItemSnapshot) == 120, "ItemSnapshot layout changed");
static_assert(sizeof(UiLayout) == 304, "UiLayout layout changed");
static_assert(offsetof(WorldSnapshot, ui) == 48 + kMaxActors * 80 + kMaxItems * 120,
              "WorldSnapshot layout changed");

// Map orientation: D3's isometric camera shows the world axes diagonally, so
// live data needs a rotation (and possibly a mirror) to match the game's view.
// Measured in-game: world (-1,-1) is screen up (player directly below a pylon).
// Candidates = (mirror x first?, then rotate CCW by degrees):
//   0 raw | 1 mirror +135 (screen right = world (+1,-1)) | 2 rotate -135 (right = (-1,+1))
//   3 mirror -45 | 4 mirror +45   (earlier guesses, kept for comparison)
struct MapOrientation {
    bool mirror;
    float degrees;
    const char* name;
};
constexpr MapOrientation kMapOrientationTable[] = {
    {false, 0.f, "raw"},          {true, 135.f, "mirror +135"}, {false, -135.f, "rotate -135"},
    {true, -45.f, "mirror -45"},  {true, 45.f, "mirror +45"},
};
constexpr int kMapOrientations = sizeof(kMapOrientationTable) / sizeof(kMapOrientationTable[0]);

inline Vec2 OrientForMap(Vec2 d, int orientation) {
    if (orientation <= 0 || orientation >= kMapOrientations) return d;
    const MapOrientation& o = kMapOrientationTable[orientation];
    if (o.mirror) d.x = -d.x;
    const float r = o.degrees * 0.017453292f;
    const float c = std::cos(r), s = std::sin(r);
    return {d.x * c - d.y * s, d.x * s + d.y * c};
}

inline const char* MapOrientationName(int orientation) {
    return (orientation >= 0 && orientation < kMapOrientations)
               ? kMapOrientationTable[orientation].name
               : "raw";
}

// World -> game-window pixels. D3's camera has a fixed angle and zoom relative to
// the player, so a constant perspective projection works (TurboHUD 2.6.10's
// IWindow.WorldToScreenCoordinate). `dz` = height above the player's height.
// Consistent with the in-game minimap check: world (-1,-1) is screen up and
// world (-1,+1) screen right. False when the point is behind the camera or far
// off screen.
inline bool WorldToScreen(Vec2 world, float dz, Vec2 player, float screenW, float screenH, Vec2* out) {
    const float dx = world.x - player.x, dy = world.y - player.y;
    float w = -0.515f * dx - 0.514f * dy - 0.686f * dz + 97.985f;
    if (w < 1.f) return false;
    const float aspect = screenW / screenH * 0.75f;
    const float sx = (-1.682f * dx + 1.683f * dy + 0.007045f) / w / aspect;
    const float sy = (-1.54f * dx - 1.539f * dy + 2.307f * dz + 6.161f) / w;
    if (std::fabs(sx) > 1.5f || std::fabs(sy) > 1.5f) return false;
    out->x = (sx + 1.f) / 2.f * screenW;
    out->y = (1.f - sy) / 2.f * screenH;
    return true;
}

// World -> minimap pixel mapping. Player-centered, map up = world +y after
// OrientForMap. Returns false when the point falls outside the map circle;
// `edge` (optional) receives the clamped rim position for off-screen indicators.
inline bool WorldToMinimap(Vec2 world, Vec2 player, float worldUnitsPerPx, Vec2 mapCenterPx,
                           float mapRadiusPx, Vec2* outPx, Vec2* edge = nullptr,
                           int orientation = 0) {
    const Vec2 d = OrientForMap({(world.x - player.x) / worldUnitsPerPx,
                                 (world.y - player.y) / worldUnitsPerPx},
                                orientation);
    const float dx = d.x;
    const float dy = d.y;
    const float d2 = dx * dx + dy * dy;
    const float r2 = mapRadiusPx * mapRadiusPx;
    if (d2 > r2) {
        if (edge != nullptr) {
            const float inv = (d2 > 0.f) ? (mapRadiusPx * 0.94f / std::sqrt(d2)) : 0.f;
            edge->x = mapCenterPx.x + dx * inv;
            edge->y = mapCenterPx.y - dy * inv;
        }
        return false;
    }
    outPx->x = mapCenterPx.x + dx;
    outPx->y = mapCenterPx.y - dy;  // flip: screen y grows downward
    return true;
}

// Big (TAB) map: centred on the game window, 1 world unit = H/1200 px, same
// orientation as the minimap, shifted by the map's pan offset (TurboHUD 2.6.10
// GetMinimapCoordinates, map mode 1).
inline Vec2 WorldToBigMap(Vec2 world, Vec2 player, Vec2 pan, float screenW, float screenH, int orientation) {
    const float s = screenH / 1200.0f;
    const Vec2 d = OrientForMap({(world.x - player.x - pan.x) * s, (world.y - player.y - pan.y) * s}, orientation);
    return {screenW * 0.5f + d.x, screenH * 0.5f - d.y};
}

}  // namespace d3hud
