#include "LuaPlugin.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "Paths.h"

namespace d3hud {

namespace {

const std::string* g_current = nullptr;

// Marks plugin code as running for the length of a scope.
struct PluginScope {
    explicit PluginScope(const std::string* name) : prev(g_current) { g_current = name; }
    ~PluginScope() { g_current = prev; }
    PluginScope(const PluginScope&) = delete;
    PluginScope& operator=(const PluginScope&) = delete;
    const std::string* prev;
};

constexpr size_t kMaxScriptBytes = 1u << 20;

bool ReadScript(const std::filesystem::path& path, std::string& out) {
    File f = OpenFile(path, L"rb");
    if (f == nullptr) return false;
    char buf[4096];
    out.clear();
    for (size_t n; (n = fread(buf, 1, sizeof(buf), f)) > 0;) {
        out.append(buf, n);
        if (out.size() > kMaxScriptBytes) return false;
    }
    return true;
}

struct EventName {
    const char* name;
    d3h_event_type type;
};
constexpr EventName kEvents[] = {
    {"frame", D3H_EVENT_FRAME},
    {"snapshot", D3H_EVENT_SNAPSHOT},
    {"drop", D3H_EVENT_DROP},
    {"inventory_open", D3H_EVENT_INVENTORY_OPEN},
    {"inventory_close", D3H_EVENT_INVENTORY_CLOSE},
    {"world_change", D3H_EVENT_WORLD_CHANGE},
    {"item_hover", D3H_EVENT_ITEM_HOVER},
    {"hotkey", D3H_EVENT_HOTKEY},
    {"setting_changed", D3H_EVENT_SETTING_CHANGED},
};

constexpr const char* kSettingTypes[] = {"bool", "int", "float", "color", "string", "choice", nullptr};

}  // namespace

const std::string* CurrentPluginName() { return g_current; }

// The Lua heap with a hard cap, so a runaway script cannot eat the overlay's memory.
static void* LuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* h = static_cast<LuaHeap*>(ud);
    if (ptr == nullptr) osize = 0;
    if (nsize == 0) {
        if (ptr != nullptr) {
            h->used -= osize;
            free(ptr);
        }
        return nullptr;
    }
    if (h->used - osize + nsize > h->limit) return nullptr;
    void* p = realloc(ptr, nsize);
    if (p != nullptr) h->used = h->used - osize + nsize;
    return p;
}

static void BudgetHook(lua_State* L, lua_Debug*) { luaL_error(L, "script used too many instructions (endless loop?)"); }

// ---- the d3hud.* functions -------------------------------------------------------------------

struct LuaBinding {
    static LuaPlugin* Self(lua_State* L) { return static_cast<LuaPlugin*>(lua_touserdata(L, lua_upvalueindex(1))); }

    static void SetNum(lua_State* L, const char* k, double v) {
        lua_pushnumber(L, v);
        lua_setfield(L, -2, k);
    }
    static void SetBool(lua_State* L, const char* k, bool v) {
        lua_pushboolean(L, v);
        lua_setfield(L, -2, k);
    }
    static void SetStr(lua_State* L, const char* k, const char* v) {
        lua_pushstring(L, v);
        lua_setfield(L, -2, k);
    }

    static int Log(lua_State* L) {
        const char* msg = luaL_checkstring(L, 1);
        static const char* const levels[] = {"info", "warn", "error", nullptr};
        const int level = luaL_checkoption(L, 2, "info", levels);
        Self(L)->host_.log(level, msg);
        return 0;
    }

    static int Frame(lua_State* L) {
        d3h_frame f{};
        f.size = sizeof(f);
        Self(L)->host_.get_frame(&f);
        lua_createtable(L, 0, 20);
        SetNum(L, "frame_index", static_cast<double>(f.frame_index));
        SetNum(L, "time_ms", f.time_ms);
        SetNum(L, "player_x", f.player_pos.x);
        SetNum(L, "player_y", f.player_pos.y);
        SetNum(L, "player_heading", f.player_heading);
        SetNum(L, "world_sno", f.world_sno);
        SetNum(L, "actor_count", f.actor_count);
        SetNum(L, "item_count", f.item_count);
        SetNum(L, "hovered_item", f.hovered_item >= 0 ? f.hovered_item + 1 : 0);  // 1-based, 0 = none
        SetNum(L, "screen_w", f.screen_w);
        SetNum(L, "screen_h", f.screen_h);
        SetNum(L, "rift_progress", f.rift_progress);
        SetNum(L, "rift_timer_fraction", f.rift_timer_fraction);
        SetNum(L, "stash_tab", f.stash_tab);
        lua_createtable(L, 0, 8);
        SetBool(L, "inventory", (f.ui_flags & D3H_UI_INVENTORY) != 0);
        SetBool(L, "stash", (f.ui_flags & D3H_UI_STASH) != 0);
        SetBool(L, "salvage", (f.ui_flags & D3H_UI_SALVAGE) != 0);
        SetBool(L, "big_map", (f.ui_flags & D3H_UI_BIG_MAP) != 0);
        SetBool(L, "minimap", (f.ui_flags & D3H_UI_MINIMAP) != 0);
        SetBool(L, "rift", (f.ui_flags & D3H_UI_RIFT) != 0);
        SetBool(L, "greater_rift", (f.ui_flags & D3H_UI_GREATER_RIFT) != 0);
        lua_setfield(L, -2, "ui");
        return 1;
    }

