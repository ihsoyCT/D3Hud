/* The host API: everything a plugin (Lua script or, later, a native DLL) may see and do.
 *
 * Plain C so that every language binding and a DLL ABI can sit on the same table. Rules:
 *   - Versioned. `d3h_host.api_version` only ever grows; additions go at the END of a struct and
 *     structs start with a `size` field, so an old plugin keeps working with a newer host and a
 *     newer plugin can detect a host that lacks a function (check `size` before using it).
 *   - Data is copied out into plugin-owned structs. The frozen Snapshot structs (Snapshot.h) are
 *     NOT exposed: they change with every snapshot version, this API does not.
 *   - Strings are UTF-8, NUL terminated; pointers handed to a callback are valid only during it.
 *   - Everything is called on the overlay's UI thread. Drawing and widget calls are valid only
 *     inside the callback that is currently running for them (frame / panel / page).
 *
 * Deliberately NOT here (see PLUGIN_PLAN.md, "Kept out of the API"): game memory access, raw input
 * injection (no clicks, no key presses), file or network access, reader plugins. A plugin cannot
 * make the HUD do anything that plays the game.
 */
#ifndef D3HUD_HOST_API_H
#define D3HUD_HOST_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D3H_API_VERSION 1
#define D3H_NAME_LEN 48

typedef int32_t d3h_bool; /* 0 / 1 */

typedef struct d3h_vec2 {
    float x, y;
} d3h_vec2;

typedef struct d3h_rect {
    float x0, y0, x1, y1;
} d3h_rect;

/* Colours are 0xAARRGGBB; for settings the alpha byte is ignored. */
typedef uint32_t d3h_color;

/* ---- Data (read-only) ------------------------------------------------------------------- */

/* Same numbering as ActorKind in Snapshot.h (appended values are safe). */
typedef enum d3h_actor_kind {
    D3H_ACTOR_PLAYER = 0, D3H_ACTOR_TRASH, D3H_ACTOR_CHAMPION, D3H_ACTOR_RARE, D3H_ACTOR_MINION,
    D3H_ACTOR_BOSS, D3H_ACTOR_PYLON, D3H_ACTOR_SHRINE, D3H_ACTOR_CHEST, D3H_ACTOR_GLOBE,
    D3H_ACTOR_GROUND_ITEM, D3H_ACTOR_PORTAL, D3H_ACTOR_PYLON_SPAWN, D3H_ACTOR_RIFT_ORB,
    D3H_ACTOR_RIFT_ORB_TIERED
} d3h_actor_kind;

/* Same numbering as ItemRarity. */
typedef enum d3h_rarity {
    D3H_RARITY_COMMON = 0, D3H_RARITY_MAGIC, D3H_RARITY_RARE, D3H_RARITY_LEGENDARY, D3H_RARITY_SET,
    D3H_RARITY_UNKNOWN
} d3h_rarity;

typedef enum d3h_verdict { D3H_VERDICT_UNKNOWN = 0, D3H_VERDICT_JUNK, D3H_VERDICT_KEEP, D3H_VERDICT_CHECK } d3h_verdict;

enum { /* d3h_actor.flags / d3h_item.flags */
    D3H_FLAG_ANCIENT = 1, D3H_FLAG_PRIMAL = 2, D3H_FLAG_BONUS = 4, D3H_FLAG_UNIDENTIFIED = 8,
    D3H_FLAG_USED = 16 /* pylon / shrine already clicked */
};

enum { /* d3h_frame.ui_flags */
    D3H_UI_INVENTORY = 1, D3H_UI_STASH = 2, D3H_UI_SALVAGE = 4, D3H_UI_BIG_MAP = 8,
    D3H_UI_MINIMAP = 16, D3H_UI_RIFT = 32, D3H_UI_GREATER_RIFT = 64
};

/* Per-frame facts. Fill `size = sizeof(d3h_frame)` before the call. */
typedef struct d3h_frame {
    uint32_t size;
    uint64_t frame_index;
    double time_ms;
    d3h_vec2 player_pos; /* world units */
    float player_heading;
    uint32_t world_sno;
    uint32_t actor_count;
    uint32_t item_count;
    int32_t hovered_item; /* index into the item list, -1 = none */
    float screen_w, screen_h;
    uint32_t ui_flags;
    float rift_progress;       /* 0..1, valid with D3H_UI_RIFT */
    float rift_timer_fraction; /* greater rift: elapsed share of the limit, -1 unknown */
    int32_t stash_tab;         /* selected global stash tab, -1 unknown */
} d3h_frame;

typedef struct d3h_actor {
    uint32_t size;
    uint64_t id;
    d3h_vec2 pos;
    float heading;
    float hp_fraction; /* 0..1, or -1 when the life of this actor is not read (only elites and bosses are) */
    int32_t kind;      /* d3h_actor_kind */
    int32_t rarity;    /* ground items: d3h_rarity */
    uint32_t flags;
    char name[D3H_NAME_LEN];
    /* appended in API 1 before release; older plugins never see them (check `size`) */
    float hp_current, hp_max; /* life in game units; 0 when unknown */
    uint32_t affix_mask;      /* elites: bit i = affix i of the host's affix table (d3hud.affixes in Lua) */
} d3h_actor;

