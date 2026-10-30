#include "BasesCache.h"

#include <charconv>
#include <string_view>

namespace d3hud {
namespace {

bool ParseList(const std::string& line, std::vector<uint64_t>* out) {
    uint64_t v = 0;
    for (size_t i = 0; i < line.size();) {
        const size_t j = line.find(';', i);
        const std::string_view tok(line.data() + i, (j == std::string::npos ? line.size() : j) - i);
        if (tok.empty()) return false;
        const auto [p, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), v, 16);
        if (ec != std::errc()) return false;
        out->push_back(v);
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return true;
}

}  // namespace

bool BasesCache::Save(const std::string& path) const {
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "w");
    if (f == nullptr) return false;
    fprintf(f, "pid=%u\ncreated=%llu\nactor=%llX\n", pid, static_cast<unsigned long long>(created),
            static_cast<unsigned long long>(actorBase));
    auto list = [&](const char* key, const std::vector<uint64_t>& v) {
        fprintf(f, "%s=", key);
        for (size_t i = 0; i < v.size(); ++i) fprintf(f, i ? ";%llX" : "%llX", static_cast<unsigned long long>(v[i]));
        fprintf(f, "\n");
    };
    list("acd", acd);
    list("attrib", attrib);
    list("scene", scene);
    list("ui", ui);
    fclose(f);
    return true;
}

bool BasesCache::Load(const std::string& path) {
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "r");
    if (f == nullptr) return false;
    char line[8192];
    uint32_t filePid = 0;
    uint64_t fileCreated = 0;
    bool haveActor = false, haveUi = false;
    std::vector<uint64_t> fileAcd, fileAttrib, fileScene, fileUi;
    while (fgets(line, sizeof(line), f) != nullptr) {
        std::string_view sv(line);
        while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r')) sv.remove_suffix(1);
        const size_t eq = sv.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = sv.substr(0, eq), val = sv.substr(eq + 1);
        auto num = [&](auto* out, int base = 10) {
            return std::from_chars(val.data(), val.data() + val.size(), *out, base).ec == std::errc();
        };
        if (key == "pid") { if (!num(&filePid)) filePid = 0; }
        else if (key == "created") { if (!num(&fileCreated)) fileCreated = 0; }
        else if (key == "actor") { haveActor = num(&actorBase, 16); }
        else if (key == "acd") { ParseList(std::string(val), &fileAcd); }
        else if (key == "attrib") { ParseList(std::string(val), &fileAttrib); }
        else if (key == "scene") { ParseList(std::string(val), &fileScene); }
        else if (key == "ui") { haveUi = ParseList(std::string(val), &fileUi); }
    }
    fclose(f);
    if (filePid != pid || fileCreated != created || !haveActor || !haveUi || actorBase == 0) {
        actorBase = 0;
        return false;  // stale: another (or restarted) game process
    }
    acd = std::move(fileAcd);
    attrib = std::move(fileAttrib);
    scene = std::move(fileScene);
    ui = std::move(fileUi);
    return true;
}

}  // namespace d3hud