    static const char* KindName(int32_t k) {
        static const char* const names[] = {"player", "trash", "champion", "rare", "minion", "boss", "pylon", "shrine",
                                            "chest", "globe", "ground_item", "portal", "pylon_spawn", "rift_orb",
                                            "rift_orb_tiered"};
        return k >= 0 && k < static_cast<int32_t>(std::size(names)) ? names[k] : "unknown";
    }

    // The actor as a Lua table: everything the snapshot knows about it.
    static void PushActor(lua_State* L, const d3h_actor& a, const d3h_frame& f) {
        lua_createtable(L, 0, 20);
        SetNum(L, "id", static_cast<double>(a.id));
        SetNum(L, "x", a.pos.x);
        SetNum(L, "y", a.pos.y);
        SetNum(L, "dist", std::hypot(a.pos.x - f.player_pos.x, a.pos.y - f.player_pos.y));  // world units from the player
        SetNum(L, "heading", a.heading);
        SetNum(L, "hp", a.hp_fraction);  // 0..1, -1 = not read
        SetNum(L, "hp_current", a.hp_current);
        SetNum(L, "hp_max", a.hp_max);
        SetNum(L, "kind", a.kind);
        SetStr(L, "kind_name", KindName(a.kind));
        SetNum(L, "rarity", a.rarity);
        SetNum(L, "flags", a.flags);
        SetBool(L, "ancient", (a.flags & D3H_FLAG_ANCIENT) != 0);
        SetBool(L, "primal", (a.flags & D3H_FLAG_PRIMAL) != 0);
        SetBool(L, "bonus", (a.flags & D3H_FLAG_BONUS) != 0);
        SetBool(L, "unidentified", (a.flags & D3H_FLAG_UNIDENTIFIED) != 0);
        SetBool(L, "used", (a.flags & D3H_FLAG_USED) != 0);  // pylon / shrine already clicked
        SetStr(L, "name", a.name);
    }

    // d3hud.actor(i) -> table or nil (1-based)
    static int Actor(lua_State* L) {
        const lua_Integer i = luaL_checkinteger(L, 1);
        d3h_actor a{};
        a.size = sizeof(a);
        d3h_frame f{};
        f.size = sizeof(f);
        if (i < 1 || !Self(L)->host_.get_actor(static_cast<uint32_t>(i - 1), &a)) return 0;
        Self(L)->host_.get_frame(&f);
        PushActor(L, a, f);
        return 1;
    }

    // d3hud.actors() -> array of actor tables: every actor of the current snapshot
    static int Actors(lua_State* L) {
        const d3h_host& h = Self(L)->host_;
        d3h_frame f{};
        f.size = sizeof(f);
        h.get_frame(&f);
        lua_createtable(L, static_cast<int>(f.actor_count), 0);
        for (uint32_t i = 0; i < f.actor_count; ++i) {
            d3h_actor a{};
            a.size = sizeof(a);
            if (!h.get_actor(i, &a)) break;
            PushActor(L, a, f);
            lua_rawseti(L, -2, static_cast<lua_Integer>(i) + 1);
        }
        return 1;
    }

    static int Item(lua_State* L) {
        const lua_Integer i = luaL_checkinteger(L, 1);
        d3h_item it{};
        it.size = sizeof(it);
        if (i < 1 || !Self(L)->host_.get_item(static_cast<uint32_t>(i - 1), &it)) return 0;
        lua_createtable(L, 0, 14);
        SetNum(L, "id", static_cast<double>(it.id));
        SetNum(L, "sno", it.sno);
        SetNum(L, "rarity", it.rarity);
        SetNum(L, "verdict", it.verdict);
        SetNum(L, "flags", it.flags);
        SetNum(L, "location", it.location);
        SetNum(L, "grid_x", it.grid_x);
        SetNum(L, "grid_y", it.grid_y);
        SetNum(L, "height", it.height);
        SetNum(L, "augment_rank", it.augment_rank);
        SetStr(L, "name", it.name);
        SetStr(L, "type_name", it.type_name);
        return 1;
    }

