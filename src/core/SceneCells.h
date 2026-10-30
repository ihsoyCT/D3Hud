// Walkable nav cells per scene SNO (scene_cells.bin, see tools/extract_scene_cells.py):
// the shapes of the prebuilt tiles levels are made of. Cells are scene-relative
// (0..240); add the scene's world origin to place them.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace d3hud {

struct CellRect {
    float x0, y0, x1, y1;
};

// Scene tile roles (from the scene's code name; file version 2).
constexpr uint32_t kSceneEntrance = 1;  // "*_entrance_*": where a level is entered
constexpr uint32_t kSceneExit = 2;      // "*_exit_*" / rift "*_dungeonstone_exit_*"

class SceneCells {
public:
    bool Load(const std::string& path);
    // Cells of a scene SNO (nullptr if unknown / no walkable cells).
    const std::vector<CellRect>* Find(uint32_t sceneSno) const;
    // kScene* bits of a scene SNO (0 if unknown or none).
    uint32_t Flags(uint32_t sceneSno) const;
    size_t sceneCount() const { return scenes_.size(); }

private:
    std::unordered_map<uint32_t, std::vector<CellRect>> scenes_;
    std::unordered_map<uint32_t, uint32_t> flags_;
};

}  // namespace d3hud
