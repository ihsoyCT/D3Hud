// The values of all plugin settings, kept by the host (plugins never touch files). Keys are
// "<plugin>.<key>". Definitions come from the plugins when they load; stored values read from the
// file before a plugin defines its key are kept and applied then, so nothing is lost when a plugin
// is missing for a session. Pure logic, no UI.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "HostApi.h"
#include "Paths.h"

namespace d3hud {

struct PluginSettingDef {
    std::string key;  // full key
    std::string label, tooltip, choices;  // choices: "A\0B\0" style, as passed by the plugin
    int type = D3H_SETTING_BOOL;
    double def = 0, min = 0, max = 0;
    std::string defString;
    bool hidden = false;
};

class PluginSettings {
public:
    // Registers a definition; false if the key is empty or already defined with another type.
    bool Define(const std::string& plugin, const d3h_setting_desc& d) {
        if (d.key == nullptr || d.key[0] == '\0') return false;
        PluginSettingDef def;
        def.key = plugin + "." + d.key;
        def.type = d.type;
        def.label = d.label != nullptr ? d.label : d.key;
        def.tooltip = d.tooltip != nullptr ? d.tooltip : "";
        def.def = d.def;
        def.min = d.min;
        def.max = d.max;
        def.defString = d.def_string != nullptr ? d.def_string : "";
        def.hidden = (d.flags & D3H_SETTING_HIDDEN) != 0;
        if (d.type == D3H_SETTING_CHOICE && d.choices != nullptr) {
            // the list ends with an empty string (double NUL)
            for (const char* p = d.choices; *p != '\0'; p += strlen(p) + 1) {
                def.choices += p;
                def.choices += '\0';
            }
            def.choices += '\0';
        }
        if (d.type < D3H_SETTING_BOOL || d.type > D3H_SETTING_CHOICE) return false;
        if (const auto it = defs_.find(def.key); it != defs_.end() && it->second.type != def.type) return false;
        const bool isString = d.type == D3H_SETTING_STRING;
        const auto have = values_.find(def.key);
        if (have == values_.end() || (!isString && !Parses(have->second)))
            values_[def.key] = isString ? def.defString : Format(def, def.def);
        else if (!isString)  // a value stored by an earlier session: keep it, within the new limits
            have->second = Format(def, Clamp(def, std::strtod(have->second.c_str(), nullptr)));
        order_.erase(std::remove(order_.begin(), order_.end(), def.key), order_.end());
        order_.push_back(def.key);
        defs_[def.key] = std::move(def);
        return true;
    }

    [[nodiscard]] const PluginSettingDef* Find(const std::string& fullKey) const {
        const auto it = defs_.find(fullKey);
        return it == defs_.end() ? nullptr : &it->second;
    }
    // Definitions of one plugin in definition order.
    [[nodiscard]] std::vector<const PluginSettingDef*> Of(const std::string& plugin) const {
        std::vector<const PluginSettingDef*> out;
        const std::string prefix = plugin + ".";
        for (const std::string& k : order_)
            if (k.starts_with(prefix)) out.push_back(&defs_.at(k));
        return out;
    }

    [[nodiscard]] bool GetNumber(const std::string& fullKey, double& out) const {
        const PluginSettingDef* d = Find(fullKey);
        if (d == nullptr || d->type == D3H_SETTING_STRING) return false;
        out = std::strtod(values_.at(fullKey).c_str(), nullptr);
        return true;
    }
    [[nodiscard]] bool GetString(const std::string& fullKey, std::string& out) const {
        const PluginSettingDef* d = Find(fullKey);
        if (d == nullptr || d->type != D3H_SETTING_STRING) return false;
        out = values_.at(fullKey);
        return true;
    }
    // Clamps to the definition; true when the stored value changed.
    [[nodiscard]] bool SetNumber(const std::string& fullKey, double v) {
        const PluginSettingDef* d = Find(fullKey);
        if (d == nullptr || d->type == D3H_SETTING_STRING) return false;
        return Store(fullKey, Format(*d, Clamp(*d, v)));
    }
    [[nodiscard]] bool SetString(const std::string& fullKey, std::string_view v) {
        const PluginSettingDef* d = Find(fullKey);
        if (d == nullptr || d->type != D3H_SETTING_STRING) return false;
        return Store(fullKey, std::string(v.substr(0, kMaxString)));
    }

