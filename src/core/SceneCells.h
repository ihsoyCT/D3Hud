// Walkable nav cells per scene SNO (scene_cells.bin, see tools/extract_scene_cells.py):
// the shapes of the prebuilt tiles levels are made of. Cells are scene-relative
// (0..240); add the scene's world origin to place them.
#pragma once

#include <cstdint>
#include <filesystem>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace d3hud {

struct CellRect {
    float x0, y0, x1, y1;
};

// Scene tile roles (from the scene's code name; file version 2).
constexpr uint32_t kSceneEntrance = 1;  // "*_entrance_*": where a level is entered
constexpr uint32_t kSceneExit = 2;      // "*_exit_*" / rift "*_dungeonstone_exit_*"

// The game's own nav cell: 32 bytes, min xyz, max xyz (floats), u16 flags (bit 0 = walkable), u16 neighbour count,
// u32 first neighbour. Measured live: a loaded scene asset keeps its cells at the pointer 392 bytes after its SNO.
constexpr size_t kGameNavCellSize = 32;

// Pure (unit-tested): the walkable cells of `count` game nav cells (`out` may stay empty: a solid tile). All or nothing:
// false (and nothing kept) if any cell is not a sane box inside a scene, so a false match of the scanner is never taken
// for a scene shape.
bool ExtractWalkableCells(const uint8_t* mem, size_t count, std::vector<CellRect>* out);

class SceneCells {
public:
    [[nodiscard]] bool Load(const std::filesystem::path& path);
    // Adds the scenes of a file that are not known yet (the cells the reader learned from the game).
    [[nodiscard]] bool Merge(const std::filesystem::path& path);
    // Adds a scene learned from the game; false if the SNO is known already. Thread-safe.
    bool Add(uint32_t sceneSno, std::vector<CellRect> cells);
    // Writes the scenes added with Add (flags unknown there) in the same file format.
    [[nodiscard]] bool SaveLearned(const std::filesystem::path& path) const;
    // Cells of a scene SNO (nullptr if unknown; an empty list for a scene known to have no walkable cell).
    const std::vector<CellRect>* Find(uint32_t sceneSno) const;
    // kScene* bits of a scene SNO (0 if unknown or none).
    uint32_t Flags(uint32_t sceneSno) const;
    size_t sceneCount() const;

private:
    bool ReadFile(const std::filesystem::path& path, bool replace);

    // Find hands out pointers: scenes are only ever added, never changed or removed, and map nodes do not move.
    mutable std::shared_mutex mu_;
    std::unordered_set<uint32_t> learned_;
    std::unordered_map<uint32_t, std::vector<CellRect>> scenes_;
    std::unordered_map<uint32_t, uint32_t> flags_;
};

}  // namespace d3hud
