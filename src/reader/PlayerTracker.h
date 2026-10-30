// Offset-free local-player position discovery.
//
// Method (validated against 4 memory captures): the player's position is the
// most heavily repeated exact (x, z) float pair inside a dense neighborhood —
// PROVIDED the player has been standing ~2 s (a just-moved player splinters
// across interpolation variants). Static scene anchors are monolithic (one
// value, mono > 0.5) and mesh grids are diagonal (x == z); both excluded.
#pragma once

#include <cstdint>
#include <vector>

#include "Process.h"

namespace d3hud {

struct PlayerPosition {
    float x = 0.f;
    float z = 0.f;
    float y = 0.f;  // not read by the pair tracker (0 placeholder)
};

struct ClusterInfo {
    float centerX = 0, centerZ = 0;  // neighborhood center
    float topX = 0, topZ = 0;        // most-repeated exact pair in the cluster
    int total = 0;                   // pairs in 3x3 bin neighborhood
    int distinct = 0;                // distinct exact pairs
    int topCount = 0;                // copies of the top exact pair
    float mono = 0.f;                // topCount / total
};

struct PlayerFindResult {
    PlayerPosition pos;
    uint64_t recordAddr = 0;  // address of one instance of the top (x, z) pair
    int clusterVotes = 0;     // topCount of the winning cluster
    int totalValidHits = 0;
    ClusterInfo cluster;
};

// One full pass over private RW memory, returning candidate clusters.
std::vector<ClusterInfo> ScanClusters(const Process& proc);

// Pure ranking logic (unit-tested): highest top-count cluster wins.
PlayerFindResult PickPlayerCluster(const std::vector<ClusterInfo>& clusters);

class PlayerTracker {
public:
    static bool Find(const Process& proc, PlayerFindResult* out, bool verbose = false);
    bool ReadPosition(const Process& proc, PlayerPosition* out) const;
    bool valid() const { return record_ != 0; }
    uint64_t record() const { return record_; }
    void setRecord(uint64_t addr) { record_ = addr; }

private:
    uint64_t record_ = 0;  // address of an (x, z) pair belonging to the player
};

}  // namespace d3hud
