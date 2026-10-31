#include "PluginHost.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <print>

#include "HudRenderer.h"
#include "Paths.h"
#include "Win32.h"
#include "imgui.h"

namespace d3hud {

namespace {

PluginHost* g_host = nullptr;  // the C callbacks have no context argument; one host per process

// Plugin-supplied printf formats are never passed to ImGui as they are.
const char* SafeFormat(const char* f) {
    for (const char* ok : {"%.0f", "%.1f", "%.2f", "%.3f", "%d"})
        if (f != nullptr && !strcmp(f, ok)) return ok;
    return "%.2f";
}

ImU32 ArgbToImColor(d3h_color c) {
    return IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, (c >> 24) & 255);
}

std::string FullKey(const char* key) {
    const std::string* plugin = CurrentPluginName();
    return plugin != nullptr && key != nullptr ? *plugin + "." + key : std::string();
}

}  // namespace

// ---- host functions (plugins -> overlay) --------------------------------------------------------

struct PluginHost::HostFns {
    static void Log(int32_t level, const char* msg) {
        const std::string* p = CurrentPluginName();
        const char* tag = level == D3H_LOG_ERROR ? "error" : level == D3H_LOG_WARN ? "warn" : "info";
        std::string line = std::format("[{}] {}: {}", p != nullptr ? p->c_str() : "host", tag, msg);
        std::print(stderr, "plugin {}\n", line);
        g_host->log_.push_back(std::move(line));
        if (g_host->log_.size() > 60) g_host->log_.erase(g_host->log_.begin());
    }

    static void GetFrame(d3h_frame* out) {
        const uint32_t size = out->size;
        d3h_frame f{};
        f.size = size;
        f.rift_timer_fraction = -1.f;
        f.hovered_item = -1;
        f.stash_tab = -1;
        f.screen_w = g_host->screenW_;
        f.screen_h = g_host->screenH_;
        if (const WorldSnapshot* s = g_host->snap_) {
            f.frame_index = s->frameIndex;
            f.time_ms = s->timestampMs;
            f.player_pos = {s->playerPos.x, s->playerPos.y};
            f.player_heading = s->playerHeading;
            f.world_sno = s->worldSnoId;
            f.actor_count = s->actorCount;
            f.item_count = s->itemCount;
            f.hovered_item = s->hoveredItemPlus1 > 0 ? static_cast<int32_t>(s->hoveredItemPlus1) - 1 : -1;
            const UiLayout& u = s->ui;
            f.ui_flags = (u.inventoryOpen ? D3H_UI_INVENTORY : 0u) | (u.stashOpen ? D3H_UI_STASH : 0u) |
                         (u.salvageOpen ? D3H_UI_SALVAGE : 0u) | (u.bigMapOpen ? D3H_UI_BIG_MAP : 0u) |
                         (u.minimapOpen ? D3H_UI_MINIMAP : 0u) | (u.riftOpen ? D3H_UI_RIFT : 0u) |
                         (u.riftOpen == 2 ? D3H_UI_GREATER_RIFT : 0u);
            f.rift_progress = u.riftProgress;
            f.rift_timer_fraction = u.riftTimerFraction;
            f.stash_tab = u.stashTab;
            f.effect_count = s->effectCount;
        }
        // a plugin built against a shorter struct gets just that prefix
        memcpy(out, &f, std::min<size_t>(size, sizeof(f)));
    }

    static d3h_bool GetActor(uint32_t i, d3h_actor* out) {
        const WorldSnapshot* s = g_host->snap_;
        if (s == nullptr || i >= s->actorCount) return 0;
        const ActorSnapshot& a = s->actors[i];
        d3h_actor r{};
        r.size = sizeof(r);
        r.id = a.id;
        r.pos = {a.world.x, a.world.y};
        r.heading = a.heading;
        r.hp_fraction = a.maxHp > 0.f ? std::clamp(a.currentHp / a.maxHp, 0.f, 1.f) : -1.f;
        r.affix_mask = a.affixMask;
        r.hp_current = a.maxHp > 0.f ? a.currentHp : 0.f;
        r.hp_max = a.maxHp > 0.f ? a.maxHp : 0.f;
        r.kind = static_cast<int32_t>(a.kind);
        r.rarity = static_cast<int32_t>(a.itemRarity);
        r.flags = a.itemFlags;
        strncpy_s(r.name, sizeof(r.name), a.name, _TRUNCATE);
        r.size = out->size;  // the caller's struct size: a prefix is copied
        memcpy(out, &r, std::min<size_t>(out->size, sizeof(r)));
        return 1;
    }