typedef struct d3h_item {
    uint32_t size;
    uint64_t id;
    uint32_t sno;
    int32_t rarity;  /* d3h_rarity */
    int32_t verdict; /* d3h_verdict from the built-in salvage rules */
    uint32_t flags;
    int32_t location; /* inventory slot, as in the Items panel */
    int32_t grid_x, grid_y;
    int32_t height; /* grid cells tall */
    int32_t augment_rank;
    char name[D3H_NAME_LEN];
    char type_name[D3H_NAME_LEN];
} d3h_item;

/* ---- Events ----------------------------------------------------------------------------- */

typedef enum d3h_event_type {
    D3H_EVENT_FRAME = 1,        /* once per drawn frame, before panels */
    D3H_EVENT_SNAPSHOT,         /* a new snapshot arrived from the reader */
    D3H_EVENT_DROP,             /* a new legendary/set item appeared on the ground: index = actor */
    D3H_EVENT_INVENTORY_OPEN,
    D3H_EVENT_INVENTORY_CLOSE,
    D3H_EVENT_WORLD_CHANGE,     /* value = new world sno */
    D3H_EVENT_ITEM_HOVER,       /* index = item, -1 when the hover ended */
    D3H_EVENT_HOTKEY,           /* text = the hotkey id the plugin registered */
    D3H_EVENT_SETTING_CHANGED   /* text = full key ("<plugin>.<key>") */
} d3h_event_type;

typedef struct d3h_event {
    uint32_t size;
    int32_t type; /* d3h_event_type */
    int32_t index;
    int64_t value;
    const char* text; /* may be NULL */
} d3h_event;

typedef void (*d3h_event_fn)(const d3h_event* ev, void* user);

/* ---- Drawing (valid inside event / panel / page callbacks) ------------------------------ */

/* Layers are drawn back to front; the HUD's own markers sit on D3H_LAYER_MARKERS. */
typedef enum d3h_layer { D3H_LAYER_BACKGROUND = 0, D3H_LAYER_MARKERS, D3H_LAYER_OVERLAY, D3H_LAYER_TOP } d3h_layer;

/* ---- Settings --------------------------------------------------------------------------- */

typedef enum d3h_setting_type {
    D3H_SETTING_BOOL = 1, D3H_SETTING_INT, D3H_SETTING_FLOAT, D3H_SETTING_COLOR, D3H_SETTING_STRING,
    D3H_SETTING_CHOICE /* int index; `choices` = "A\0B\0C\0\0" */
} d3h_setting_type;

typedef struct d3h_setting_desc {
    uint32_t size;
    const char* key; /* unique inside the plugin; stored as "<plugin>.<key>" in d3hud.ini */
    int32_t type;    /* d3h_setting_type */
    const char* label;
    const char* tooltip; /* may be NULL */
    double def, min, max; /* bool/int/float/choice/color (color: 0xRRGGBB) */
    const char* def_string; /* strings */
    const char* choices;
    uint32_t flags; /* D3H_SETTING_HIDDEN: stored, but no control in the plugin's page (state a plugin keeps) */
} d3h_setting_desc;

enum { D3H_SETTING_HIDDEN = 1 };

/* ---- Panels, pages, hotkeys ------------------------------------------------------------- */

struct d3h_ui;

/* A dock panel. The host owns dock tab, hover peek, pin, drag and the remembered position. */
typedef void (*d3h_draw_fn)(const struct d3h_ui* ui, void* user);

typedef struct d3h_panel_desc {
    uint32_t size;
    const char* id;
    const char* title;
    d3h_draw_fn draw;
    void* user;
    float default_w, default_h; /* > 0 makes the panel resizable */
} d3h_panel_desc;

/* A page in the Settings window, for anything the setting schema cannot express. */
typedef struct d3h_page_desc {
    uint32_t size;
    const char* id;
    const char* title;
    const char* group;
    int32_t order;
    d3h_draw_fn draw;
    void* user;
} d3h_page_desc;

/* The widget subset available inside panel and page callbacks. Mirrors the Dear ImGui calls of the
 * same name; `id`s must be unique inside one callback. Return values are "changed / clicked". */
