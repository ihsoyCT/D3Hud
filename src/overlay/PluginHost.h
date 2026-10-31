// The overlay's side of the plugin system: implements the host API table (HostApi.h) on top of the
// current snapshot and Dear ImGui, loads the Lua plugins from <exe>/plugins/<name>/, routes events,
// adds the plugins' settings pages to the Settings window and persists their settings.
//
// Not provided by this host yet (the table entries are NULL; plugins must check): world_to_minimap,
// item_screen_rect, draw_circle / draw_quad, hot_rect, register_hotkey, set_salvage_policy, and the DROP event.
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Effects.h"
#include "ImageStore.h"
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
    // The graphics device the plugins' images become textures on; call before LoadAll.
    void SetDevice(ID3D11Device* device) { images_.SetDevice(device); }

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
    void DrawExtrasOf(const std::string& plugin);  // options + dock panel controls, at the end of the plugin's page
    bool HasExtras(const std::string& plugin) const;
    void AddSettingsPageFor(const std::string& plugin);
    // The Plugins page's checkbox: pauses the plugin and hides its dock tabs, remembered in plugins.ini.
    void SetPluginEnabled(Entry& e, bool on);
    void ApplyEnabled(const Entry& e);
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
    ImageStore images_;                                   // the plugins' pictures (d3hud.image)
    std::filesystem::path loadingDir_;                    // the folder of the plugin whose main.lua is running right now
    std::map<std::string, std::filesystem::path> pluginDirs_;  // plugin -> its folder
    std::map<std::string, std::set<std::string>> imagesOf_;    // plugin -> the images it has loaded
    std::set<std::string> switchedOff_;  // plugins the user switched off (plugins.disabled in plugins.ini)
    std::set<std::string> pluginsWithPage_;               // plugins that registered a page of their own
    std::map<std::string, std::vector<std::string>> panelsOf_;  // plugin -> its dock panel ids
    std::map<std::string, std::vector<std::string>> boxesOf_;   // plugin -> its movable box ids
    const WorldSnapshot* snap_ = nullptr;  // valid during OnFrame
    double tickNow_ = -1;  // the game tick of this frame (extrapolated from the snapshot), -1 unknown
    PowerNames powers_;    // names for the effects
    float screenW_ = 0, screenH_ = 0;
    uint64_t lastFrameIndex_ = ~0ull;
    uint32_t lastWorld_ = 0;
    bool lastInventory_ = false;
    int lastHover_ = -1;
    double sinceSave_ = 0;
    std::vector<std::string> log_;  // recent plugin log lines for the Plugins page
};

}  // namespace d3hud
