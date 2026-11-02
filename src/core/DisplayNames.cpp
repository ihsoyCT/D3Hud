#include "DisplayNames.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

namespace d3hud {

static std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool DisplayNames::Load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;
        names_[Lower(line.substr(0, tab))] = line.substr(tab + 1);
    }
    return !names_.empty();
}

const std::string* DisplayNames::Find(const std::string& internalName) const {
    const auto it = names_.find(Lower(internalName));
    return it == names_.end() ? nullptr : &it->second;
}

std::string DisplayNames::Of(const std::string& internalName) const {
    const std::string* n = Find(internalName);
    return n != nullptr ? *n : internalName;
}

bool RareNames::Load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || tab + 1 >= line.size()) continue;
        const unsigned long long gbid = strtoull(line.c_str(), nullptr, 10);
        if (gbid != 0) parts_[static_cast<uint32_t>(gbid)] = line.substr(tab + 1);
    }
    return !parts_.empty();
}

std::string RareNames::Compose(uint32_t prefix, uint32_t suffix) const {
    const auto p = parts_.find(prefix), s = parts_.find(suffix);
    std::string out;
    if (p != parts_.end()) out = p->second;
    if (s != parts_.end()) out += s->second;
    const size_t first = out.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : out.substr(first);
}

}  // namespace d3hud
