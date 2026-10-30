// Draws a WorldSnapshot as HUD elements with ImGui. Pure presentation: takes
// snapshots from any source, owns no state beyond options.
#pragma once

#include <deque>
#include <filesystem>
#include <map>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Salvage.h"
#include "SceneCells.h"
#include "Settings.h"
#include "Snapshot.h"
#include "imgui.h"

namespace d3hud {

// Minimap placement, persisted in d3hud.ini. Position and radius are fractions
// of the window height (resolution independent), anchored to the top-right
// corner where D3 draws its own minimap; zoom is the world distance shown at
// the rim. Calibrate in F8 mode against icons on the game's minimap.
enum class MarkerShape : uint8_t { Dot = 0, Ring, Diamond, DiamondFilled };

// How one actor kind looks on the minimap / TAB map; all editable in Settings > Minimap.
struct KindStyle {
    bool show = true;
    uint32_t rgb = 0xFFFFFF;  // 0xRRGGBB
    float size = 2.2f;        // marker radius in px (before the global scale)
    MarkerShape shape = MarkerShape::Dot;
    float alpha = 1.0f;
    bool label = false;       // name next to the marker (shrines/pylons; "pylon?" for spawns)
    bool pulse = false;       // expanding ring (pylons)
    float maxDist = 0.0f;     // hide beyond this many world units from the player; 0 = no limit
};

// Labels over legendary/set items lying on the ground, and the colours of the drop log.
struct LabelStyle {
    bool enabled = true;
    int mode = 0;            // 0 legendary + set, 1 ancient/primal only, 2 sets only
    bool showBonus = true;   // bonus drops are always labelled (ignoring the mode)
    float scale = 1.0f;      // text size multiplier
    float bgAlpha = 0.69f;   // dark plate behind the text
    uint32_t colLegendary = 0xFF8C19, colSet = 0x50FF50, colPrimal = 0xFF4646, colBonus = 0x5AE6FF;  // 0xRRGGBB
};

// "A"/"P" badges on item icons and the card shown for hovered items.
struct ItemStyle {
    bool badges = true;
    float badgeScale = 1.0f;
    int badgeCorner = 0;        // 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
    bool badgeAugment = true;   // prefix the Caldesann's augment rank ("125A")
    uint32_t colAncient = 0xFFAF32, colPrimal = 0xFF4646;  // 0xRRGGBB
    int hoverMode = 0;          // card for: 0 unidentified items only, 1 every item, 2 off
    float hoverAlpha = 0.88f;
    bool search = true;          // search box over the stash; matches get highlighted
    uint32_t searchRgb = 0xFFE633;
    bool markIndexed = true;    // red fill on the stash tabs/pages the search has NOT seen yet
    float searchDim = 0.6f;     // darkening of items that do not match
};

// The drop log (Drops panel) and what happens when something good drops.
struct DropStyle {
    bool alertPrimal = true;    // beep + flash on a primal drop
    bool alertAncient = false;  // ... on an ancient drop
    bool sound = true;
    bool flash = true;
    int maxEntries = 500;       // oldest entries are dropped past this; 0 = keep all
    bool showRunTimer = false;  // "Run 04:12" at the top centre of the screen
    bool showRift = true;       // "Rift 47%  ETA 1:40" while a rift progress bar is open
    int minRunSeconds = 30;     // shorter world visits are not listed as runs (town hops)
};

struct MinimapLayout {
    MinimapLayout();
    DropStyle drops;
    LabelStyle labels;
    ItemStyle items;
    bool onGameMap = true;       // dots only, over the game's minimap (else own disc, bottom-right)
    bool autoMinimap = true;     // follow the game's own minimap control (placement below = fallback)
    float rightOffset = 0.168f;  // centre distance from the right edge / window height (16:9 measured)
    float topOffset = 0.192f;    // centre distance from the top edge / window height
    float radius = 0.125f;       // / window height
    float worldRadius = 156.0f;  // world units from centre to rim
    bool beyondRim = true;       // also draw actors outside the circle (faded)
    // marker look
    static constexpr int kKindCount = static_cast<int>(ActorKind::RiftOrbTiered) + 1;
    KindStyle kinds[kKindCount];
    float markerScale = 1.0f;
    bool outline = true;         // dark outline behind dots
    bool bigMapMarkers = true;   // markers on the TAB map too
    bool showScenes = true;      // map prediction (walkable cells)
    float sceneAlpha = 1.0f;
    bool sceneLabels = true;     // "Entrance" / "Exit" on the prediction
    bool distanceLabels = true;  // "Conduit Pylon  34m" on pylon/shrine names
    bool pylonArrow = true;      // rim arrow + distance to the nearest unused pylon when it is off the map
    bool eliteRing = true;       // ring around the nearest rare/champion
    int trashLimit = 0;          // max trash/minion dots drawn (thinned evenly); 0 = all
    uint32_t playerRgb = 0xFFFFFF;  // own-minimap player arrow
    float playerSize = 7.0f;
    void ResetMarkers();
    // auto salvage (stored in the same ini)
    SafeArea safeArea;
    SalvageRules salvageRules;

