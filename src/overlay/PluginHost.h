// The overlay's side of the plugin system: implements the host API table (HostApi.h) on top of the
// current snapshot and Dear ImGui, loads the Lua plugins from <exe>/plugins/<name>/, routes events,
// adds the plugins' settings pages to the Settings window and persists their settings.
//
// Not provided by this host yet (the table entries are NULL; plugins must check): world_to_minimap,
// item_screen_rect, the draw_* primitives other than draw_text, register_panel, hot_rect,
// register_hotkey, set_salvage_policy, and the DROP event.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "LuaPlugin.h"
#include "PluginSettings.h"
#include "Snapshot.h"

namespace d3hud {

class HudRenderer;

class PluginHost {
public:
    explicit PluginHost(HudRenderer& renderer);
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    // Reads `settingsFile`, then loads every <pluginsDir>/*/ that has a plugin.lua. A plugin that
    // fails to load is listed on the Plugins page with its error; the others are unaffected.
    void LoadAll(const std::filesystem::path& pluginsDir, const std::filesystem::path& settingsFile);
    // Once per drawn frame, inside the ImGui frame and before the panels: fires SNAPSHOT (when the
    // snapshot advanced), WORLD_CHANGE / INVENTORY_* / ITEM_HOVER on changes, then FRAME.
    void OnFrame(const WorldSnapshot& snap, float screenW, float screenH);
    void Shutdown();  // saves the settings

    struct Entry {
        std::string folder, name, version, error;
        std::unique_ptr<LuaPlugin> plugin;  // null when loading failed
    };
    [[nodiscard]] const std::vector<Entry>& plugins() const { return plugins_; }

private:
    struct HostFns;  // the C callbacks (PluginHost.cpp)
    friend struct HostFns;

    void Fire(int32_t type, int32_t index = -1, int64_t value = 0, const char* text = nullptr);
    void DrawPluginsPage();
    void DrawSettingsOf(const std::string& plugin);
    void AddSettingsPageFor(const std::string& plugin);
    void Save();

    HudRenderer& renderer_;
    d3h_host host_{};
    d3h_ui ui_{};
    PluginSettings settings_;
    std::filesystem::path settingsFile_;
    std::vector<Entry> plugins_;
    struct Sub {
        int32_t type;
        d3h_event_fn fn;
        void* user;
        const std::string* plugin;
    };
    std::vector<Sub> subs_;
    const WorldSnapshot* snap_ = nullptr;  // valid during OnFrame
    float screenW_ = 0, screenH_ = 0;
    uint64_t lastFrameIndex_ = ~0ull;
    uint32_t lastWorld_ = 0;
    bool lastInventory_ = false;
    int lastHover_ = -1;
    double sinceSave_ = 0;
    std::vector<std::string> log_;  // recent plugin log lines for the Plugins page
};

}  // namespace d3hud
