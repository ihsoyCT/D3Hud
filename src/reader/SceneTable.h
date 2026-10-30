// Scenes (prebuilt level tiles) loaded for the worlds in memory - the source of
// the map prediction. Record layout (TurboHUD 2.6.10 struct, = cluster #37 of the
// current TurboHUD's struct mining; verified live: town scenes on the 240 grid):
//   stride 1976   +0 u32 scene id (low 16 bits = slot)   +12 u32 world id
//   +236 u32 scene SNO   +256/+260 f32 x0, y0   +376/+380 f32 x1, y1
// Records live in several blocks; like AcdTable, every candidate block base is
// tried and only self-consistent records (id index == slot, known SNO, sane
// rectangle) are accepted.
#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Process.h"
#include "SceneCells.h"
#include "SharedScan.h"
#include "Snapshot.h"

namespace d3hud {

constexpr uint32_t kSceneStride = 1976;

struct SceneRecord {
    uint32_t id = 0, world = 0, sno = 0;
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
};

// Pure (unit-tested): decode one record (kSceneStride bytes) expected at `slot`.
bool DecodeScene(const uint8_t* rec, uint32_t slot, const SceneCells& known, SceneRecord* out);

// Sweepable scene-block discovery (see SharedScan.h).
class SceneTable;

// Sweepable scene-block discovery (see SharedScan.h).
class SceneBlockScan : public ChunkMatcher {
public:
    SceneBlockScan(const SceneCells& known, std::vector<std::pair<uint64_t, SceneRecord>>* hits,
                   const Vec2* player)
        : known_(known), hits_(hits), player_(player) {}
    size_t tail() const override { return kSceneStride; }
    void Match(const uint8_t* data, size_t n, uint64_t addr, size_t chunkSize) override;
    int Finish(SceneTable* table, bool verbose = false);

private:
    const SceneCells& known_;
    std::vector<std::pair<uint64_t, SceneRecord>>* hits_;
    const Vec2* player_;
    std::unordered_map<uint64_t, int> tally_;
    std::unordered_set<uint64_t> holdsPlayer_;  // bases whose record contains the player
};

class SceneTable {
public:
    // One pass over private memory; keeps every implied block base with >= 2
    // records, and the base of a lone record containing `player` (optional; live:
    // a rift level held one 320x320 scene, so one record is all there is).
    // `hits` (optional, diagnostics): every self-consistent record found, as {address, record}.
    int Discover(const Process& proc, const SceneCells& known, bool verbose = false,
                 std::vector<std::pair<uint64_t, SceneRecord>>* hits = nullptr, const Vec2* player = nullptr);
    // All valid records in the known blocks (every world in memory).
    bool Read(const Process& proc, const SceneCells& known, std::vector<SceneRecord>* out) const;
    size_t baseCount() const;
    void SetBases(const std::vector<uint64_t>& bases);
    std::vector<uint64_t> GetBases() const;

private:
    mutable std::mutex mu_;
    std::vector<uint64_t> bases_;
};

}  // namespace d3hud
