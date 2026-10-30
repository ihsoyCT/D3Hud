// One sweep over the game's private memory feeding several matchers.
// Discovery passes used to walk the heap one by one (~4-6 s each); the sweep
// reads every chunk once and lets all matchers scan it, so startup costs one
// pass instead of one per table.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Process.h"

namespace d3hud {

class ChunkMatcher {
public:
    virtual ~ChunkMatcher() = default;
    // Bytes past the logical chunk end the matcher needs to see (records
    // spanning chunk borders). The sweep reads chunk + max(tail) bytes.
    virtual size_t tail() const = 0;
    // Scan one buffer. `n` = actual bytes read, `addr` = its address,
    // `chunkSize` = the logical chunk: matchers only report finds whose start
    // lies within the first chunkSize bytes (tail bytes are re-scanned as the
    // head of the next chunk).
    virtual void Match(const uint8_t* data, size_t n, uint64_t addr, size_t chunkSize) = 0;
    // Disabled matchers stay in the sweep but do no work (e.g. no evidence
    // yet); Finish() must then leave the table untouched.
    virtual bool enabled() const { return true; }
};

// Walks MEM_PRIVATE regions in 16 MB chunks, reading each once for all
// enabled matchers. Same window semantics as the former per-table loops.
void SharedSweep(const Process& proc, const std::vector<ChunkMatcher*>& matchers);

// Start-of-scan limit for a matcher: its own tail, capped by what was read.
inline size_t MatchLimit(size_t n, size_t chunkSize, size_t tail) {
    return n < chunkSize + tail ? n : chunkSize + tail;
}

}  // namespace d3hud
