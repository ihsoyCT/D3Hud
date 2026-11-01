#include "SceneCells.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "Paths.h"

namespace d3hud {

bool ExtractWalkableCells(const uint8_t* mem, size_t count, std::vector<CellRect>* out) {
    out->clear();
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* p = mem + i * kGameNavCellSize;
        float f[6];
        uint16_t flags;
        memcpy(f, p, sizeof(f));
        memcpy(&flags, p + 24, 2);
        for (float v : f)
            if (!std::isfinite(v)) {
                out->clear();
                return false;
            }
        if (f[0] < 0.f || f[1] < 0.f || !(f[3] > f[0]) || !(f[4] > f[1]) || f[3] > 1000.f || f[4] > 1000.f) {
            out->clear();
            return false;
        }
        if (flags & 1) out->push_back({f[0], f[1], f[3], f[4]});
    }
    return count > 0;
}

bool SceneCells::ReadFile(const std::filesystem::path& path, bool replace) {
    File f = OpenFile(path, L"rb");
    if (f == nullptr) return false;
    char magic[4];
    uint32_t version = 0, count = 0;
    bool ok = fread(magic, 4, 1, f) == 1 && memcmp(magic, "D3SC", 4) == 0 &&
              fread(&version, 4, 1, f) == 1 && (version == 1 || version == 2) &&
              fread(&count, 4, 1, f) == 1;
    std::unordered_map<uint32_t, std::vector<CellRect>> scenes;
    std::unordered_map<uint32_t, uint32_t> flags;
    for (uint32_t i = 0; ok && i < count; ++i) {
        uint32_t sno = 0, fl = 0, n = 0;
        ok = fread(&sno, 4, 1, f) == 1 && (version < 2 || fread(&fl, 4, 1, f) == 1) &&
             fread(&n, 4, 1, f) == 1 && n < 100000;
        if (!ok) break;
        if (fl != 0) flags[sno] = fl;
        std::vector<CellRect>& cells = scenes[sno];
        cells.resize(n);
        ok = n == 0 || fread(cells.data(), sizeof(CellRect), n, f) == n;
    }
    if (!ok || scenes.empty()) return false;
    std::unique_lock lock(mu_);
    if (replace) {
        scenes_.clear();
        learned_.clear();
        flags_.clear();
    }
    for (auto& [sno, cells] : scenes) {
        if (!scenes_.emplace(sno, std::move(cells)).second) continue;
        if (!replace) learned_.insert(sno);  // merged files are the learned ones: they are written back with the new ones
        if (auto it = flags.find(sno); it != flags.end()) flags_[sno] = it->second;
    }
    return true;
}

bool SceneCells::Load(const std::filesystem::path& path) { return ReadFile(path, true); }

bool SceneCells::Merge(const std::filesystem::path& path) { return ReadFile(path, false); }

bool SceneCells::Add(uint32_t sceneSno, std::vector<CellRect> cells) {
    std::unique_lock lock(mu_);
    if (!scenes_.emplace(sceneSno, std::move(cells)).second) return false;
    learned_.insert(sceneSno);
    return true;
}

bool SceneCells::SaveLearned(const std::filesystem::path& path) const {
    std::shared_lock lock(mu_);
    File f = OpenFile(path, L"wb");
    if (f == nullptr) return false;
    const uint32_t version = 2, count = static_cast<uint32_t>(learned_.size());
    bool ok = fwrite("D3SC", 4, 1, f) == 1 && fwrite(&version, 4, 1, f) == 1 && fwrite(&count, 4, 1, f) == 1;
    for (uint32_t sno : learned_) {
        const std::vector<CellRect>& cells = scenes_.at(sno);
        const uint32_t flags = 0, n = static_cast<uint32_t>(cells.size());
        ok = ok && fwrite(&sno, 4, 1, f) == 1 && fwrite(&flags, 4, 1, f) == 1 && fwrite(&n, 4, 1, f) == 1 &&
             (n == 0 || fwrite(cells.data(), sizeof(CellRect), n, f) == n);
    }
    return ok;
}

const std::vector<CellRect>* SceneCells::Find(uint32_t sceneSno) const {
    std::shared_lock lock(mu_);
    auto it = scenes_.find(sceneSno);
    return it == scenes_.end() ? nullptr : &it->second;
}

uint32_t SceneCells::Flags(uint32_t sceneSno) const {
    std::shared_lock lock(mu_);
    auto it = flags_.find(sceneSno);
    return it == flags_.end() ? 0 : it->second;
}

size_t SceneCells::sceneCount() const {
    std::shared_lock lock(mu_);
    return scenes_.size();
}

}  // namespace d3hud
