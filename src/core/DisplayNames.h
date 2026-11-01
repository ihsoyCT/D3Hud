// The English names of things the HUD sees by their internal name (monsters, NPCs, shrines, items, powers):
// "ZombieSkinny_A" -> "Risen". One table for all of them, analysis/names_en.tsv (name<TAB>english), built from the game's own
// text by tools/build_names_en.py. Names are matched without regard to case.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace d3hud {

class DisplayNames {
public:
    [[nodiscard]] bool Load(const std::filesystem::path& path);
    // nullptr when the game has no English name for it
    [[nodiscard]] const std::string* Find(const std::string& internalName) const;
    // The English name, or the internal name when there is none.
    [[nodiscard]] std::string Of(const std::string& internalName) const;
    [[nodiscard]] size_t size() const { return names_.size(); }

private:
    std::unordered_map<std::string, std::string> names_;  // lower case internal name -> English
};

// The generated names of rare monsters: a prefix and a suffix, each a GBID (the game's hash of the string key, e.g.
// "RareGenericPrefix068") with its text. analysis/rare_names.tsv: gbid<TAB>text; the suffix text starts with its own
// separator (" the Doomed", ", Torturer of Souls").
class RareNames {
public:
    [[nodiscard]] bool Load(const std::filesystem::path& path);
    // "Kauza, Torturer of Souls"; empty when neither part is known
    [[nodiscard]] std::string Compose(uint32_t prefix, uint32_t suffix) const;
    [[nodiscard]] size_t size() const { return parts_.size(); }

private:
    std::unordered_map<uint32_t, std::string> parts_;
};

}  // namespace d3hud