    // panel placement (same ini): collapsed tabs dock bottom-right; hover
    // expands, F8+click pins, dragging moves (fractions of window size).
    static constexpr int kPanelCount = 5;  // Info, Drops, Items, Actors, Settings
    bool panelPinned[kPanelCount] = {};
    bool panelHidden[kPanelCount] = {false, false, false, true, false};  // Actors = debug
    float panelX[kPanelCount] = {-1.f, -1.f, -1.f, -1.f, -1.f};  // < 0 = docked
    float panelY[kPanelCount] = {-1.f, -1.f, -1.f, -1.f, -1.f};
    float panelW[kPanelCount] = {};  // size of resizable panels (fraction of window); 0 = default
    float panelH[kPanelCount] = {};
    float uiScale = 1.0f;            // global text/widget scale (Settings > General)
    int dockCorner = 0;              // 0 bottom-right, 1 bottom-left, 2 top-right, 3 top-left
    float dockAlpha = 0.55f;         // dock background (a bit stronger while interactive)
    bool dockAutoHide = false;       // dock shows only near the cursor or while a panel is open
    float panelAlpha = 0.94f;        // panel background opacity
    bool compact = false;            // tighter padding and spacing
    bool readerAutostart = false;    // start D3Hud.Reader together with the overlay (live mode)
    int readerHz = 60;               // reader publish rate, applied when the reader starts
    bool diagFps = false;            // fps / counts line at the bottom-left
    bool diagReader = false;         // reader state, last log line and errors at the bottom-left
    bool salvageConfirm = false;     // the anvil button needs a second click before a run starts
    bool salvagePreview = true;      // red marks on items a run would destroy
    bool safeOnlyAtAnvil = true;     // safe area + its editor are shown only while the salvage tab is open
    uint32_t salvagePreviewRgb = 0xFF3232;

    // global hotkeys (virtual-key codes, 0 = unbound); ids 1.. in main.cpp
    enum HotkeyAction { kHkClickThrough, kHkPanels, kHkQuit, kHkMapOrientation, kHkPanelFirst /* + PanelId */ };
    static constexpr int kHotkeyCount = kHkPanelFirst + kPanelCount;
    uint8_t hotkeys[kHotkeyCount] = {0x77, 0x78, 0x23, 0x76, 0, 0, 0, 0, 0};  // F8 F9 END F7

