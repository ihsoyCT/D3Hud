// Array-of-bytes pattern scanning with ?? wildcards — over buffers, files,
// and remote process memory. Used both live (M2) and offline (offset work
// against dumps and the on-disk game exe).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Process.h"

namespace d3hud {

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> mask;  // 1 = must match, 0 = wildcard

    // "48 8B 05 ?? ?? ?? ?? 48 85 C0" — hex bytes, ?? wildcards
    static bool Parse(const char* text, Pattern* out);
    size_t size() const { return bytes.size(); }
};

// Scan an in-memory buffer; hits are returned as absolute addresses (base + offset).
std::vector<uint64_t> ScanBuffer(const Pattern& p, const uint8_t* data, size_t size,
                                 uint64_t baseAddr, size_t maxMatches = 16);

// Scan a file's raw bytes (e.g., the game exe on disk for offline pattern work).
std::vector<uint64_t> ScanFile(const Pattern& p, const char* path, uint64_t baseAddr = 0,
                               size_t maxMatches = 16);

// Scan readable regions of a remote process, optionally restricted to a module's
// range and/or a region type (MEM_PRIVATE / MEM_MAPPED / MEM_IMAGE; 0 = all).
std::vector<uint64_t> ScanProcess(const Process& proc, const Pattern& p,
                                  const ModuleInfo* restrictToModule = nullptr,
                                  size_t maxMatches = 16, DWORD typeFilter = 0);

}  // namespace d3hud