    // d3hud.world_to_screen(x, y [, height]) -> sx, sy  (nil when off screen)
    static int WorldToScreenFn(lua_State* L) {
        const d3h_vec2 w{static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2))};
        d3h_vec2 out{};
        if (!Self(L)->host_.world_to_screen(w, static_cast<float>(luaL_optnumber(L, 3, 0.0)), &out)) return 0;
        lua_pushnumber(L, out.x);
        lua_pushnumber(L, out.y);
        return 2;
    }

    // d3hud.text_size(text [, scale]) -> width, height in pixels
    static int TextSize(lua_State* L) {
        const d3h_vec2 v = Self(L)->host_.text_size(luaL_checkstring(L, 1), static_cast<float>(luaL_optnumber(L, 2, 1.0)));
        lua_pushnumber(L, v.x);
        lua_pushnumber(L, v.y);
        return 2;
    }

    static int DrawText(lua_State* L) {
        const d3h_vec2 pos{static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2))};
        const auto color = static_cast<d3h_color>(luaL_checkinteger(L, 3));
        const char* text = luaL_checkstring(L, 4);
        const float scale = static_cast<float>(luaL_optnumber(L, 5, 1.0));
        Self(L)->host_.draw_text(D3H_LAYER_OVERLAY, pos, color, text, scale);
        return 0;
    }

    // d3hud.on("frame", function(ev) ... end)
    static int On(lua_State* L) {
        LuaPlugin* p = Self(L);
        const char* name = luaL_checkstring(L, 1);
        luaL_checktype(L, 2, LUA_TFUNCTION);
        int type = 0;
        for (const EventName& e : kEvents)
            if (!strcmp(name, e.name)) type = e.type;
        if (type == 0) return luaL_error(L, "unknown event '%s'", name);
        lua_rawgeti(L, LUA_REGISTRYINDEX, p->handlersRef_);
        lua_rawgeti(L, -1, type);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_createtable(L, 4, 0);
            lua_pushvalue(L, -1);
            lua_rawseti(L, -3, type);
        }
        lua_pushvalue(L, 2);
        lua_rawseti(L, -2, static_cast<lua_Integer>(lua_rawlen(L, -2)) + 1);
        if (std::find(p->subscribed_.begin(), p->subscribed_.end(), type) == p->subscribed_.end()) {
            p->subscribed_.push_back(type);
            p->host_.subscribe(type, &LuaPlugin::EventTrampoline, p);
        }
        return 0;
    }

    static const char* StrField(lua_State* L, int t, const char* k) {
        lua_getfield(L, t, k);
        const char* s = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : nullptr;
        lua_pop(L, 1);  // the string stays alive in the table
        return s;
    }
    static double NumField(lua_State* L, int t, const char* k, double def) {
        lua_getfield(L, t, k);
        const double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : lua_isboolean(L, -1) ? lua_toboolean(L, -1) : def;
        lua_pop(L, 1);
        return v;
    }

    // d3hud.setting{ key=, type=, label=, tooltip=, default=, min=, max=, choices={...}, hidden= }
    static int Setting(lua_State* L) {
        LuaPlugin* p = Self(L);
        luaL_checktype(L, 1, LUA_TTABLE);
        const char* key = StrField(L, 1, "key");
        const char* typeName = StrField(L, 1, "type");
        if (key == nullptr || typeName == nullptr) return luaL_error(L, "setting needs key and type");
        int type = 0;
        for (int i = 0; kSettingTypes[i] != nullptr; ++i)
            if (!strcmp(typeName, kSettingTypes[i])) type = i + 1;
        if (type == 0) return luaL_error(L, "setting '%s': unknown type '%s'", key, typeName);
        d3h_setting_desc d{};
        d.size = sizeof(d);
        d.key = key;
        d.type = type;
        d.label = StrField(L, 1, "label");
        d.tooltip = StrField(L, 1, "tooltip");
        d.min = NumField(L, 1, "min", 0);
        d.max = NumField(L, 1, "max", 0);
        d.def = NumField(L, 1, "default", 0);
        if (type == D3H_SETTING_CHOICE) d.def -= 1;  // Lua indexes are 1-based
        d.def_string = StrField(L, 1, "default");
        if (type != D3H_SETTING_STRING) d.def_string = nullptr;
        if (NumField(L, 1, "hidden", 0) != 0) d.flags |= D3H_SETTING_HIDDEN;
        std::string choices;
        if (type == D3H_SETTING_CHOICE) {
            lua_getfield(L, 1, "choices");
            if (lua_istable(L, -1)) {
                for (lua_Integer i = 1;; ++i) {
                    lua_rawgeti(L, -1, i);
                    const char* c = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
                    if (c == nullptr) {
                        lua_pop(L, 1);
                        break;
                    }
                    choices += c;
                    choices += '\0';
                    lua_pop(L, 1);
                }
                d.min = 0;
                d.max = std::max<double>(0, static_cast<double>(std::count(choices.begin(), choices.end(), '\0')) - 1);
            }
            lua_pop(L, 1);
            choices += '\0';
            d.choices = choices.c_str();
        }
        if (!p->host_.define_setting(&d)) return luaL_error(L, "setting '%s' was rejected by the host", key);
        p->settingTypes_[key] = type;
        return 0;
    }

    static int Get(lua_State* L) {
        LuaPlugin* p = Self(L);
        const char* key = luaL_checkstring(L, 1);
        const auto it = p->settingTypes_.find(key);
        if (it == p->settingTypes_.end()) return luaL_error(L, "unknown setting '%s' (define it first)", key);
        const d3h_host& h = p->host_;
        bool ok = false;
        switch (it->second) {
            case D3H_SETTING_BOOL: { d3h_bool v = 0; ok = h.get_bool(key, &v); lua_pushboolean(L, v != 0); break; }
            case D3H_SETTING_INT: { int32_t v = 0; ok = h.get_int(key, &v); lua_pushinteger(L, v); break; }
            case D3H_SETTING_CHOICE: { int32_t v = 0; ok = h.get_int(key, &v); lua_pushinteger(L, v + 1); break; }
            case D3H_SETTING_FLOAT: { float v = 0; ok = h.get_float(key, &v); lua_pushnumber(L, v); break; }
            case D3H_SETTING_COLOR: { d3h_color v = 0; ok = h.get_color(key, &v); lua_pushinteger(L, v); break; }
            default: {
                std::string buf(4097, '\0');
                ok = h.get_string(key, buf.data(), buf.size());
                lua_pushstring(L, buf.c_str());
                break;
            }
        }
        if (!ok) return luaL_error(L, "setting '%s' could not be read", key);
        return 1;
    }

    static int Set(lua_State* L) {
        LuaPlugin* p = Self(L);
        const char* key = luaL_checkstring(L, 1);
        const auto it = p->settingTypes_.find(key);
        if (it == p->settingTypes_.end()) return luaL_error(L, "unknown setting '%s' (define it first)", key);
        switch (it->second) {
            case D3H_SETTING_BOOL: (void)p->host_.set_value(key, lua_toboolean(L, 2) ? 1.0 : 0.0); break;
            case D3H_SETTING_CHOICE: (void)p->host_.set_value(key, luaL_checknumber(L, 2) - 1.0); break;
            case D3H_SETTING_STRING: (void)p->host_.set_string(key, luaL_checkstring(L, 2)); break;
            default: (void)p->host_.set_value(key, luaL_checknumber(L, 2)); break;
        }
        return 0;
    }

    // d3hud.page{ id=, title=, group=, order=, draw=function(ui) ... end }
    static int Page(lua_State* L) {
        LuaPlugin* p = Self(L);
        luaL_checktype(L, 1, LUA_TTABLE);
        const char* id = StrField(L, 1, "id");
        const char* title = StrField(L, 1, "title");
        if (id == nullptr || title == nullptr) return luaL_error(L, "page needs id and title");
        lua_getfield(L, 1, "draw");
        if (!lua_isfunction(L, -1)) return luaL_error(L, "page '%s' needs a draw function", id);
        auto ctx = std::make_unique<LuaPlugin::PageCtx>();
        ctx->plugin = p;
        ctx->ref = luaL_ref(L, LUA_REGISTRYINDEX);  // pops the function
        d3h_page_desc d{};
        d.size = sizeof(d);
        d.id = id;
        d.title = title;
        d.group = StrField(L, 1, "group");
        if (d.group == nullptr) d.group = "Plugins";
        d.order = static_cast<int32_t>(NumField(L, 1, "order", 1000));
        d.draw = &LuaPlugin::PageTrampoline;
        d.user = ctx.get();
        if (!p->host_.register_page(&d)) return luaL_error(L, "page '%s' was rejected by the host", id);
        p->pages_.push_back(std::move(ctx));
        return 0;
    }

    // d3hud.panel{ id=, title=, width=, height=, draw=function(ui) ... end }: a dock panel (hover peek, pin, drag)
    static int Panel(lua_State* L) {
        LuaPlugin* p = Self(L);
        luaL_checktype(L, 1, LUA_TTABLE);
        const char* id = StrField(L, 1, "id");
        const char* title = StrField(L, 1, "title");
        if (id == nullptr || title == nullptr) return luaL_error(L, "panel needs id and title");
        lua_getfield(L, 1, "draw");
        if (!lua_isfunction(L, -1)) return luaL_error(L, "panel '%s' needs a draw function", id);
        auto ctx = std::make_unique<LuaPlugin::PageCtx>();
        ctx->plugin = p;
        ctx->ref = luaL_ref(L, LUA_REGISTRYINDEX);
        d3h_panel_desc d{};
        d.size = sizeof(d);
        d.id = id;
        d.title = title;
        d.draw = &LuaPlugin::PageTrampoline;  // a panel draws like a page: draw(ui)
        d.user = ctx.get();
        d.default_w = static_cast<float>(NumField(L, 1, "width", 0));
        d.default_h = static_cast<float>(NumField(L, 1, "height", 0));
        if (!p->host_.register_panel(&d)) return luaL_error(L, "panel '%s' was rejected by the host", id);
        p->pages_.push_back(std::move(ctx));
        return 0;
    }

    static d3h_color ColorArg(lua_State* L, int i) { return static_cast<d3h_color>(luaL_checkinteger(L, i)); }

    // d3hud.draw_rect(x0, y0, x1, y1, argb [, filled = true [, thickness]])
    static int DrawRect(lua_State* L) {
        const d3h_rect r{static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2)),
                         static_cast<float>(luaL_checknumber(L, 3)), static_cast<float>(luaL_checknumber(L, 4))};
        const bool filled = lua_isnoneornil(L, 6) ? true : lua_toboolean(L, 6) != 0;
        Self(L)->host_.draw_rect(D3H_LAYER_OVERLAY, r, ColorArg(L, 5), filled, static_cast<float>(luaL_optnumber(L, 7, 1.0)));
        return 0;
    }

    // d3hud.draw_line(x0, y0, x1, y1, argb [, thickness])
    static int DrawLine(lua_State* L) {
        const d3h_vec2 a{static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2))};
        const d3h_vec2 b{static_cast<float>(luaL_checknumber(L, 3)), static_cast<float>(luaL_checknumber(L, 4))};
        Self(L)->host_.draw_line(D3H_LAYER_OVERLAY, a, b, ColorArg(L, 5), static_cast<float>(luaL_optnumber(L, 6, 1.0)));
        return 0;
    }

    // ---- ui.* (valid only while a page draws) ----
    static const d3h_ui& Ui(lua_State* L) {
        LuaPlugin* p = Self(L);
        if (p->ui_ == nullptr) luaL_error(L, "ui functions work only inside a page's draw function");
        return *p->ui_;
    }
    static int UiText(lua_State* L) { Ui(L).text(luaL_checkstring(L, 1)); return 0; }
    static int UiTextDisabled(lua_State* L) { Ui(L).text_disabled(luaL_checkstring(L, 1)); return 0; }
    static int UiTextColored(lua_State* L) {
        Ui(L).text_colored(static_cast<d3h_color>(luaL_checkinteger(L, 1)), luaL_checkstring(L, 2));
        return 0;
    }
    static int UiSeparator(lua_State* L) { Ui(L).separator(); return 0; }
    static int UiSeparatorText(lua_State* L) { Ui(L).separator_text(luaL_checkstring(L, 1)); return 0; }
    static int UiSameLine(lua_State* L) { Ui(L).same_line(); return 0; }
    static int UiSpacing(lua_State* L) { Ui(L).spacing(); return 0; }
    static int UiButton(lua_State* L) { lua_pushboolean(L, Ui(L).button(luaL_checkstring(L, 1)) != 0); return 1; }
    static int UiHeader(lua_State* L) { lua_pushboolean(L, Ui(L).collapsing_header(luaL_checkstring(L, 1)) != 0); return 1; }
    static int UiHovered(lua_State* L) { lua_pushboolean(L, Ui(L).is_item_hovered() != 0); return 1; }
    static int UiTooltip(lua_State* L) { Ui(L).tooltip(luaL_checkstring(L, 1)); return 0; }
    // widgets that edit a value return (changed, new value)
    static int UiCheckbox(lua_State* L) {
        d3h_bool v = lua_toboolean(L, 2);
        const bool changed = Ui(L).checkbox(luaL_checkstring(L, 1), &v) != 0;
        lua_pushboolean(L, changed);
        lua_pushboolean(L, v != 0);
        return 2;
    }
    static int UiSliderInt(lua_State* L) {
        int32_t v = static_cast<int32_t>(luaL_checkinteger(L, 2));
        const bool changed = Ui(L).slider_int(luaL_checkstring(L, 1), &v, static_cast<int32_t>(luaL_checkinteger(L, 3)),
                                              static_cast<int32_t>(luaL_checkinteger(L, 4))) != 0;
        lua_pushboolean(L, changed);
        lua_pushinteger(L, v);
        return 2;
    }
    static int UiSliderFloat(lua_State* L) {
        float v = static_cast<float>(luaL_checknumber(L, 2));
        const bool changed = Ui(L).slider_float(luaL_checkstring(L, 1), &v, static_cast<float>(luaL_checknumber(L, 3)),
                                                static_cast<float>(luaL_checknumber(L, 4)), luaL_optstring(L, 5, "%.2f")) != 0;
        lua_pushboolean(L, changed);
        lua_pushnumber(L, v);
        return 2;
    }
    static int UiColor(lua_State* L) {
        d3h_color v = static_cast<d3h_color>(luaL_checkinteger(L, 2));
        const bool changed = Ui(L).color_edit(luaL_checkstring(L, 1), &v) != 0;
        lua_pushboolean(L, changed);
        lua_pushinteger(L, v);
        return 2;
    }
    static int UiInput(lua_State* L) {
        char buf[256] = {};
        const char* cur = luaL_checkstring(L, 2);
        strncpy_s(buf, sizeof(buf), cur, _TRUNCATE);
        const bool changed = Ui(L).input_text(luaL_checkstring(L, 1), buf, sizeof(buf)) != 0;
        lua_pushboolean(L, changed);
        lua_pushstring(L, buf);
        return 2;
    }
    // ui.combo(label, index (1-based), {"A", "B"})
    static int UiCombo(lua_State* L) {
        luaL_checktype(L, 3, LUA_TTABLE);
        std::string items;
        for (lua_Integer i = 1;; ++i) {
            lua_rawgeti(L, 3, i);
            const char* c = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
            lua_pop(L, 1);
            if (c == nullptr) break;
            items += c;
            items += '\0';
        }
        items += '\0';
        int32_t idx = static_cast<int32_t>(luaL_checkinteger(L, 2)) - 1;
        const bool changed = Ui(L).combo(luaL_checkstring(L, 1), &idx, items.c_str()) != 0;
        lua_pushboolean(L, changed);
        lua_pushinteger(L, idx + 1);
        return 2;
    }

    static void Register(lua_State* L, LuaPlugin* p, const char* name, lua_CFunction fn) {
        lua_pushlightuserdata(L, p);
        lua_pushcclosure(L, fn, 1);
        lua_setfield(L, -2, name);
    }
};