    // Binds every persisted field to its ini key (see HudRenderer.cpp); Load and Save are built on it.
    void Describe(SettingsTable& table);
    // Plugin dock panels: their placement lives in the same ini (pluginpanel.<id>.*). Plugins register
    // after the file was read, so HudRenderer::AddPluginPanel loads just that panel's keys.
    struct PluginPanelState {
        bool pinned = false, hidden = false;
        float x = -1.f, y = -1.f, w = 0.f, h = 0.f;  // x < 0 = docked
    };
    std::map<std::string, PluginPanelState> pluginPanels;
    void DescribePluginPanel(SettingsTable& table, const std::string& id);
    [[nodiscard]] bool Load(const std::filesystem::path& path);
    [[nodiscard]] bool Save(const std::filesystem::path& path) const;
};

// Panels of the HUD: collapsed tabs docked at the bottom-right. Hovering a tab
// (or an open panel) expands it - view-only, since the overlay is
// click-through; F8 makes the overlay interactive and a click on the tab pins
// the panel open; dragging a panel moves it and the position persists.
enum class PanelId { Info = 0, Drops, Items, Actors, Settings };

// The background reader process as the Info panel shows it; the panel's
// buttons set the *Clicked flags, the owner acts on them.
struct ReaderStatus {
    bool running = false;
    bool connected = false;  // live ring open
    unsigned long pid = 0;
    const char* line = "";   // last line of reader.log
    const char* error = "";  // last start failure
    bool startClicked = false, stopClicked = false;
};

// Auto-salvage as the panel shows it; the panel sets the *Clicked flags, the
// owner (SalvageRunner) acts on them.
struct SalvageStatus {
    bool running = false;
    const char* status = "";
    bool startClicked = false, stopClicked = false;
};

struct HudOptions {
    bool showMinimap = true;
    bool showPanels = true;
    bool inputInteractive = false;    // F8 state: true = overlay captures mouse
    unsigned panelToggleMask = 0;     // bit i: the hotkey of panel i fired this frame (Draw consumes it)
    int mapOrientation = 0;           // F7 cycles; see OrientForMap (live default: 2)
    float cursorX = -1.f, cursorY = -1.f;  // mouse in overlay client px (Win32: works click-through)
    bool haveLabelCenter = false;  // live: player position extrapolated to "now" for world labels
    Vec2 labelCenter;
    ReaderStatus* reader = nullptr;  // live mode: reader start/stop in the Info panel
    SalvageStatus* salvage = nullptr;  // live mode: auto salvage panel
    // Out (filled by Draw): overlay client-px rects where the owner makes the overlay
    // interactive while the cursor is inside, so they are clickable without F8: the
    // anvil's salvage button, the safe-area toolbar (and grid while editing), the search box.
    struct HotRect { float x0, y0, x1, y1; };
    static constexpr int kMaxHot = 8;
    HotRect hot[kMaxHot] = {};
    int hotCount = 0;
    void AddHot(float x0, float y0, float x1, float y1) {
        if (hotCount < kMaxHot) hot[hotCount++] = {x0, y0, x1, y1};
    }
    bool mouseLive = false;  // the overlay receives mouse input right now (F8, or over a hot rect)
};

// One page of the Settings window. The built-in pages are registered like any other, so
// plugins can add theirs later: the host owns the window, the list and the persistence.
struct SettingsPage {
    std::string id;     // stable key, e.g. "minimap" or "<plugin>.options"
    std::string title;  // shown in the list and as the page heading
    std::string group;  // list section heading; pages of one group sit together
    int order = 0;      // sort key within the list (stable)
    std::function<void()> draw;
};

class HudRenderer {
public:
    HudRenderer();
    void AddSettingsPage(SettingsPage page);
    // A dock panel owned by a plugin: a tab in the dock, hover peek, pin, drag and a remembered position,
    // exactly like the built-in panels. `id` is unique ("<plugin>.<panel>"); `defaultSize` > 0 = resizable.
    void AddPluginPanel(std::string id, std::string title, std::function<void()> draw, ImVec2 defaultSize);
    // "show in dock / keep open / reset position" for one plugin panel (the plugin's Settings page).
    void DrawPluginPanelSettings(const std::string& id);
    // Shows the Settings window pinned open on the page with this id (nothing is saved); false if unknown.
    bool OpenSettingsPage(const std::string& id);
    void Draw(const WorldSnapshot& snap, HudOptions& opt, double fps, const char* sourceName);
    // Loads the layout from `path` (defaults if missing); the calibration panel saves there.
    void SetLayoutPath(const std::filesystem::path& path);
    // Scene shapes for the map prediction (scene_cells.bin); false if missing.
    bool LoadSceneCells(const std::filesystem::path& path) { return sceneCells_.Load(path); }
    // Hotkey bindings for main.cpp: re-register when TakeHotkeysDirty(); while a key is being
    // captured in Settings the owner must ignore hotkey presses.
    const uint8_t* hotkeys() const { return layout_.hotkeys; }
    bool TakeHotkeysDirty() { const bool d = hotkeysDirty_; hotkeysDirty_ = false; return d; }
    bool capturingHotkey() const { return captureHotkey_ >= 0; }
    bool readerAutostart() const { return layout_.readerAutostart; }
    int readerHz() const { return layout_.readerHz; }
    const SafeArea& safeArea() const { return layout_.safeArea; }
    const SalvageRules& salvageRules() const { return layout_.salvageRules; }

private:
    // Panel framework: returns true when the panel should draw its body.
    // `defaultSize` > 0 makes the panel user-resizable (size remembered).
    // The state of one panel, wherever it lives: built-in panels keep it in layout_ and the arrays
    // below, plugin panels in layout_.pluginPanels and their PluginPanel.
    struct PanelView {
        bool* pinned;
        float *x, *y, *w, *h;  // saved position / size (fractions of the window); x < 0 = docked
        ImVec2 *pos, *size;    // last frame's window rect (size.x == 0: collapsed, no hover area)
        float* hoverUntil;     // linger after the cursor leaves
        bool *resizable, *moved;
        bool peekable;         // opens on hover (Settings only opens when pinned)
    };
    struct PluginPanel {
        std::string id, title, label;  // label = title##id, the ImGui window name
        std::function<void()> draw;
        ImVec2 defaultSize;
        ImVec2 pos, size;
        float hoverUntil = 0.f;
        bool resizable = false, moved = false;
    };
    PanelView ViewOf(PanelId id);
    PanelView ViewOf(PluginPanel& p);
    // Looked up by id each time: "reset everything" replaces layout_, which would leave a stored pointer dangling.
    MinimapLayout::PluginPanelState& St(const PluginPanel& p) { return layout_.pluginPanels[p.id]; }
    // Panel framework: returns true when the panel should draw its body.
    // `defaultSize` > 0 makes the panel user-resizable (size remembered).
    bool PanelOpenView(const PanelView& v, const HudOptions& opt, const char* title, ImVec2 defaultSize);
    void PanelEndView(const PanelView& v, bool wasOpen);
    // An open panel for the length of a scope: PanelEndView runs on every way out (early returns included).
    //   PanelScope panel(*this, PanelId::Items, opt, "Items");
    //   if (!panel) return;   // collapsed: nothing to draw
    class PanelScope {
    public:
        PanelScope(HudRenderer& r, PanelId id, const HudOptions& opt, const char* title,
                   ImVec2 defaultSize = ImVec2(0.f, 0.f))
            : r_(r), view_(r.ViewOf(id)), open_(r.PanelOpenView(view_, opt, title, defaultSize)) {}
        PanelScope(HudRenderer& r, PluginPanel& p, const HudOptions& opt)
            : r_(r), view_(r.ViewOf(p)), open_(r.PanelOpenView(view_, opt, p.label.c_str(), p.defaultSize)) {}
        ~PanelScope() { r_.PanelEndView(view_, open_); }
        PanelScope(const PanelScope&) = delete;
        PanelScope& operator=(const PanelScope&) = delete;
        explicit operator bool() const { return open_; }