    static d3h_bool GetEffect(uint32_t i, d3h_effect* out) {
        const WorldSnapshot* s = g_host->snap_;
        if (s == nullptr || i >= s->effectCount || i >= static_cast<uint32_t>(kMaxEffects)) return 0;
        const EffectSnapshot& e = s->effects[i];
        d3h_effect r{};
        r.sno = e.sno;
        r.kind = e.kind == EffectKind::Cooldown ? D3H_EFFECT_COOLDOWN : D3H_EFFECT_BUFF;
        r.stacks = e.count;
        r.slot = e.slot;
        const EffectTimes t = TimesOf(e, g_host->tickNow_);
        r.duration = t.duration;
        if (g_host->tickNow_ >= 0) {
            r.timed = 1;
            r.remaining = t.remaining;
            r.elapsed = t.elapsed;
        }
        const std::string readable = g_host->powers_.Readable(e.sno);
        strncpy_s(r.name, sizeof(r.name), readable.c_str(), _TRUNCATE);
        if (const std::string* code = g_host->powers_.Code(e.sno)) strncpy_s(r.code, sizeof(r.code), code->c_str(), _TRUNCATE);
        r.size = out->size;  // the caller's struct size: a prefix is copied
        memcpy(out, &r, std::min<size_t>(out->size, sizeof(r)));
        return 1;
    }

    static d3h_bool GetItem(uint32_t i, d3h_item* out) {
        const WorldSnapshot* s = g_host->snap_;
        if (s == nullptr || i >= s->itemCount) return 0;
        const ItemSnapshot& it = s->items[i];
        d3h_item r{};
        r.size = sizeof(r);
        r.id = it.id;
        r.sno = it.snoId;
        r.rarity = static_cast<int32_t>(it.rarity);
        r.verdict = static_cast<int32_t>(it.verdict);
        r.flags = (it.ancient ? D3H_FLAG_ANCIENT : 0u) | (it.primal ? D3H_FLAG_PRIMAL : 0u) |
                  (it.bonus ? D3H_FLAG_BONUS : 0u) | (it.identified ? 0u : D3H_FLAG_UNIDENTIFIED);
        r.location = it.location;
        r.grid_x = it.gridX;
        r.grid_y = it.gridY;
        r.height = it.height;
        r.augment_rank = it.augmentRank;
        strncpy_s(r.name, sizeof(r.name), it.name, _TRUNCATE);
        strncpy_s(r.type_name, sizeof(r.type_name), it.typeName, _TRUNCATE);
        r.size = out->size;  // the caller's struct size: a prefix is copied
        memcpy(out, &r, std::min<size_t>(out->size, sizeof(r)));
        return 1;
    }

    static d3h_bool WorldToScreenFn(d3h_vec2 world, float dz, d3h_vec2* out) {
        const WorldSnapshot* s = g_host->snap_;
        if (s == nullptr) return 0;
        Vec2 px;
        if (!WorldToScreen({world.x, world.y}, dz, s->playerPos, g_host->screenW_, g_host->screenH_, &px)) return 0;
        *out = {px.x, px.y};
        return 1;
    }

    static void Subscribe(int32_t type, d3h_event_fn fn, void* user) {
        if (const std::string* p = CurrentPluginName()) g_host->subs_.push_back({type, fn, user, p});
    }

