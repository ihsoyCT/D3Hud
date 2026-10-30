#include "AobScanner.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace d3hud {

bool Pattern::Parse(const char* text, Pattern* out) {
    if (out == nullptr || text == nullptr) return false;
    Pattern p;
    const char* s = text;
    while (*s != '\0') {
        if (*s == ' ' || *s == '\t') {
            ++s;
            continue;
        }
        if (s[0] == '?' && s[1] == '?') {
            p.bytes.push_back(0);
            p.mask.push_back(0);
            s += 2;
            continue;
        }
        char hex[3] = {s[0], s[1], 0};
        char* end = nullptr;
        long v = strtol(hex, &end, 16);
        if (end != hex + 2) return false;
        p.bytes.push_back(static_cast<uint8_t>(v));
        p.mask.push_back(1);
        s += 2;
    }
    if (p.bytes.empty() || p.bytes.size() != p.mask.size()) return false;
    *out = p;
    return true;
}

std::vector<uint64_t> ScanBuffer(const Pattern& p, const uint8_t* data, size_t size,
                                 uint64_t baseAddr, size_t maxMatches) {
    std::vector<uint64_t> hits;
    const size_t n = p.size();
    if (n == 0 || size < n) return hits;
    // first anchored (non-wildcard) byte for a cheap prefilter
    size_t anchor = 0;
    bool anyAnchored = false;
    for (size_t i = 0; i < n; ++i) {
        if (p.mask[i]) {
            anchor = i;
            anyAnchored = true;
            break;
        }
    }
    for (size_t off = 0; off + n <= size; ++off) {
        if (anyAnchored && data[off + anchor] != p.bytes[anchor]) continue;
        bool ok = true;
        for (size_t i = 0; i < n; ++i) {
            if (p.mask[i] && data[off + i] != p.bytes[i]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            hits.push_back(baseAddr + off);
            if (hits.size() >= maxMatches) break;
        }
    }
    return hits;
}

std::vector<uint64_t> ScanFile(const Pattern& p, const char* path, uint64_t baseAddr,
                               size_t maxMatches) {
    // Files we scan (game exe, region dumps) are well under 256 MB — read whole.
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (f == nullptr) return {};
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || static_cast<size_t>(len) > (256u << 20)) {
        fclose(f);
        return {};
    }
    std::vector<uint8_t> whole(static_cast<size_t>(len));
    if (fread(whole.data(), 1, whole.size(), f) != whole.size()) {
        fclose(f);
        return {};
    }
    fclose(f);
    return ScanBuffer(p, whole.data(), whole.size(), baseAddr, maxMatches);
}

std::vector<uint64_t> ScanProcess(const Process& proc, const Pattern& p,
                                  const ModuleInfo* restrictToModule, size_t maxMatches,
                                  DWORD typeFilter) {
    std::vector<uint64_t> hits;
    const size_t n = p.size();
    constexpr size_t kChunk = 1u << 20;  // 1 MB

    uint64_t lo = 0, hi = 0;
    if (restrictToModule != nullptr) {
        lo = restrictToModule->base;
        hi = restrictToModule->base + restrictToModule->size;
    }

    for (const RegionInfo& r : proc.Regions(typeFilter, true)) {
        if (restrictToModule != nullptr) {
            const uint64_t rEnd = r.base + r.size;
            if (rEnd <= lo || r.base >= hi) continue;
        }
        uint64_t done = 0;
        while (done < r.size && hits.size() < maxMatches) {
            const size_t take = static_cast<size_t>(std::min<uint64_t>(kChunk, r.size - done));
            const uint64_t addr = r.base + done;
            const std::vector<uint8_t> data = proc.ReadVec(addr, take);
            if (data.empty()) break;  // unreadable tail — skip region remainder
            auto part = ScanBuffer(p, data.data(), data.size(), addr, maxMatches - hits.size());
            hits.insert(hits.end(), part.begin(), part.end());
            // avoid duplicating a hit that starts in the last n-1 bytes: only advance n-1 less
            done += take >= n ? take - (n - 1) : take;
        }
        if (hits.size() >= maxMatches) break;
    }
    return hits;
}

}  // namespace d3hud
