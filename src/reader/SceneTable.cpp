#include "SceneTable.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <print>
#include <unordered_map>
#include <unordered_set>

namespace d3hud {

static constexpr uint32_t kSnoOffset = 236, kRectMinOffset = 256, kRectMaxOffset = 376, kWorldOffset = 12;
static constexpr uint32_t kMaxSlots = 2048;

bool DecodeScene(const uint8_t* rec, uint32_t slot, const SceneCells& known, SceneRecord* out) {
    memcpy(&out->id, rec, 4);
    if (out->id == 0xFFFFFFFF || (out->id & 0xFFFF) != slot) return false;
    memcpy(&out->world, rec + kWorldOffset, 4);
    memcpy(&out->sno, rec + kSnoOffset, 4);
    memcpy(&out->x0, rec + kRectMinOffset, 8);
    memcpy(&out->x1, rec + kRectMaxOffset, 8);
    const float w = out->x1 - out->x0, h = out->y1 - out->y0;
    if (!std::isfinite(w) || !std::isfinite(h) || out->x0 < 0.f || out->y0 < 0.f || w < 30.f || h < 30.f ||
        w > 480.f || h > 480.f || out->x1 > 1e5f || out->y1 > 1e5f)
        return false;
    return known.Find(out->sno) != nullptr;
}

void SceneBlockScan::Match(const uint8_t* data, size_t n, uint64_t addr, size_t chunkSize) {
    const size_t limit = MatchLimit(n, chunkSize, tail());
    for (size_t i = 0; i + tail() <= limit && i < chunkSize; i += 8) {
        uint32_t sno;
        memcpy(&sno, data + i + kSnoOffset, 4);
        if (sno == 0 || sno > 0x00FFFFFF || known_.Find(sno) == nullptr) continue;
        uint32_t id;
        memcpy(&id, data + i, 4);
        SceneRecord rec;
        if (!DecodeScene(data + i, id & 0xFFFF, known_, &rec)) continue;
        const uint64_t at = addr + i;
        if (hits_ != nullptr) hits_->push_back({at, rec});
        const uint64_t span = static_cast<uint64_t>(id & 0xFFFF) * kSceneStride;
        if (span > at) continue;
        tally_[at - span]++;
        // noise records have id 0 / 0xFF000000 or world 0
        if (player_ != nullptr && rec.world != 0 && (rec.id >> 16) != 0 && (rec.id >> 16) != 0xFF00 &&
            player_->x >= rec.x0 && player_->x < rec.x1 && player_->y >= rec.y0 && player_->y < rec.y1)
            holdsPlayer_.insert(at - span);
    }
}

int SceneBlockScan::Finish(SceneTable* table, bool verbose) {
    std::vector<std::pair<int, uint64_t>> ranked;
    for (const auto& [base, votes] : tally_)
        if (votes >= 2 || holdsPlayer_.count(base) != 0) ranked.push_back({votes, base});
    std::sort(ranked.rbegin(), ranked.rend());
    std::vector<uint64_t> bases;
    for (const auto& [votes, base] : ranked) bases.push_back(base);
    if (verbose) std::print("scene table: {} block base(s)\n", bases.size());
    table->SetBases(bases);
    return static_cast<int>(bases.size());
}

int SceneTable::Discover(const Process& proc, const SceneCells& known, bool verbose,
                         std::vector<std::pair<uint64_t, SceneRecord>>* hits, const Vec2* player) {
    SceneBlockScan scan(known, hits, player);
    SharedSweep(proc, {&scan});
    return scan.Finish(this, verbose);
}

bool SceneTable::Read(const Process& proc, const SceneCells& known, std::vector<SceneRecord>* out) const {
    out->clear();
    std::vector<uint64_t> bases;
    {
        std::lock_guard<std::mutex> lock(mu_);
        bases = bases_;
    }
    constexpr uint32_t kBatch = 64;
    std::vector<uint8_t> buf(static_cast<size_t>(kBatch) * kSceneStride);
    std::unordered_set<uint64_t> seen;  // (world, id): a slot can look valid in two blocks
    for (uint64_t base : bases) {
        for (uint32_t first = 0; first < kMaxSlots; first += kBatch) {
            if (!proc.Read(base + static_cast<uint64_t>(first) * kSceneStride, buf.data(), buf.size())) continue;
            for (uint32_t k = 0; k < kBatch; ++k) {
                SceneRecord rec;
                if (!DecodeScene(buf.data() + static_cast<size_t>(k) * kSceneStride, first + k, known, &rec)) continue;
                if (seen.insert((static_cast<uint64_t>(rec.world) << 32) | rec.id).second) out->push_back(rec);
            }
        }
    }
    return !out->empty();
}

size_t SceneTable::baseCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return bases_.size();
}

void SceneTable::SetBases(const std::vector<uint64_t>& bases) {
    std::lock_guard<std::mutex> lock(mu_);
    bases_ = bases;
}

std::vector<uint64_t> SceneTable::GetBases() const {
    std::lock_guard<std::mutex> lock(mu_);
    return bases_;
}

}  // namespace d3hud