    static void DrawText(int32_t layer, d3h_vec2 pos, d3h_color color, const char* text, float scale) {
        if (text == nullptr) return;
        ImDrawList* dl = layer <= D3H_LAYER_MARKERS ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();
        dl->AddText(font, ImGui::GetFontSize() * std::clamp(scale, 0.25f, 8.f), ImVec2(pos.x, pos.y),
                    ArgbToImColor(color), text);
    }

    static d3h_vec2 TextSize(const char* text, float scale) {
        if (text == nullptr) return {0.f, 0.f};
        const float size = ImGui::GetFontSize() * std::clamp(scale, 0.25f, 8.f);
        const ImVec2 v = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.f, text);
        return {v.x, v.y};
    }

    static ImDrawList* ListFor(int32_t layer) {
        return layer <= D3H_LAYER_MARKERS ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList();
    }
    static void DrawLine(int32_t layer, d3h_vec2 a, d3h_vec2 b, d3h_color color, float thickness) {
        ListFor(layer)->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), ArgbToImColor(color), std::clamp(thickness, 0.5f, 32.f));
    }
    static void DrawRect(int32_t layer, d3h_rect r, d3h_color color, d3h_bool filled, float thickness) {
        if (filled) ListFor(layer)->AddRectFilled(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), ArgbToImColor(color));
        else ListFor(layer)->AddRect(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), ArgbToImColor(color), 0.f, 0, std::clamp(thickness, 0.5f, 32.f));
    }

    static d3h_bool RegisterPanel(const d3h_panel_desc* d) {
        const std::string* plugin = CurrentPluginName();
        if (plugin == nullptr || d->id == nullptr || d->title == nullptr || d->draw == nullptr) return 0;
        PluginHost* host = g_host;
        const d3h_draw_fn draw = d->draw;
        void* const user = d->user;
        const std::string id = *plugin + "." + d->id;
        host->renderer_.AddPluginPanel(id, d->title, [host, draw, user] { draw(&host->ui_, user); },
                                       ImVec2(d->default_w, d->default_h));
        host->panelsOf_[*plugin].push_back(id);
        return 1;
    }

    static d3h_bool RegisterBox(const d3h_box_desc* d) {
        const std::string* plugin = CurrentPluginName();
        if (plugin == nullptr || d->id == nullptr || d->title == nullptr || d->draw == nullptr) return 0;
        PluginHost* host = g_host;
        const d3h_box_fn draw = d->draw;
        void* const user = d->user;
        const std::string id = *plugin + "." + d->id;
        HudRenderer::BoxLook look;
        look.hasFill = (d->flags & D3H_BOX_FILL_SET) != 0;
        look.hasBorder = (d->flags & D3H_BOX_BORDER_SET) != 0;
        look.fillArgb = d->fill;
        look.borderArgb = d->border;
        look.padding = d->padding;
        host->renderer_.AddPluginBox(
            id, d->title,
            [draw, user](ImVec2 origin, ImVec2* size) {
                d3h_vec2 used{0.f, 0.f};
                draw({origin.x, origin.y}, &used, user);
                *size = ImVec2(used.x, used.y);
            },
            ImVec2(std::clamp(d->default_x, 0.f, 1.f), std::clamp(d->default_y, 0.f, 1.f)), look);
        host->boxesOf_[*plugin].push_back(id);
        return 1;
    }

    static d3h_bool RegisterPage(const d3h_page_desc* d) {
        const std::string* plugin = CurrentPluginName();
        if (plugin == nullptr || d->id == nullptr || d->title == nullptr || d->draw == nullptr) return 0;
        PluginHost* host = g_host;
        const d3h_draw_fn draw = d->draw;
        void* const user = d->user;
        // the plugin's first page also carries its options and dock panel controls: one place per plugin
        const bool first = host->pluginsWithPage_.insert(*plugin).second;
        host->renderer_.AddSettingsPage({*plugin + "." + d->id, d->title, d->group != nullptr ? d->group : "Plugins",
                                         d->order, [host, draw, user, first, name = *plugin] {
                                             draw(&host->ui_, user);
                                             if (first) host->DrawExtrasOf(name);
                                         }});
        return 1;
    }

    static d3h_bool Define(const d3h_setting_desc* d) {
        const std::string* plugin = CurrentPluginName();
        return plugin != nullptr && g_host->settings_.Define(*plugin, *d) ? 1 : 0;
    }
    static d3h_bool GetBool(const char* k, d3h_bool* o) {
        double v;
        if (!g_host->settings_.GetNumber(FullKey(k), v)) return 0;
        *o = v != 0.0;
        return 1;
    }
    static d3h_bool GetInt(const char* k, int32_t* o) {
        double v;
        if (!g_host->settings_.GetNumber(FullKey(k), v)) return 0;
        *o = static_cast<int32_t>(v);
        return 1;
    }
    static d3h_bool GetFloat(const char* k, float* o) {
        double v;
        if (!g_host->settings_.GetNumber(FullKey(k), v)) return 0;
        *o = static_cast<float>(v);
        return 1;
    }
    static d3h_bool GetColor(const char* k, d3h_color* o) {
        double v;
        if (!g_host->settings_.GetNumber(FullKey(k), v)) return 0;
        *o = static_cast<d3h_color>(v);
        return 1;
    }
    static d3h_bool GetString(const char* k, char* o, size_t n) {
        std::string v;
        if (n == 0 || !g_host->settings_.GetString(FullKey(k), v)) return 0;
        strncpy_s(o, n, v.c_str(), _TRUNCATE);
        return 1;
    }
    static d3h_bool SetValue(const char* k, double v) {
        (void)g_host->settings_.SetNumber(FullKey(k), v);  // unchanged is fine
        return g_host->settings_.Find(FullKey(k)) != nullptr;
    }
    static d3h_bool SetString(const char* k, const char* v) {
        (void)g_host->settings_.SetString(FullKey(k), v != nullptr ? v : "");
        return g_host->settings_.Find(FullKey(k)) != nullptr;
    }

    // ---- ui (ImGui) ----
    static void Text(const char* t) { ImGui::TextUnformatted(t); }
    static void TextColored(d3h_color c, const char* t) {
        ImGui::PushStyleColor(ImGuiCol_Text, ArgbToImColor(c));
        ImGui::TextUnformatted(t);
        ImGui::PopStyleColor();
    }
    static void TextDisabled(const char* t) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(t);
        ImGui::PopStyleColor();
    }
    static void Separator() { ImGui::Separator(); }
    static void SeparatorText(const char* t) { ImGui::SeparatorText(t); }
    static void SameLine() { ImGui::SameLine(); }
    static void Spacing() { ImGui::Spacing(); }
    static d3h_bool Button(const char* l) { return ImGui::Button(l); }
    static d3h_bool Checkbox(const char* l, d3h_bool* v) {
        bool b = *v != 0;
        const bool changed = ImGui::Checkbox(l, &b);
        *v = b;
        return changed;
    }
    static d3h_bool SliderInt(const char* l, int32_t* v, int32_t lo, int32_t hi) {
        int x = *v;
        const bool changed = ImGui::SliderInt(l, &x, lo, hi);
        *v = x;
        return changed;
    }
    static d3h_bool SliderFloat(const char* l, float* v, float lo, float hi, const char* fmt) {
        return ImGui::SliderFloat(l, v, lo, hi, SafeFormat(fmt));
    }
    static d3h_bool Combo(const char* l, int32_t* idx, const char* items) {
        int x = *idx;
        const bool changed = ImGui::Combo(l, &x, items);
        *idx = x;
        return changed;
    }
    static d3h_bool ColorEdit(const char* l, d3h_color* rgb) {
        float c[3] = {((*rgb >> 16) & 255) / 255.f, ((*rgb >> 8) & 255) / 255.f, (*rgb & 255) / 255.f};
        // a label starting with "##" is a compact swatch (click it for the picker), for use in a row of widgets
        const ImGuiColorEditFlags flags = l[0] == '#' && l[1] == '#' ? ImGuiColorEditFlags_NoInputs : 0;
        if (!ImGui::ColorEdit3(l, c, flags)) return 0;
        const auto b = [](float f) { return static_cast<uint32_t>(std::clamp(f, 0.f, 1.f) * 255.f + 0.5f); };
        *rgb = (b(c[0]) << 16) | (b(c[1]) << 8) | b(c[2]);
        return 1;
    }
    static d3h_bool InputText(const char* l, char* buf, size_t n) { return ImGui::InputText(l, buf, n); }
    static d3h_bool CollapsingHeader(const char* l) { return ImGui::CollapsingHeader(l); }
    static d3h_bool IsItemHovered() { return ImGui::IsItemHovered(); }
    static void Tooltip(const char* t) { ImGui::SetTooltip("%s", t); }
};

