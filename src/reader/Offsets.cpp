#include "Offsets.h"

#include <cstdio>
#include <print>

#include "AobScanner.h"

namespace d3hud {

// PLACEHOLDER TABLE — every entry is a stub until the M1a capture session.
// The demo entry exists so the resolve/verify plumbing is exercised end-to-end.
const std::vector<OffsetDef> kOffsetDefs = {
    {"demo.entry", "44 33 48 55 44 4D 31 4D 41 52 4B 45 52", 0, true},
    {"player.object_manager", nullptr, 0, true},
    {"actor.list_head", nullptr, 0, true},
    {"local.player_attribs", nullptr, 0, true},
};

std::vector<ResolvedOffset> ResolveAll(const Process& proc, const ModuleInfo& module) {
    std::vector<ResolvedOffset> out;
    for (const OffsetDef& def : kOffsetDefs) {
        ResolvedOffset r;
        r.name = def.name;
        if (def.pattern != nullptr) {
            Pattern p;
            if (Pattern::Parse(def.pattern, &p)) {
                auto hits = ScanProcess(proc, p, &module, 1);
                if (!hits.empty()) r.address = hits[0] + def.extraOffset;
            }
        }
        r.found = r.address != 0;
        out.push_back(r);
    }
    return out;
}

void PrintOffsets(const std::vector<ResolvedOffset>& resolved) {
    std::print("{:<28} {:<18} {}\n", "name", "address", "status");
    for (const auto& r : resolved) {
        std::print("{:<28} 0x{:016X} {}{}\n", r.name, static_cast<unsigned long long>(r.address),
               r.found ? "FOUND" : "missing", r.found ? "" : " (placeholder)");
    }
}

}  // namespace d3hud
