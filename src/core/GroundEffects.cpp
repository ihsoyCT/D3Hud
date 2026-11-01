#include "GroundEffects.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace d3hud {

static std::string_view Trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

static float Positive(std::string_view s, float cap) {
    s = Trim(s);
    if (s.empty()) return 0.f;
    const std::string copy(s);
    char* end = nullptr;
    const double v = std::strtod(copy.c_str(), &end);
    if (end == copy.c_str() || !(v > 0.0)) return 0.f;
    return static_cast<float>(std::min<double>(v, cap));
}

std::vector<GroundEffectSpec> ParseGroundEffects(std::string_view text) {
    std::vector<GroundEffectSpec> out;
    while (!text.empty() && out.size() < kMaxGroundEffects) {
        const size_t comma = text.find(',');
        std::string_view entry = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
        std::string_view field[4];
        for (int i = 0; i < 4; ++i) {
            const size_t colon = i < 3 ? entry.find(':') : std::string_view::npos;
            field[i] = Trim(entry.substr(0, colon));
            entry = colon == std::string_view::npos ? std::string_view{} : entry.substr(colon + 1);
        }
        if (field[0].empty() || field[0].size() > 100) continue;
        GroundEffectSpec s;
        s.name.assign(field[0]);
        std::transform(s.name.begin(), s.name.end(), s.name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        s.radius = Positive(field[1], 200.f);
        s.life = Positive(field[2], 600.f);
        s.label.assign(field[3]);
        out.push_back(std::move(s));
    }
    return out;
}

bool IniValue(std::string_view text, std::string_view key, std::string* out) {
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        while (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() <= key.size() || line.compare(0, key.size(), key) != 0 || line[key.size()] != '=') continue;
        const std::string_view value = line.substr(key.size() + 1);
        out->clear();
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '\\' && i + 1 < value.size()) {
                const char n = value[++i];
                *out += n == 'n' ? '\n' : n == 'r' ? '\r' : n;
            } else {
                *out += value[i];
            }
        }
        return true;
    }
    return false;
}

}  // namespace d3hud