// ---- PluginHost -----------------------------------------------------------------------------------

PluginHost::PluginHost(HudRenderer& renderer) : renderer_(renderer) {
    g_host = this;
    host_.size = sizeof(host_);
    host_.api_version = D3H_API_VERSION;
    host_.log = HostFns::Log;
    host_.get_frame = HostFns::GetFrame;
    host_.get_actor = HostFns::GetActor;
    host_.get_item = HostFns::GetItem;
    host_.get_effect = HostFns::GetEffect;
    if (!powers_.Load(ExeDir() / "sno_powers.tsv")) std::print(stderr, "no sno_powers.tsv - effects get generic names\n");
    host_.world_to_screen = HostFns::WorldToScreenFn;
    host_.subscribe = HostFns::Subscribe;
    host_.draw_text = HostFns::DrawText;
    host_.register_page = HostFns::RegisterPage;
    host_.register_panel = HostFns::RegisterPanel;
    host_.register_box = HostFns::RegisterBox;
    host_.draw_line = HostFns::DrawLine;
    host_.draw_rect = HostFns::DrawRect;
    host_.text_size = HostFns::TextSize;
    host_.define_setting = HostFns::Define;
    host_.get_bool = HostFns::GetBool;
    host_.get_int = HostFns::GetInt;
    host_.get_float = HostFns::GetFloat;
    host_.get_color = HostFns::GetColor;
    host_.get_string = HostFns::GetString;
    host_.set_value = HostFns::SetValue;
    host_.set_string = HostFns::SetString;

    ui_.size = sizeof(ui_);
    ui_.text = HostFns::Text;
    ui_.text_colored = HostFns::TextColored;
    ui_.text_disabled = HostFns::TextDisabled;
    ui_.separator = HostFns::Separator;
    ui_.separator_text = HostFns::SeparatorText;
    ui_.same_line = HostFns::SameLine;
    ui_.spacing = HostFns::Spacing;
    ui_.button = HostFns::Button;
    ui_.checkbox = HostFns::Checkbox;
    ui_.slider_int = HostFns::SliderInt;
    ui_.slider_float = HostFns::SliderFloat;
    ui_.combo = HostFns::Combo;
    ui_.color_edit = HostFns::ColorEdit;
    ui_.input_text = HostFns::InputText;
    ui_.collapsing_header = HostFns::CollapsingHeader;
    ui_.is_item_hovered = HostFns::IsItemHovered;
    ui_.tooltip = HostFns::Tooltip;

    renderer_.AddSettingsPage({"plugins", "Plugins", "Plugins", 900, [this] { DrawPluginsPage(); }});
}

