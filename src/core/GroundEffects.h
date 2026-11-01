// The areas monsters leave on the ground (molten pools, desecrator, ...) are told apart by the actor's name. The names are
// a list the user can change (the groundeffects plugin's "list" setting, kept in plugins.ini): the reader reads the names
// to decide which actors are ground effects, the plugin reads the rest (radius, life, label) to draw them. Both parse the
// same text, so the syntax lives here (pure, unit-tested).
//
//   list   = entry { "," entry }
//   entry  = name [ ":" radius [ ":" seconds [ ":" label ] ] ]
//
// name: the actor's name in any case. radius: world units (empty = the plugin's default). seconds: how long the area lasts,
// when it is a fixed time worth counting down (empty = unknown). label: what to call it (empty = the name).
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace d3hud {

struct GroundEffectSpec {
    std::string name;   // lower case
    float radius = 0.f;  // 0 = not given
    float life = 0.f;    // 0 = not given
    std::string label;   // empty = not given
};

// The areas that were built in before the list was a setting (the radii are from TurboHUD's EliteMonsterSkillPlugin). The
// groundeffects plugin's default for "list" must be this same text (a test compares them).
inline constexpr const char* kDefaultGroundEffects =
    "monsteraffix_frozen_iceclusters:15.8:3:Frozen ball, monsteraffix_molten_deathstart_proxy:13:3:Molten blast, "
    "monsteraffix_molten_deathexplosion_proxy:13::Molten, monsteraffix_molten_firering:13::Molten, "
    "monsteraffix_desecrator_damage_aoe:8::Desecrator, x1_monsteraffix_thunderstorm_impact:16::Thunderstorm, "
    "monsteraffix_plagued_endcloud:12::Plagued, creepmobarm:12::Plagued, gluttony_gascloud_proxy:20::Gas cloud, "
    "monsteraffix_arcaneenchanted_petsweep:6::Arcane, monsteraffix_arcaneenchanted_petsweep_reverse:6::Arcane, "
    "arcaneenchanteddummy_spawn:6:2:Arcane, x1_monsteraffix_frozenpulse_monster:14::Frozen pulse";

constexpr size_t kMaxGroundEffects = 300;

// Entries without a name are skipped, numbers that do not parse or are not positive count as "not given".
std::vector<GroundEffectSpec> ParseGroundEffects(std::string_view text);

// The value of `key` in the text of a settings file of "key=value" lines (the plugin settings file: line breaks and the
// backslash are escaped in values). False if the key is not there.
bool IniValue(std::string_view fileText, std::string_view key, std::string* out);

}  // namespace d3hud