    // Text file of "key=value" lines. Strings have '\n' and '\\' escaped so a value stays on one line.
    [[nodiscard]] std::string SaveText() const {
        std::string out = "# D3Hud plugin settings (written by the overlay; edit with the overlay closed)\n";
        for (const auto& [k, v] : values_) out += std::format("{}={}\n", k, Escape(v));
        return out;
    }
    void LoadText(std::string_view text) {
        while (!text.empty()) {
            const size_t nl = text.find('\n');
            std::string_view line = text.substr(0, nl);
            text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
            while (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            const size_t eq = line.find('=');
            if (line.empty() || line.front() == '#' || eq == std::string_view::npos || eq == 0) continue;
            const std::string key(line.substr(0, eq));
            std::string value = Unescape(line.substr(eq + 1));
            if (const PluginSettingDef* d = Find(key)) {
                if (d->type == D3H_SETTING_STRING) value = value.substr(0, kMaxString);
                else if (Parses(value)) value = Format(*d, Clamp(*d, std::strtod(value.c_str(), nullptr)));
                else continue;
            }
            values_[key] = std::move(value);
        }
    }

    [[nodiscard]] bool Load(const std::filesystem::path& path) {
        File f = OpenFile(path, L"rb");
        if (f == nullptr) return false;
        std::string text;
        char buf[4096];
        for (size_t n; (n = fread(buf, 1, sizeof(buf), f)) > 0;) text.append(buf, n);
        LoadText(text);
        dirty_ = false;
        return true;
    }
    [[nodiscard]] bool Save(const std::filesystem::path& path) {
        File f = OpenFile(path, L"wb");
        if (f == nullptr) return false;
        const std::string text = SaveText();
        const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
        if (ok) dirty_ = false;
        return ok;
    }
    [[nodiscard]] bool dirty() const { return dirty_; }

    static constexpr size_t kMaxString = 4096;

private:
    bool Store(const std::string& key, std::string v) {
        std::string& cur = values_[key];
        if (cur == v) return false;
        cur = std::move(v);
        dirty_ = true;
        return true;
    }
    static double Clamp(const PluginSettingDef& d, double v) {
        if (d.type == D3H_SETTING_BOOL) return v != 0.0 ? 1.0 : 0.0;
        if (d.type == D3H_SETTING_COLOR) return std::clamp(v, 0.0, 16777215.0);
        if (d.max > d.min) v = std::clamp(v, d.min, d.max);
        if (d.type == D3H_SETTING_INT || d.type == D3H_SETTING_CHOICE) v = std::floor(v);
        return v;
    }
    static std::string Format(const PluginSettingDef& d, double v) {
        return d.type == D3H_SETTING_FLOAT ? std::format("{}", v) : std::format("{}", static_cast<long long>(v));
    }
    static bool Parses(const std::string& s) {
        char* end = nullptr;
        (void)std::strtod(s.c_str(), &end);
        return end != s.c_str();
    }
    static std::string Escape(const std::string& s) {
        std::string o;
        for (const char c : s) {
            if (c == '\n') o += "\\n";
            else if (c == '\r') o += "\\r";
            else if (c == '\\') o += "\\\\";
            else o += c;
        }
        return o;
    }
    static std::string Unescape(std::string_view s) {
        std::string o;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                const char n = s[++i];
                o += n == 'n' ? '\n' : n == 'r' ? '\r' : n;
            } else {
                o += s[i];
            }
        }
        return o;
    }

    std::map<std::string, PluginSettingDef> defs_;
    std::map<std::string, std::string> values_;
    std::vector<std::string> order_;
    bool dirty_ = false;
};

}  // namespace d3hud