PluginHost::~PluginHost() {
    Shutdown();
    if (g_host == this) g_host = nullptr;
}

void PluginHost::LoadAll(const std::filesystem::path& pluginsDir, const std::filesystem::path& settingsFile) {
    namespace fs = std::filesystem;
    settingsFile_ = settingsFile;
    (void)settings_.Load(settingsFile_);  // none yet on a first start
    d3h_setting_desc off{};  // the host's own setting: names of the plugins the user switched off, comma separated
    off.size = sizeof(off);
    off.key = "disabled";
    off.type = D3H_SETTING_STRING;
    off.label = "switched off";
    off.def_string = "";
    off.flags = D3H_SETTING_HIDDEN;
    if (settings_.Define("plugins", off)) {
        std::string list;
        (void)settings_.GetString("plugins.disabled", list);
        size_t from = 0;
        while (from <= list.size()) {
            size_t comma = list.find(',', from);
            if (comma == std::string::npos) comma = list.size();
            if (comma > from) switchedOff_.insert(list.substr(from, comma - from));
            from = comma + 1;
        }
    }

    std::error_code ec;
    std::vector<fs::path> dirs;
    for (const fs::directory_entry& e : fs::directory_iterator(pluginsDir, ec))
        if (e.is_directory(ec) && fs::exists(e.path() / L"plugin.lua", ec)) dirs.push_back(e.path());
    std::sort(dirs.begin(), dirs.end());

    for (const fs::path& dir : dirs) {
        Entry entry;
        entry.folder = Utf8(dir.filename());
        std::string error;
        entry.plugin = LuaPlugin::Load(dir, host_, error);
        if (entry.plugin != nullptr) {
            entry.name = entry.plugin->manifest().name;
            entry.version = entry.plugin->manifest().version;
            AddSettingsPageFor(entry.name);
            ApplyEnabled(entry);
        } else {
            entry.name = entry.folder;
            entry.error = error;
            std::print(stderr, "plugin {} not loaded: {}\n", entry.folder, error);
        }
        plugins_.push_back(std::move(entry));
    }
}

