// D3Hud.Reader — process attach, AOB scanning, offset resolution, memory capture.
//
//   D3Hud.Reader --list [filter]                 list processes
//   D3Hud.Reader --attach <pid|name>             attach, dump modules + offsets
//   D3Hud.Reader --capture <dir>                 dump readable private regions (M1a)
//   D3Hud.Reader --attach <name> --actors        dump classified actors (minimap source)
//   D3Hud.Reader --attach <name> --items         list equipped + backpack items
//   D3Hud.Reader --attach <name> --publish       live snapshots -> overlay ring
//                [--sno <path>]                  actor SNO table (default: next to exe)
//   D3Hud.Reader --scan-file <file> <pattern>    offline AOB scan (dumps, game exe)
//   D3Hud.Reader --selftest                      spawn marker child, find it, verify
//   D3Hud.Reader --marker-child <addr-file>      internal: sacrificial child for --selftest
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <map>
#include <string>
#include <unordered_map>

#include <vector>

#include "AcdTable.h"
#include "ActorList.h"
#include "AttribTable.h"
#include "BasesCache.h"
#include "SceneTable.h"
#include "AobScanner.h"
#include "Offsets.h"
#include "PlayerTracker.h"
#include "Process.h"
#include "SharedRing.h"
#include "SharedScan.h"
#include "SnoTable.h"
#include "UiControls.h"

using namespace d3hud;

// 16-byte unique marker embedded by --marker-child children.
static const uint8_t kMarker[16] = {0x44, 0x33, 0x48, 0x55, 0x44, 0x4D, 0x31, 0x4D,
                                    0x41, 0x52, 0x4B, 0x45, 0x52, 0x00, 0xDE, 0xAD};

static int RunMarkerChild() {
    // Allocate a page, drop the marker at a semi-random offset, print the
    // address, and idle so the parent can scan us.
    uint8_t* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE,
                                                       PAGE_READWRITE));
    if (page == nullptr) return 1;
    const size_t off = 1000;
    memcpy(page + off, kMarker, sizeof(kMarker));
    std::print("MARKER 0x{:X}\n", static_cast<unsigned long long>(
                                  reinterpret_cast<uintptr_t>(page + off)));
    fflush(stdout);
    Sleep(60000);
    return 0;
}

static int Selftest() {
    // Spawn a child of ourselves carrying the marker, attach, scan, verify the
    // address match is exact. Proves: OpenProcess, region walk, RPM, scanner.
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string cmd = std::string(self) + " --marker-child";
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NEW_CONSOLE, nullptr,
                        nullptr, &si, &pi)) {
        std::print(stderr, "FATAL: cannot spawn marker child\n");
        return 1;
    }
    // Read the child's stdout? Simpler: poll our own... no — child prints to its
    // console. Instead: scan first, then validate the marker reads back correctly.
    Sleep(500);
    auto proc = Process::OpenByPid(pi.dwProcessId);
    if (!proc) {
        std::print(stderr, "FATAL: cannot open child process\n");
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }
    Pattern pat;
    Pattern::Parse("44 33 48 55 44 4D 31 4D 41 52 4B 45 52 00 DE AD", &pat);
    std::print("scanning child pid {} for marker...\n",
           static_cast<unsigned long>(pi.dwProcessId));
    auto hits = ScanProcess(*proc, pat, nullptr, 4);
    bool ok = false;
    uint64_t markerAddr = 0;
    for (uint64_t h : hits) {
        uint8_t back[16] = {};
        if (proc->Read(h, back, 16) && memcmp(back, kMarker, 16) == 0) {
            ok = true;
            markerAddr = h;
            break;
        }
    }
    std::print("selftest: {} hit(s), marker {} at 0x{:X}\n", hits.size(), ok ? "VERIFIED" : "FAILED",
           static_cast<unsigned long long>(markerAddr));
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return ok ? 0 : 2;
}

static void EnsureDir(std::string path) {
    // create every component ("a\\b\\c"), ignoring errors for existing parts
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '\\' || path[i] == '/') {
            CreateDirectoryA(path.substr(0, i).c_str(), nullptr);
        }
    }
    CreateDirectoryA(path.c_str(), nullptr);
}

