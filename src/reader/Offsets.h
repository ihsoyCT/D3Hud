// The one file that owns every game-memory pattern. Patch day = edit here.
// Entries are placeholders until the M1a capture session produces real dumps;
// ResolveAll() reports which entries resolve, so breakage is visible at a glance.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Process.h"

namespace d3hud {

struct OffsetDef {
    const char* name;
    const char* pattern;    // AOB with ?? wildcards
    int extraOffset;        // bytes to add to a match (e.g., instruction -> operand)
    bool isPlaceholder;     // true until real patterns land from M1a
};

struct ResolvedOffset {
    const char* name = nullptr;
    uint64_t address = 0;
    bool found = false;
};

extern const std::vector<OffsetDef> kOffsetDefs;

// Scans the target module of the process for every definition.
std::vector<ResolvedOffset> ResolveAll(const Process& proc, const ModuleInfo& module);

void PrintOffsets(const std::vector<ResolvedOffset>& resolved);

}  // namespace d3hud