typedef struct d3h_ui {
    uint32_t size;
    void (*text)(const char* text);
    void (*text_colored)(d3h_color color, const char* text);
    void (*text_disabled)(const char* text);
    void (*separator)(void);
    void (*separator_text)(const char* label);
    void (*same_line)(void);
    void (*spacing)(void);
    d3h_bool (*button)(const char* label);
    d3h_bool (*checkbox)(const char* label, d3h_bool* value);
    d3h_bool (*slider_int)(const char* label, int32_t* value, int32_t min, int32_t max);
    d3h_bool (*slider_float)(const char* label, float* value, float min, float max, const char* format);
    d3h_bool (*combo)(const char* label, int32_t* index, const char* items_nul_separated);
    d3h_bool (*color_edit)(const char* label, d3h_color* rgb);
    d3h_bool (*input_text)(const char* label, char* buffer, size_t buffer_size);
    d3h_bool (*collapsing_header)(const char* label);
    d3h_bool (*is_item_hovered)(void);
    void (*tooltip)(const char* text);
} d3h_ui;

/* ---- The host table ---------------------------------------------------------------------- */

typedef enum d3h_log_level { D3H_LOG_INFO = 0, D3H_LOG_WARN, D3H_LOG_ERROR } d3h_log_level;

/* Salvage policy hook: the host calls it for every backpack item before the salvage rules decide.
 * The answer can only make the outcome MORE conservative: D3H_VERDICT_KEEP / CHECK always win;
 * D3H_VERDICT_JUNK is honoured only for items the built-in rules (IsSalvageCandidate /
 * CanSalvageAllOfQuality) already allow. The host enforces that, not the plugin. */
typedef int32_t (*d3h_salvage_policy_fn)(const d3h_item* item, void* user);

typedef struct d3h_host {
    uint32_t size;        /* sizeof(d3h_host) of the host */
    uint32_t api_version; /* D3H_API_VERSION of the host */

    /* diagnostics */
    void (*log)(int32_t level, const char* message);

    /* data */
    void (*get_frame)(d3h_frame* out);
    d3h_bool (*get_actor)(uint32_t index, d3h_actor* out);
    d3h_bool (*get_item)(uint32_t index, d3h_item* out);
    d3h_bool (*world_to_screen)(d3h_vec2 world, float height_above_player, d3h_vec2* out);
    d3h_bool (*world_to_minimap)(d3h_vec2 world, d3h_vec2* out); /* false off the map or without a minimap */
    d3h_bool (*item_screen_rect)(uint32_t item_index, d3h_rect* out);

    /* events */
    void (*subscribe)(int32_t event_type, d3h_event_fn fn, void* user);

    /* drawing (screen pixels of the overlay window) */
    void (*draw_line)(int32_t layer, d3h_vec2 a, d3h_vec2 b, d3h_color color, float thickness);
    void (*draw_rect)(int32_t layer, d3h_rect r, d3h_color color, d3h_bool filled, float thickness);
    void (*draw_circle)(int32_t layer, d3h_vec2 center, float radius, d3h_color color, d3h_bool filled, float thickness);
    void (*draw_quad)(int32_t layer, d3h_vec2 a, d3h_vec2 b, d3h_vec2 c, d3h_vec2 d, d3h_color color, d3h_bool filled);
    void (*draw_text)(int32_t layer, d3h_vec2 pos, d3h_color color, const char* text, float scale);

    /* UI */
    d3h_bool (*register_panel)(const d3h_panel_desc* desc);
    d3h_bool (*register_page)(const d3h_page_desc* desc);
    void (*hot_rect)(d3h_rect r); /* the overlay takes clicks inside, without F8; valid for this frame */

    /* settings (all persisted by the host; plugins never touch files) */
    d3h_bool (*define_setting)(const d3h_setting_desc* desc);
    d3h_bool (*get_bool)(const char* key, d3h_bool* out);
    d3h_bool (*get_int)(const char* key, int32_t* out);
    d3h_bool (*get_float)(const char* key, float* out);
    d3h_bool (*get_color)(const char* key, d3h_color* out);
    d3h_bool (*get_string)(const char* key, char* out, size_t out_size);
    d3h_bool (*set_value)(const char* key, double value); /* bool / int / float / color / choice */
    d3h_bool (*set_string)(const char* key, const char* value);

    /* hotkeys: shown in Settings > Hotkeys, namespaced per plugin; fires D3H_EVENT_HOTKEY */
    d3h_bool (*register_hotkey)(const char* id, const char* label, int32_t default_vk);

    /* policy hooks */
    d3h_bool (*set_salvage_policy)(d3h_salvage_policy_fn fn, void* user);

    /* size in screen pixels of `text` as draw_text would draw it at `scale` */
    d3h_vec2 (*text_size)(const char* text, float scale);
} d3h_host;

/* True when the host's table is large enough to contain `field` (a newer plugin on an older host). */
#define D3H_HOST_HAS(host, field) ((host)->size >= offsetof(d3h_host, field) + sizeof((host)->field))

/* A plugin's entry points (the DLL track exports these; the Lua loader calls the script's equivalents). */
typedef struct d3h_plugin_info {
    uint32_t size;
    uint32_t api_version; /* the API the plugin was written against */
    const char* name;
    const char* version;
} d3h_plugin_info;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* D3HUD_HOST_API_H */
