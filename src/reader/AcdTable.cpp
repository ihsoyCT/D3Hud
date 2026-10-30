#include "AcdTable.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <print>
#include <unordered_map>

namespace d3hud {

bool DecodeAcdItem(const uint8_t* rec, uint32_t index, AcdItemRecord* out) {
    memcpy(&out->acd, rec, 4);
    if (out->acd == 0xFFFFFFFF || (out->acd & 0xFFFF) != index) return false;
    uint32_t unid;
    memcpy(&out->sno, rec + 0x90, 4);
    memcpy(&out->gbid, rec + 0xB4, 4);
    memcpy(&out->attribGroup, rec + 0x130, 4);
    memcpy(&out->location, rec + 0x124, 4);
    memcpy(&out->x, rec + 0x128, 4);
    memcpy(&out->y, rec + 0x12C, 4);
    memcpy(&unid, rec + 0x190, 4);
    out->unidentified = unid == 1;
    return true;
}

ActorKind ApplyMonsterQuality(ActorKind kind, uint32_t quality) {
    if (kind != ActorKind::Trash && kind != ActorKind::Boss) return kind;
    switch (static_cast<MonsterQuality>(quality)) {
        case MonsterQuality::Champion: return ActorKind::Champion;
        case MonsterQuality::Rare:     return ActorKind::Rare;
        case MonsterQuality::Minion:   return ActorKind::Minion;
        case MonsterQuality::Unique:
        case MonsterQuality::Boss:     return ActorKind::Boss;
        default:                       return kind;  // name-based Boss stays Boss
    }
}

AcdBlockScan::AcdBlockScan(const std::vector<RawActor>& actors,
                           const std::unordered_set<uint32_t>* knownSnos)
    : knownSnos_(knownSnos) {
    for (const RawActor& a : actors)
        if (a.hasAcd()) snoById_[a.acd] = a.sno;
}

void AcdBlockScan::Match(const uint8_t* data, size_t n, uint64_t addr, size_t chunkSize) {
    // Two kinds of evidence for a block base:
    //  linked: a record whose (id, sno) matches a live actor's acd link - certain;
    //  loose:  any record with a known SNO and a placement-like +0x124 - needed for
    //          blocks that hold only items (no world actor links into them).
    const size_t limit = MatchLimit(n, chunkSize, tail());
    for (size_t i = 0; i + tail() <= limit && i < chunkSize; i += 8) {
        uint32_t id, sno;
        memcpy(&id, data + i, 4);
        if (id == 0 || id == 0xFFFFFFFF || (id >> 16) == 0) continue;
        memcpy(&sno, data + i + 0x90, 4);
        const uint64_t at = addr + i;
        const uint64_t span = static_cast<uint64_t>(id & 0xFFFF) * kAcdStride;
        if (span > at) continue;
        auto it = snoById_.find(id);
        if (it != snoById_.end()) {
            if (sno == it->second) linked_[at - span]++;
            continue;
        }
        if (knownSnos_ == nullptr || sno == 0 || sno > 0x00FFFFFF || !knownSnos_->count(sno))
            continue;
        int32_t location;
        memcpy(&location, data + i + 0x124, 4);
        if (location >= -1 && location <= 64) loose_[at - span]++;
    }
}

int AcdBlockScan::Finish(AcdTable* table, bool verbose) {
    constexpr int kLooseVotes = 3;
    std::vector<std::pair<int, uint64_t>> ranked;
    for (const auto& [base, votes] : linked_) ranked.push_back({votes + 1000, base});
    for (const auto& [base, votes] : loose_)
        if (votes >= kLooseVotes && !linked_.count(base)) ranked.push_back({votes, base});
    std::sort(ranked.rbegin(), ranked.rend());
    std::vector<uint64_t> bases;
    for (const auto& [votes, base] : ranked) bases.push_back(base);
    if (verbose)
        std::print("acd table: {} block base(s) ({} linked, {} item-only) from {} linked actors\n",
               bases.size(), linked_.size(), bases.size() - linked_.size(), snoById_.size());
    table->SetBases(bases);
    return static_cast<int>(bases.size());
}

int AcdTable::Discover(const Process& proc, const std::vector<RawActor>& actors,
                       const std::unordered_set<uint32_t>* knownSnos, bool verbose) {
    AcdBlockScan scan(actors, knownSnos);
    SharedSweep(proc, {&scan});
    return scan.Finish(this, verbose);
}

bool AcdTable::Quality(const Process& proc, uint32_t acdId, uint32_t* quality) const {
    std::vector<uint64_t> bases;
    {
        std::lock_guard<std::mutex> lock(mu_);
        bases = bases_;
    }
    const uint64_t off = static_cast<uint64_t>(acdId & 0xFFFF) * kAcdStride;
    for (uint64_t base : bases) {
        uint8_t rec[0xBC];
        if (!proc.Read(base + off, rec, sizeof(rec))) continue;
        uint32_t id;
        memcpy(&id, rec, 4);
        if (id != acdId) continue;  // another block's record (or stale): keep looking
        memcpy(quality, rec + 0xB8, 4);
        return true;
    }
    return false;
}

bool AcdTable::ReadItem(const Process& proc, uint32_t acdId, AcdItemRecord* out) const {
    std::vector<uint64_t> bases;
    {
        std::lock_guard<std::mutex> lock(mu_);
        bases = bases_;
    }
    const uint64_t off = static_cast<uint64_t>(acdId & 0xFFFF) * kAcdStride;
    uint8_t rec[kAcdStride];
    for (uint64_t base : bases) {
        if (!proc.Read(base + off, rec, sizeof(rec))) continue;
        if (DecodeAcdItem(rec, acdId & 0xFFFF, out) && out->acd == acdId) return true;
    }
    return false;
}

bool AcdTable::ReadPlaced(const Process& proc, std::vector<AcdItemRecord>* out) const {
    out->clear();
    std::vector<uint64_t> bases;
    {
        std::lock_guard<std::mutex> lock(mu_);
        bases = bases_;
    }
    constexpr uint32_t kSlots = 4096, kBatch = 64;  // id low 16 bits; observed indices < 1000
    std::vector<uint8_t> buf(static_cast<size_t>(kBatch) * kAcdStride);
    std::unordered_map<uint32_t, bool> seen;  // the same id can only be live in one block
    for (uint64_t base : bases) {
        for (uint32_t first = 0; first < kSlots; first += kBatch) {
            if (!proc.Read(base + static_cast<uint64_t>(first) * kAcdStride, buf.data(), buf.size()))
                continue;
            for (uint32_t k = 0; k < kBatch; ++k) {
                AcdItemRecord r;
                if (!DecodeAcdItem(buf.data() + static_cast<size_t>(k) * kAcdStride, first + k, &r))
                    continue;
                if (r.location < 0 || r.location > 64) continue;
                if (!seen.emplace(r.acd, true).second) continue;
                out->push_back(r);
            }
        }
    }
    return !out->empty();
}

size_t AcdTable::baseCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return bases_.size();
}

void AcdTable::SetBases(const std::vector<uint64_t>& bases) {
    std::lock_guard<std::mutex> lock(mu_);
    bases_ = bases;
}

std::vector<uint64_t> AcdTable::GetBases() const {
    std::lock_guard<std::mutex> lock(mu_);
    return bases_;
}

}  // namespace d3hud
