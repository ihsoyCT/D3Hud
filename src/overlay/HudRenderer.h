// Draws a WorldSnapshot as HUD elements with ImGui. Pure presentation: takes
// snapshots from any source, owns no state beyond options.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Salvage.h"
#include "SceneCells.h"
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

struct MinimapLayout {
    MinimapLayout();
    LabelStyle labels;
    bool onGameMap = true;       // dots only, over the game's minimap (else own disc, bottom-right)
    float rightOffset = 0.110f;  // centre distance from the right edge / window height
    float topOffset = 0.115f;    // centre distance from the top edge / window height
    float radius = 0.100f;       // / window height
    float worldRadius = 120.0f;  // world units from centre to rim
    bool beyondRim = true;       // also draw actors outside the circle (faded)
    // marker look
    static constexpr int kKindCount = static_cast<int>(ActorKind::PylonSpawn) + 1;
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
    bool salvageConfirm = false;     // the anvil button needs a second click before a run starts
    bool salvagePreview = true;      // red marks on items a run would destroy
    uint32_t salvagePreviewRgb = 0xFF3232;

    // global hotkeys (virtual-key codes, 0 = unbound); ids 1.. in main.cpp
    enum HotkeyAction { kHkClickThrough, kHkPanels, kHkQuit, kHkMapOrientation, kHkPanelFirst /* + PanelId */ };
    static constexpr int kHotkeyCount = kHkPanelFirst + kPanelCount;
    uint8_t hotkeys[kHotkeyCount] = {0x77, 0x78, 0x23, 0x76, 0, 0, 0, 0, 0};  // F8 F9 END F7

    bool Load(const char* path);
    bool Save(const char* path) const;
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
    // Out (filled by Draw): the salvage button over the blacksmith's anvil, in
    // overlay client px. The owner makes the overlay interactive while the
    // cursor is inside it, so the button is clickable without F8.
    float salvageButtonX0 = -1.f, salvageButtonY0 = -1.f, salvageButtonX1 = -1.f, salvageButtonY1 = -1.f;
    // Same for the safe-area toolbar under the backpack (plus the whole grid while editing).
    float editAreaX0 = -1.f, editAreaY0 = -1.f, editAreaX1 = -1.f, editAreaY1 = -1.f;
    bool mouseLive = false;  // the overlay receives mouse input right now (F8, or over a hot rect)
};

class HudRenderer {
public:
    void Draw(const WorldSnapshot& snap, HudOptions& opt, double fps, const char* sourceName);
    // Loads the layout from `path` (defaults if missing); the calibration panel saves there.
    void SetLayoutPath(const char* path);
    // Scene shapes for the map prediction (scene_cells.bin); false if missing.
    bool LoadSceneCells(const char* path) { return sceneCells_.Load(path); }
    // Hotkey bindings for main.cpp: re-register when TakeHotkeysDirty(); while a key is being
    // captured in Settings the owner must ignore hotkey presses.
    const uint8_t* hotkeys() const { return layout_.hotkeys; }
    bool TakeHotkeysDirty() { const bool d = hotkeysDirty_; hotkeysDirty_ = false; return d; }
    bool capturingHotkey() const { return captureHotkey_ >= 0; }
    const SafeArea& safeArea() const { return layout_.safeArea; }
    const SalvageRules& salvageRules() const { return layout_.salvageRules; }

private:
    // Panel framework: returns true when the panel should draw its body.
    // `defaultSize` > 0 makes the panel user-resizable (size remembered).
    bool PanelOpen(PanelId id, const HudOptions& opt, const char* title, ImVec2 defaultSize = ImVec2(0.f, 0.f));
    void PanelEnd(PanelId id, bool wasOpen);
    void DrawDock(const HudOptions& opt);
    void DrawSettings(const WorldSnapshot& snap, const HudOptions& opt);
    void DrawMinimapSettings();
    void DrawHotkeySettings();
    void DrawLabelSettings();
    void DrawProfileSettings();
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
    int settingsTab_ = 0;
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
    char layoutPath_[260] = {};
    const char* layoutStatus_ = "";
};

const char* VerdictName(SalvageVerdict v);

}  // namespace d3hud
