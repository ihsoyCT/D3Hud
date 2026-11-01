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

// Pure (unit-tested): decode one record (kSceneStride bytes) expected at `slot`; DecodeSceneShape does not ask whether
// the scene SNO is known.
bool DecodeSceneShape(const uint8_t* rec, uint32_t slot, SceneRecord* out);
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

// Scenes the game has loaded but scene_cells.bin does not know (a newer game version brings new tiles). A loaded scene
// asset keeps its SNO at +0, its nav cell count at +384, a pointer to the cells at +392 and their size in bytes at +404
// (measured live: 379 of 379 uniquely identified scenes, and the walkable cells equalled the TurboHUD table's).
// Sweepable (see SharedScan.h); Finish reads the cells of every asset found and adds them to `cells`.
class NavAssetScan : public ChunkMatcher {
public:
    explicit NavAssetScan(const SceneCells& known) : known_(known) {}
    size_t tail() const override { return 412; }
    void Match(const uint8_t* data, size_t n, uint64_t addr, size_t chunkSize) override;
    // Returns how many scenes were new.
    int Finish(const Process& proc, SceneCells* cells, bool verbose = false);
    // The SNOs found by Match (diagnostics).
    std::vector<uint32_t> Learned() const {
        std::vector<uint32_t> out;
        for (const Asset& a : found_) out.push_back(a.sno);
        return out;
    }

private:
    struct Asset {
        uint32_t sno, count;
        uint64_t cells;
    };
    const SceneCells& known_;
    std::vector<Asset> found_;
    std::unordered_set<uint32_t> seen_;
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
    // `unknown` (optional) receives the SNOs of valid records whose scene shape is not known (a tile that loaded after the
    // last scan): the caller then looks for their cells in the game.
    [[nodiscard]] bool Read(const Process& proc, const SceneCells& known, std::vector<SceneRecord>* out,
                            std::vector<uint32_t>* unknown = nullptr) const;
    size_t baseCount() const;
    void SetBases(const std::vector<uint64_t>& bases);
    std::vector<uint64_t> GetBases() const;

private:
    mutable std::mutex mu_;
    std::vector<uint64_t> bases_;
};

}  // namespace d3hud
