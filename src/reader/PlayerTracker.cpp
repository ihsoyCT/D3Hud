#include "PlayerTracker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <unordered_map>
#include <vector>

#include "AobScanner.h"

namespace d3hud {

// ---------------------------------------------------------------------------
// Cluster scan: the local player is the densest coordinate neighborhood that
// is neither monolithic (static anchors: one exact value, thousands of copies)
// nor diagonal (mesh grids: x == z) nor noise (thousands of distinct values).
// Works when the player has been standing for ~2 s; a just-moved player's
// copies splinter across interpolation variants and can lose to NPC crowds.
// ---------------------------------------------------------------------------

struct BinData {
    int total = 0;
    std::unordered_map<uint64_t, int> exact;  // packed (x,z) bits -> count
    uint64_t sampleAddr = 0;                  // one instance address of top value
};

static uint64_t Pack(const float x, const float z) {
    uint32_t bx, bz;
    memcpy(&bx, &x, 4);
    memcpy(&bz, &z, 4);
    return (static_cast<uint64_t>(bx) << 32) | bz;
}

static void Unpack(uint64_t packed, float* x, float* z) {
    uint32_t bx = static_cast<uint32_t>(packed >> 32);
    uint32_t bz = static_cast<uint32_t>(packed);
    memcpy(x, &bx, 4);
    memcpy(z, &bz, 4);
}

std::vector<ClusterInfo> ScanClusters(const Process& proc) {
    constexpr float kBin = 4.0f;
    constexpr float kMinMag = 30.f, kMaxMag = 4000.f;

    std::unordered_map<int64_t, BinData> bins;
    std::vector<uint64_t> chunk(1 << 18);

    for (const RegionInfo& r : proc.Regions(MEM_PRIVATE, true)) {
        if (r.size < 0x10000) continue;
        uint64_t done = 0;
        while (done < r.size) {
            const size_t take = static_cast<size_t>(std::min<uint64_t>(chunk.size() * 4, r.size - done));
            const uint64_t addr = r.base + done;
            if (!proc.Read(addr, chunk.data(), (take / 4) * 4)) break;
            const size_t nFloats = take / 4;
            const float* f = reinterpret_cast<const float*>(chunk.data());
            for (size_t i = 0; i + 1 < nFloats; ++i) {
                const float x = f[i], z = f[i + 1];
                if (!(kMinMag < std::fabs(x) && std::fabs(x) < kMaxMag)) continue;
                if (!(kMinMag < std::fabs(z) && std::fabs(z) < kMaxMag)) continue;
                if (std::fabs(x - std::round(x)) < 1e-4f && std::fabs(z - std::round(z)) < 1e-4f) continue;
                if (std::fabs(x - z) <= 4.f) continue;  // mesh diagonals
                const int64_t bx = static_cast<int64_t>(std::floor(x / kBin));
                const int64_t bz = static_cast<int64_t>(std::floor(z / kBin));
                const int64_t key = (bx << 32) | (bz & 0xFFFFFFFFLL);
                BinData& b = bins[key];
                b.total++;
                const int c = ++b.exact[Pack(x, z)];
                (void)c;
            }
            done += (take / 4) * 4;
        }
    }

    // roll up 3x3 neighborhoods
    std::vector<ClusterInfo> out;
    for (const auto& kv : bins) {
        const int64_t bx = kv.first >> 32;
        const int64_t bz = ((kv.first & 0xFFFFFFFFLL) << 32) >> 32;
        int total = 0;
        std::unordered_map<uint64_t, int> vals;
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                const auto it = bins.find(((bx + dx) << 32) | ((bz + dz) & 0xFFFFFFFFLL));
                if (it == bins.end()) continue;
                total += it->second.total;
                for (const auto& vk : it->second.exact) vals[vk.first] += vk.second;
            }
        }
        if (vals.empty()) continue;
        int top = 0;
        uint64_t topPacked = 0;
        for (const auto& vk : vals) {
            if (vk.second > top) {
                top = vk.second;
                topPacked = vk.first;
            }
        }
        const float mono = static_cast<float>(top) / static_cast<float>(total);
        if (total < 400 || vals.size() < 3 || mono > 0.5f) continue;
        float cx, cz;
        Unpack(topPacked, &cx, &cz);
        out.push_back({(bx * 4) + 2.f, (bz * 4) + 2.f, cx, cz, total, static_cast<int>(vals.size()), top, mono});
    }
    return out;
}

PlayerFindResult PickPlayerCluster(const std::vector<ClusterInfo>& clusters) {
    PlayerFindResult out;
    const ClusterInfo* best = nullptr;
    for (const auto& c : clusters) {
        if (c.mono > 0.5f) continue;                    // static anchors
        if (std::fabs(c.topX - c.topZ) <= 4.f) continue; // mesh diagonals
        if (best == nullptr || c.topCount > best->topCount) best = &c;
    }
    if (best == nullptr) return out;
    out.pos = {best->topX, best->topZ, 0.f};
    out.clusterVotes = best->topCount;
    out.totalValidHits = best->total;
    out.recordAddr = 0;  // filled by Find after locating an instance
    out.cluster = *best;
    return out;
}

bool PlayerTracker::Find(const Process& proc, PlayerFindResult* out, bool verbose) {
    std::vector<ClusterInfo> clusters = ScanClusters(proc);
    std::sort(clusters.begin(), clusters.end(),
              [](const ClusterInfo& a, const ClusterInfo& b) { return a.topCount > b.topCount; });
    if (verbose) {
        std::print("find: {} candidate clusters\n", clusters.size());
        for (size_t i = 0; i < clusters.size() && i < 5; ++i) {
            const ClusterInfo& c = clusters[i];
            std::print("  cluster ({:.0f},{:.0f}) total={} distinct={} top={} mono={:.2f}\n", c.centerX, c.centerZ,
                   c.total, c.distinct, c.topCount, c.mono);
        }
    }
    if (clusters.empty()) return false;
    PlayerFindResult r = PickPlayerCluster(clusters);
    if (r.clusterVotes < 100) {
        if (verbose) std::print("find: best top-count {} below threshold\n", r.clusterVotes);
        return false;
    }
    // locate a concrete instance of the top exact value to track
    const float tx = r.pos.x, tz = r.pos.z;
    uint64_t found = 0;
    for (const RegionInfo& reg : proc.Regions(MEM_PRIVATE, true)) {
        if (found) break;
        if (reg.size < 0x10000) continue;
        std::vector<uint8_t> data = proc.ReadVec(reg.base, std::min<uint64_t>(reg.size, 64u << 20));
        if (data.empty()) continue;
        const size_t n = data.size() / 4 - 1;
        const float* f = reinterpret_cast<const float*>(data.data());
        for (size_t i = 0; i < n; ++i) {
            if (f[i] == tx && f[i + 1] == tz) {
                found = reg.base + i * 4;
                break;
            }
        }
    }
    if (found == 0) return false;
    r.recordAddr = found;
    if (out != nullptr) *out = r;
    return true;
}

bool PlayerTracker::ReadPosition(const Process& proc, PlayerPosition* out) const {
    if (record_ == 0) return false;
    float v[2];
    if (!proc.Read(record_, v, sizeof(v))) return false;
    if (!std::isfinite(v[0]) || !std::isfinite(v[1])) return false;
    if (std::fabs(v[0]) > 5000.f || std::fabs(v[1]) > 5000.f) return false;
    if (std::fabs(v[0] - v[1]) <= 4.f) return false;  // degenerated into mesh values
    out->x = v[0];
    out->z = v[1];
    out->y = 0.f;
    return true;
}

}  // namespace d3hud
