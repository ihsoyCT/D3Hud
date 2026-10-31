#include "AttribTable.h"

#include <algorithm>
#include <cstring>
#include <print>
#include <unordered_map>
#include <unordered_set>

namespace d3hud {

ItemRarity RarityFromQuality(uint32_t q, bool inSet) {
    if (q >= 9) return inSet ? ItemRarity::Set : ItemRarity::Legendary;
    if (q >= 6) return ItemRarity::Rare;
    if (q >= 3) return ItemRarity::Magic;
    return ItemRarity::Common;
}

static bool IsMask(uint32_t m) { return m >= 3 && m <= 4095 && (m & (m + 1)) == 0; }

AttribBlockScan::AttribBlockScan(const std::vector<uint32_t>& groupIds) {
    wanted_.insert(groupIds.begin(), groupIds.end());
    wanted_.erase(0);
    wanted_.erase(0xFFFFFFFF);
}

void AttribBlockScan::Match(const uint8_t* data, size_t n, uint64_t addr, size_t chunkSize) {
    const size_t limit = MatchLimit(n, chunkSize, tail());
    for (size_t i = 0; i + tail() <= limit && i < chunkSize; i += 8) {
        uint32_t id, mask;
        memcpy(&id, data + i, 4);
        if (!wanted_.count(id)) continue;
        memcpy(&mask, data + i + 0x18, 4);
        if (!IsMask(mask)) continue;
        const uint64_t at = addr + i;
        const uint64_t span = static_cast<uint64_t>(id & 0xFFFF) * kAttribGroupStride;
        if (span <= at) tally_[at - span]++;
    }
}

int AttribBlockScan::Finish(AttribTable* table, bool verbose) {
    if (!enabled()) return 0;  // no group ids: leave the table as-is
    std::vector<std::pair<int, uint64_t>> ranked;
    for (const auto& [base, votes] : tally_) ranked.push_back({votes, base});
    std::sort(ranked.rbegin(), ranked.rend());
    std::vector<uint64_t> bases;
    for (const auto& [votes, base] : ranked) bases.push_back(base);
    if (verbose)
        std::print("attribute groups: {} block base(s) from {} item group id(s)\n", bases.size(),
                   wanted_.size());
    table->SetBases(bases);
    return static_cast<int>(bases.size());
}

int AttribTable::Discover(const Process& proc, const std::vector<uint32_t>& groupIds, bool verbose) {
    AttribBlockScan scan(groupIds);
    SharedSweep(proc, {&scan});
    return scan.Finish(this, verbose);
}

AttribTable::Result AttribTable::Get(const Process& proc, uint32_t groupId, uint32_t index,
                                     uint32_t* value) const {
    std::vector<uint64_t> bases;
    {
        std::lock_guard<std::mutex> lock(mu_);
        bases = bases_;
    }
    const uint32_t key = AttribKey(index);
    const uint64_t off = static_cast<uint64_t>(groupId & 0xFFFF) * kAttribGroupStride;
    for (uint64_t base : bases) {
        uint8_t hdr[0x1C];
        if (!proc.Read(base + off, hdr, sizeof(hdr))) continue;
        uint32_t id, mask;
        memcpy(&id, hdr, 4);
        memcpy(&mask, hdr + 0x18, 4);
        if (id != groupId || !IsMask(mask)) continue;  // another block's group (or stale)
        uint64_t entry = 0;
        if (!proc.Read(base + off + kAttribBucketsOffset + 8ull * AttribBucket(key, mask), &entry, 8))
            return Result::NotFound;
        for (int hops = 0; entry != 0 && hops < 64; ++hops) {
            uint8_t e[16];
            if (!proc.Read(entry, e, sizeof(e))) break;
            uint32_t k;
            memcpy(&k, e + 8, 4);
            if (k == key) {
                memcpy(value, e + 12, 4);
                return Result::Found;
            }
            memcpy(&entry, e, 8);
        }
        return Result::NotFound;
    }
    return Result::Unresolved;
}

// Appends every entry of the hash map at `map`: u32 bucket mask at +0, u64 pointer to the buckets at +0x10.
static bool WalkMap(const Process& proc, uint64_t map, std::vector<AttribEntry>* out) {
    uint32_t mask;
    uint64_t bucketsAt;
    if (!proc.Read(map, &mask, 4) || !IsMask(mask) || !proc.Read(map + 0x10, &bucketsAt, 8) || bucketsAt == 0)
        return false;
    std::vector<uint64_t> buckets(static_cast<size_t>(mask) + 1);
    if (!proc.Read(bucketsAt, buckets.data(), buckets.size() * 8)) return false;
    for (uint64_t entry : buckets) {
        for (int hops = 0; entry != 0 && hops < 64; ++hops) {
            uint8_t e[16];
            if (!proc.Read(entry, e, sizeof(e))) break;
            uint32_t k, v;
            memcpy(&k, e + 8, 4);
            memcpy(&v, e + 12, 4);
            out->push_back({k & 0xFFF, k >> 12, v});
            memcpy(&entry, e, 8);
        }
    }
    return true;
}

AttribTable::Result AttribTable::Enumerate(const Process& proc, uint32_t groupId,
                                           std::vector<AttribEntry>* out) const {
    out->clear();
    std::vector<uint64_t> bases;
    {
        std::lock_guard<std::mutex> lock(mu_);
        bases = bases_;
    }
    const uint64_t off = static_cast<uint64_t>(groupId & 0xFFFF) * kAttribGroupStride;
    for (uint64_t base : bases) {
        uint8_t hdr[0x1C];
        if (!proc.Read(base + off, hdr, sizeof(hdr))) continue;
        uint32_t id, mask;
        memcpy(&id, hdr, 4);
        memcpy(&mask, hdr + 0x18, 4);
        if (id != groupId || !IsMask(mask)) continue;
        if (!WalkMap(proc, base + off + kAttribMapOffset, out)) return Result::NotFound;
        // The hero's attributes (hundreds) are not in the inline map but in a second map the group points to
        // at +0x10 (mask 0x1FF, ~1000 entries live); it has the same layout as the inline one.
        uint64_t external = 0;
        if (proc.Read(base + off + 0x10, &external, 8) && external != 0) (void)WalkMap(proc, external, out);  // optional
        return Result::Found;
    }
    return Result::Unresolved;
}

size_t AttribTable::baseCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return bases_.size();
}

void AttribTable::SetBases(const std::vector<uint64_t>& bases) {
    std::lock_guard<std::mutex> lock(mu_);
    bases_ = bases;
}

std::vector<uint64_t> AttribTable::GetBases() const {
    std::lock_guard<std::mutex> lock(mu_);
    return bases_;
}

}  // namespace d3hud