    private:
        HudRenderer& r_;
        PanelView view_;
        bool open_;
    };
    std::deque<PluginPanel> pluginPanels_;
    void DrawPluginPanel(PluginPanel& p, const HudOptions& opt);
    void DrawDock(const HudOptions& opt);
    void DrawSettings(const WorldSnapshot& snap, const HudOptions& opt);
    void RegisterBuiltinSettingsPages();
    void DrawGeneralSettings();
    void DrawPanelSettings(int panel);  // the shared "show in dock / pinned / reset position" part
    void SaveLayout();  // layout_.Save + status line
    void DrawMinimapSettings();
    bool minimapFound_ = false;  // last frame: the game's minimap control was used for placement
    void DrawHotkeySettings();
    void DrawLabelSettings();
    void DrawProfileSettings();
    void DrawItemSettings();
    void DrawItemSearch(const WorldSnapshot& snap, HudOptions& opt);
    // The reader only reports the stash tab that is open, so the overlay remembers what it saw
    // in each tab (global index page * 5 + tab) while the user browses; session only.
    void UpdateStashCache(const WorldSnapshot& snap);
    std::vector<ItemSnapshot> stashCache_[64];
    bool stashSeen_[64] = {};
    int pageTabCount_[4] = {};  // tabs a page has, learned while the page is shown (0 = unknown)
    int lastStashTab_ = -1;
    double stashTabSince_ = 0.0;
    char searchQuery_[64] = {};
    void DrawDropSettings();
    // Runs: one per world visit (the world id changes), with their own drop counts.
    struct RunEntry {
        char start[9] = {};  // local wall-clock HH:MM:SS
        double seconds = 0.0;
        uint32_t world = 0;
        int drops = 0, ancient = 0, primal = 0;
    };
    void UpdateRuns(const WorldSnapshot& snap);
    void ResetSession();
    void DrawRunTimer(const WorldSnapshot& snap);
    std::vector<std::pair<double, float>> riftSamples_;  // (time, progress) while a rift bar is open
    std::vector<RunEntry> runs_;  // finished runs, oldest first
    RunEntry run_;                // the current one
    bool haveRun_ = false;
    double runStartT_ = 0.0, sessionStartT_ = -1.0;
    double grStartT_ = -1.0;  // when the overlay first saw the greater-rift bar (its 15 min timer start, estimated)
    int sessDrops_ = 0, sessAncient_ = 0, sessPrimal_ = 0, sessBonus_ = 0;  // legendary/set, ancient, primal, bonus
    void DrawReaderSettings();
    void DrawDiagnostics(const WorldSnapshot& snap, const HudOptions& opt, double fps);
    double resetArmedUntil_ = 0.0;  // first click of "reset everything"
    bool ExportDropsCsv(const std::filesystem::path& path) const;
    double flashUntil_ = 0.0;   // drop alert: screen-border flash
    uint32_t flashRgb_ = 0;
    std::vector<std::string> profiles_;  // names of d3hud.<name>.ini next to d3hud.ini
    bool profilesDirty_ = true;
    char profileName_[32] = {};
    bool hotkeysDirty_ = true;
    double salvageArmedUntil_ = 0.0;  // first click of a confirmed salvage start
    bool captureReady_ = false;  // no key held since the capture began
    int captureHotkey_ = -1;  // hotkey action waiting for a key press, or -1
    void DrawSalvageSettings();
    static constexpr int kPanels = MinimapLayout::kPanelCount;
    ImVec2 panelPos_[kPanels] = {};   // last frame's window rect (min)
    ImVec2 panelSize_[kPanels] = {};
    float panelHoverUntil_[kPanels] = {};  // linger after the cursor leaves
    bool panelResizable_[kPanels] = {};
    bool panelMoved_[kPanels] = {};        // title-bar drag in progress; saved on release
    int settingsTab_ = 0;  // index into settingsPages_
    std::vector<SettingsPage> settingsPages_;
    ImVec2 dockPos_ = {}, dockSize_ = {};  // last frame's dock rect (auto-hide keeps testing it)