void PluginHost::ApplyEnabled(const Entry& e) {
    if (e.plugin == nullptr) return;
    const bool off = switchedOff_.contains(e.name);
    e.plugin->SetEnabled(!off);
    if (const auto it = panelsOf_.find(e.name); it != panelsOf_.end())
        for (const std::string& id : it->second) renderer_.SetPluginPanelOff(id, off);
    if (const auto it = boxesOf_.find(e.name); it != boxesOf_.end())
        for (const std::string& id : it->second) renderer_.SetPluginBoxOff(id, off);
}

void PluginHost::SetPluginEnabled(Entry& e, bool on) {
    if (on) switchedOff_.erase(e.name);
    else switchedOff_.insert(e.name);
    ApplyEnabled(e);
    std::string list;
    for (const std::string& n : switchedOff_) list += (list.empty() ? "" : ",") + n;
    (void)settings_.SetString("plugins.disabled", list);  // saved with the other settings
}

bool PluginHost::HasExtras(const std::string& plugin) const {
    return std::ranges::any_of(settings_.Of(plugin), [](const PluginSettingDef* d) { return !d->hidden; }) ||
           panelsOf_.contains(plugin) || boxesOf_.contains(plugin);
}

// A plugin without a page of its own still gets one for its options and dock panels.
void PluginHost::AddSettingsPageFor(const std::string& plugin) {
    if (pluginsWithPage_.contains(plugin) || !HasExtras(plugin)) return;
    renderer_.AddSettingsPage({plugin + ".settings", plugin, "Plugins", 2000, [this, plugin] { DrawExtrasOf(plugin); }});
}

void PluginHost::DrawExtrasOf(const std::string& plugin) {
    const bool options = std::ranges::any_of(settings_.Of(plugin), [](const PluginSettingDef* d) { return !d->hidden; });
    if (options) {
        ImGui::SeparatorText("Options");
        DrawSettingsOf(plugin);
    }
    if (const auto it = panelsOf_.find(plugin); it != panelsOf_.end()) {
        ImGui::SeparatorText("Dock panel");
        for (const std::string& id : it->second) renderer_.DrawPluginPanelSettings(id);
    }
    if (const auto it = boxesOf_.find(plugin); it != boxesOf_.end()) {
        ImGui::SeparatorText("On the screen");
        for (const std::string& id : it->second) renderer_.DrawPluginBoxSettings(id);
    }
}

