#include "ActorList.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <print>
#include <unordered_map>

namespace d3hud {

static uint32_t U32(const uint8_t* p, uint32_t off) {
    uint32_t v;
    memcpy(&v, p + off, 4);
    return v;
}

static float F32(const uint8_t* p, uint32_t off) {
    float v;
    memcpy(&v, p + off, 4);
    return v;
}

static bool IsLive(const uint8_t* rec, uint32_t slot) {
    const uint32_t id = U32(rec, 0x00);
    return id != 0xFFFFFFFF && (id & 0xFFFF) == slot &&
           U32(rec, 0x08) == 0 && U32(rec, 0x0C) == 0;  // empty name field
}

// Gizmos, players and items carry -1 at +0x90; monsters carry a value there,
// so it only strengthens the scan signature, never the per-record decode.
static bool IsScanAnchor(const uint8_t* rec, uint32_t slot) {
    return IsLive(rec, slot) && U32(rec, 0x90) == 0xFFFFFFFF;
}

bool DecodeActor(const uint8_t* rec, uint32_t slot, RawActor* out) {
    if (!IsLive(rec, slot)) return false;
    out->slot = slot;
    out->id = U32(rec, 0x00);
    out->acd = U32(rec, 0x88);
    out->sno = U32(rec, 0x8C);
    out->x = F32(rec, 0xB0);
    out->z = F32(rec, 0xB4);
    out->y = F32(rec, 0xB8);
    out->heading = 2.f * std::atan2(F32(rec, 0xA8), F32(rec, 0xAC));
    out->state = U32(rec, 0x250);
    out->tag90 = U32(rec, 0x90);
    out->dead = !out->hasAcd();  // only consulted for monsters
    return std::isfinite(out->x) && std::isfinite(out->z);
}

bool MatchesArraySignature(const uint8_t* rec, uint32_t* slot) {
    const uint32_t id = U32(rec, 0x00);
    if (id == 0xFFFFFFFF) return false;
    const uint32_t s = id & 0xFFFF;
    if (!IsScanAnchor(rec, s) || !IsScanAnchor(rec + kActorStride, s + 1)) return false;
    *slot = s;
    return true;
}

void ActorBaseScan::Match(const uint8_t* data, size_t n, uint64_t addr, size_t chunkSize) {
    const size_t limit = MatchLimit(n, chunkSize, tail());
    for (size_t i = 0; i + tail() <= limit && i < chunkSize; i += 8) {
        uint32_t slot;
        if (!MatchesArraySignature(data + i, &slot)) continue;
        const uint64_t at = addr + i;
        const uint64_t span = static_cast<uint64_t>(slot) * kActorStride;
        if (span <= at) tally_[at - span]++;
    }
}

bool ActorBaseScan::Finish(uint64_t* base, int* votes, bool verbose) {
    auto best = std::max_element(tally_.begin(), tally_.end(),
                                 [](const auto& a, const auto& b) { return a.second < b.second; });
    if (best == tally_.end()) return false;
    if (verbose)
        std::print("actor array: {} candidate base(s), best 0x{:X} with {} votes\n", tally_.size(),
               static_cast<unsigned long long>(best->first), best->second);
    *base = best->first;
    *votes = best->second;
    return true;
}

bool ValidateActorBase(const Process& proc, uint64_t base) {
    // The array shape must still hold: a record for slot k with slot k+1
    // right behind it, somewhere in the first records.
    std::vector<uint8_t> rec(2 * kActorStride);
    for (uint32_t k = 0; k < 16; ++k) {
        if (!proc.Read(base + static_cast<uint64_t>(k) * kActorStride, rec.data(), rec.size()))
            return false;
        uint32_t slot = 0;
        if (MatchesArraySignature(rec.data(), &slot) && slot == k) return true;
    }
    return false;
}

bool ActorList::FindBase(const Process& proc, uint64_t* base, int* votes, bool verbose) {
    ActorBaseScan scan;
    SharedSweep(proc, {&scan});
    return scan.Finish(base, votes, verbose);
}

bool ActorList::Read(const Process& proc, std::vector<RawActor>* out) const {
    out->clear();
    if (base_ == 0) return false;
    constexpr uint32_t kBatch = 64, kMaxSlots = 8192, kMaxMisses = 256;
    std::vector<uint8_t> buf(static_cast<size_t>(kBatch) * kActorStride);
    uint32_t misses = 0;
    for (uint32_t first = 0; first < kMaxSlots && misses < kMaxMisses; first += kBatch) {
        if (!proc.Read(base_ + static_cast<uint64_t>(first) * kActorStride, buf.data(), buf.size()))
            break;
        for (uint32_t k = 0; k < kBatch && misses < kMaxMisses; ++k) {
            RawActor a;
            if (DecodeActor(buf.data() + static_cast<size_t>(k) * kActorStride, first + k, &a)) {
                out->push_back(a);
                misses = 0;
            } else {
                misses++;
            }
        }
    }
    return !out->empty();
}

}  // namespace d3hud
