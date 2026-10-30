#include "SceneCells.h"

#include <cstdio>
#include <cstring>

#include "Paths.h"

namespace d3hud {

bool SceneCells::Load(const std::filesystem::path& path) {
    scenes_.clear();
    flags_.clear();
    FILE* f = OpenFile(path, L"rb");
    if (f == nullptr) return false;
    char magic[4];
    uint32_t version = 0, count = 0;
    bool ok = fread(magic, 4, 1, f) == 1 && memcmp(magic, "D3SC", 4) == 0 &&
              fread(&version, 4, 1, f) == 1 && (version == 1 || version == 2) &&
              fread(&count, 4, 1, f) == 1;
    for (uint32_t i = 0; ok && i < count; ++i) {
        uint32_t sno = 0, flags = 0, n = 0;
        ok = fread(&sno, 4, 1, f) == 1 && (version < 2 || fread(&flags, 4, 1, f) == 1) &&
             fread(&n, 4, 1, f) == 1 && n < 100000;
        if (!ok) break;
        if (flags != 0) flags_[sno] = flags;
        std::vector<CellRect>& cells = scenes_[sno];
        cells.resize(n);
        ok = n == 0 || fread(cells.data(), sizeof(CellRect), n, f) == n;
    }
    fclose(f);
    if (!ok) {
        scenes_.clear();
        flags_.clear();
    }
    return ok && !scenes_.empty();
}

const std::vector<CellRect>* SceneCells::Find(uint32_t sceneSno) const {
    auto it = scenes_.find(sceneSno);
    return it == scenes_.end() ? nullptr : &it->second;
}

uint32_t SceneCells::Flags(uint32_t sceneSno) const {
    auto it = flags_.find(sceneSno);
    return it == flags_.end() ? 0 : it->second;
}

}  // namespace d3hud