void PluginHost::DrawSettingsOf(const std::string& plugin) {
    for (const PluginSettingDef* d : settings_.Of(plugin)) {
        if (d->hidden) continue;
        ImGui::PushID(d->key.c_str());
        double v = 0;
        std::string s;
        bool changed = false;
        switch (d->type) {
            case D3H_SETTING_BOOL: {
                bool b = settings_.GetNumber(d->key, v) && v != 0.0;
                if (ImGui::Checkbox(d->label.c_str(), &b)) changed = settings_.SetNumber(d->key, b ? 1.0 : 0.0);
                break;
            }
            case D3H_SETTING_INT: {
                int x = settings_.GetNumber(d->key, v) ? static_cast<int>(v) : 0;
                const bool edited = d->max > d->min
                                        ? ImGui::SliderInt(d->label.c_str(), &x, static_cast<int>(d->min), static_cast<int>(d->max))
                                        : ImGui::InputInt(d->label.c_str(), &x);
                if (edited) changed = settings_.SetNumber(d->key, x);
                break;
            }
            case D3H_SETTING_FLOAT: {
                float x = settings_.GetNumber(d->key, v) ? static_cast<float>(v) : 0.f;
                const bool edited = d->max > d->min
                                        ? ImGui::SliderFloat(d->label.c_str(), &x, static_cast<float>(d->min), static_cast<float>(d->max))
                                        : ImGui::InputFloat(d->label.c_str(), &x);
                if (edited) changed = settings_.SetNumber(d->key, x);
                break;
            }
            case D3H_SETTING_COLOR: {
                d3h_color rgb = settings_.GetNumber(d->key, v) ? static_cast<d3h_color>(v) : 0;
                if (HostFns::ColorEdit(d->label.c_str(), &rgb)) changed = settings_.SetNumber(d->key, rgb);
                break;
            }
            case D3H_SETTING_STRING: {
                char buf[512] = {};
                if (settings_.GetString(d->key, s)) strncpy_s(buf, sizeof(buf), s.c_str(), _TRUNCATE);
                if (ImGui::InputText(d->label.c_str(), buf, sizeof(buf))) changed = settings_.SetString(d->key, buf);
                break;
            }
            case D3H_SETTING_CHOICE: {
                int x = settings_.GetNumber(d->key, v) ? static_cast<int>(v) : 0;
                if (ImGui::Combo(d->label.c_str(), &x, d->choices.c_str())) changed = settings_.SetNumber(d->key, x);
                break;
            }
            default: break;
        }
        if (!d->tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d->tooltip.c_str());
        ImGui::PopID();
        if (changed) Fire(D3H_EVENT_SETTING_CHANGED, -1, 0, d->key.c_str());
    }
}

