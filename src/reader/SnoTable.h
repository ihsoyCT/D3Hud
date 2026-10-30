// Actor SNO table (analysis/sno_actors.tsv: sno, name, subtype, mobility, kind)
// and the mapping from SNO kind/name to the minimap's ActorKind.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Snapshot.h"

namespace d3hud {

struct SnoInfo {
    std::string name;
    std::string kind;  // Monster, Gizmo, Item, Player, ServerProp, ClientEffect, ...
};

class SnoTable {
public:
    [[nodiscard]] bool Load(const std::filesystem::path& path);
    const SnoInfo* Find(uint32_t sno) const;
    size_t size() const { return table_.size(); }
    std::unordered_set<uint32_t> Ids() const {
        std::unordered_set<uint32_t> ids;
        for (const auto& kv : table_) ids.insert(kv.first);
        return ids;
    }

private:
    std::unordered_map<uint32_t, SnoInfo> table_;
};

// Item display names (analysis/items_gbid.tsv: gbid, sno, type, code, name,
// set_gbid), keyed by the GBID stored in an item's ACD (+0xB4).
struct ItemNameInfo {
    std::string name;    // English display name, e.g. "Guardian's Gaze"
    std::string type;    // item type, e.g. "Helm", "HandXbow"
    bool inSet = false;  // set_gbid != -1 (e.g. Guardian's Jeopardy, Unhallowed Essence)
    uint8_t height = 2;  // grid cells tall (columns width, height; rings/amulets/belts/gems 1)
};

class ItemNameTable {
public:
    [[nodiscard]] bool Load(const std::filesystem::path& path);
    const ItemNameInfo* Find(uint32_t gbid) const;
    size_t size() const { return table_.size(); }

private:
    std::unordered_map<uint32_t, ItemNameInfo> table_;
};

// Pure (unit-tested): "bonus drops" worth logging besides legendary/set gear -
// Angelic Crucibles, Ramaladni's Gift, Hellforge Embers (GeneralUtility), demonic
// and realm keys, legendary gems (UpgradeableJewel).
bool IsBonusDrop(const std::string& itemType, const std::string& itemName);

// Pure (unit-tested): minimap kind for an actor, or false if it should not be drawn.
// Items need a game-state link (acd) — client-only copies are leftovers.
// `dead` hides corpses (RawActor::dead: the monster's acd link was cleared).
bool ClassifyActor(const std::string& snoKind, const std::string& name, bool hasAcd, bool dead,
                   ActorKind* out);

}  // namespace d3hud