    void DrawMinimap(const WorldSnapshot& snap, const HudOptions& opt);
    void DrawBigMap(const WorldSnapshot& snap, const HudOptions& opt);
    // Map prediction: the current world's walkable scene cells, mapped by `toPx`
    // (world -> pixels); cells whose centre fails `keep` are skipped.
    template <typename ToPx, typename Keep>
    void DrawScenes(const WorldSnapshot& snap, ImDrawList* dl, ToPx toPx, Keep keep);
    SceneCells sceneCells_;
    void DrawActorPanel(const WorldSnapshot& snap, const HudOptions& opt);
    void DrawSalvagePanel(const WorldSnapshot& snap, const HudOptions& opt);
    void DrawHoveredItem(const WorldSnapshot& snap, const HudOptions& opt);
    void DrawItemBadges(const WorldSnapshot& snap);
    void UpdateDrops(const WorldSnapshot& snap);
    void DrawGroundLabels(const WorldSnapshot& snap, const HudOptions& opt);
    void DrawDropsPanel(const WorldSnapshot& snap, const HudOptions& opt);
    void DrawAutoSalvage(const WorldSnapshot& snap, HudOptions& opt);
    bool editSafeArea_ = false;

    // Session log of legendary/set items seen on the ground (newest last).
    struct DropEntry {
        ItemSnapshot item;
        char time[9] = {};  // local wall-clock HH:MM:SS when first seen
    };
    std::vector<DropEntry> drops_;
    std::unordered_map<uint64_t, size_t> dropIndex_;  // item (acd) id -> drops_ index
    void DrawInfo(const WorldSnapshot& snap, const HudOptions& opt, double fps, const char* sourceName);
    int selectedRow_ = -1;
    ImGuiTextFilter filter_;
    MinimapLayout layout_;
    std::filesystem::path layoutPath_;
    const char* layoutStatus_ = "";
};

const char* VerdictName(SalvageVerdict v);

}  // namespace d3hud
