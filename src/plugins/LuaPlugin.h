// A Lua plugin: one sandboxed Lua state per plugin folder, talking to the host only through the
// plain-C host API (HostApi.h). Layout of a plugin folder:
//   plugin.lua  `return { name = "...", version = "...", api = 1 }`  (run with no globals at all)
//   main.lua    the plugin; sees the whitelisted standard library plus the `d3hud` table
// Safety: no io/os/debug/require/load*, text chunks only, a memory cap, an instruction budget per
// call, and a plugin that raises errors repeatedly is switched off. See docs in PLUGIN_PLAN.md.
#pragma once

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
    [[nodiscard]] int errorCount() const { return errors_; }
    [[nodiscard]] const std::string& lastError() const { return lastError_; }

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
    static void EventTrampoline(const d3h_event* ev, void* user);
    static void PageTrampoline(const d3h_ui* ui, void* user);

    const d3h_host& host_;
    lua_State* L_ = nullptr;
    std::unique_ptr<LuaHeap> heap_;
    PluginManifest manifest_;
    int errors_ = 0;
    std::string lastError_;
    int handlersRef_ = 0;  // registry table: event type -> array of Lua functions
    int uiRef_ = 0;        // registry: the `ui` table handed to page draw functions
    const d3h_ui* ui_ = nullptr;  // valid only while a page draws
    std::map<std::string, int> settingTypes_;  // key -> d3h_setting_type, for d3hud.get
    std::vector<int> subscribed_;
    std::vector<std::unique_ptr<PageCtx>> pages_;
};

}  // namespace d3hud
