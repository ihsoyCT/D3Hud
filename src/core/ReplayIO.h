// Replay file I/O: a tiny length-free format — header + fixed-stride WorldSnapshots.
#pragma once

#include "Snapshot.h"

#include <cstdint>
#include <cstdio>

namespace d3hud {

struct ReplayHeader {
    uint32_t magic;         // 'D3HR'
    uint32_t version;
    uint32_t frameCount;
    float frameIntervalMs;
};

class ReplayWriter {
public:
    ~ReplayWriter() { Close(); }
    [[nodiscard]] bool Open(const char* path, float frameIntervalMs);
    void Append(const WorldSnapshot& snap);
    void Close();
    uint32_t count() const { return count_; }

private:
    FILE* f_ = nullptr;
    ReplayHeader hdr_ = {};
    uint32_t count_ = 0;
};

class ReplayReader {
public:
    ~ReplayReader() { Close(); }
    [[nodiscard]] bool Open(const char* path);
    [[nodiscard]] bool Read(uint32_t index, WorldSnapshot* out) const;
    void Close();
    uint32_t frameCount() const { return hdr_.frameCount; }
    float frameIntervalMs() const { return hdr_.frameIntervalMs; }

private:
    FILE* f_ = nullptr;
    ReplayHeader hdr_ = {};
};

}  // namespace d3hud