void PluginHost::DrawPluginsPage() {
    ImGui::TextDisabled("Lua plugins from the plugins folder next to the exe. See PLUGIN_PLAN.md and plugins/ for examples.");
    if (plugins_.empty()) ImGui::TextUnformatted("No plugins found.");
    if (renderer_.PluginBoxCount() > 0) {
        if (ImGui::Button(renderer_.EditingBoxes() ? "Done moving" : "Move on-screen boxes"))
            renderer_.SetEditBoxes(!renderer_.EditingBoxes());
        ImGui::SameLine();
        ImGui::TextDisabled("Shows a frame around every box the plugins draw on the screen; drag them where you want them.");
    }
    double totalUs = 0;
    for (const Entry& e : plugins_)
        if (e.plugin != nullptr) totalUs += e.plugin->timing().avgUs;
    if (!plugins_.empty()) {
        ImGui::Text("Time in plugin code: %.0f us per frame (%.2f%% of a 60 fps frame)", totalUs, totalUs / 166.67);
        ImGui::TextDisabled("Per drawn frame, averaged over about a second. Includes event handlers and page / panel drawing.");
    }
    for (Entry& e : plugins_) {
        ImGui::SeparatorText(e.version.empty() ? e.name.c_str() : std::format("{} {}", e.name, e.version).c_str());
        if (e.plugin != nullptr) {
            bool on = e.plugin->enabled();
            if (ImGui::Checkbox(("enabled##" + e.folder).c_str(), &on)) SetPluginEnabled(e, on);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Off: the plugin's code stops at once (no events, no drawing) and its dock tabs go away.\n"
                                  "Remembered. Switching it on again also gives a plugin that stopped after errors another try.");
        }
        if (e.plugin == nullptr) {
            ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f), "not loaded: %s", e.error.c_str());
        } else if (!e.plugin->enabled()) {
            ImGui::TextDisabled("switched off");
        } else if (e.plugin->disabled()) {
            ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f), "disabled after %d errors: %s", e.plugin->errorCount(),
                               e.plugin->lastError().c_str());
        } else if (e.plugin->errorCount() > 0) {
            ImGui::TextColored(ImVec4(1.f, 0.8f, 0.3f, 1.f), "%d error(s), last: %s", e.plugin->errorCount(),
                               e.plugin->lastError().c_str());
        } else {
            ImGui::TextUnformatted("running");
        }
        if (e.plugin != nullptr) {
            const LuaPlugin::Timing& t = e.plugin->timing();
            ImGui::Text("%.0f us per frame (peak %.0f us, %.1f calls per frame, %.2f%% of a 60 fps frame)", t.avgUs, t.peakUs,
                        t.calls, t.avgUs / 166.67);
        }
    }
    if (!log_.empty()) {
        ImGui::SeparatorText("Log");
        for (const std::string& l : log_) ImGui::TextUnformatted(l.c_str());
    }
}

void PluginHost::Fire(int32_t type, int32_t index, int64_t value, const char* text) {
    const d3h_event ev{sizeof(d3h_event), type, index, value, text};
    for (const Sub& s : std::vector<Sub>(subs_))  // a copy: handlers may subscribe
        if (s.type == type) s.fn(&ev, s.user);
}

void PluginHost::OnFrame(const WorldSnapshot& snap, float screenW, float screenH) {
    for (const Entry& e : plugins_)
        if (e.plugin != nullptr) e.plugin->EndFrame();  // the previous frame's calls, page draws included
    snap_ = &snap;
    {
        LARGE_INTEGER freq, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&now);
        tickNow_ = TickAtTime(snap.gameTick, snap.timestampMs, 1000.0 * static_cast<double>(now.QuadPart) / static_cast<double>(freq.QuadPart));
    }
    screenW_ = screenW;
    screenH_ = screenH;
    if (snap.frameIndex != lastFrameIndex_) {
        lastFrameIndex_ = snap.frameIndex;
        if (snap.worldSnoId != lastWorld_) {
            lastWorld_ = snap.worldSnoId;
            Fire(D3H_EVENT_WORLD_CHANGE, -1, snap.worldSnoId);
        }
        const bool inv = snap.ui.inventoryOpen != 0;
        if (inv != lastInventory_) {
            lastInventory_ = inv;
            Fire(inv ? D3H_EVENT_INVENTORY_OPEN : D3H_EVENT_INVENTORY_CLOSE);
        }
        const int hover = snap.hoveredItemPlus1 > 0 ? snap.hoveredItemPlus1 - 1 : -1;
        if (hover != lastHover_) {
            lastHover_ = hover;
            Fire(D3H_EVENT_ITEM_HOVER, hover);
        }
        Fire(D3H_EVENT_SNAPSHOT);
    }
    Fire(D3H_EVENT_FRAME);  // snap_ stays valid for the rest of the frame: the pages draw after this

    sinceSave_ += ImGui::GetIO().DeltaTime;
    if (sinceSave_ >= 5.0) {
        sinceSave_ = 0;
        if (settings_.dirty()) Save();
    }
}

void PluginHost::Save() {
    if (!settingsFile_.empty() && !settings_.Save(settingsFile_))
        std::print(stderr, "cannot write {}\n", Utf8(settingsFile_));
}

void PluginHost::Shutdown() {
    snap_ = nullptr;
    if (settings_.dirty()) Save();
}

}  // namespace d3hud
