#include "SharedScan.h"

#include <algorithm>

namespace d3hud {

void SharedSweep(const Process& proc, const std::vector<ChunkMatcher*>& matchers) {
    constexpr size_t kChunk = 16u << 20;
    size_t maxTail = 0;
    bool any = false;
    for (ChunkMatcher* m : matchers) {
        if (!m->enabled()) continue;
        any = true;
        maxTail = std::max(maxTail, m->tail());
    }
    if (!any) return;

    std::vector<uint8_t> buf;
    for (const RegionInfo& r : proc.Regions(MEM_PRIVATE, true)) {
        if (r.size < 8) continue;
        for (size_t off = 0; off < r.size; off += kChunk) {
            const size_t n = std::min(kChunk + maxTail, r.size - off);
            buf.resize(n);
            if (!proc.Read(r.base + off, buf.data(), n)) continue;
            for (ChunkMatcher* m : matchers) {
                if (m->enabled()) m->Match(buf.data(), n, r.base + off, kChunk);
            }
        }
    }
}

}  // namespace d3hud
