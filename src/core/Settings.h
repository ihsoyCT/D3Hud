// A table of named settings and the ini text format they are stored in ("key=value" per line,
// '#' comments). Each entry binds a key to a field with its limits, so Load and Save come from one
// registration instead of two hand-written lists. Pure logic, no UI: the overlay builds the table
// from its layout struct on demand (it holds references into it), plugins can register their own.
#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "Paths.h"

namespace d3hud {

class SettingsTable {
public:
    // bool, integer or float field. Loaded values are clamped to [lo, hi]; `decimals` is the
    // precision Save writes for floats. Colours are plain uint32_t with hi = 0xFFFFFF.
    template <class T>
        requires std::is_arithmetic_v<T>
    void Add(std::string key, T& field, T lo = std::numeric_limits<T>::lowest(), T hi = std::numeric_limits<T>::max(),
             int decimals = 2) {
        Entry e;
        e.key = std::move(key);
        if constexpr (std::is_same_v<T, bool>) {
            e.parse = [&field](std::string_view s) {
                double v;
                if (!ParseNumber(s, v)) return false;
                field = v != 0.0;
                return true;
            };
            e.format = [&field] { return std::string(field ? "1" : "0"); };
        } else {
            e.parse = [&field, lo, hi](std::string_view s) {
                double v;
                if (!ParseNumber(s, v)) return false;
                v = std::clamp(v, static_cast<double>(lo), static_cast<double>(hi));
                field = static_cast<T>(v);
                return true;
            };
            e.format = [&field, decimals] {
                if constexpr (std::is_floating_point_v<T>) return std::format("{:.{}f}", field, decimals);
                else return std::format("{}", field);
            };
        }
        Push(std::move(e));
    }

    // Enum field, stored as its number.
    template <class E>
        requires std::is_enum_v<E>
    void Add(std::string key, E& field, E lo, E hi) {
        Entry e;
        e.key = std::move(key);
        e.parse = [&field, lo, hi](std::string_view s) {
            double v;
            if (!ParseNumber(s, v)) return false;
            field = static_cast<E>(std::clamp(v, static_cast<double>(lo), static_cast<double>(hi)));
            return true;
        };
        e.format = [&field] { return std::format("{}", static_cast<long long>(field)); };
        Push(std::move(e));
    }

    // 64-bit mask written in hex (the safe-area cells); loaded bits outside `mask` are dropped.
    void AddHex64(std::string key, uint64_t& field, uint64_t mask = ~0ull) {
        Entry e;
        e.key = std::move(key);
        e.parse = [&field, mask](std::string_view s) {
            const std::string t(s);
            char* end = nullptr;
            const unsigned long long v = std::strtoull(t.c_str(), &end, 16);
            if (end == t.c_str()) return false;
            field = v & mask;
            return true;
        };
        e.format = [&field] { return std::format("{:x}", field); };
        Push(std::move(e));
    }

    // An older spelling of `key` that Load still accepts; Save writes only the current one.
    void Alias(std::string oldKey, const std::string& key) {
        if (const auto it = index_.find(key); it != index_.end()) index_[std::move(oldKey)] = it->second;
    }

    // A '#' line written before the next entry (without the '#').
    void Comment(std::string text) { pendingComment_ = std::move(text); }

    // Applies "key=value" lines; unknown keys and unreadable values are ignored. Returns the number applied.
    size_t LoadText(std::string_view text) const {
        size_t applied = 0;
        while (!text.empty()) {
            const size_t nl = text.find('\n');
            std::string_view line = text.substr(0, nl);
            text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
            line = Trim(line);
            if (line.empty() || line.front() == '#') continue;
            const size_t eq = line.find('=');
            if (eq == std::string_view::npos) continue;
            const auto it = index_.find(std::string(Trim(line.substr(0, eq))));
            if (it != index_.end() && entries_[it->second].parse(Trim(line.substr(eq + 1)))) ++applied;
        }
        return applied;
    }

    [[nodiscard]] std::string SaveText() const {
        std::string out;
        for (const Entry& e : entries_) {
            if (!e.comment.empty()) out += std::format("# {}\n", e.comment);
            out += std::format("{}={}\n", e.key, e.format());
        }
        return out;
    }

    // False when the file cannot be opened / written.
    [[nodiscard]] bool Load(const std::filesystem::path& path) const {
        File f = OpenFile(path, L"rb");
        if (f == nullptr) return false;
        std::string text;
        char buf[4096];
        for (size_t n; (n = fread(buf, 1, sizeof(buf), f)) > 0;) text.append(buf, n);
        (void)LoadText(text);  // the count only matters to tests
        return true;
    }

    [[nodiscard]] bool Save(const std::filesystem::path& path) const {
        File f = OpenFile(path, L"wb");
        if (f == nullptr) return false;
        const std::string text = SaveText();
        return fwrite(text.data(), 1, text.size(), f) == text.size();
    }

    [[nodiscard]] size_t size() const { return entries_.size(); }
    [[nodiscard]] bool Has(const std::string& key) const { return index_.contains(key); }

private:
    struct Entry {
        std::string key;
        std::string comment;
        std::function<bool(std::string_view)> parse;
        std::function<std::string()> format;
    };

    void Push(Entry e) {
        e.comment = std::move(pendingComment_);
        pendingComment_.clear();
        index_[e.key] = entries_.size();
        entries_.push_back(std::move(e));
    }

    static std::string_view Trim(std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
        return s;
    }

    static bool ParseNumber(std::string_view s, double& out) {
        const std::string t(s);  // strtod needs a terminator
        char* end = nullptr;
        out = std::strtod(t.c_str(), &end);
        return end != t.c_str();
    }

    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> index_;
    std::string pendingComment_;
};

}  // namespace d3hud
