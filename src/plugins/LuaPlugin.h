// A Lua plugin: one sandboxed Lua state per plugin folder, talking to the host only through the
// plain-C host API (HostApi.h). Layout of a plugin folder:
//   plugin.lua  `return { name = "...", version = "...", api = 1 }`  (run with no globals at all)
//   main.lua    the plugin; sees the whitelisted standard library plus the `d3hud` table
// Safety: no io/os/debug/require/load*, text chunks only, a memory cap, an instruction budget per
// call, and a plugin that raises errors repeatedly is switched off. Shared Lua libraries (plugins/lib/<name>.lua)
// are loaded with d3hud.require("name"): by plain name only, into the plugin's own sandbox. See docs in PLUGIN_PLAN.md.
#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "HostApi.h"

struct lua_State;

namespace d3hud {

struct LuaHeap {  // the capped allocator state of one plugin
    size_t used = 0;
    size_t limit = 32u << 20;
};

struct PluginManifest {
    std::string name, version;
    int api = 0;
};

// The plugin whose code is running right now (nullptr outside plugin code). The host functions
// use it to namespace settings and to attribute log lines; all plugin code runs on the UI thread.
[[nodiscard]] const std::string* CurrentPluginName();

class LuaPlugin {
public:
    static constexpr int kMaxErrors = 5;                    // then the plugin is disabled
    static constexpr int kInstructionBudget = 2'000'000;    // per call from the host
    static constexpr size_t kMemoryLimit = 32u << 20;       // bytes of Lua heap

    // Loads and runs main.lua. On failure returns nullptr and says why in `error`.
    [[nodiscard]] static std::unique_ptr<LuaPlugin> Load(const std::filesystem::path& dir, const d3h_host& host,
                                                         std::string& error);
    ~LuaPlugin();
    LuaPlugin(const LuaPlugin&) = delete;
    LuaPlugin& operator=(const LuaPlugin&) = delete;

    [[nodiscard]] const PluginManifest& manifest() const { return manifest_; }
    [[nodiscard]] bool disabled() const { return errors_ >= kMaxErrors; }
    // The user's switch (Settings > Plugins): off = no events, no panel or page drawing, no cost, at once. Switching
    // it on again also clears the error count, so a plugin that gave up after repeated errors gets another try.
    void SetEnabled(bool on);
    [[nodiscard]] bool enabled() const { return enabled_; }
    [[nodiscard]] int errorCount() const { return errors_; }
    [[nodiscard]] const std::string& lastError() const { return lastError_; }

    // Time spent inside the plugin's code (events, page and panel draws), per drawn frame. The figures
    // are published once per kTimingWindowSec (the average and the worst frame of that window), so a
    // display of them stays readable instead of flickering with every frame.
    static constexpr double kTimingWindowSec = 0.5;
    struct Timing {
        double avgUs = 0;   // average per frame over the last window
        double peakUs = 0;  // worst frame of the last window
        double calls = 0;   // average calls into the plugin per frame
    };
    // Closes the books of one drawn frame; the host calls it once per frame.
    void EndFrame(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    [[nodiscard]] const Timing& timing() const { return timing_; }

private:
    struct PageCtx {
        LuaPlugin* plugin;
        int ref;  // registry reference of the page's draw function
    };
    LuaPlugin(const d3h_host& host) : host_(host) {}

    friend struct LuaBinding;  // the d3hud.* functions (LuaPlugin.cpp)

    bool Run(int nargs, int nresults);  // calls the function on the stack under the budget; false = error (already counted)
    void Fail(const std::string& msg);
    void OnEvent(const d3h_event& ev);
    void DrawPage(PageCtx& page, const d3h_ui& ui);
    void DrawBox(PageCtx& box, d3h_vec2 origin, d3h_vec2* size);
    static void EventTrampoline(const d3h_event* ev, void* user);
    static void PageTrampoline(const d3h_ui* ui, void* user);
    static void BoxTrampoline(d3h_vec2 origin, d3h_vec2* size, void* user);

    const d3h_host& host_;
    lua_State* L_ = nullptr;
    std::unique_ptr<LuaHeap> heap_;
    PluginManifest manifest_;
    int errors_ = 0;
    bool enabled_ = true;
    Timing timing_;
    double accumUs_ = 0;  // the frame in progress
    int accumCalls_ = 0;
    double windowUs_ = 0, windowPeakUs_ = 0, windowCalls_ = 0;  // the window in progress
    int windowFrames_ = 0;
    std::chrono::steady_clock::time_point windowStart_{};
    std::string lastError_;
    int handlersRef_ = 0;  // registry table: event type -> array of Lua functions
    int envRef_ = 0;       // registry: the sandbox environment (shared libraries run in it too)
    int libsRef_ = 0;      // registry: loaded libraries, name -> value (false while loading)
    std::filesystem::path libDir_;  // <plugins>/lib: what d3hud.require can load
    int uiRef_ = 0;        // registry: the `ui` table handed to page draw functions
    const d3h_ui* ui_ = nullptr;  // valid only while a page draws
    std::map<std::string, int> settingTypes_;  // key -> d3h_setting_type, for d3hud.get
    std::vector<int> subscribed_;
    std::vector<std::unique_ptr<PageCtx>> pages_;
};

}  // namespace d3hud