// ---- LuaPlugin ----------------------------------------------------------------------------------

void LuaPlugin::Fail(const std::string& msg) {
    ++errors_;
    lastError_ = msg;
    const std::string line = std::format("[{}] {}{}", manifest_.name, msg, disabled() ? " (plugin disabled)" : "");
    host_.log(D3H_LOG_ERROR, line.c_str());
}

void LuaPlugin::EndFrame(std::chrono::steady_clock::time_point now) {
    if (windowFrames_ == 0) windowStart_ = now;
    windowUs_ += accumUs_;
    windowCalls_ += accumCalls_;
    windowPeakUs_ = std::max(windowPeakUs_, accumUs_);
    ++windowFrames_;
    accumUs_ = 0;
    accumCalls_ = 0;
    if (std::chrono::duration<double>(now - windowStart_).count() < kTimingWindowSec) return;
    timing_.avgUs = windowUs_ / windowFrames_;
    timing_.peakUs = windowPeakUs_;
    timing_.calls = windowCalls_ / windowFrames_;
    windowUs_ = windowPeakUs_ = windowCalls_ = 0;
    windowFrames_ = 0;
}

bool LuaPlugin::Run(int nargs, int nresults) {
    const PluginScope scope(&manifest_.name);
    const auto started = std::chrono::steady_clock::now();
    lua_sethook(L_, BudgetHook, LUA_MASKCOUNT, kInstructionBudget);
    const int rc = lua_pcall(L_, nargs, nresults, 0);
    lua_sethook(L_, nullptr, 0, 0);
    accumUs_ += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
    ++accumCalls_;
    if (rc == LUA_OK) return true;
    Fail(lua_isstring(L_, -1) ? lua_tostring(L_, -1) : "(error object)");
    lua_pop(L_, 1);
    return false;
}

