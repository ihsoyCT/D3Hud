#include "SnoTable.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace d3hud {

bool SnoTable::Load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream row(line);
        std::string sno, name, subtype, mobility, kind;
        if (!std::getline(row, sno, '\t') || !std::getline(row, name, '\t') ||
            !std::getline(row, subtype, '\t') || !std::getline(row, mobility, '\t') ||
            !std::getline(row, kind, '\t'))
            continue;
        table_[static_cast<uint32_t>(strtoul(sno.c_str(), nullptr, 10))] = {name, kind};
    }
    return !table_.empty();
}

const SnoInfo* SnoTable::Find(uint32_t sno) const {
    auto it = table_.find(sno);
    return it == table_.end() ? nullptr : &it->second;
}

bool ItemNameTable::Load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream row(line);
        std::string gbid, sno, type, code, name, setGbid;
        if (!std::getline(row, gbid, '\t') || !std::getline(row, sno, '\t') ||
            !std::getline(row, type, '\t') || !std::getline(row, code, '\t') ||
            !std::getline(row, name, '\t'))
            continue;
        std::string width, height;  // optional columns
        std::getline(row, setGbid, '\t');
        std::getline(row, width, '\t');
        std::getline(row, height, '\t');
        const bool inSet = !setGbid.empty() && strtoul(setGbid.c_str(), nullptr, 10) != 0xFFFFFFFFul;
        const unsigned long h = height.empty() ? 2 : strtoul(height.c_str(), nullptr, 10);
        table_[static_cast<uint32_t>(strtoul(gbid.c_str(), nullptr, 10))] = {
            name, type, inSet, static_cast<uint8_t>(h == 1 ? 1 : 2)};
    }
    return !table_.empty();
}

const ItemNameInfo* ItemNameTable::Find(uint32_t gbid) const {
    auto it = table_.find(gbid);
    return it == table_.end() ? nullptr : &it->second;
}

static bool Contains(const std::string& haystack, const char* needle) {
    std::string h = haystack, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
    return h.find(n) != std::string::npos;
}

bool IsBonusDrop(const std::string& type, const std::string& name) {
    if (type == "GeneralUtility" || type == "DemonicKey" || type == "UpgradeableJewel") return true;
    return type == "CraftingReagent" && name.rfind("Key of ", 0) == 0;  // realm keys (Key of War, ...)
}

bool ClassifyActor(const std::string& snoKind, const std::string& name, bool hasAcd, bool dead,
                   ActorKind* out) {
    // invisible server prop marking where a greater rift pylon may spawn
    if (name == "MarkerLocation_TieredRiftPylon") {
        *out = ActorKind::PylonSpawn;
        return true;
    }
    if (snoKind == "Player") {
        *out = ActorKind::Player;
        return hasAcd;
    }
    if (snoKind == "Monster") {
        // corpses: the game clears a monster's acd link when it dies
        if (dead) return false;
        // allies, invisible helpers, affix effects (frozen pulse etc.), voice-over
        // triggers and portal spawners are monster actors as well
        for (const char* skip : {"Generic_Proxy", "Hireling", "_pet_", "Follower", "MonsterAffix",
                                 "Voiceover", "PortalSpawner"})
            if (Contains(name, skip)) return false;
        *out = (Contains(name, "Boss") || Contains(name, "Unique")) ? ActorKind::Boss
                                                                    : ActorKind::Trash;
        return true;
    }
    if (snoKind == "Gizmo") {
        if (Contains(name, "Shrine")) {
            // rift pylons are named x1_LR_Shrine_* ("*_ChainPylon" gizmos are act 3 props)
            *out = Contains(name, "LR_Shrine") ? ActorKind::Pylon : ActorKind::Shrine;
            return true;
        }
        if (Contains(name, "Chest")) {
            *out = ActorKind::Chest;
            return true;
        }
        if (Contains(name, "Portal")) {
            *out = ActorKind::Portal;
            return true;
        }
        return false;
    }
    if (snoKind == "Item" && Contains(name, "rifts_orb")) {
        // the orbs that fill the rift progress bar: shown even without an ACD (they are not items)
        *out = Contains(name, "tiered") ? ActorKind::RiftOrbTiered : ActorKind::RiftOrb;
        return true;
    }
    if (snoKind == "Item") {
        if (!hasAcd) return false;
        *out = Contains(name, "HealthGlobe") ? ActorKind::Globe : ActorKind::GroundItem;
        return true;
    }
    return false;
}

}  // namespace d3hud
