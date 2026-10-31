// Item attributes (quality, ancient rank, unidentified, ...): each ACD names a
// "fast attribute group" (ACD +0x130, a container id). A group is 0x12E8 bytes:
//   +0x00 u32 group id   +0x04 flags   +0x18 inline map: u32 bucket mask (map+0),
//   u64 buckets[mask+1] at map+0x1C = group+0x34 (live: 32/32 entries where the hash says)
// Buckets head chains of 16-byte entries [u64 next][u32 key][u32 value];
// key = attribute index | (modifier << 12), 0xFFFFF000 | index for no modifier;
// bucket = (key ^ (key >> 12)) & mask (layout per TurboHUD 2.6.10, verified live).
//
// Attribute indices in the current client are the 2.6.10 ones + 5 from ~380 up
// (analysis/attributes.tsv is the 2.6.10 list). Groups live in several blocks;
// as with AcdTable every candidate block base is tried and only an exact id match
// is accepted.
#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Effects.h"
#include "Process.h"
#include "SharedScan.h"
#include "Snapshot.h"

namespace d3hud {

constexpr uint32_t kAttribGroupStride = 0x12E8;
constexpr uint32_t kAttribMapOffset = 0x18;
constexpr uint32_t kAttribBucketsOffset = 0x34;  // group + 0x18 (map) + 0x1C (buckets)
constexpr uint32_t kAttrItemQualityLevel = 383;  // 0..2 normal, 3..5 magic, 6..8 rare, 9 legendary
constexpr uint32_t kAttrAncientRank = 413;       // 1 ancient, 2 primal
constexpr uint32_t kAttrUnidentified = 414;      // 1 = unidentified (gear and consumables)
constexpr uint32_t kAttrAugmentGemRank = 432;    // CubeEnchantedGemRank: Caldesann's Despair augment
constexpr uint32_t kAttrHitpointsCur = 115;       // float bits; monsters keep their current life here
constexpr uint32_t kAttrHitpointsMaxTotal = 124;  // float bits
constexpr uint32_t kAttrGizmoState = 464;        // shrines/pylons: nonzero once clicked
constexpr uint32_t kAttrGizmoOperated = 475;     // Gizmo_Has_Been_Operated
                                                 // (main stat bonus = rank * 5)

// Pure (unit-tested) helpers.
inline uint32_t AttribKey(uint32_t index) { return 0xFFFFF000u | index; }
inline uint32_t AttribBucket(uint32_t key, uint32_t mask) { return (key ^ (key >> 12)) & mask; }
ItemRarity RarityFromQuality(uint32_t qualityLevel, bool inSet);

// Sweepable attribute-block discovery (see SharedScan.h). Disabled (and Finish
// leaves the table untouched) when no group ids are given.
class AttribTable;

// Sweepable attribute-block discovery (see SharedScan.h). Disabled (and Finish
// leaves the table untouched) when no group ids are given.
class AttribBlockScan : public ChunkMatcher {
public:
    explicit AttribBlockScan(const std::vector<uint32_t>& groupIds);
    size_t tail() const override { return 0x20; }
    bool enabled() const override { return !wanted_.empty(); }
    void Match(const uint8_t* data, size_t n, uint64_t addr, size_t chunkSize) override;
    int Finish(AttribTable* table, bool verbose = false);

private:
    std::unordered_set<uint32_t> wanted_;
    std::unordered_map<uint64_t, int> tally_;
};

class AttribTable {
public:
    // One pass over private memory for the given group ids; keeps every implied
    // block base. Thread-safe with respect to Get().
    int Discover(const Process& proc, const std::vector<uint32_t>& groupIds, bool verbose = false);

    // Value of attribute `index` (no modifier) in group `groupId`. Returns
    // NotFound when the group was located but has no such attribute, Unresolved
    // when no known block holds the group.
    enum class Result { Found, NotFound, Unresolved };
    Result Get(const Process& proc, uint32_t groupId, uint32_t index, uint32_t* value) const;

    // Every entry of a group (research: buffs and cooldowns carry the power SNO as modifier).
    // Same Found/Unresolved meaning as Get; NotFound when the group header is unreadable.
    Result Enumerate(const Process& proc, uint32_t groupId, std::vector<AttribEntry>* out) const;

    size_t baseCount() const;
    void SetBases(const std::vector<uint64_t>& bases);
    std::vector<uint64_t> GetBases() const;

private:
    mutable std::mutex mu_;
    std::vector<uint64_t> bases_;
};

}  // namespace d3hud