void LuaPlugin::OnEvent(const d3h_event& ev) {
    if (disabled() || L_ == nullptr) return;
    lua_rawgeti(L_, LUA_REGISTRYINDEX, handlersRef_);
    lua_rawgeti(L_, -1, ev.type);
    if (!lua_istable(L_, -1)) {
        lua_pop(L_, 2);
        return;
    }
    const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L_, -1));
    for (lua_Integer i = 1; i <= n && !disabled(); ++i) {
        lua_rawgeti(L_, -1, i);
        lua_createtable(L_, 0, 4);
        for (const EventName& e : kEvents)
            if (e.type == ev.type) LuaBinding::SetStr(L_, "type", e.name);
        LuaBinding::SetNum(L_, "index", ev.index >= 0 ? ev.index + 1 : 0);
        LuaBinding::SetNum(L_, "value", static_cast<double>(ev.value));
        if (ev.text != nullptr) LuaBinding::SetStr(L_, "text", ev.text);
        (void)Run(1, 0);  // the error is counted and logged inside
    }
    lua_pop(L_, 2);
}

void LuaPlugin::DrawPage(PageCtx& page, const d3h_ui& ui) {
    if (L_ == nullptr) return;
    if (disabled()) {
        ui.text_disabled(("Plugin disabled after repeated errors: " + lastError_).c_str());
        return;
    }
    ui_ = &ui;
    lua_rawgeti(L_, LUA_REGISTRYINDEX, page.ref);
    lua_rawgeti(L_, LUA_REGISTRYINDEX, uiRef_);
    (void)Run(1, 0);
    ui_ = nullptr;
}

