// Draws a WorldSnapshot as HUD elements with ImGui. Pure presentation: takes
// snapshots from any source, owns no state beyond options.
#pragma once

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
struct MinimapLayout {
    bool onGameMap = true;       // dots only, over the game's minimap (else own disc, bottom-right)
    float rightOffset = 0.110f;  // centre distance from the right edge / window height
    float topOffset = 0.115f;    // centre distance from the top edge / window height
    float radius = 0.100f;       // / window height
    float worldRadius = 120.0f;  // world units from centre to rim
    bool beyondRim = true;       // also draw actors outside the circle (faded)
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
    const SafeArea& safeArea() const { return layout_.safeArea; }
    const SalvageRules& salvageRules() const { return layout_.salvageRules; }

private:
    // Panel framework: returns true when the panel should draw its body.
    bool PanelOpen(PanelId id, const HudOptions& opt, const char* title);
    void PanelEnd(PanelId id, bool wasOpen);
    void DrawDock(const HudOptions& opt);
    void DrawSettings(const WorldSnapshot& snap, const HudOptions& opt);
    void DrawMinimapSettings();
    void DrawSalvageSettings();
    static constexpr int kPanels = MinimapLayout::kPanelCount;
    ImVec2 panelPos_[kPanels] = {};   // last frame's window rect (min)
    ImVec2 panelSize_[kPanels] = {};
    float panelHoverUntil_[kPanels] = {};  // linger after the cursor leaves
    bool panelMoved_[kPanels] = {};        // title-bar drag in progress; saved on release
    int settingsTab_ = 0;

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