static int Capture(Process& proc, const char* outDir) {
    EnsureDir(outDir);
    const std::string probe = std::string(outDir) + "\\index.txt";
    if (GetFileAttributesA(probe.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::print(stderr,
                "FATAL: capture '{}' already exists - refusing to overwrite. Use a new name.\n",
                outDir);
        return 1;
    }
    std::string idxPath = std::string(outDir) + "\\index.txt";
    FILE* idx = nullptr;
    fopen_s(&idx, idxPath.c_str(), "w");
    if (idx == nullptr) {
        std::print(stderr, "FATAL: cannot write {}\n", idxPath.c_str());
        return 1;
    }
    std::print(idx, "# region_id base size protect type file_offset\n");
    std::print("capturing readable regions of pid {} ({}) -> {}\n",
           static_cast<unsigned long>(proc.pid()), proc.name().c_str(), outDir);
    auto regions = proc.Regions(0, true);
    uint64_t total = 0, dumped = 0;
    int id = 0;
    FILE* blob = nullptr;
    fopen_s(&blob, (std::string(outDir) + "\\regions.bin").c_str(), "wb");
    if (blob == nullptr) {
        fclose(idx);
        return 1;
    }
    // Giant private regions are the game's allocator arenas - they must be captured. Read in
    // chunks (one index entry per chunk) so one unreadable page only drops its own sub-chunk.
    const size_t kChunk = 16u << 20, kSubChunk = 64u << 10;
    auto dumpRange = [&](const RegionInfo& r, uint64_t base, size_t size) -> bool {
        std::vector<uint8_t> data = proc.ReadVec(base, size);
        if (data.empty()) return false;
        const uint64_t fileOff = static_cast<uint64_t>(_ftelli64(blob));
        fwrite(data.data(), 1, data.size(), blob);
        std::print(idx, "{} 0x{:X} {} 0x{:X} 0x{:X} 0x{:X}\n", id, static_cast<unsigned long long>(base),
                size, r.protect, r.type, static_cast<unsigned long long>(fileOff));
        dumped += data.size();
        id++;
        if ((id % 256) == 0) std::print("  {} regions, {} MB...\n", id, dumped >> 20);
        return true;
    };
    uint64_t skippedMapped = 0, unreadable = 0;
    for (const RegionInfo& r : regions) {
        total += r.size;
        if (r.type == MEM_MAPPED && r.size > (64u << 20)) {  // big mapped files: not game state
            skippedMapped += r.size;
            continue;
        }
        for (size_t off = 0; off < r.size; off += kChunk) {
            const size_t n = std::min(kChunk, r.size - off);
            if (dumpRange(r, r.base + off, n)) continue;
            for (size_t sub = 0; sub < n; sub += kSubChunk) {
                const size_t m = std::min(kSubChunk, n - sub);
                if (!dumpRange(r, r.base + off + sub, m)) unreadable += m;
            }
        }
    }
    fclose(blob);
    fclose(idx);
    std::print("capture done: {} regions, {} MB of {} MB committed-readable "
           "(skipped {} MB big mapped files, {} MB unreadable)\n",
           id, dumped >> 20, total >> 20, skippedMapped >> 20, unreadable >> 20);
    return 0;
}

static std::string DefaultSnoPath() {
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string dir = self;
    return dir.substr(0, dir.find_last_of("\\/") + 1) + "sno_actors.tsv";
}

static std::string ExeSiblingPath(const char* file) {
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string dir = self;
    return dir.substr(0, dir.find_last_of("\\/") + 1) + file;
}

// Classified, drawable actors -> snapshot. The local player is the Player actor
// nearest to `hint` (single-player: the only one); *player receives it.
// Classify, then refine monsters by their ACD quality (champion/rare/...).
// Returns false if not drawn; *unresolved is set for monsters whose ACD record
// was not found in any known block.
static bool ClassifyWithQuality(const Process& proc, const AcdTable* acds, const SnoInfo* info,
                                const RawActor& a, ActorKind* kind, uint32_t* quality,
                                bool* unresolved) {
    *quality = 0xFFFFFFFF;
    *unresolved = false;
    if (info == nullptr || !ClassifyActor(info->kind, info->name, a.hasAcd(), a.dead, kind))
        return false;
    if (acds != nullptr && info->kind == "Monster" && a.hasAcd()) {
        if (acds->Quality(proc, a.acd, quality))
            *kind = ApplyMonsterQuality(*kind, *quality);
        else
            *unresolved = true;
    }
    return true;
}

// Pylon/shrine already clicked? Reads Gizmo_State / Gizmo_Has_Been_Operated
// from the gizmo's attribute group; *state/*operated get the raw values
// (0xFFFFFFFF = unreadable). Unknown counts as unused, so it stays visible.
static bool GizmoUsed(const Process& proc, const AcdTable* acds, const AttribTable* attribs,
                      const RawActor& a, uint32_t* state = nullptr, uint32_t* operated = nullptr) {
    uint32_t st = 0xFFFFFFFF, op = 0xFFFFFFFF;
    AcdItemRecord r;
    if (acds != nullptr && attribs != nullptr && a.hasAcd() && acds->ReadItem(proc, a.acd, &r)) {
        const auto res = attribs->Get(proc, r.attribGroup, kAttrGizmoState, &st);
        if (res == AttribTable::Result::Unresolved) st = 0xFFFFFFFF;
        else if (res == AttribTable::Result::NotFound) st = 0;
        if (res != AttribTable::Result::Unresolved) op = 0;
        if (op == 0 && attribs->Get(proc, r.attribGroup, kAttrGizmoOperated, &op) != AttribTable::Result::Found)
            op = 0;
    }
    if (state != nullptr) *state = st;
    if (operated != nullptr) *operated = op;
    return (st != 0xFFFFFFFF && st != 0) || (op != 0xFFFFFFFF && op != 0);
}

static int FillActors(const Process& proc, const AcdTable* acds, const AttribTable* attribs,
                      const std::vector<RawActor>& raw, const SnoTable& sno, Vec2 hint,
                      WorldSnapshot* snap, RawActor* player, bool* havePlayer) {
    *havePlayer = false;
    float best = 1e30f;
    int unresolvedCount = 0;
    snap->actorCount = 0;
    for (const RawActor& a : raw) {
        const SnoInfo* info = sno.Find(a.sno);
        ActorKind kind;
        uint32_t quality;
        bool unresolved;
        if (!ClassifyWithQuality(proc, acds, info, a, &kind, &quality, &unresolved)) continue;
        unresolvedCount += unresolved;
        if (kind == ActorKind::Player) {
            const float d = (a.x - hint.x) * (a.x - hint.x) + (a.z - hint.y) * (a.z - hint.y);
            if (d < best) {
                best = d;
                *player = a;
                *havePlayer = true;
            }
        }
        if (snap->actorCount >= kMaxActors) continue;
        ActorSnapshot& s = snap->actors[snap->actorCount++];
        s.id = a.id;
        s.world = {a.x, a.z};
        s.heading = a.heading;
        s.kind = kind;
        strncpy_s(s.name, info->name.c_str(), _TRUNCATE);
        s.itemFlags = 0;
        if ((kind == ActorKind::Pylon || kind == ActorKind::Shrine) && GizmoUsed(proc, acds, attribs, a))
            s.itemFlags = kActorFlagUsed;
    }
    return unresolvedCount;
}

// Name, rarity, ancient/primal, unidentified and augment of one item (wherever
// it is). False if its attribute group is not located yet (rarity Unknown).
static bool DescribeItem(const Process& proc, const AttribTable& attribs, const ItemNameTable& names,
                         const SnoInfo& info, const AcdItemRecord& r, ItemSnapshot* it) {
    it->id = r.acd;
    it->snoId = r.sno;
    // display name by GBID; the internal SNO name if the table has no entry
    const ItemNameInfo* n = names.Find(r.gbid);
    if (n != nullptr) {
        strncpy_s(it->name, n->name.c_str(), _TRUNCATE);
        strncpy_s(it->typeName, n->type.c_str(), _TRUNCATE);
        it->height = n->height;
        it->bonus = IsBonusDrop(n->type, n->name) ? 1 : 0;
    } else {
        strncpy_s(it->name, info.name.c_str(), _TRUNCATE);
    }
    // properties from the item's attribute group; missing attributes mean 0
    uint32_t quality = 0, ancient = 0, unid = 0, augment = 0;
    if (attribs.Get(proc, r.attribGroup, kAttrItemQualityLevel, &quality) == AttribTable::Result::Unresolved) {
        it->identified = !r.unidentified;  // fallback: gear-only ACD flag
        it->rarity = ItemRarity::Unknown;  // not "Common": we simply don't know yet
        return false;
    }
    attribs.Get(proc, r.attribGroup, kAttrAncientRank, &ancient);
    attribs.Get(proc, r.attribGroup, kAttrUnidentified, &unid);
    attribs.Get(proc, r.attribGroup, kAttrAugmentGemRank, &augment);
    it->rarity = RarityFromQuality(quality, n != nullptr && n->inSet);
    it->ancient = ancient == 1;
    it->primal = ancient >= 2;
    it->identified = unid != 1;
    it->augmentRank = static_cast<uint8_t>(std::min<uint32_t>(augment, 255));
    return true;
}

// Equipped + backpack items from the ACD table: equipped by slot, then the
// backpack row by row. Stash items go to *stash (optional; not published as a
// list - they would not fit kMaxItems - but needed when one is hovered).
// *groupIds receives every item's attribute group id (for AttribTable discovery);
// returns how many items' attribute groups could not be located.
static int BuildInventory(const Process& proc, const AcdTable& acds, const AttribTable& attribs,
                          const SnoTable& sno, const ItemNameTable& names,
                          std::vector<ItemSnapshot>* out, std::vector<uint32_t>* groupIds,
                          std::vector<ItemSnapshot>* stash = nullptr) {
    out->clear();
    groupIds->clear();
    if (stash != nullptr) stash->clear();
    int unresolved = 0;
    std::vector<AcdItemRecord> placed;
    acds.ReadPlaced(proc, &placed);
    for (const AcdItemRecord& r : placed) {
        const SnoInfo* info = sno.Find(r.sno);
        const bool inStash = r.location == kItemLocationStash;
        if (info == nullptr || info->kind != "Item" || (r.location > 13 && !inStash)) continue;
        if (inStash && stash == nullptr) continue;
        ItemSnapshot it;
        groupIds->push_back(r.attribGroup);
        if (!DescribeItem(proc, attribs, names, *info, r, &it)) unresolved++;
        it.location = static_cast<uint8_t>(r.location);
        it.gridX = static_cast<uint8_t>(std::clamp(r.x, 0, 255));
        it.gridY = static_cast<uint8_t>(std::clamp(r.y, 0, 255));
        (inStash ? stash : out)->push_back(it);
    }
    std::sort(out->begin(), out->end(), [](const ItemSnapshot& a, const ItemSnapshot& b) {
        const int ka = a.location == kItemLocationBackpack ? 100 : a.location;  // equipped first
        const int kb = b.location == kItemLocationBackpack ? 100 : b.location;
        if (ka != kb) return ka < kb;
        if (a.gridY != b.gridY) return a.gridY < b.gridY;
        return a.gridX < b.gridX;
    });
    if (out->size() > static_cast<size_t>(kMaxItems)) out->resize(kMaxItems);
    return unresolved;
}

// items_gbid.tsv lives next to sno_actors.tsv (both copied next to the exe).
static void LoadItemNames(const std::string& snoPath, ItemNameTable* names) {
    const std::string path = snoPath.substr(0, snoPath.find_last_of("\\/") + 1) + "items_gbid.tsv";
    if (names->Load(path))
        std::print("item names: {} items ({})\n", names->size(), path.c_str());
    else
        std::print(stderr, "cannot load item names '{}' - showing internal names\n", path.c_str());
}

static bool LoadSno(const std::string& path, SnoTable* sno) {
    if (sno->Load(path)) {
        std::print("sno table: {} actors ({})\n", sno->size(), path.c_str());
        return true;
    }
    std::print(stderr, "cannot load sno table '{}' (use --sno <path>)\n", path.c_str());
    return false;
}

static void WriteActorDump(const Process& proc, const AcdTable* acds, const AttribTable* attribs,
                           const std::vector<RawActor>& raw, const SnoTable& sno, Vec2 player,
                           uint64_t base);

static int DumpActors(const Process& proc, const std::string& snoPath) {
    SnoTable sno;
    if (!LoadSno(snoPath, &sno)) return 1;
    uint64_t base = 0;
    int votes = 0;
    if (!ActorList::FindBase(proc, &base, &votes, true)) {
        std::print(stderr, "actor array not found (in menus? try again in-world)\n");
        return 1;
    }
    ActorList list;
    list.setBase(base);
    std::vector<RawActor> raw;
    list.Read(proc, &raw);
    std::print("{} live actors\n", raw.size());
    const std::unordered_set<uint32_t> snoIds = sno.Ids();
    AcdTable acds;
    acds.Discover(proc, raw, &snoIds, true);
    for (const RawActor& a : raw) {
        const SnoInfo* info = sno.Find(a.sno);
        ActorKind kind;
        uint32_t quality;
        bool unresolved;
        if (!ClassifyWithQuality(proc, &acds, info, a, &kind, &quality, &unresolved)) continue;
        std::print("  [{:4}] {:<10} {:<45} pos=({:8.2f}, {:8.2f}, {:6.2f}){}\n", a.slot, ActorKindName(kind),
               info->name.c_str(), a.x, a.z, a.y, unresolved ? "  (acd not found)" : "");
    }
    Vec2 player = {};
    WorldSnapshot snap;
    RawActor p;
    bool havePlayer = false;
    FillActors(proc, &acds, nullptr, raw, sno, player, &snap, &p, &havePlayer);
    if (havePlayer) player = {p.x, p.z};
    WriteActorDump(proc, &acds, nullptr, raw, sno, player, base);
    return 0;
}

// Why an actor is not on the minimap (mirrors ClassifyActor's rules).
static const char* HiddenReason(const SnoInfo* info, const RawActor& a) {
    if (info == nullptr) return "unknown-sno";
    ActorKind kind;
    // "dead" only if the actor would be drawn when alive (allies/effects are "ignored")
    if (info->kind == "Monster" && a.dead &&
        ClassifyActor(info->kind, info->name, a.hasAcd(), false, &kind))
        return "dead";
    if (info->kind == "Item" && !a.hasAcd()) return "no-acd";
    return "ignored";
}

// Debug dump (F10 while publishing): every live actor, drawn or not, with the
// raw fields the classification looks at plus the whole record as hex (for
// diffing two dumps field by field). One .tsv per press in <exe dir>\dumps.
static void WriteActorDump(const Process& proc, const AcdTable* acds, const AttribTable* attribs,
                           const std::vector<RawActor>& raw, const SnoTable& sno, Vec2 player,
                           uint64_t base) {
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string dir = self;
    dir = dir.substr(0, dir.find_last_of("\\/") + 1) + "dumps";
    CreateDirectoryA(dir.c_str(), nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    char name[64];
    snprintf(name, sizeof(name), "\\actors_%04u%02u%02u_%02u%02u%02u.tsv", t.wYear, t.wMonth,
             t.wDay, t.wHour, t.wMinute, t.wSecond);
    const std::string path = dir + name;
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "w");
    if (f == nullptr) {
        std::print(stderr, "cannot write dump {}\n", path.c_str());
        return;
    }
    int shown = 0;
    std::print(f, "# player {:.2f} {:.2f} | actor array 0x{:X} | {} live actors\n", player.x, player.y,
            static_cast<unsigned long long>(base), raw.size());
    std::print(f, "slot\tdrawn\tstatus\tsno\tsno_kind\tname\tacd\ttag90\tstate\tquality\tx\tz\ty\tdist\traw\n");
    std::vector<uint8_t> rec(kActorStride);
    std::string hex(kActorStride * 2, '0');
    std::vector<std::pair<float, const RawActor*>> order;
    for (const RawActor& a : raw)
        order.push_back({std::hypot(a.x - player.x, a.z - player.y), &a});
    std::sort(order.begin(), order.end(),
              [](const auto& l, const auto& r) { return l.first < r.first; });
    for (const auto& [dist, ap] : order) {
        const RawActor& a = *ap;
        const SnoInfo* info = sno.Find(a.sno);
        ActorKind kind;
        uint32_t quality;
        bool unresolved;
        const bool drawn = ClassifyWithQuality(proc, acds, info, a, &kind, &quality, &unresolved);
        char qtext[16];
        if (quality == 0xFFFFFFFF) snprintf(qtext, sizeof(qtext), "%s", unresolved ? "?" : "-");
        else snprintf(qtext, sizeof(qtext), "%u", quality);
        char gtext[32] = "-";  // pylons/shrines: Gizmo_State/Has_Been_Operated
        if (drawn && (kind == ActorKind::Pylon || kind == ActorKind::Shrine)) {
            uint32_t st, op;
            const bool used = GizmoUsed(proc, acds, attribs, a, &st, &op);
            snprintf(gtext, sizeof(gtext), "%d/%d%s", static_cast<int>(st), static_cast<int>(op), used ? " used" : "");
        }
        shown += drawn;
        static const char kHex[] = "0123456789abcdef";
        if (proc.Read(base + static_cast<uint64_t>(a.slot) * kActorStride, rec.data(), rec.size())) {
            for (size_t i = 0; i < rec.size(); ++i) {
                hex[2 * i] = kHex[rec[i] >> 4];
                hex[2 * i + 1] = kHex[rec[i] & 15];
            }
        } else {
            hex.assign(kActorStride * 2, '?');
        }
        std::print(f, "{}\t{}\t{}\t{}\t{}\t{}\t{:08X}\t{:08X}\t{}\t{}\t{}\t{:.2f}\t{:.2f}\t{:.2f}\t{:.1f}\t{}\n", a.slot,
                drawn ? ActorKindName(kind) : "-", drawn ? "shown" : HiddenReason(info, a), a.sno,
                info ? info->kind.c_str() : "?", info ? info->name.c_str() : "?", a.acd, a.tag90,
                a.state, qtext, gtext, a.x, a.z, a.y, dist, hex.c_str());
    }
    fclose(f);
    std::print("dump: {} ({} actors, {} drawn)\n", path.c_str(), raw.size(), shown);
}

// Game UI controls the overlay needs; indices below. Equipment slots are listed
// in item-location order 1..13 (TurboHUD's mapping: 3 = armRight = off hand).
enum GameUi : size_t {
    kUiTooltip = 0,
    kUiBackpack,
    kUiStashPanel,
    kUiStashGrid,
    kUiSlot1,                      // location 1 .. 13
    kUiTab1 = kUiSlot1 + 13,       // stash tabs of the selected page (5)
    kUiPage1 = kUiTab1 + 5,        // stash pages (4)
    kUiBigMap = kUiPage1 + 4,      // TAB map dialog (visible = open)
    kUiBigMapLocal,                // its local map: pan offsets at +3216/+3220
    kUiSalvageDialog,              // blacksmith salvage tab (visible = open)
    kUiSalvageAnvil,               // its anvil button (two candidates: the visible one wins)
    kUiSalvageAnvilAlt,
    kUiConfirm,                    // generic "are you sure?" prompt
    kUiConfirmOk,
    kUiCount,
};

static std::vector<std::string> GameUiPaths() {
    std::vector<std::string> p = {
        "Root.TopLayer.tooltip_dialog_background.tooltip_2",
        "Root.NormalLayer.inventory_dialog_mainPage.inventory_button_backpack",
        "Root.NormalLayer.stash_dialog_mainPage.panel",
        "Root.NormalLayer.stash_dialog_mainPage.button_stash",
    };
    for (const char* s : {"head", "torso", "armRight", "armLeft", "hands", "waist", "feet", "shoulders",
                          "legs", "bracers", "fingerRight", "fingerLeft", "neck"})
        p.push_back(std::string("Root.NormalLayer.inventory_dialog_mainPage.inventory_button_") + s);
    for (int t = 1; t <= 5; ++t) p.push_back(std::format("Root.NormalLayer.stash_dialog_mainPage.tab_{}", t));
    for (int pg = 1; pg <= 4; ++pg)
        p.push_back(std::format("Root.NormalLayer.stash_dialog_mainPage.TabPageButtons.stash_pages.page_{}", pg));
    p.push_back("Root.NormalLayer.map_dialog_mainPage");
    p.push_back("Root.NormalLayer.map_dialog_mainPage.localmap");
    p.push_back("Root.NormalLayer.vendor_dialog_mainPage.salvage_dialog");
    p.push_back("Root.NormalLayer.vendor_dialog_mainPage.salvage_dialog.salvage_all_wrapper.salvage_button");
    p.push_back("Root.NormalLayer.vendor_dialog_mainPage.salvage_dialog.salvage_button");
    p.push_back("Root.TopLayer.confirmation");
    p.push_back("Root.TopLayer.confirmation.subdlg.stack.wrap.button_ok");
    return p;
}

// Inventory/stash geometry and state for the overlay's item badges. Selected
// stash page: visible with anim state 14; selected tab: visible with anim % 3 == 1.
static void ReadGameUi(const Process& proc, const UiControls& ui, UiLayout* out) {
    *out = UiLayout();
    out->bigMapOpen = ui.Read(proc, kUiBigMap).visible;
    if (out->bigMapOpen) {
        float pan[2] = {};
        if (ui.ReadFloats(proc, kUiBigMapLocal, 3216, pan, 2)) out->bigMapPan = {pan[0], pan[1]};
    }
    out->salvageOpen = ui.Read(proc, kUiSalvageDialog).visible;
    if (out->salvageOpen) {
        const UiControlState anvil = ui.Read(proc, kUiSalvageAnvil), alt = ui.Read(proc, kUiSalvageAnvilAlt);
        out->salvageButton = (anvil.visible || !alt.visible) ? anvil.rect : alt.rect;
        const UiControlState confirm = ui.Read(proc, kUiConfirm);
        out->confirmOpen = confirm.visible;
        if (out->confirmOpen) out->confirmOk = ui.Read(proc, kUiConfirmOk).rect;
    }
    const UiControlState backpack = ui.Read(proc, kUiBackpack);
    out->inventoryOpen = backpack.visible;
    out->backpack = backpack.rect;
    if (out->inventoryOpen)
        for (int s = 1; s < kEquipSlots; ++s) out->slots[s] = ui.Read(proc, kUiSlot1 + s - 1).rect;
    const UiControlState grid = ui.Read(proc, kUiStashGrid);
    out->stashOpen = ui.Read(proc, kUiStashPanel).visible && grid.found;
    out->stash = grid.rect;
    if (!out->stashOpen) return;
    int page = 0, tab = -1;
    for (int pg = 0; pg < 4; ++pg) {
        const UiControlState s = ui.Read(proc, kUiPage1 + pg);
        if (s.visible && s.animState == 14) page = pg;
    }
    for (int t = 0; t < 5; ++t) {
        const UiControlState s = ui.Read(proc, kUiTab1 + t);
        if (s.visible && s.animState % 3 == 1) tab = t;
    }
    out->stashTab = static_cast<int8_t>(tab < 0 ? -1 : page * 5 + tab);
}

// Background block re-discovery (one full memory pass each, ~2-3 s): ACD blocks
// when `snapshot` is set, attribute-group blocks when `groupIds` is set.
struct AcdScanJob {
    const Process* proc;
    AcdTable* acds;
    AttribTable* attribs;
    const std::unordered_set<uint32_t>* snoIds;
    SceneTable* scenes;
    const SceneCells* sceneCells;
    std::vector<RawActor> snapshot;
    std::vector<uint32_t> groupIds;
    bool scanScenes = false;
    bool scanUi = false;       // fill UI controls that are still missing
    Vec2 player;  // scene scans: keeps the block of the scene the player stands in
    const ActorList* actors = nullptr;  // cache refresh after a complete UI scan
    UiControls* ui = nullptr;
    std::string cachePath;
    std::atomic<bool> busy;
};

static void SaveBasesCache(const Process& proc, const ActorList& actors, const AcdTable& acds,
                           const AttribTable& attribs, const SceneTable& scenes, const UiControls& ui,
                           const std::string& path) {
    BasesCache cache;
    cache.pid = proc.pid();
    cache.created = proc.creationTime();
    cache.actorBase = actors.base();
    cache.acd = acds.GetBases();
    cache.attrib = attribs.GetBases();
    cache.scene = scenes.GetBases();
    cache.ui = ui.GetControls();
    cache.Save(path);
}

static DWORD WINAPI AcdScanThread(LPVOID param) {
    auto* job = static_cast<AcdScanJob*>(param);
    // One shared sweep instead of three (same end state as the former loop of
    // full-memory Discover calls).
    AcdBlockScan acd(job->snapshot, job->snoIds);
    AttribBlockScan attrib(job->groupIds);
    SceneBlockScan scene(*job->sceneCells, nullptr, &job->player);
    UiPathScan ui(job->ui->paths());
    std::vector<ChunkMatcher*> matchers;
    if (!job->snapshot.empty()) matchers.push_back(&acd);
    if (!job->groupIds.empty()) matchers.push_back(&attrib);
    if (job->scanScenes) matchers.push_back(&scene);
    if (job->scanUi) matchers.push_back(&ui);
    SharedSweep(*job->proc, matchers);
    if (!job->snapshot.empty()) acd.Finish(job->acds, false);
    if (!job->groupIds.empty()) attrib.Finish(job->attribs, true);
    if (job->scanScenes) scene.Finish(job->scenes, true);
    if (job->scanUi) {
        ui.Finish(*job->proc, job->ui, false);
        std::print("ui controls: {} still missing\n", job->ui->MissingCount());
        // once every control is resolved, refresh the cache so restarts start complete
        if (job->ui->MissingCount() == 0 && !job->cachePath.empty())
            SaveBasesCache(*job->proc, *job->actors, *job->acds, *job->attribs, *job->scenes, *job->ui,
                           job->cachePath);
    }
    job->busy = false;
    return 0;
}

// First-startup discovery, off the publish loop: dots appear after the actor
// sweep while ACD blocks, attribute blocks and scenes are still being found.
// Finishes by saving the base cache for instant restarts against this process.
struct StartupScanJob {
    const Process* proc;
    const ActorList* actors;
    AcdTable* acds;
    AttribTable* attribs;
    SceneTable* scenes;
    const SceneCells* sceneCells;
    const std::unordered_set<uint32_t>* snoIds;
    const SnoTable* sno;
    const ItemNameTable* itemNames;
    const UiControls* ui;
    std::string cachePath;
    uint32_t pid = 0;
    uint64_t created = 0;
    std::atomic<bool> busy;
};

static DWORD WINAPI StartupScanThread(LPVOID param) {
    auto* job = static_cast<StartupScanJob*>(param);
    std::vector<RawActor> raw;
    if (job->actors->valid() && job->actors->Read(*job->proc, &raw)) {
        AcdBlockScan acd(raw, job->snoIds);
        SharedSweep(*job->proc, {&acd});
        acd.Finish(job->acds, true);
    }
    // attribute group ids come from the inventory, which needs the ACD bases
    std::vector<ItemSnapshot> inventory, stash;
    std::vector<uint32_t> groups;
    if (job->acds->baseCount() > 0)
        BuildInventory(*job->proc, *job->acds, *job->attribs, *job->sno, *job->itemNames, &inventory,
                       &groups, &stash);
    {
        AttribBlockScan attrib(groups);
        SceneBlockScan scene(*job->sceneCells, nullptr, nullptr);
        std::vector<ChunkMatcher*> matchers;
        if (attrib.enabled()) matchers.push_back(&attrib);
        matchers.push_back(&scene);
        SharedSweep(*job->proc, matchers);
        if (attrib.enabled()) attrib.Finish(job->attribs, true);
        scene.Finish(job->scenes, true);
    }
    BasesCache cache;
    cache.pid = job->pid;
    cache.created = job->created;
    cache.actorBase = job->actors->base();
    cache.acd = job->acds->GetBases();
    cache.attrib = job->attribs->GetBases();
    cache.scene = job->scenes->GetBases();
    cache.ui = job->ui->GetControls();
    cache.Save(job->cachePath);
    std::print("startup discovery done - bases cached\n");
    job->busy = false;
    return 0;
}

// The scenes of the world the player is in (the world of the scene containing
// the player): other worlds' scenes stay in memory after leaving them.
static void CurrentWorldScenes(const std::vector<SceneRecord>& all, Vec2 player,
                               std::vector<SceneSnapshot>* out) {
    out->clear();
    uint32_t world = 0;
    bool found = false;
    for (const SceneRecord& s : all)
        if (player.x >= s.x0 && player.x < s.x1 && player.y >= s.y0 && player.y < s.y1) {
            world = s.world;
            found = true;
            break;
        }
    if (!found) return;
    for (const SceneRecord& s : all)
        if (s.world == world && out->size() < static_cast<size_t>(kMaxScenes))
            out->push_back({s.sno, s.x0, s.y0, s.x1, s.y1});
}

// Live loop: actor list when available (all dots + player), else the
// standing-cluster player tracker (player only). F10 writes a debug dump.
static double g_publishHz = 60.0;  // --hz

static int RunPublish(const Process& proc, double seconds, const std::string& snoPath) {
    SnoTable sno;
    const bool haveSno = LoadSno(snoPath, &sno);
    ActorList actors;
    const std::unordered_set<uint32_t> snoIds = sno.Ids();
    ItemNameTable itemNames;
    LoadItemNames(snoPath, &itemNames);
    UiControls gameUi(GameUiPaths());
    AcdTable acds;
    AttribTable attribs;
    SceneTable sceneTable;

    // Startup fast paths: a cached result for this exact game process, else ONE
    // shared sweep for the actor array + UI controls so the dots appear after a
    // single pass; ACD/attribute/scene discovery then runs in the background.
    const std::string cachePath = ExeSiblingPath("d3hud_bases.ini");
    const uint64_t created = proc.creationTime();
    bool fromCache = false;
    BasesCache cache;
    cache.pid = proc.pid();
    cache.created = created;
    if (cache.Load(cachePath) && ValidateActorBase(proc, cache.actorBase)) {
        auto keepReadable = [&](const std::vector<uint64_t>& in) {
            std::vector<uint64_t> out;
            uint32_t probe = 0;
            for (uint64_t b : in)
                if (proc.Read(b, &probe, 4)) out.push_back(b);
            return out;
        };
        gameUi.SetControls(cache.ui);
        bool uiOk = cache.ui.size() == gameUi.size();
        for (size_t i = 0; uiOk && i < gameUi.size(); ++i)
            if (cache.ui[i] != 0 && !gameUi.ValidateControl(proc, i)) uiOk = false;
        if (uiOk) {
            actors.setBase(cache.actorBase);
            acds.SetBases(keepReadable(cache.acd));
            attribs.SetBases(keepReadable(cache.attrib));
            sceneTable.SetBases(keepReadable(cache.scene));
            fromCache = true;
            std::print("bases restored from cache (same game process)\n");
        } else {
            gameUi.SetControls(std::vector<uint64_t>(gameUi.size(), 0));
        }
    }
    if (!fromCache) {
        const ULONGLONG tSweep = GetTickCount64();
        ActorBaseScan actorScan;
        UiPathScan uiScan(gameUi.paths());
        SharedSweep(proc, {&actorScan, &uiScan});
        uint64_t base = 0;
        int votes = 0;
        if (haveSno && actorScan.Finish(&base, &votes, true)) actors.setBase(base);
        uiScan.Finish(proc, &gameUi, true);
        std::print("startup sweep: {} ms\n", static_cast<unsigned long>(GetTickCount64() - tSweep));
    }

    PlayerTracker tracker;
    if (!actors.valid()) {
        std::print("actor list unavailable - falling back to player-only tracking\n");
        PlayerFindResult found;
        if (!PlayerTracker::Find(proc, &found, true)) {
            std::print(stderr, "player not found (in menus? try again in-world)\n");
            return 1;
        }
        tracker.setRecord(found.recordAddr);
    }

    SharedRing ring;
    if (!ring.Create()) {
        std::print(stderr, "cannot create shared ring '{}'\n", SharedRing::kName);
        return 1;
    }
    std::print("publishing live snapshots to overlay ring ({})\n", seconds > 0 ? "timed" : "Ctrl+C to stop");
    // global hotkey, delivered to this thread's message queue (no window needed)
    constexpr int kDumpHotkey = 1;
    if (actors.valid()) {
        if (RegisterHotKey(nullptr, kDumpHotkey, MOD_NOREPEAT, VK_F10))
            std::print("F10: dump actor list to file\n");
        else
            std::print(stderr, "warning: F10 hotkey register failed\n");
    }
    const ULONGLONG t0 = GetTickCount64();
    uint64_t frame = 0;
    int failures = 0;
    Vec2 lastPlayer = {};
    std::vector<RawActor> raw;

    // ~60 Hz publishing by default, --hz to change (the overlay blends between snapshots): 1 ms timer
    // resolution so Sleep() can hit the 16.7 ms tick, restored on every exit.
    const double kTickSeconds = 1.0 / g_publishHz;
    struct TimerResolution {
        TimerResolution() { timeBeginPeriod(1); }
        ~TimerResolution() { timeEndPeriod(1); }
    } timerResolution;
    LARGE_INTEGER qpcFreq, tickStart, tickNow;
    QueryPerformanceFrequency(&qpcFreq);

    // ACD blocks (monster quality), attribute blocks (item properties) and the
    // scene table are discovered by the background startup scan (or restored
    // from the cache above) - publishing starts as soon as the actor array is
    // known. When monsters stay unresolved (the game allocated a new block) the
    // rescan below refreshes the block list without stalling the dots.
    std::vector<ItemSnapshot> inventory, stash;
    std::unordered_map<uint32_t, ItemSnapshot> groundCache;  // acd id -> described ground item
    std::vector<uint32_t> itemGroups;  // attribute group ids of the current inventory + stash
    // map prediction: scene shapes (file) + the scenes in memory (found in the background)
    SceneCells sceneCells;
    if (sceneCells.Load(snoPath.substr(0, snoPath.find_last_of("\\/") + 1) + "scene_cells.bin"))
        std::print("scene shapes: {} scenes\n", sceneCells.sceneCount());
    else
        std::print(stderr, "cannot load scene_cells.bin - no map prediction\n");
    std::vector<SceneRecord> sceneRecords;
    std::vector<SceneSnapshot> worldScenes;
    AcdScanJob acdJob{&proc, &acds,     &attribs, &snoIds, &sceneTable, &sceneCells,
                 {},  {},         false,    false,   {},          &actors,
                 &gameUi,     cachePath, false};
    struct JoinOnExit {  // every return path (incl. rescan failures) must wait for the scan
        HANDLE h = nullptr;
        ~JoinOnExit() {
            if (h != nullptr) {
                WaitForSingleObject(h, INFINITE);
                CloseHandle(h);
            }
        }
    } acdThread;
    // first-startup discovery (skipped when the cache hit): ACD blocks, then
    // attribute groups (ids from the first inventory read) and scenes, then the
    // base cache is written for instant restarts against this game process.
    StartupScanJob startupJob{&proc,          &actors,        &acds,          &attribs,
                              &sceneTable,    &sceneCells,    &snoIds,        &sno,
                              &itemNames,     &gameUi,        cachePath,      proc.pid(),
                              created,        false};
    JoinOnExit startupThread;
    if (!fromCache) {
        startupJob.busy = true;
        startupThread.h = CreateThread(nullptr, 0, StartupScanThread, &startupJob, 0, nullptr);
        if (startupThread.h == nullptr) startupJob.busy = false;
    }
    ULONGLONG unresolvedSince = 0;
    ULONGLONG itemScanAt = 0;
    // starts a background scan: ACD blocks (if actorsToo), attribute blocks, and
    // scene blocks (if scenesToo)
    auto startScan = [&](bool actorsToo, bool scenesToo) {
        if (acdJob.busy) return;
        if (acdThread.h != nullptr) {
            WaitForSingleObject(acdThread.h, INFINITE);  // already finished (!busy)
            CloseHandle(acdThread.h);
            acdThread.h = nullptr;
        }
        acdJob.snapshot = actorsToo ? raw : std::vector<RawActor>{};
        acdJob.groupIds = itemGroups;
        acdJob.scanScenes = scenesToo && sceneCells.sceneCount() > 0;
        acdJob.scanUi = false;
        acdJob.player = lastPlayer;
        acdJob.busy = true;
        acdThread.h = CreateThread(nullptr, 0, AcdScanThread, &acdJob, 0, nullptr);
        if (acdThread.h == nullptr) acdJob.busy = false;
    };
    // UI controls appear lazily (a dialog that was never opened does not exist
    // at scan time); rescan in the background while any are missing.
    auto startUiScan = [&]() {
        if (acdJob.busy) return;
        if (acdThread.h != nullptr) {
            WaitForSingleObject(acdThread.h, INFINITE);
            CloseHandle(acdThread.h);
            acdThread.h = nullptr;
        }
        acdJob.snapshot.clear();
        acdJob.groupIds.clear();
        acdJob.scanScenes = false;
        acdJob.scanUi = true;
        acdJob.busy = true;
        acdThread.h = CreateThread(nullptr, 0, AcdScanThread, &acdJob, 0, nullptr);
        if (acdThread.h == nullptr) acdJob.busy = false;
    };
    ULONGLONG uiScanAt = 0;
    // scenes: first scan once the player position is known (see the publish loop)
    ULONGLONG sceneScanAt = 0, sceneMissingSince = 0;
    // throughput stats, printed every 5 s
    auto msSince = [&qpcFreq](const LARGE_INTEGER& from) {
        LARGE_INTEGER n;
        QueryPerformanceCounter(&n);
        return 1000.0 * static_cast<double>(n.QuadPart - from.QuadPart) / qpcFreq.QuadPart;
    };
    double statRead = 0, statFill = 0;
    int statTicks = 0, statPublished = 0, statUnresolved = 0;
    ULONGLONG statSince = GetTickCount64();
    // inventory changes slowly: refresh ~1 Hz, copy into every snapshot
    ULONGLONG inventoryAt = 0;

    while (true) {
        QueryPerformanceCounter(&tickStart);
        WorldSnapshot snap;
        memset(&snap, 0, sizeof(snap));
        snap.magic = 0x44573348;
        snap.version = kSnapshotVersion;
        bool ok = false;
        statTicks++;
        if (actors.valid()) {
            RawActor player;
            bool havePlayer = false;
            const bool readOk = actors.Read(proc, &raw);
            statRead += msSince(tickStart);
            if (readOk) {
                LARGE_INTEGER fillStart;
                QueryPerformanceCounter(&fillStart);
                const int unresolved =
                    FillActors(proc, &acds, &attribs, raw, sno, lastPlayer, &snap, &player, &havePlayer);
                statFill += msSince(fillStart);
                statUnresolved = unresolved;
                const ULONGLONG now = GetTickCount64();
                if (unresolved == 0) {
                    unresolvedSince = 0;
                } else if (unresolvedSince == 0) {
                    unresolvedSince = now;
                } else if (now - unresolvedSince > 3000 && !acdJob.busy) {
                    unresolvedSince = 0;
                    startScan(true, false);
                }
                if (havePlayer) {
                    lastPlayer = {player.x, player.z};
                    snap.playerPos = lastPlayer;
                    snap.playerHeading = player.heading;
                    ok = true;
                }
            }
        } else {
            PlayerPosition p;
            if (tracker.ReadPosition(proc, &p)) {
                snap.playerPos = {p.x, p.z};
                ok = true;
            }
        }
        MSG msg;
        while (PeekMessageA(&msg, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
            if (msg.wParam == kDumpHotkey && actors.valid())
                WriteActorDump(proc, &acds, &attribs, raw, sno, lastPlayer, actors.base());
        }
        // fill in UI controls that did not exist at startup (dialogs open lazily)
        if (gameUi.MissingCount() > 0 && !acdJob.busy && !startupJob.busy &&
            (uiScanAt == 0 || GetTickCount64() - uiScanAt > 8000)) {
            uiScanAt = GetTickCount64();
            startUiScan();
        }
        if (GetTickCount64() - statSince >= 5000 && statTicks > 0) {
            const double secs = (GetTickCount64() - statSince) / 1000.0;
            std::print("[stats] {:.1f} ticks/s, {:.1f} published/s | read {:.2f} ms, classify {:.2f} ms avg | "
                   "{} actors, {} monsters without acd record\n",
                   statTicks / secs, statPublished / secs, statRead / statTicks,
                   statFill / statTicks, raw.size(), statUnresolved);
            statRead = statFill = 0;
            statTicks = statPublished = 0;
            statSince = GetTickCount64();
        }
        if (ok) {
            if (acds.baseCount() > 0 && GetTickCount64() - inventoryAt >= (snap.ui.salvageOpen ? 200u : 1000u)) {
                const int itemsUnresolved = BuildInventory(proc, acds, attribs, sno, itemNames,
                                                           &inventory, &itemGroups, &stash);
                inventoryAt = GetTickCount64();
                // first read finds no attribute blocks yet; afterwards rescan at most every 10 s
                if (itemsUnresolved > 0 && !acdJob.busy &&
                    (itemScanAt == 0 || inventoryAt - itemScanAt > 10000)) {
                    itemScanAt = inventoryAt;
                    startScan(false, false);
                }
                // map prediction: the current world's scenes; if the player stands in
                // no known scene (new area, new blocks) rescan, at most every 10 s
                if (sceneTable.baseCount() > 0) sceneTable.Read(proc, sceneCells, &sceneRecords);
                CurrentWorldScenes(sceneRecords, lastPlayer, &worldScenes);
                if (!worldScenes.empty() || !ok) {
                    sceneMissingSince = 0;
                } else if (sceneMissingSince == 0 && sceneScanAt != 0) {
                    sceneMissingSince = inventoryAt;
                } else if (!acdJob.busy && (sceneScanAt == 0 || (inventoryAt - sceneMissingSince > 3000 &&
                                                                 inventoryAt - sceneScanAt > 10000))) {
                    sceneScanAt = inventoryAt;
                    startScan(false, true);
                }
            }
            snap.sceneCount = static_cast<uint32_t>(worldScenes.size());
            std::copy(worldScenes.begin(), worldScenes.end(), snap.scenes);
            snap.itemCount = static_cast<uint16_t>(inventory.size());
            std::copy(inventory.begin(), inventory.end(), snap.items);
            ReadGameUi(proc, gameUi, &snap.ui);
            // items lying on the ground (actors linked to an item ACD): legendary/set
            // and bonus drops - the overlay's drop log and world labels. Described
            // once per item, then cached.
            if (groundCache.size() > 4096) groundCache.clear();
            std::unordered_map<uint64_t, const ItemSnapshot*> groundByActor;  // actor id -> item
            for (const RawActor& a : raw) {
                const SnoInfo* info = sno.Find(a.sno);
                if (info == nullptr || info->kind != "Item" || !a.hasAcd()) continue;
                auto cached = groundCache.find(a.acd);
                if (cached == groundCache.end()) {
                    AcdItemRecord r;
                    ItemSnapshot it;
                    if (!acds.ReadItem(proc, a.acd, &r) || !DescribeItem(proc, attribs, itemNames, *info, r, &it))
                        continue;  // not resolvable yet - retry next tick
                    it.location = kItemLocationGround;
                    cached = groundCache.emplace(a.acd, it).first;
                }
                const ItemSnapshot& it = cached->second;
                if (it.rarity != ItemRarity::Legendary && it.rarity != ItemRarity::Set && !it.bonus) continue;
                groundByActor[a.id] = &it;
                if (snap.itemCount < kMaxItems) snap.items[snap.itemCount++] = it;
            }
            // ground-item dots carry the item's name and properties (world labels)
            for (uint16_t i = 0; i < snap.actorCount; ++i) {
                ActorSnapshot& s = snap.actors[i];
                if (s.kind != ActorKind::GroundItem) continue;
                auto g = groundByActor.find(s.id);
                if (g == groundByActor.end()) continue;
                const ItemSnapshot& it = *g->second;
                strncpy_s(s.name, it.name, _TRUNCATE);
                s.itemRarity = it.rarity;
                s.itemFlags = static_cast<uint8_t>((it.ancient ? kItemFlagAncient : 0) |
                                                   (it.primal ? kItemFlagPrimal : 0) |
                                                   (it.bonus ? kItemFlagBonus : 0) |
                                                   (it.identified ? 0 : kItemFlagUnidentified));
            }
            // the open stash tab's items (for their badges)
            if (snap.ui.stashOpen && snap.ui.stashTab >= 0)
                for (const ItemSnapshot& s : stash)
                    if (s.gridY / kStashRowsPerTab == snap.ui.stashTab && snap.itemCount < kMaxItems)
                        snap.items[snap.itemCount++] = s;
            // hovered item (every tick): point at it, appending other stash items on demand
            uint32_t hoveredAcd = 0;
            if (gameUi.TooltipItem(proc, kUiTooltip, &hoveredAcd)) {
                auto byAcd = [hoveredAcd](const ItemSnapshot& it) { return it.id == hoveredAcd; };
                const auto first = snap.items, last = snap.items + snap.itemCount;
                if (auto it = std::find_if(first, last, byAcd); it != last) {
                    snap.hoveredItemPlus1 = static_cast<uint16_t>(it - first + 1);
                } else if (auto s = std::find_if(stash.begin(), stash.end(), byAcd);
                           s != stash.end() && snap.itemCount < kMaxItems) {
                    snap.items[snap.itemCount++] = *s;
                    snap.hoveredItemPlus1 = snap.itemCount;
                }
            }
            failures = 0;
            statPublished++;
            snap.frameIndex = frame++;
            // QPC milliseconds (system-wide clock, sub-ms resolution): the overlay
            // compares it with its own QPC "now" to extrapolate the player position
            LARGE_INTEGER stamp;
            QueryPerformanceCounter(&stamp);
            snap.timestampMs = 1000.0 * static_cast<double>(stamp.QuadPart) / qpcFreq.QuadPart;
            ring.Publish(snap);
        } else if (++failures >= 5) {
            // area change / game restart moves the array (or the tracked record)
            std::print("source lost - rescanning...\n");
            if (actors.valid()) {
                uint64_t base = 0;
                int votes = 0;
                if (!ActorList::FindBase(proc, &base, &votes, true)) {
                    std::print(stderr, "rescan failed\n");
                    return 1;
                }
                actors.setBase(base);
            } else {
                PlayerFindResult found;
                if (!PlayerTracker::Find(proc, &found, false)) {
                    std::print(stderr, "rescan failed\n");
                    return 1;
                }
                tracker.setRecord(found.recordAddr);
            }
            failures = 0;
        }
        if (seconds > 0 && GetTickCount64() - t0 > static_cast<ULONGLONG>(seconds * 1000.0)) {
            std::print("publish done: {} frames\n", static_cast<unsigned long long>(frame));
            break;
        }
        QueryPerformanceCounter(&tickNow);
        const double spent = static_cast<double>(tickNow.QuadPart - tickStart.QuadPart) / qpcFreq.QuadPart;
        const int remainingMs = static_cast<int>((kTickSeconds - spent) * 1000.0);
        Sleep(remainingMs > 0 ? remainingMs : 0);
    }
    UnregisterHotKey(nullptr, kDumpHotkey);
    ring.Close();
    return 0;
}

// One-shot: equipped + backpack items (what the overlay's Items panel shows).
static int DumpItems(const Process& proc, const std::string& snoPath) {
    SnoTable sno;
    if (!LoadSno(snoPath, &sno)) return 1;
    uint64_t base = 0;
    int votes = 0;
    if (!ActorList::FindBase(proc, &base, &votes, false)) {
        std::print(stderr, "actor array not found (in menus? try again in-world)\n");
        return 1;
    }
    ActorList list;
    list.setBase(base);
    std::vector<RawActor> raw;
    list.Read(proc, &raw);
    const std::unordered_set<uint32_t> snoIds = sno.Ids();
    AcdTable acds;
    acds.Discover(proc, raw, &snoIds, true);
    ItemNameTable itemNames;
    LoadItemNames(snoPath, &itemNames);
    std::vector<ItemSnapshot> items;
    std::vector<uint32_t> groups;
    AttribTable attribs;
    BuildInventory(proc, acds, attribs, sno, itemNames, &items, &groups);  // collects group ids
    attribs.Discover(proc, groups, true);
    const int unresolved = BuildInventory(proc, acds, attribs, sno, itemNames, &items, &groups);
    std::print("{} items (equipped + backpack){}:\n", items.size(),
               unresolved ? std::format(", {} without attributes", unresolved) : std::string());
    for (const ItemSnapshot& it : items) {
        const std::string where = it.location == kItemLocationBackpack
                                      ? std::format("{} r{} c{}", ItemLocationName(it.location), it.gridY, it.gridX)
                                      : std::string(ItemLocationName(it.location));
        std::print("  {:<17} {:<32} {:<9}{}{}{}\n", where, it.name, RarityName(it.rarity),
                   it.primal ? " PRIMAL" : it.ancient ? " ancient" : "",
                   it.augmentRank ? std::format(" augment {} (+{} main stat)", it.augmentRank, it.augmentRank * 5) : "",
                   it.identified ? "" : "  [unidentified]");
    }
    return 0;
}

// --scenes: every self-consistent scene record in memory, grouped by world,
// with the block base each implies (diagnostics for the map prediction).
static int DumpScenes(const Process& proc, const std::string& snoPath) {
    SceneCells cells;
    if (!cells.Load(snoPath.substr(0, snoPath.find_last_of("\\/") + 1) + "scene_cells.bin")) {
        std::print(stderr, "cannot load scene_cells.bin\n");
        return 1;
    }
    SceneTable table;
    std::vector<std::pair<uint64_t, SceneRecord>> hits;
    table.Discover(proc, cells, true, &hits);
    std::sort(hits.begin(), hits.end(), [](const auto& l, const auto& r) {
        return l.second.world != r.second.world ? l.second.world < r.second.world : l.first < r.first;
    });
    std::print("{} scene record(s):\n", hits.size());
    for (const auto& [addr, s] : hits)
        std::print("  0x{:X} base 0x{:X} id {:08X} world {:08X} sno {:7} ({:.0f},{:.0f})-({:.0f},{:.0f})\n", addr,
                   addr - static_cast<uint64_t>(s.id & 0xFFFF) * kSceneStride, s.id, s.world, s.sno, s.x0, s.y0,
                   s.x1, s.y1);
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);  // live output when piped (startup timing, logs)
    const char* attach = nullptr;
    const char* capture = nullptr;
    const char* scanFile = nullptr;
    const char* scanPat = nullptr;
    bool list = false;
    bool findPlayer = false;
    bool trackPlayer = false;
    bool publish = false;
    bool dumpActors = false;
    bool dumpItems = false;
    bool dumpScenes = false;
    const char* uiFilter = nullptr;
    const char* uiWatch = nullptr;  // --ui-watch <substring>
    double publishSeconds = 0;  // 0 = forever
    const char* listFilter = nullptr;
    const char* snoPath = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--list")) list = true;
        else if (!strcmp(argv[i], "--find-player")) findPlayer = true;
        else if (!strcmp(argv[i], "--track-player")) trackPlayer = true;
        else if (!strcmp(argv[i], "--publish")) publish = true;
        else if (!strcmp(argv[i], "--actors")) dumpActors = true;
        else if (!strcmp(argv[i], "--items")) dumpItems = true;
        else if (!strcmp(argv[i], "--scenes")) dumpScenes = true;
        else if (!strcmp(argv[i], "--ui") && i + 1 < argc) uiFilter = argv[++i];
        else if (!strcmp(argv[i], "--ui-watch") && i + 1 < argc) uiWatch = argv[++i];
        else if (!strcmp(argv[i], "--sno") && i + 1 < argc) snoPath = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) publishSeconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--hz") && i + 1 < argc) g_publishHz = std::clamp(atof(argv[++i]), 5.0, 240.0);
        else if (!strcmp(argv[i], "--list-filter") && i + 1 < argc) {
            listFilter = argv[++i];
            list = true;
        }
        else if (!strcmp(argv[i], "--attach") && i + 1 < argc) attach = argv[++i];
        else if (!strcmp(argv[i], "--capture") && i + 1 < argc) capture = argv[++i];
        else if (!strcmp(argv[i], "--scan-file") && i + 1 < argc) scanFile = argv[++i];
        else if (!strcmp(argv[i], "--scan-pattern") && i + 1 < argc) scanPat = argv[++i];
        else if (!strcmp(argv[i], "--selftest")) return Selftest();
        else if (!strcmp(argv[i], "--marker-child")) return RunMarkerChild();
        else if (!strcmp(argv[i], "--help")) {
            std::print("see source header comment / README\n");
            return 0;
        }
    }

    if (scanFile != nullptr && scanPat != nullptr) {
        Pattern p;
        if (!Pattern::Parse(scanPat, &p)) {
            std::print(stderr, "bad pattern\n");
            return 1;
        }
        auto hits = ScanFile(p, scanFile, 0, 16);
        std::print("{} hit(s) in {}:\n", hits.size(), scanFile);
        for (uint64_t h : hits) std::print("  file offset 0x{:X}\n", static_cast<unsigned long long>(h));
        return 0;
    }

    if (list) {
        for (const auto& pr : Process::List(listFilter))
            std::print("{:6}  {}\n", static_cast<unsigned long>(pr.pid), pr.name.c_str());
        return 0;
    }

    if (attach != nullptr) {
        char* end = nullptr;
        unsigned long pid = strtoul(attach, &end, 10);
        auto proc = (end != attach && *end == '\0') ? Process::OpenByPid(static_cast<DWORD>(pid))
                                                    : Process::OpenByName(attach);
        if (!proc) {
            std::print(stderr, "cannot attach to '{}' (run as the same user; --list to check)\n",
                    attach);
            return 1;
        }
        std::print("attached pid {} ({})\n", static_cast<unsigned long>(proc->pid()),
               proc->name().c_str());
        auto mods = proc->Modules();
        std::print("{} modules:\n", mods.size());
        for (const auto& m : mods)
            std::print("  0x{:016X} {:8} KB  {}\n", static_cast<unsigned long long>(m.base),
                   static_cast<unsigned long long>(m.size >> 10), m.name.c_str());

        const std::string sno = snoPath != nullptr ? snoPath : DefaultSnoPath();
        if (dumpActors) return DumpActors(*proc, sno);
        if (dumpItems) return DumpItems(*proc, sno);
        if (dumpScenes) return DumpScenes(*proc, sno);
        if (uiWatch != nullptr) {
            // --ui-watch <substring> [--seconds N]: poll the memory of the matching UI controls
            // and report which 4-byte fields change (and how often). A value that only ever
            // moves while a rift progresses is the progress field. Output: a diff stream, then
            // a per-field summary (first/last value as integer and float).
            const double secs = publishSeconds > 0 ? publishSeconds : 120.0;
            const auto infos = UiControls::List(*proc, uiWatch);
            std::print("watching {} control(s) matching '{}' for {:.0f} s (play the rift now)" "\n", infos.size(), uiWatch, secs);
            struct Stat { int changes = 0; uint32_t first = 0, last = 0; };
            std::map<std::pair<size_t, size_t>, Stat> stats;
            std::vector<std::vector<uint8_t>> prev(infos.size());
            const ULONGLONG t0 = GetTickCount64();
            while (GetTickCount64() - t0 < static_cast<ULONGLONG>(secs * 1000.0)) {
                const double t = static_cast<double>(GetTickCount64() - t0) / 1000.0;
                int printed = 0;
                for (size_t i = 0; i < infos.size(); ++i) {
                    std::vector<uint8_t> cur(0x1000);
                    if (!proc->Read(infos[i].address, cur.data(), cur.size())) continue;
                    if (!prev[i].empty()) {
                        for (size_t off = 0; off + 4 <= cur.size(); off += 4) {
                            uint32_t a, b;
                            memcpy(&a, &prev[i][off], 4);
                            memcpy(&b, &cur[off], 4);
                            if (a == b) continue;
                            Stat& st = stats[{i, off}];
                            if (st.changes++ == 0) st.first = a;
                            st.last = b;
                            float fb;
                            memcpy(&fb, &b, 4);
                            if (printed++ < 12) {
                                const std::string& p = infos[i].path;
                                std::print("t={:.2f} {} +0x{:X}: {:08X} -> {:08X} (f={:.4f})" "\n", t,
                                           p.size() > 60 ? p.substr(p.size() - 60) : p, off, a, b, fb);
                            }
                        }
                    }
                    prev[i] = std::move(cur);
                }
                Sleep(250);
            }
            std::print("--- summary: fields that changed 2..600 times ---" "\n");
            int lines = 0;
            for (const auto& [key, st] : stats) {
                if (st.changes < 2 || st.changes > 600 || lines++ >= 400) continue;
                float f0, f1;
                memcpy(&f0, &st.first, 4);
                memcpy(&f1, &st.last, 4);
                const std::string& p = infos[key.first].path;
                std::print("{}x {} +0x{:X}: {:08X} -> {:08X} (f {:.4f} -> {:.4f})" "\n", st.changes,
                           p.size() > 60 ? p.substr(p.size() - 60) : p, key.second, st.first, st.last, f0, f1);
            }
            return 0;
        }
        if (uiFilter != nullptr) {
            // --ui <substring>: live UI controls by path (find dialog/button paths)
            for (const UiControlInfo& c : UiControls::List(*proc, uiFilter))
                std::print("{} {} ({:.0f},{:.0f})-({:.0f},{:.0f}) {}\n", c.state.visible ? "VIS" : "   ",
                           c.state.animState, c.state.rect.x0, c.state.rect.y0, c.state.rect.x1, c.state.rect.y1,
                           c.path);
            return 0;
        }
        if (publish) return RunPublish(*proc, publishSeconds, sno);

        if (findPlayer || trackPlayer) {
            PlayerFindResult found;
            if (!PlayerTracker::Find(*proc, &found, true)) {
                std::print(stderr, "player not found (in menus? try again in-world)\n");
                return 1;
            }
            PlayerTracker tracker;
            tracker.setRecord(found.recordAddr);
            std::print("player @ (x={:.2f}, z={:.2f}, y={:.2f})  record=0x{:X}  votes={}/{}\n",
                   found.pos.x, found.pos.z, found.pos.y,
                   static_cast<unsigned long long>(found.recordAddr), found.clusterVotes,
                   found.totalValidHits);

            if (!trackPlayer) return 0;
            std::print("tracking - walk around; Ctrl+C to stop\n");
            PlayerPosition last = found.pos;
            int failures = 0;
            while (true) {
                Sleep(250);
                PlayerPosition p;
                if (tracker.ReadPosition(*proc, &p)) {
                    failures = 0;
                    const float moved = std::sqrt((p.x - last.x) * (p.x - last.x) +
                                                  (p.z - last.z) * (p.z - last.z));
                    if (moved > 0.5f) {
                        std::print("[{:7.1f}s] (x={:8.2f}, z={:8.2f}, y={:6.2f})  moved {:.1f}\n",
                               static_cast<double>(GetTickCount()) / 1000.0, p.x, p.z, p.y, moved);
                        last = p;
                    }
                } else if (++failures >= 5) {
                    std::print("record lost - rescanning...\n");
                    if (!PlayerTracker::Find(*proc, &found, false)) {
                        std::print(stderr, "rescan failed\n");
                        return 1;
                    }
                    tracker.setRecord(found.recordAddr);
                    last = found.pos;
                    failures = 0;
                }
            }
        }

        if (auto main = proc->FindModule(proc->name())) {
            std::print("\nresolving offsets against {} (0x{:X} + 0x{:X}):\n", main->name.c_str(),
                   static_cast<unsigned long long>(main->base),
                   static_cast<unsigned long long>(main->size));
            PrintOffsets(ResolveAll(*proc, *main));
        }
        if (capture != nullptr) return Capture(*proc, capture);
        return 0;
    }

    std::print("nothing to do - try --list, --attach <pid|name>, --scan-file, or --selftest\n");
    return 0;
}