void LuaPlugin::EventTrampoline(const d3h_event* ev, void* user) { static_cast<LuaPlugin*>(user)->OnEvent(*ev); }

void LuaPlugin::PageTrampoline(const d3h_ui* ui, void* user) {
    auto* page = static_cast<PageCtx*>(user);
    page->plugin->DrawPage(*page, *ui);
}

LuaPlugin::~LuaPlugin() {
    if (L_ != nullptr) lua_close(L_);
}

std::unique_ptr<LuaPlugin> LuaPlugin::Load(const std::filesystem::path& dir, const d3h_host& host, std::string& error) {
    std::string manifestSrc, mainSrc;
    if (!ReadScript(dir / L"plugin.lua", manifestSrc)) {
        error = "plugin.lua missing or too large";
        return nullptr;
    }
    if (!ReadScript(dir / L"main.lua", mainSrc)) {
        error = "main.lua missing or too large";
        return nullptr;
    }

    std::unique_ptr<LuaPlugin> p(new LuaPlugin(host));
    p->heap_ = std::make_unique<LuaHeap>();
    p->heap_->limit = kMemoryLimit;
    lua_State* L = p->L_ = lua_newstate(LuaAlloc, p->heap_.get());
    if (L == nullptr) {
        error = "cannot create a Lua state";
        return nullptr;
    }
    // Only the safe libraries: no io, os, debug, package (hence no require / loadfile / dofile).
    struct Lib { const char* name; lua_CFunction open; };
    for (const Lib& lib : {Lib{"_G", luaopen_base}, Lib{LUA_TABLIBNAME, luaopen_table}, Lib{LUA_STRLIBNAME, luaopen_string},
                           Lib{LUA_MATHLIBNAME, luaopen_math}, Lib{LUA_UTF8LIBNAME, luaopen_utf8},
                           Lib{LUA_COLIBNAME, luaopen_coroutine}}) {
        luaL_requiref(L, lib.name, lib.open, 1);
        lua_pop(L, 1);
    }

    // The manifest runs with an empty environment: it can only build a table.
    {
        const PluginScope scope(&p->manifest_.name);
        if (luaL_loadbufferx(L, manifestSrc.data(), manifestSrc.size(), "@plugin.lua", "t") != LUA_OK) {
            error = lua_tostring(L, -1);
            return nullptr;
        }
        lua_newtable(L);
        lua_setupvalue(L, -2, 1);
        lua_sethook(L, BudgetHook, LUA_MASKCOUNT, kInstructionBudget);
        const int rc = lua_pcall(L, 0, 1, 0);
        lua_sethook(L, nullptr, 0, 0);
        if (rc != LUA_OK || !lua_istable(L, -1)) {
            error = rc != LUA_OK && lua_isstring(L, -1) ? lua_tostring(L, -1) : "plugin.lua must return a table";
            return nullptr;
        }
        const char* name = LuaBinding::StrField(L, -1, "name");
        const char* version = LuaBinding::StrField(L, -1, "version");
        p->manifest_.api = static_cast<int>(LuaBinding::NumField(L, -1, "api", 0));
        lua_pop(L, 1);
        if (name == nullptr || !*name || strspn(name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(name)) {
            error = "plugin.lua: name must be letters, digits, '-' or '_'";
            return nullptr;
        }
        if (p->manifest_.api < 1 || p->manifest_.api > static_cast<int>(D3H_API_VERSION)) {
            error = std::format("plugin.lua: api {} is not supported (host has {})", p->manifest_.api, D3H_API_VERSION);
            return nullptr;
        }
        p->manifest_.name = name;
        p->manifest_.version = version != nullptr ? version : "";
    }

    // Registry tables: event handlers and the ui table.
    lua_newtable(L);
    p->handlersRef_ = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_newtable(L);  // ui
    const struct { const char* name; lua_CFunction fn; } uiFns[] = {
        {"text", LuaBinding::UiText}, {"text_disabled", LuaBinding::UiTextDisabled},
        {"text_colored", LuaBinding::UiTextColored}, {"separator", LuaBinding::UiSeparator},
        {"separator_text", LuaBinding::UiSeparatorText}, {"same_line", LuaBinding::UiSameLine},
        {"spacing", LuaBinding::UiSpacing}, {"button", LuaBinding::UiButton},
        {"checkbox", LuaBinding::UiCheckbox}, {"slider_int", LuaBinding::UiSliderInt},
        {"slider_float", LuaBinding::UiSliderFloat}, {"combo", LuaBinding::UiCombo},
        {"color_edit", LuaBinding::UiColor}, {"input_text", LuaBinding::UiInput},
        {"collapsing_header", LuaBinding::UiHeader}, {"is_item_hovered", LuaBinding::UiHovered},
        {"tooltip", LuaBinding::UiTooltip}};
    for (const auto& f : uiFns) LuaBinding::Register(L, p.get(), f.name, f.fn);
    p->uiRef_ = luaL_ref(L, LUA_REGISTRYINDEX);

    // The sandbox environment of main.lua: whitelisted globals, the standard libraries, `d3hud`.
    lua_newtable(L);  // env
    for (const char* g : {"assert", "error", "ipairs", "next", "pairs", "pcall", "select", "tonumber", "tostring", "type",
                          "xpcall", "rawequal", "rawget", "rawset", "rawlen", "setmetatable", "getmetatable"}) {
        lua_getglobal(L, g);
        lua_setfield(L, -2, g);
    }
    for (const char* lib : {LUA_TABLIBNAME, LUA_STRLIBNAME, LUA_MATHLIBNAME, LUA_UTF8LIBNAME, LUA_COLIBNAME}) {
        lua_getglobal(L, lib);
        lua_setfield(L, -2, lib);
    }
    lua_getglobal(L, LUA_STRLIBNAME);  // string.dump produces bytecode; bytecode is never loaded, but keep it out
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);

    lua_newtable(L);  // d3hud
    lua_pushinteger(L, D3H_API_VERSION);
    lua_setfield(L, -2, "api");
    LuaBinding::Register(L, p.get(), "log", LuaBinding::Log);
    LuaBinding::Register(L, p.get(), "on", LuaBinding::On);
    if (D3H_HOST_HAS(&host, get_frame)) LuaBinding::Register(L, p.get(), "frame", LuaBinding::Frame);
    if (D3H_HOST_HAS(&host, get_actor)) {
        LuaBinding::Register(L, p.get(), "actor", LuaBinding::Actor);
        LuaBinding::Register(L, p.get(), "actors", LuaBinding::Actors);
    }
    if (D3H_HOST_HAS(&host, get_item)) LuaBinding::Register(L, p.get(), "item", LuaBinding::Item);
    if (D3H_HOST_HAS(&host, world_to_screen) && host.world_to_screen != nullptr)
        LuaBinding::Register(L, p.get(), "world_to_screen", LuaBinding::WorldToScreenFn);
    if (D3H_HOST_HAS(&host, text_size) && host.text_size != nullptr)
        LuaBinding::Register(L, p.get(), "text_size", LuaBinding::TextSize);
    if (D3H_HOST_HAS(&host, draw_text)) LuaBinding::Register(L, p.get(), "draw_text", LuaBinding::DrawText);
    if (D3H_HOST_HAS(&host, define_setting)) {
        LuaBinding::Register(L, p.get(), "setting", LuaBinding::Setting);
        LuaBinding::Register(L, p.get(), "get", LuaBinding::Get);
        LuaBinding::Register(L, p.get(), "set", LuaBinding::Set);
    }
    if (D3H_HOST_HAS(&host, draw_rect) && host.draw_rect != nullptr)
        LuaBinding::Register(L, p.get(), "draw_rect", LuaBinding::DrawRect);
    if (D3H_HOST_HAS(&host, draw_line) && host.draw_line != nullptr)
        LuaBinding::Register(L, p.get(), "draw_line", LuaBinding::DrawLine);
    if (D3H_HOST_HAS(&host, register_panel) && host.register_panel != nullptr)
        LuaBinding::Register(L, p.get(), "panel", LuaBinding::Panel);
    // names for the numbers in actor / item tables
    lua_newtable(L);
    static const std::pair<const char*, int> kinds[] = {{"player", D3H_ACTOR_PLAYER}, {"trash", D3H_ACTOR_TRASH}, {"champion", D3H_ACTOR_CHAMPION},
                          {"rare", D3H_ACTOR_RARE}, {"minion", D3H_ACTOR_MINION}, {"boss", D3H_ACTOR_BOSS},
                          {"pylon", D3H_ACTOR_PYLON}, {"shrine", D3H_ACTOR_SHRINE}, {"chest", D3H_ACTOR_CHEST},
                          {"globe", D3H_ACTOR_GLOBE}, {"ground_item", D3H_ACTOR_GROUND_ITEM}, {"portal", D3H_ACTOR_PORTAL},
                          {"pylon_spawn", D3H_ACTOR_PYLON_SPAWN}, {"rift_orb", D3H_ACTOR_RIFT_ORB},
                          {"rift_orb_tiered", D3H_ACTOR_RIFT_ORB_TIERED}};
    for (const auto& k : kinds) {
        lua_pushinteger(L, k.second);
        lua_setfield(L, -2, k.first);
    }
    lua_setfield(L, -2, "kind");
    if (D3H_HOST_HAS(&host, register_page)) LuaBinding::Register(L, p.get(), "page", LuaBinding::Page);
    lua_setfield(L, -2, "d3hud");

    if (luaL_loadbufferx(L, mainSrc.data(), mainSrc.size(), "@main.lua", "t") != LUA_OK) {
        error = lua_tostring(L, -1);
        return nullptr;
    }
    lua_pushvalue(L, -2);
    lua_setupvalue(L, -2, 1);  // _ENV = the sandbox table
    if (!p->Run(0, 0)) {
        error = p->lastError_;
        return nullptr;
    }
    lua_pop(L, 1);  // env
    p->errors_ = 0;
    p->accumUs_ = 0;  // loading is not per-frame cost
    p->accumCalls_ = 0;
    return p;
}

}  // namespace d3hud
