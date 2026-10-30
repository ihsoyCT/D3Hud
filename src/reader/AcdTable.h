// ACD (ActorCommonData) lookup: the game-state record an actor links to via its
// acd id (actor +0x88). Records are 0x3C0 bytes, spread over several memory
// blocks; record for id lives at blockBase + (id & 0xFFFF) * 0x3C0 for one of
// the blocks. Layout (current client, see analysis/ENGINE_MINING.md):
//   +0x00 u32 acd id   +0x90 u32 actor SNO   +0xB8 u32 monster quality
// Quality: 0 normal, 1 champion, 2 rare, 3 minion, 4 unique, 5 hireling, 7 boss.
//
// The block layout is not decoded; instead every candidate block base is tried
// and a record is only accepted if its stored id equals the full acd id.
#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "ActorList.h"
#include "Process.h"
#include "Snapshot.h"

namespace d3hud {

constexpr uint32_t kAcdStride = 0x3C0;

enum class MonsterQuality : uint32_t {
    Normal = 0, Champion = 1, Rare = 2, Minion = 3, Unique = 4, Hireling = 5, Boss = 7,
};

// An ACD record's inventory placement: +0x124 location (0 backpack, 1..13
// equipped, 15 stash, 16+ potion/socket/follower slots), +0x128 column, +0x12C row;
// +0xB4 item GBID (game-balance id: key for display names, 384/384 resolved live);
// +0x190 u32 1 = unidentified (live: 8/8 unided backpack gear, 0 on all equipped,
// gems and consumables; two pointers at +0x180/+0x188 are set only on those items).
struct AcdItemRecord {
    uint32_t acd = 0;
    uint32_t sno = 0;
    uint32_t gbid = 0;
    uint32_t attribGroup = 0;   // +0x130: fast attribute group id (see AttribTable)
    bool unidentified = false;  // +0x190: set on unrevealed *gear* only; the
                                // Unidentified attribute is authoritative
    int32_t location = -1;
    int32_t x = 0, y = 0;
};

// Pure (unit-tested): decode the placement/identity fields of one ACD record
// (kAcdStride bytes). False if the slot is free or the id does not match `index`.
bool DecodeAcdItem(const uint8_t* rec, uint32_t index, AcdItemRecord* out);

// Pure (unit-tested): refine a monster's minimap kind by its ACD quality.
ActorKind ApplyMonsterQuality(ActorKind kind, uint32_t quality);

// Sweepable ACD block discovery (see SharedScan.h).
class AcdTable;

// Sweepable ACD block discovery (see SharedScan.h).
class AcdBlockScan : public ChunkMatcher {
public:
    AcdBlockScan(const std::vector<RawActor>& actors, const std::unordered_set<uint32_t>* knownSnos);
    size_t tail() const override { return 0x128; }
    bool enabled() const override { return !snoById_.empty() || knownSnos_ != nullptr; }
    void Match(const uint8_t* data, size_t n, uint64_t addr, size_t chunkSize) override;
    // Ranks the implied block bases and installs them in `table`.
    int Finish(AcdTable* table, bool verbose = false);

private:
    std::unordered_map<uint32_t, uint32_t> snoById_;  // acd id -> actor SNO
    const std::unordered_set<uint32_t>* knownSnos_;
    std::unordered_map<uint64_t, int> linked_, loose_;
};

class AcdTable {
public:
    // One pass over private memory: finds ACD records of the given linked actors
    // (id at +0, matching SNO at +0x90) and keeps every implied block base.
    // Thread-safe with respect to Quality().
    // `knownSnos` (optional) also admits blocks holding only items: records with a
    // known SNO and a placement-like +0x124, at least 3 per block.
    int Discover(const Process& proc, const std::vector<RawActor>& actors,
                 const std::unordered_set<uint32_t>* knownSnos = nullptr, bool verbose = false);

    // Monster quality for an acd id; false if no known block holds the record.
    bool Quality(const Process& proc, uint32_t acdId, uint32_t* quality) const;

    // The ACD record of one item (e.g. a ground item, via its actor's acd link);
    // false if no known block holds it.
    bool ReadItem(const Process& proc, uint32_t acdId, AcdItemRecord* out) const;

    // Every live ACD record in the known blocks that has a placement (location
    // >= 0); the caller filters by SNO kind. Reads ~4 MB per block - call ~1 Hz.
    bool ReadPlaced(const Process& proc, std::vector<AcdItemRecord>* out) const;

    size_t baseCount() const;
    // Install externally discovered bases (cache load); sorted order is the caller's.
    void SetBases(const std::vector<uint64_t>& bases);
    std::vector<uint64_t> GetBases() const;

private:
    mutable std::mutex mu_;
    std::vector<uint64_t> bases_;  // most-voted first
};

}  // namespace d3hud
