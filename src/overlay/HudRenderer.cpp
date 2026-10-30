#include "HudRenderer.h"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <print>
#include <vector>

#include "imgui.h"
#include "ItemSearch.h"
#include "imgui_internal.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace d3hud {
namespace {

ImU32 KindColor(ActorKind k) {
    switch (k) {
        case ActorKind::Player:    return IM_COL32(255, 255, 255, 255);
        case ActorKind::Trash:     return IM_COL32(210, 210, 210, 220);
        case ActorKind::Champion:  return IM_COL32(80, 130, 255, 255);   // blue
        case ActorKind::Rare:      return IM_COL32(255, 215, 0, 255);    // yellow
        case ActorKind::Minion:    return IM_COL32(200, 170, 40, 220);
        case ActorKind::Boss:      return IM_COL32(190, 70, 255, 255);   // purple
        case ActorKind::Pylon:     return IM_COL32(0, 230, 230, 255);    // cyan
        case ActorKind::Shrine:    return IM_COL32(80, 255, 120, 255);   // green
        case ActorKind::Chest:     return IM_COL32(255, 170, 40, 255);
        case ActorKind::Globe:     return IM_COL32(255, 70, 70, 255);
        case ActorKind::GroundItem:return IM_COL32(255, 128, 0, 255);    // orange
        case ActorKind::Portal:    return IM_COL32(255, 60, 220, 255);   // magenta
        case ActorKind::PylonSpawn:return IM_COL32(0, 230, 230, 150);    // faint cyan
    }
    return IM_COL32(255, 255, 255, 255);
}

float KindRadius(ActorKind k) {
    switch (k) {
        case ActorKind::Boss:
        case ActorKind::Rare:      return 5.0f;
        case ActorKind::Champion:  return 3.6f;
        case ActorKind::Minion:    return 2.6f;
        case ActorKind::Pylon:
        case ActorKind::Shrine:    return 4.2f;
        case ActorKind::Portal:    return 5.0f;
        default:                   return 2.2f;
    }
}

// Drop log / world label colour: bonus drops cyan, set green, legendary orange.
ImU32 DropColor(const LabelStyle& L, ItemRarity rarity, bool bonus) {
    const uint32_t rgb = bonus ? L.colBonus : rarity == ItemRarity::Set ? L.colSet : L.colLegendary;
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
}

}  // namespace

const char* VerdictName(SalvageVerdict v) {
    switch (v) {
        case SalvageVerdict::Unknown: return "-";
        case SalvageVerdict::Junk:    return "JUNK";
        case SalvageVerdict::Keep:    return "KEEP";
        case SalvageVerdict::Check:   return "CHECK";
    }
    return "?";
}

// Hotkey actions, in MinimapLayout::HotkeyAction order (ini key / label).
static const char* const kHotkeyIds[MinimapLayout::kHotkeyCount] = {
    "clickThrough", "panels", "quit", "mapOrientation", "panelInfo", "panelDrops", "panelItems", "panelActors", "panelSettings"};
static const char* const kHotkeyLabels[MinimapLayout::kHotkeyCount] = {
    "Click-through / interact", "Show/hide panels", "Quit", "Cycle map orientation", "Toggle Info panel", "Toggle Drops panel",
    "Toggle Items panel", "Toggle Actors panel", "Toggle Settings panel"};

static void KeyName(int vk, char* out, size_t n) {
    if (vk == 0) { snprintf(out, n, "(none)"); return; }
    if (vk >= VK_F1 && vk <= VK_F24) { snprintf(out, n, "F%d", vk - VK_F1 + 1); return; }
    const bool ext = vk == VK_END || vk == VK_HOME || vk == VK_INSERT || vk == VK_DELETE || vk == VK_PRIOR ||
                     vk == VK_NEXT || (vk >= VK_LEFT && vk <= VK_DOWN) || vk == VK_DIVIDE || vk == VK_NUMLOCK;
    const LONG lp = (static_cast<LONG>(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC)) << 16) | (ext ? (1 << 24) : 0);
    if (GetKeyNameTextA(lp, out, static_cast<int>(n)) <= 0) snprintf(out, n, "key 0x%02X", vk);
}

MinimapLayout::MinimapLayout() { ResetMarkers(); }

void MinimapLayout::ResetMarkers() {
    markerScale = 1.0f;
    outline = bigMapMarkers = showScenes = sceneLabels = distanceLabels = pylonArrow = eliteRing = true;
    trashLimit = 0;
    playerRgb = 0xFFFFFF;
    playerSize = 7.0f;
    sceneAlpha = 1.0f;
    auto set = [&](ActorKind k, uint32_t rgb, float size, MarkerShape shape, float alpha = 1.0f,
                   bool label = false, bool pulse = false) {
        kinds[static_cast<int>(k)] = KindStyle{true, rgb, size, shape, alpha, label, pulse};
    };
    set(ActorKind::Player,     0xFFFFFF, 2.2f, MarkerShape::Dot);  // not drawn: the centre arrow is the player
    set(ActorKind::Trash,      0xD2D2D2, 2.2f, MarkerShape::Dot, 0.86f);
    set(ActorKind::Champion,   0x5082FF, 3.6f, MarkerShape::Dot);
    set(ActorKind::Rare,       0xFFD700, 5.0f, MarkerShape::Dot);
    set(ActorKind::Minion,     0xC8AA28, 2.6f, MarkerShape::Dot, 0.86f);
    set(ActorKind::Boss,       0xBE46FF, 5.0f, MarkerShape::Dot);
    set(ActorKind::Pylon,      0x00F0FF, 8.0f, MarkerShape::DiamondFilled, 1.0f, true, true);
    set(ActorKind::Shrine,     0x50FF78, 6.0f, MarkerShape::DiamondFilled, 1.0f, true);
    set(ActorKind::Chest,      0xFFAA28, 2.2f, MarkerShape::Dot);
    set(ActorKind::Globe,      0xFF4646, 2.2f, MarkerShape::Dot);
    set(ActorKind::GroundItem, 0xFF8000, 2.2f, MarkerShape::Dot);
    set(ActorKind::Portal,     0xFF3CDC, 5.0f, MarkerShape::Ring);
    set(ActorKind::PylonSpawn, 0x00F0FF, 6.0f, MarkerShape::Diamond, 0.6f, true);
}

bool MinimapLayout::Load(const char* path) {
    FILE* f = fopen(path, "r");
    if (f == nullptr) return false;
    char line[128];
    while (fgets(line, sizeof(line), f) != nullptr) {
        char key[64];
        float v;
        unsigned long long cells;
        if (sscanf(line, " safeCells = %llx", &cells) == 1) {
            safeArea.cells = cells & ((1ull << (kBackpackCols * kBackpackRows)) - 1);
            continue;
        }
        if (sscanf(line, " %63[^=]= %f", key, &v) != 2) continue;
        if (!strcmp(key, "onGameMap")) onGameMap = v != 0.f;
        else if (!strcmp(key, "rightOffset")) rightOffset = v;
        else if (!strcmp(key, "topOffset")) topOffset = v;
        else if (!strcmp(key, "radius")) radius = v;
        else if (!strcmp(key, "worldRadius")) worldRadius = v;
        else if (!strcmp(key, "beyondRim")) beyondRim = v != 0.f;
        else if (!strcmp(key, "protectEquipped")) salvageRules.protectEquippedAncients = v != 0.f;
        else if (!strcmp(key, "uiScale")) uiScale = std::clamp(v, 0.5f, 3.0f);
        else if (!strncmp(key, "salvage.", 8)) {
            const char* k = key + 8;
            if (!strcmp(k, "rarityMask")) salvageRules.rarityMask = static_cast<uint32_t>(v) & 0x1F;
            else if (!strcmp(k, "keepAncient")) salvageRules.keepAncient = v != 0.f;
            else if (!strcmp(k, "keepPrimal")) salvageRules.keepPrimal = v != 0.f;
            else if (!strcmp(k, "neverFamilies")) salvageRules.neverFamilies = static_cast<uint32_t>(v) & ((1u << kGearFamilyCount) - 1);
            else if (!strcmp(k, "maxPerRun")) salvageRules.maxPerRun = std::max(0, static_cast<int>(v));
            else if (!strcmp(k, "extraDelayMs")) salvageRules.extraDelayMs = std::clamp(static_cast<int>(v), 0, 2000);
            else if (!strcmp(k, "useAll")) salvageRules.useGameSalvageAll = v != 0.f;
            else if (!strcmp(k, "burst")) salvageRules.burst = v != 0.f;
            else if (!strcmp(k, "confirm")) salvageConfirm = v != 0.f;
            else if (!strcmp(k, "preview")) salvagePreview = v != 0.f;
            else if (!strcmp(k, "safeOnlyAtAnvil")) safeOnlyAtAnvil = v != 0.f;
            else if (!strcmp(k, "previewRgb")) salvagePreviewRgb = static_cast<uint32_t>(v) & 0xFFFFFF;
        }
        else if (!strcmp(key, "readerAutostart")) readerAutostart = v != 0.f;
        else if (!strcmp(key, "readerHz")) readerHz = std::clamp(static_cast<int>(v), 5, 240);
        else if (!strcmp(key, "diagFps")) diagFps = v != 0.f;
        else if (!strcmp(key, "diagReader")) diagReader = v != 0.f;
        else if (!strcmp(key, "dockCorner")) dockCorner = std::clamp(static_cast<int>(v), 0, 3);
        else if (!strcmp(key, "dockAlpha")) dockAlpha = std::clamp(v, 0.f, 1.f);
        else if (!strcmp(key, "dockAutoHide")) dockAutoHide = v != 0.f;
        else if (!strcmp(key, "panelAlpha")) panelAlpha = std::clamp(v, 0.2f, 1.f);
        else if (!strcmp(key, "compact")) compact = v != 0.f;
        else if (!strncmp(key, "drops.", 6)) {
            const char* k = key + 6;
            if (!strcmp(k, "alertPrimal")) drops.alertPrimal = v != 0.f;
            else if (!strcmp(k, "alertAncient")) drops.alertAncient = v != 0.f;
            else if (!strcmp(k, "sound")) drops.sound = v != 0.f;
            else if (!strcmp(k, "flash")) drops.flash = v != 0.f;
            else if (!strcmp(k, "maxEntries")) drops.maxEntries = std::max(0, static_cast<int>(v));
            else if (!strcmp(k, "showRunTimer")) drops.showRunTimer = v != 0.f;
            else if (!strcmp(k, "showRift")) drops.showRift = v != 0.f;
            else if (!strcmp(k, "minRunSeconds")) drops.minRunSeconds = std::clamp(static_cast<int>(v), 0, 3600);
        }
        else if (!strncmp(key, "items.", 6)) {
            const char* k = key + 6;
            if (!strcmp(k, "badges")) items.badges = v != 0.f;
            else if (!strcmp(k, "badgeScale")) items.badgeScale = std::clamp(v, 0.5f, 3.0f);
            else if (!strcmp(k, "badgeCorner")) items.badgeCorner = std::clamp(static_cast<int>(v), 0, 3);
            else if (!strcmp(k, "badgeAugment")) items.badgeAugment = v != 0.f;
            else if (!strcmp(k, "colAncient")) items.colAncient = static_cast<uint32_t>(v) & 0xFFFFFF;
            else if (!strcmp(k, "colPrimal")) items.colPrimal = static_cast<uint32_t>(v) & 0xFFFFFF;
            else if (!strcmp(k, "hoverMode")) items.hoverMode = std::clamp(static_cast<int>(v), 0, 2);
            else if (!strcmp(k, "hoverAlpha")) items.hoverAlpha = std::clamp(v, 0.2f, 1.f);
            else if (!strcmp(k, "search")) items.search = v != 0.f;
            else if (!strcmp(k, "searchRgb")) items.searchRgb = static_cast<uint32_t>(v) & 0xFFFFFF;
            else if (!strcmp(k, "markIndexed")) items.markIndexed = v != 0.f;
            else if (!strcmp(k, "searchDim")) items.searchDim = std::clamp(v, 0.f, 1.f);
        }
        else if (!strncmp(key, "labels.", 7)) {
            const char* k = key + 7;
            if (!strcmp(k, "enabled")) labels.enabled = v != 0.f;
            else if (!strcmp(k, "mode")) labels.mode = std::clamp(static_cast<int>(v), 0, 2);
            else if (!strcmp(k, "showBonus")) labels.showBonus = v != 0.f;
            else if (!strcmp(k, "scale")) labels.scale = std::clamp(v, 0.5f, 3.0f);
            else if (!strcmp(k, "bgAlpha")) labels.bgAlpha = std::clamp(v, 0.f, 1.f);
            else if (!strcmp(k, "colLegendary")) labels.colLegendary = static_cast<uint32_t>(v) & 0xFFFFFF;
            else if (!strcmp(k, "colSet")) labels.colSet = static_cast<uint32_t>(v) & 0xFFFFFF;
            else if (!strcmp(k, "colPrimal")) labels.colPrimal = static_cast<uint32_t>(v) & 0xFFFFFF;
            else if (!strcmp(k, "colBonus")) labels.colBonus = static_cast<uint32_t>(v) & 0xFFFFFF;
        }
        else if (!strncmp(key, "hotkey.", 7)) {
            for (int h = 0; h < kHotkeyCount; ++h)
                if (!strcmp(key + 7, kHotkeyIds[h])) hotkeys[h] = static_cast<uint8_t>(std::clamp(v, 0.f, 254.f));
        }
        else if (!strcmp(key, "markerScale")) markerScale = v;
        else if (!strcmp(key, "outline")) outline = v != 0.f;
        else if (!strcmp(key, "bigMapMarkers")) bigMapMarkers = v != 0.f;
        else if (!strcmp(key, "showScenes")) showScenes = v != 0.f;
        else if (!strcmp(key, "sceneAlpha")) sceneAlpha = v;
        else if (!strcmp(key, "sceneLabels")) sceneLabels = v != 0.f;
        else if (!strcmp(key, "distanceLabels")) distanceLabels = v != 0.f;
        else if (!strcmp(key, "pylonArrow")) pylonArrow = v != 0.f;
        else if (!strcmp(key, "eliteRing")) eliteRing = v != 0.f;
        else if (!strcmp(key, "trashLimit")) trashLimit = std::max(0, static_cast<int>(v));
        else if (!strcmp(key, "playerRgb")) playerRgb = static_cast<uint32_t>(v) & 0xFFFFFF;
        else if (!strcmp(key, "playerSize")) playerSize = std::clamp(v, 2.f, 30.f);
        else if (!strncmp(key, "kind.", 5)) {
            // kind.<Name>.<show|color|size|shape|alpha|label|pulse> = v
            char kname[16], what[16] = {};
            if (sscanf(key, "kind.%15[^.].%15s", kname, what) == 2) {
                for (int k = 0; k < kKindCount; ++k) {
                    if (strcmp(kname, ActorKindName(static_cast<ActorKind>(k))) != 0) continue;
                    KindStyle& ks = kinds[k];
                    if (!strcmp(what, "show")) ks.show = v != 0.f;
                    else if (!strcmp(what, "color")) ks.rgb = static_cast<uint32_t>(v) & 0xFFFFFF;
                    else if (!strcmp(what, "size")) ks.size = v;
                    else if (!strcmp(what, "shape")) ks.shape = static_cast<MarkerShape>(std::clamp(static_cast<int>(v), 0, 3));
                    else if (!strcmp(what, "alpha")) ks.alpha = v;
                    else if (!strcmp(what, "label")) ks.label = v != 0.f;
                    else if (!strcmp(what, "pulse")) ks.pulse = v != 0.f;
                    else if (!strcmp(what, "maxDist")) ks.maxDist = std::max(0.f, v);
                    break;
                }
            }
        } else {
            static const char* kNames[MinimapLayout::kPanelCount] = {"Info", "Drops", "Items", "Actors",
                                                                     "Settings"};
            char pname[16], what[16] = {};
            // panel.<Info|Drops|Items|Actors|Settings>.<pinned|hidden|x|y> = v
            if (sscanf(key, "panel.%15[^.].%15s", pname, what) == 2) {
                for (int id = 0; id < MinimapLayout::kPanelCount; ++id) {
                    if (strcmp(pname, kNames[id]) != 0) continue;
                    if (!strcmp(what, "pinned")) panelPinned[id] = v != 0.f;
                    else if (!strcmp(what, "hidden")) panelHidden[id] = v != 0.f;
                    else if (!strcmp(what, "x")) panelX[id] = v;
                    else if (!strcmp(what, "y")) panelY[id] = v;
                    else if (!strcmp(what, "w")) panelW[id] = v;
                    else if (!strcmp(what, "h")) panelH[id] = v;
                    break;
                }
            }
        }
    }
    fclose(f);
    return true;
}

bool MinimapLayout::Save(const char* path) const {
    FILE* f = fopen(path, "w");
    if (f == nullptr) return false;
    std::print(f, "# D3Hud minimap layout (positions/radius = fraction of window height)\n");
    std::print(f, "onGameMap={}\nrightOffset={:.4f}\ntopOffset={:.4f}\nradius={:.4f}\nworldRadius={:.1f}\nbeyondRim={}\n",
            onGameMap ? 1 : 0, rightOffset, topOffset, radius, worldRadius, beyondRim ? 1 : 0);
    std::print(f, "# auto salvage: safe backpack cells (bit row*10+col), protections\n");
    std::print(f, "safeCells={:x}\nprotectEquipped={}\n",
               static_cast<unsigned long long>(safeArea.cells),
               salvageRules.protectEquippedAncients ? 1 : 0);
    std::print(f, "# marker look (color = 0xRRGGBB as decimal; shape 0 dot,1 ring,2 diamond,3 filled)\n");
    std::print(f, "markerScale={:.2f}\noutline={}\nbigMapMarkers={}\nshowScenes={}\nsceneAlpha={:.2f}\nsceneLabels={}\n",
               markerScale, outline ? 1 : 0, bigMapMarkers ? 1 : 0, showScenes ? 1 : 0, sceneAlpha, sceneLabels ? 1 : 0);
    std::print(f, "distanceLabels={}\npylonArrow={}\neliteRing={}\ntrashLimit={}\nplayerRgb={}\nplayerSize={:.1f}\n",
               distanceLabels ? 1 : 0, pylonArrow ? 1 : 0, eliteRing ? 1 : 0, trashLimit, playerRgb, playerSize);
    for (int k = 0; k < kKindCount; ++k) {
        const KindStyle& ks = kinds[k];
        std::print(f, "kind.{0}.show={1}\nkind.{0}.color={2}\nkind.{0}.size={3:.2f}\nkind.{0}.shape={4}\n"
                      "kind.{0}.alpha={5:.2f}\nkind.{0}.label={6}\nkind.{0}.pulse={7}\nkind.{0}.maxDist={8:.0f}\n",
                   ActorKindName(static_cast<ActorKind>(k)), ks.show ? 1 : 0, ks.rgb, ks.size,
                   static_cast<int>(ks.shape), ks.alpha, ks.label ? 1 : 0, ks.pulse ? 1 : 0, ks.maxDist);
    }
    static const char* kNames[kPanelCount] = {"Info", "Drops", "Items", "Actors", "Settings"};
    std::print(f, "# panels: pinned open, hidden from the dock, position (fraction of window; <0 = docked)\n");
    for (int i = 0; i < kPanelCount; ++i)
        std::print(f, "panel.{0}.pinned={1}\npanel.{0}.hidden={2}\npanel.{0}.x={3:.4f}\npanel.{0}.y={4:.4f}\n"
                      "panel.{0}.w={5:.4f}\npanel.{0}.h={6:.4f}\n",
                   kNames[i], panelPinned[i] ? 1 : 0, panelHidden[i] ? 1 : 0, panelX[i], panelY[i], panelW[i], panelH[i]);
    std::print(f, "salvage.rarityMask={}\nsalvage.keepAncient={}\nsalvage.keepPrimal={}\nsalvage.neverFamilies={}\nsalvage.maxPerRun={}\nsalvage.extraDelayMs={}\n",
               salvageRules.rarityMask, salvageRules.keepAncient ? 1 : 0, salvageRules.keepPrimal ? 1 : 0,
               salvageRules.neverFamilies, salvageRules.maxPerRun, salvageRules.extraDelayMs);
    std::print(f, "salvage.useAll={}\nsalvage.burst={}\n", salvageRules.useGameSalvageAll ? 1 : 0, salvageRules.burst ? 1 : 0);
    std::print(f, "salvage.confirm={}\nsalvage.preview={}\nsalvage.previewRgb={}\nsalvage.safeOnlyAtAnvil={}\n", salvageConfirm ? 1 : 0, salvagePreview ? 1 : 0, salvagePreviewRgb, safeOnlyAtAnvil ? 1 : 0);
    std::print(f, "readerAutostart={}\nreaderHz={}\ndiagFps={}\ndiagReader={}\n",
               readerAutostart ? 1 : 0, readerHz, diagFps ? 1 : 0, diagReader ? 1 : 0);
    std::print(f, "uiScale={:.2f}\ndockCorner={}\ndockAlpha={:.2f}\ndockAutoHide={}\npanelAlpha={:.2f}\ncompact={}\n",
               uiScale, dockCorner, dockAlpha, dockAutoHide ? 1 : 0, panelAlpha, compact ? 1 : 0);
    for (int h = 0; h < kHotkeyCount; ++h) std::print(f, "hotkey.{}={}\n", kHotkeyIds[h], hotkeys[h]);
    std::print(f, "drops.alertPrimal={}\ndrops.alertAncient={}\ndrops.sound={}\ndrops.flash={}\ndrops.maxEntries={}\n",
               drops.alertPrimal ? 1 : 0, drops.alertAncient ? 1 : 0, drops.sound ? 1 : 0, drops.flash ? 1 : 0, drops.maxEntries);
    std::print(f, "drops.showRunTimer={}\ndrops.minRunSeconds={}\ndrops.showRift={}\n", drops.showRunTimer ? 1 : 0, drops.minRunSeconds, drops.showRift ? 1 : 0);
    std::print(f, "items.badges={}\nitems.badgeScale={:.2f}\nitems.badgeCorner={}\nitems.badgeAugment={}\n",
               items.badges ? 1 : 0, items.badgeScale, items.badgeCorner, items.badgeAugment ? 1 : 0);
    std::print(f, "items.colAncient={}\nitems.colPrimal={}\nitems.hoverMode={}\nitems.hoverAlpha={:.2f}\n",
               items.colAncient, items.colPrimal, items.hoverMode, items.hoverAlpha);
    std::print(f, "items.search={}\nitems.searchRgb={}\nitems.searchDim={:.2f}\nitems.markIndexed={}\n", items.search ? 1 : 0, items.searchRgb, items.searchDim, items.markIndexed ? 1 : 0);
    std::print(f, "labels.enabled={}\nlabels.mode={}\nlabels.showBonus={}\nlabels.scale={:.2f}\nlabels.bgAlpha={:.2f}\n",
               labels.enabled ? 1 : 0, labels.mode, labels.showBonus ? 1 : 0, labels.scale, labels.bgAlpha);
    std::print(f, "labels.colLegendary={}\nlabels.colSet={}\nlabels.colPrimal={}\nlabels.colBonus={}\n",
               labels.colLegendary, labels.colSet, labels.colPrimal, labels.colBonus);
    fclose(f);
    return true;
}

void HudRenderer::SetLayoutPath(const char* path) {
    strncpy_s(layoutPath_, path, _TRUNCATE);
    layoutStatus_ = layout_.Load(layoutPath_) ? "loaded" : "defaults (no d3hud.ini yet)";
}

static ImU32 Rgba(uint32_t rgb, int alpha) {
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, std::clamp(alpha, 0, 255));
}

// One map marker, drawn from its kind's style (Settings > Minimap). Used
// pylons/shrines are not drawn at all.
static void DrawActorMarker(ImDrawList* dl, const ActorSnapshot& a, Vec2 px, int alpha, const MinimapLayout& L,
                            float dist = -1.f) {
    const KindStyle& st = L.kinds[static_cast<int>(a.kind)];
    if (!st.show) return;
    if (st.maxDist > 0.f && dist > st.maxDist) return;
    if ((a.kind == ActorKind::Pylon || a.kind == ActorKind::Shrine) && (a.itemFlags & kActorFlagUsed)) return;
    alpha = static_cast<int>(alpha * st.alpha);
    if (alpha <= 0) return;
    const ImU32 col = Rgba(st.rgb, alpha);
    const ImU32 dark = IM_COL32(0, 0, 0, L.outline ? alpha * 3 / 4 : 0);
    const float r = st.size * L.markerScale;
    const ImVec2 c(px.x, px.y);
    const ImVec2 t(c.x, c.y - r), rt(c.x + r, c.y), b(c.x, c.y + r), l(c.x - r, c.y);
    switch (st.shape) {
        case MarkerShape::Dot:
            // dark outline keeps dots readable on top of the game's map
            if (L.outline) dl->AddCircleFilled(c, r + 1.0f, IM_COL32(0, 0, 0, alpha * 140 / 255), 10);
            dl->AddCircleFilled(c, r, col, 10);
            break;
        case MarkerShape::Ring:
            dl->AddCircle(c, r, col, 16, 2.0f);
            break;
        case MarkerShape::Diamond:
            dl->AddQuad(t, rt, b, l, col, 2.0f);
            break;
        case MarkerShape::DiamondFilled:
            if (L.outline)
                dl->AddQuadFilled(ImVec2(t.x, t.y - 1.5f), ImVec2(rt.x + 1.5f, rt.y), ImVec2(b.x, b.y + 1.5f),
                                  ImVec2(l.x - 1.5f, l.y), dark);
            dl->AddQuadFilled(t, rt, b, l, col);
            break;
    }
    if (st.pulse) {
        const float phase = static_cast<float>(fmod(ImGui::GetTime(), 1.2) / 1.2);  // 0..1
        const int ringAlpha = static_cast<int>((1.f - phase) * alpha);
        dl->AddCircle(c, r + 4.f + phase * 14.f, Rgba(st.rgb, ringAlpha), 24, 2.0f);
    }
    if (!st.label) return;
    if (a.kind == ActorKind::PylonSpawn) {
        dl->AddText(ImVec2(c.x + r + 2.f, c.y - 7.f), col, "pylon?");
        return;
    }
    const char* label = ShrineLabel(a.name);
    if (label == nullptr) return;
    char withDist[96];
    if (L.distanceLabels && dist >= 0.f) {
        snprintf(withDist, sizeof(withDist), "%s  %.0fm", label, dist);
        label = withDist;
    }
    const ImVec2 sz = ImGui::CalcTextSize(label);
    const ImVec2 at(c.x - sz.x * 0.5f, c.y + r + 3.f);
    dl->AddRectFilled(ImVec2(at.x - 3, at.y - 1), ImVec2(at.x + sz.x + 3, at.y + sz.y + 1),
                      IM_COL32(0, 0, 0, alpha * 2 / 3), 3.f);
    dl->AddText(at, col, label);
}

template <typename ToPx, typename Keep>
void HudRenderer::DrawScenes(const WorldSnapshot& snap, ImDrawList* dl, ToPx toPx, Keep keep) {
    if (!layout_.showScenes) return;
    auto fade = [&](ImU32 c) {  // scale the alpha channel by the prediction opacity
        const int a = std::clamp(static_cast<int>(((c >> IM_COL32_A_SHIFT) & 255) * layout_.sceneAlpha), 0, 255);
        return (c & 0x00FFFFFF) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
    };
    const ImU32 kFill = fade(IM_COL32(175, 190, 215, 55));
    const ImU32 kEntranceFill = fade(IM_COL32(90, 230, 120, 75)), kExitFill = fade(IM_COL32(255, 140, 40, 95));
    const uint32_t n = std::min<uint32_t>(snap.sceneCount, kMaxScenes);
    for (uint32_t s = 0; s < n; ++s) {
        const SceneSnapshot& sc = snap.scenes[s];
        // whole scene off the map: skip its cells
        const Vec2 mid{(sc.x0 + sc.x1) * 0.5f, (sc.y0 + sc.y1) * 0.5f};
        if (!keep(mid, std::max(sc.x1 - sc.x0, sc.y1 - sc.y0))) continue;
        const std::vector<CellRect>* cells = sceneCells_.Find(sc.sno);
        if (cells == nullptr) continue;
        // level entrance / exit tiles (rift exits: the next-level portal spawns there)
        const uint32_t role = sceneCells_.Flags(sc.sno);
        const ImU32 fill = (role & kSceneExit) ? kExitFill : (role & kSceneEntrance) ? kEntranceFill : kFill;
        for (const CellRect& c : *cells) {
            const Vec2 a{sc.x0 + c.x0, sc.y0 + c.y0}, b{sc.x0 + c.x1, sc.y0 + c.y1};
            if (!keep({(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, 0.f)) continue;
            // a world-axis rectangle is a rotated quad on the (isometric) map
            const Vec2 p0 = toPx(a), p1 = toPx({b.x, a.y}), p2 = toPx(b), p3 = toPx({a.x, b.y});
            dl->AddQuadFilled(ImVec2(p0.x, p0.y), ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), ImVec2(p3.x, p3.y), fill);
        }
        if (role != 0 && layout_.sceneLabels && keep(mid, 0.f)) {
            const char* label = (role & kSceneExit) ? "Exit" : "Entrance";
            const Vec2 c = toPx(mid);
            const ImVec2 sz = ImGui::CalcTextSize(label);
            const ImVec2 at(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f);
            dl->AddRectFilled(ImVec2(at.x - 3, at.y - 1), ImVec2(at.x + sz.x + 3, at.y + sz.y + 1),
                              IM_COL32(0, 0, 0, 150), 3.f);
            dl->AddText(at, (fill & 0x00FFFFFF) | IM_COL32(0, 0, 0, 255), label);
        }
    }
}

// While the game's TAB map is open: the same markers on it (centred on the
// window, H/1200 px per world unit, following the map's pan offset).
void HudRenderer::DrawBigMap(const WorldSnapshot& snap, const HudOptions& opt) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    auto toPx = [&](Vec2 w) {
        return WorldToBigMap(w, snap.playerPos, snap.ui.bigMapPan, display.x, display.y, opt.mapOrientation);
    };
    DrawScenes(snap, dl, toPx, [&](Vec2 w, float slack) {
        const Vec2 p = toPx(w);
        const float m = slack * display.y / 1200.0f;  // world size -> px
        return p.x > -m && p.y > -m && p.x < display.x + m && p.y < display.y + m;
    });
    if (!layout_.bigMapMarkers) return;
    for (uint16_t i = 0; i < snap.actorCount; ++i) {
        const ActorSnapshot& a = snap.actors[i];
        if (a.kind == ActorKind::Player) continue;  // the game marks the player itself
        const Vec2 px = WorldToBigMap(a.world, snap.playerPos, snap.ui.bigMapPan, display.x, display.y,
                                      opt.mapOrientation);
        if (px.x < 0.f || px.y < 0.f || px.x > display.x || px.y > display.y) continue;
        DrawActorMarker(dl, a, px, 255, layout_, std::hypot(a.world.x - snap.playerPos.x, a.world.y - snap.playerPos.y));
    }
}

void HudRenderer::DrawMinimap(const WorldSnapshot& snap, const HudOptions& opt) {
    if (snap.ui.bigMapOpen) {
        DrawBigMap(snap, opt);
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const float h = io.DisplaySize.y;
    float radiusPx, cx, cy;
    if (layout_.onGameMap) {
        radiusPx = layout_.radius * h;
        cx = io.DisplaySize.x - layout_.rightOffset * h;
        cy = layout_.topOffset * h;
    } else {  // own minimap, bottom-right
        radiusPx = std::max(layout_.radius * h, 60.0f) * 1.2f;
        cx = io.DisplaySize.x - radiusPx - 24.0f;
        cy = io.DisplaySize.y - radiusPx - 24.0f;
    }
    const float unitsPerPx = layout_.worldRadius / std::max(radiusPx, 1.0f);
    const ImVec2 c(cx, cy);
    const Vec2 center = {cx, cy};
    // background layer: under the panels, over nothing else (the window is transparent)
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    if (!layout_.onGameMap) {
        dl->AddCircleFilled(c, radiusPx, IM_COL32(10, 12, 20, 150), 48);
        dl->AddCircle(c, radiusPx, IM_COL32(200, 200, 220, 90), 48, 1.5f);
        dl->AddCircle(c, radiusPx * 0.5f, IM_COL32(200, 200, 220, 45), 40, 1.0f);
    } else if (opt.inputInteractive) {
        // calibration aid: show the circle we think the game's minimap covers
        dl->AddCircle(c, radiusPx, IM_COL32(255, 255, 255, 140), 64, 1.5f);
        dl->AddLine(ImVec2(cx - 6, cy), ImVec2(cx + 6, cy), IM_COL32(255, 255, 255, 160));
        dl->AddLine(ImVec2(cx, cy - 6), ImVec2(cx, cy + 6), IM_COL32(255, 255, 255, 160));
    }

    // Map prediction under the markers: walkable cells inside the circle only.
    auto toPx = [&](Vec2 w) {
        Vec2 px;
        WorldToMinimap(w, snap.playerPos, unitsPerPx, center, 1e9f, &px, nullptr, opt.mapOrientation);
        return px;
    };
    DrawScenes(snap, dl, toPx, [&](Vec2 w, float slack) {
        const float dx = w.x - snap.playerPos.x, dy = w.y - snap.playerPos.y;
        const float r = layout_.worldRadius + slack;
        return dx * dx + dy * dy <= r * r;
    });

    // Actors: inside the circle at full strength; beyond the rim (optional) at
    // their true scaled position anywhere on screen, faded.
    int trashCount = 0;  // thinning: with a limit set, draw every `stride`-th trash/minion dot
    if (layout_.trashLimit > 0)
        for (uint16_t i = 0; i < snap.actorCount; ++i)
            trashCount += snap.actors[i].kind == ActorKind::Trash || snap.actors[i].kind == ActorKind::Minion;
    const int stride = layout_.trashLimit > 0 ? std::max(1, (trashCount + layout_.trashLimit - 1) / layout_.trashLimit) : 1;
    int trashSeen = 0;
    for (uint16_t i = 0; i < snap.actorCount; ++i) {
        const ActorSnapshot& a = snap.actors[i];
        if (a.kind == ActorKind::Player) continue;  // the centre marker is the player
        if ((a.kind == ActorKind::Trash || a.kind == ActorKind::Minion) && stride > 1 && (trashSeen++ % stride) != 0)
            continue;
        Vec2 px;
        const bool inside = WorldToMinimap(a.world, snap.playerPos, unitsPerPx, center, radiusPx, &px,
                                           nullptr, opt.mapOrientation);
        if (!inside) {
            if (!layout_.beyondRim) continue;
            WorldToMinimap(a.world, snap.playerPos, unitsPerPx, center, 1e9f, &px, nullptr,
                           opt.mapOrientation);
        }
        DrawActorMarker(dl, a, px, inside ? 255 : 170, layout_,
                        std::hypot(a.world.x - snap.playerPos.x, a.world.y - snap.playerPos.y));
    }

    // Ring around the nearest rare/champion (inside the circle only).
    if (layout_.eliteRing) {
        const ActorSnapshot* best = nullptr;
        float bestDist = FLT_MAX;
        for (uint16_t i = 0; i < snap.actorCount; ++i) {
            const ActorSnapshot& a = snap.actors[i];
            if ((a.kind != ActorKind::Rare && a.kind != ActorKind::Champion) || !layout_.kinds[static_cast<int>(a.kind)].show)
                continue;
            const float d = std::hypot(a.world.x - snap.playerPos.x, a.world.y - snap.playerPos.y);
            if (d < bestDist) { bestDist = d; best = &a; }
        }
        Vec2 px;
        if (best != nullptr && WorldToMinimap(best->world, snap.playerPos, unitsPerPx, center, radiusPx, &px, nullptr,
                                              opt.mapOrientation)) {
            const float r = layout_.kinds[static_cast<int>(best->kind)].size * layout_.markerScale + 5.f;
            dl->AddCircle(ImVec2(px.x, px.y), r, IM_COL32(255, 255, 255, 230), 20, 1.5f);
        }
    }

    // Nearest unused pylon off the map: an arrow on the rim pointing to it, with the distance.
    const KindStyle& pylonStyle = layout_.kinds[static_cast<int>(ActorKind::Pylon)];
    if (layout_.pylonArrow && pylonStyle.show) {
        const ActorSnapshot* best = nullptr;
        float bestDist = FLT_MAX;
        for (uint16_t i = 0; i < snap.actorCount; ++i) {
            const ActorSnapshot& a = snap.actors[i];
            if (a.kind != ActorKind::Pylon || (a.itemFlags & kActorFlagUsed)) continue;
            const float d = std::hypot(a.world.x - snap.playerPos.x, a.world.y - snap.playerPos.y);
            if (d < bestDist) { bestDist = d; best = &a; }
        }
        if (best != nullptr && bestDist > layout_.worldRadius) {
            Vec2 px;
            WorldToMinimap(best->world, snap.playerPos, unitsPerPx, center, 1e9f, &px, nullptr, opt.mapOrientation);
            ImVec2 dir(px.x - cx, px.y - cy);
            const float len = std::hypot(dir.x, dir.y);
            if (len > 1.f) {
                dir = ImVec2(dir.x / len, dir.y / len);
                const ImVec2 perp(-dir.y, dir.x);
                const ImVec2 tip(cx + dir.x * (radiusPx - 2.f), cy + dir.y * (radiusPx - 2.f));
                const ImVec2 base(cx + dir.x * (radiusPx - 16.f), cy + dir.y * (radiusPx - 16.f));
                const ImU32 col = Rgba(pylonStyle.rgb, 200);
                dl->AddTriangleFilled(tip, ImVec2(base.x + perp.x * 7.f, base.y + perp.y * 7.f),
                                      ImVec2(base.x - perp.x * 7.f, base.y - perp.y * 7.f), col);
                char txt[24];
                snprintf(txt, sizeof(txt), "%.0fm", bestDist);
                const ImVec2 ts = ImGui::CalcTextSize(txt);
                const ImVec2 at(cx + dir.x * (radiusPx - 30.f) - ts.x * 0.5f, cy + dir.y * (radiusPx - 30.f) - ts.y * 0.5f);
                dl->AddRectFilled(ImVec2(at.x - 3, at.y - 1), ImVec2(at.x + ts.x + 3, at.y + ts.y + 1), IM_COL32(0, 0, 0, 150), 3.f);
                dl->AddText(at, col, txt);
            }
        }
    }

    // Player arrow (own minimap only - the game's minimap draws its own player marker).
    if (!layout_.onGameMap) {
        const Vec2 dir = OrientForMap({std::cos(snap.playerHeading), std::sin(snap.playerHeading)},
                                      opt.mapOrientation);
        const float ang = -std::atan2(dir.y, dir.x);
        const float s = layout_.playerSize;
        const ImVec2 tip(cx + s * 1.6f * std::cos(ang), cy + s * 1.6f * std::sin(ang));
        const ImVec2 left(cx + s * cosf(ang + 2.5f), cy + s * sinf(ang + 2.5f));
        const ImVec2 right(cx + s * cosf(ang - 2.5f), cy + s * sinf(ang - 2.5f));
        dl->AddTriangleFilled(tip, left, right, Rgba(layout_.playerRgb, 255));
        dl->AddTriangle(tip, left, right, IM_COL32(0, 0, 0, 160), 1.0f);
    }
}

// Calibration (Settings > Minimap): move/scale our dots until they sit on the
// game's own minimap icons (pylon, waypoint, ...), then Save.
void HudRenderer::DrawMinimapSettings() {
    ImGui::Checkbox("draw over the game's minimap", &layout_.onGameMap);
    ImGui::Checkbox("draw actors beyond the rim", &layout_.beyondRim);
    ImGui::SliderFloat("from right edge", &layout_.rightOffset, 0.0f, 0.5f, "%.3f");
    ImGui::SliderFloat("from top edge", &layout_.topOffset, 0.0f, 0.5f, "%.3f");
    ImGui::SliderFloat("radius", &layout_.radius, 0.03f, 0.4f, "%.3f");
    ImGui::SliderFloat("zoom (world units to rim)", &layout_.worldRadius, 20.0f, 400.0f, "%.0f");

    ImGui::SeparatorText("Markers");
    ImGui::SliderFloat("marker size", &layout_.markerScale, 0.5f, 3.0f, "x%.2f");
    ImGui::Checkbox("dark outline", &layout_.outline);
    ImGui::SameLine();
    ImGui::Checkbox("markers on the TAB map", &layout_.bigMapMarkers);
    ImGui::Checkbox("map prediction", &layout_.showScenes);
    ImGui::SameLine();
    ImGui::Checkbox("Entrance/Exit labels", &layout_.sceneLabels);
    ImGui::Checkbox("distance on pylon/shrine names", &layout_.distanceLabels);
    ImGui::SameLine();
    ImGui::Checkbox("arrow to the nearest pylon when off the map", &layout_.pylonArrow);
    ImGui::Checkbox("ring on the nearest rare/champion", &layout_.eliteRing);
    ImGui::SliderInt("max trash dots (0 = all)", &layout_.trashLimit, 0, 200);
    {
        float c[3] = {((layout_.playerRgb >> 16) & 255) / 255.f, ((layout_.playerRgb >> 8) & 255) / 255.f,
                      (layout_.playerRgb & 255) / 255.f};
        if (ImGui::ColorEdit3("player arrow (own minimap)", c, ImGuiColorEditFlags_NoInputs))
            layout_.playerRgb = (static_cast<uint32_t>(c[0] * 255.f + 0.5f) << 16) |
                                (static_cast<uint32_t>(c[1] * 255.f + 0.5f) << 8) | static_cast<uint32_t>(c[2] * 255.f + 0.5f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f * layout_.uiScale);
        ImGui::SliderFloat("size", &layout_.playerSize, 3.0f, 20.0f, "%.0f");
    }
    ImGui::SliderFloat("prediction opacity", &layout_.sceneAlpha, 0.0f, 2.0f, "x%.2f");
    if (ImGui::BeginTable("kinds", 8,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, std::max(120.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.2f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Show", ImGuiTableColumnFlags_WidthFixed, 38.0f);
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Shape", ImGuiTableColumnFlags_WidthFixed, 130.0f * layout_.uiScale);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Opacity", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Max dist", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Extras", ImGuiTableColumnFlags_WidthFixed, 130.0f * layout_.uiScale);
        ImGui::TableHeadersRow();
        for (int k = 1; k < MinimapLayout::kKindCount; ++k) {  // 0 = player: the centre arrow, no marker
            KindStyle& st = layout_.kinds[k];
            ImGui::PushID(k);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Rgba(st.rgb, 255)), "%s",
                               ActorKindName(static_cast<ActorKind>(k)));
            ImGui::TableNextColumn();
            ImGui::Checkbox("##show", &st.show);
            ImGui::TableNextColumn();
            float rgb[3] = {((st.rgb >> 16) & 255) / 255.f, ((st.rgb >> 8) & 255) / 255.f, (st.rgb & 255) / 255.f};
            if (ImGui::ColorEdit3("##color", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                st.rgb = (static_cast<uint32_t>(rgb[0] * 255.f + 0.5f) << 16) |
                         (static_cast<uint32_t>(rgb[1] * 255.f + 0.5f) << 8) |
                         static_cast<uint32_t>(rgb[2] * 255.f + 0.5f);
            }
            ImGui::TableNextColumn();
            int shape = static_cast<int>(st.shape);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##shape", &shape, "Dot\0Ring\0Diamond\0Filled diamond\0"))
                st.shape = static_cast<MarkerShape>(shape);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::DragFloat("##size", &st.size, 0.1f, 0.5f, 16.0f, "%.1f");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::DragFloat("##alpha", &st.alpha, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::DragFloat("##maxdist", &st.maxDist, 1.0f, 0.0f, 1000.0f, st.maxDist > 0.f ? "%.0f" : "off");
            ImGui::TableNextColumn();
            if (k == static_cast<int>(ActorKind::Pylon) || k == static_cast<int>(ActorKind::Shrine) ||
                k == static_cast<int>(ActorKind::PylonSpawn)) {
                ImGui::Checkbox("name", &st.label);
                ImGui::SameLine();
            }
            ImGui::Checkbox("pulse", &st.pulse);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("Save")) layoutStatus_ = layout_.Save(layoutPath_) ? "saved" : "save FAILED";
    ImGui::SameLine();
    if (ImGui::Button("Reset placement")) {
        const MinimapLayout d;
        layout_.onGameMap = d.onGameMap;
        layout_.rightOffset = d.rightOffset;
        layout_.topOffset = d.topOffset;
        layout_.radius = d.radius;
        layout_.worldRadius = d.worldRadius;
        layout_.beyondRim = d.beyondRim;
        layoutStatus_ = "placement reset (not saved)";
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset markers")) {
        layout_.ResetMarkers();
        layoutStatus_ = "markers reset (not saved)";
    }
    ImGui::TextDisabled("%s  -  %s", layoutStatus_, layoutPath_);
}

// Debug view: every actor the minimap receives this frame, nearest first.
void HudRenderer::DrawActorPanel(const WorldSnapshot& snap, const HudOptions& opt) {
    if (!PanelOpen(PanelId::Actors, opt, "Actors (minimap source)")) return;

    int perKind[static_cast<int>(ActorKind::PylonSpawn) + 1] = {};
    std::vector<std::pair<float, uint16_t>> order;
    order.reserve(snap.actorCount);
    for (uint16_t i = 0; i < snap.actorCount; ++i) {
        const ActorSnapshot& a = snap.actors[i];
        const float dx = a.world.x - snap.playerPos.x, dy = a.world.y - snap.playerPos.y;
        order.push_back({std::sqrt(dx * dx + dy * dy), i});
        const int k = static_cast<int>(a.kind);
        if (k >= 0 && k <= static_cast<int>(ActorKind::PylonSpawn)) perKind[k]++;
    }
    std::sort(order.begin(), order.end());

    // per-kind counts in their map colours
    ImGui::Text("%u actors:", snap.actorCount);
    for (int k = 0; k <= static_cast<int>(ActorKind::PylonSpawn); ++k) {
        if (perKind[k] == 0) continue;
        const ActorKind kind = static_cast<ActorKind>(k);
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(KindColor(kind)), "%s %d",
                           ActorKindName(kind), perKind[k]);
    }
    filter_.Draw("filter (name/kind, F8 to type)", 220.0f);

    if (ImGui::BeginTable("actors", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable,
                          ImVec2(560.0f, 340.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableSetupColumn("World x, y", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableHeadersRow();
        for (const auto& [dist, i] : order) {
            const ActorSnapshot& a = snap.actors[i];
            if (filter_.IsActive() && !filter_.PassFilter(a.name) &&
                !filter_.PassFilter(ActorKindName(a.kind)))
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(KindColor(a.kind)), "%s",
                               ActorKindName(a.kind));
            ImGui::TableNextColumn();
            if ((a.kind == ActorKind::Pylon || a.kind == ActorKind::Shrine) && (a.itemFlags & kActorFlagUsed))
                ImGui::TextDisabled("%s (used)", a.name);
            else
                ImGui::TextUnformatted(a.name);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", dist);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f, %.1f", a.world.x, a.world.y);
            ImGui::TableNextColumn();
            ImGui::Text("%08llX", static_cast<unsigned long long>(a.id));
        }
        ImGui::EndTable();
    }
    PanelEnd(PanelId::Actors, true);
}

void HudRenderer::DrawSalvagePanel(const WorldSnapshot& snap, const HudOptions& opt) {
    if (!PanelOpen(PanelId::Items, opt, "Items")) return;
    // live items carry a location; synthetic ones (M0 fake data) do not
    const bool live = snap.itemCount > 0 && snap.items[0].location != kItemLocationUnknown;
    ImGui::Text("%u items %s", snap.itemCount,
                live ? "(equipped + backpack)" : "(synthetic M0 data)");
    ImGui::Separator();

    if (ImGui::BeginTable("items", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable,
                          ImVec2(560.0f, 300.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthFixed, 95.0f);
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Rarity");
        ImGui::TableSetupColumn("Ancient");
        ImGui::TableSetupColumn("Verdict");
        ImGui::TableHeadersRow();
        for (uint16_t i = 0; i < snap.itemCount; ++i) {
            const ItemSnapshot& it = snap.items[i];
            // rows are labelled by item name and duplicates are normal (4x the same
            // consumable): scope each row's widgets by its index
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (it.location == kItemLocationBackpack || it.location == kItemLocationStash)
                ImGui::Text("%s r%u c%u", ItemLocationName(it.location), it.gridY, it.gridX);
            else
                ImGui::TextUnformatted(ItemLocationName(it.location));
            ImGui::TableNextColumn();
            // span-all-columns selectable: whole-row hover highlight + click selection
            char label[96];
            if (it.identified) {
                snprintf(label, sizeof(label), "%s", it.name);
            } else {
                snprintf(label, sizeof(label), "(unided)");
            }
            if (ImGui::Selectable(label, selectedRow_ == i, ImGuiSelectableFlags_SpanAllColumns))
                selectedRow_ = i;
            if (!it.identified) {
                // The unided reveal: what the item really is under the "?" - the
                // real name when known (live), else just the type (synthetic data)
                const char* reveal = it.name[0] != '\0' ? it.name : it.typeName;
                if (it.ancient || it.primal) {
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "-> %s%s", reveal,
                                       it.primal ? " [PRIMAL]" : " [ANCIENT]");
                } else if (it.name[0] != '\0') {
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.1f, 1.0f), "-> %s", reveal);
                } else {
                    ImGui::TextDisabled("-> %s", reveal);
                }
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(it.typeName);
            ImGui::TableNextColumn();
            switch (it.rarity) {  // D3's item colours
                case ItemRarity::Legendary: ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.1f, 1.0f), "Legendary"); break;
                case ItemRarity::Set:       ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Set"); break;
                case ItemRarity::Rare:      ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Rare"); break;
                case ItemRarity::Magic:     ImGui::TextColored(ImVec4(0.45f, 0.55f, 1.0f, 1.0f), "Magic"); break;
                case ItemRarity::Unknown:   ImGui::TextDisabled("..."); break;  // properties still loading
                default:                    ImGui::TextUnformatted(RarityName(it.rarity));
            }
            ImGui::TableNextColumn();
            if (it.primal)       ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Primal");
            else if (it.ancient) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Ancient");
            else                 ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            switch (it.verdict) {
                case SalvageVerdict::Junk:  ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "JUNK");  break;
                case SalvageVerdict::Keep:  ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "KEEP");  break;
                case SalvageVerdict::Check: ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "CHECK"); break;
                default: ImGui::TextDisabled("-");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    PanelEnd(PanelId::Items, true);
}

// ---------------------------------------------------------------------------
// Panel framework: one dock bar bottom-right with a tab per visible panel.
// Hovering a tab (or an open panel body) expands it in place - view-only while
// the overlay is click-through. F8 + click on the tab pins it open; dragging a
// panel then moves it, and the position persists (fractions of window size).
// ---------------------------------------------------------------------------
static const char* PanelName(PanelId id) {
    switch (id) {
        case PanelId::Info: return "Info";
        case PanelId::Drops: return "Drops";
        case PanelId::Items: return "Items";
        case PanelId::Actors: return "Actors";
        case PanelId::Settings: return "Settings";
    }
    return "?";
}

// Anchor point (and ImGui pivot) in a screen corner, `inset` px from the edges.
static void CornerAnchor(int corner, ImVec2 display, float inset, ImVec2* pos, ImVec2* pivot) {
    const bool right = corner == 0 || corner == 2, bottom = corner == 0 || corner == 1;
    *pos = ImVec2(right ? display.x - 12.f : 12.f, bottom ? display.y - inset : inset);
    *pivot = ImVec2(right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f);
}

bool HudRenderer::PanelOpen(PanelId id, const HudOptions& opt, const char* title, ImVec2 defaultSize) {
    const int i = static_cast<int>(id);
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 cur(opt.cursorX, opt.cursorY);
    // hovered: cursor inside last frame's window rect or the dock tab
    const bool inRect = opt.cursorX >= 0.f && panelSize_[i].x > 0.f &&
                        cur.x >= panelPos_[i].x - 4 && cur.x <= panelPos_[i].x + panelSize_[i].x + 4 &&
                        cur.y >= panelPos_[i].y - 4 && cur.y <= panelPos_[i].y + panelSize_[i].y + 4;
    if (inRect) panelHoverUntil_[i] = (float)ImGui::GetTime() + 0.30f;  // linger
    // Settings is an editor, not a peek: it opens only when pinned (F8 + click on its tab)
    const bool peek = id != PanelId::Settings && ImGui::GetTime() < panelHoverUntil_[i];
    const bool expanded = layout_.panelPinned[i] || peek;
    if (!expanded) {
        panelSize_[i] = ImVec2(0.f, 0.f);  // no stale rect: a collapsed panel has no hover area
        return false;
    }
    // docked panels open above the dock bar; moved panels keep their position.
    // While the user drags the title bar ImGui owns the position.
    const ImVec2 display = io.DisplaySize;
    const ImGuiWindow* moving = ImGui::GetCurrentContext()->MovingWindow;
    const bool beingMoved = moving != nullptr && std::strcmp(moving->Name, title) == 0;
    if (!beingMoved) {
        if (layout_.panelX[i] >= 0.f)
            ImGui::SetNextWindowPos(ImVec2(layout_.panelX[i] * display.x, layout_.panelY[i] * display.y),
                                    ImGuiCond_Always);
        else {
            ImVec2 pos, pivot;
            CornerAnchor(layout_.dockCorner, display, 44.f, &pos, &pivot);  // just beside the dock bar
            ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
        }
    }
    ImGui::SetNextWindowBgAlpha(layout_.panelAlpha);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    panelResizable_[i] = defaultSize.x > 0.f;
    if (panelResizable_[i]) {
        const ImVec2 size = layout_.panelW[i] > 0.f
                                ? ImVec2(layout_.panelW[i] * display.x, layout_.panelH[i] * display.y)
                                : defaultSize;
        ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(420.f, 280.f), ImVec2(display.x, display.y));
    } else {
        flags |= ImGuiWindowFlags_AlwaysAutoResize;
    }
    if (!opt.inputInteractive) flags |= ImGuiWindowFlags_NoInputs;  // hover = view-only
    if (!ImGui::Begin(title, nullptr, flags)) {
        ImGui::End();
        return false;
    }
    if (opt.inputInteractive) {
        ImGui::PushStyleColor(ImGuiCol_Text, layout_.panelPinned[i] ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f)
                                                                    : ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        if (ImGui::SmallButton(layout_.panelPinned[i] ? "pinned" : "pin")) {
            layout_.panelPinned[i] = !layout_.panelPinned[i];
            layout_.Save(layoutPath_);
        }
        ImGui::PopStyleColor();
        if (layout_.panelX[i] >= 0.f) {
            ImGui::SameLine();
            if (ImGui::SmallButton("dock")) {  // back to the bottom-right corner
                layout_.panelX[i] = layout_.panelY[i] = -1.f;
                layout_.Save(layoutPath_);
            }
        }
        ImGui::Separator();
    }
    return true;
}

void HudRenderer::PanelEnd(PanelId id, bool wasOpen) {
    if (!wasOpen) return;
    const int i = static_cast<int>(id);
    ImGuiWindow* win = ImGui::GetCurrentWindow();  // before End(): that pops it
    panelPos_[i] = win->Rect().Min;
    panelSize_[i] = win->Rect().GetSize();
    const bool moving = ImGui::GetCurrentContext()->MovingWindow == win;
    ImGui::End();
    if (panelResizable_[i] && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {  // resize finished
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float w = panelSize_[i].x / display.x, h = panelSize_[i].y / display.y;
        if (std::abs(w - layout_.panelW[i]) > 0.002f || std::abs(h - layout_.panelH[i]) > 0.002f) {
            layout_.panelW[i] = w;
            layout_.panelH[i] = h;
            layout_.Save(layoutPath_);
        }
    }
    // Follow the title-bar drag in the layout every frame, so the forced position of the
    // next frame (PanelOpen) is where the panel was dropped; write the file on release.
    if (moving) {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        layout_.panelX[i] = panelPos_[i].x / display.x;
        layout_.panelY[i] = panelPos_[i].y / display.y;
        panelMoved_[i] = true;
    } else if (panelMoved_[i]) {
        panelMoved_[i] = false;
        layout_.Save(layoutPath_);
    }
}

void HudRenderer::DrawDock(const HudOptions& opt) {
    // a slim bar of tabs, bottom-right; a click (interactive mode) toggles pin
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (layout_.dockAutoHide && dockSize_.x > 0.f && !opt.inputInteractive) {
        bool anyOpen = false;  // a panel is showing: keep the dock to go with it
        for (int i = 0; i < kPanels; ++i) anyOpen |= panelSize_[i].x > 0.f;
        const float m = 40.0f;  // proximity margin around the dock
        const bool closeToDock = opt.cursorX >= dockPos_.x - m && opt.cursorX <= dockPos_.x + dockSize_.x + m &&
                          opt.cursorY >= dockPos_.y - m && opt.cursorY <= dockPos_.y + dockSize_.y + m;
        if (!anyOpen && !closeToDock) return;
    }
    ImVec2 pos, pivot;
    CornerAnchor(layout_.dockCorner, display, 12.f, &pos, &pivot);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f,
                          opt.inputInteractive ? std::min(1.0f, layout_.dockAlpha + 0.3f) : layout_.dockAlpha));
    if (ImGui::Begin("##dock", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         (opt.inputInteractive ? 0 : ImGuiWindowFlags_NoInputs))) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.9f, 1.0f));
        bool first = true;
        for (int i = 0; i < MinimapLayout::kPanelCount; ++i) {
            if (layout_.panelHidden[i]) continue;
            const bool pinned = layout_.panelPinned[i];
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  pinned ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f) : ImVec4(0.85f, 0.85f, 0.9f, 1.0f));
            if (!first) ImGui::SameLine();
            first = false;
            if (ImGui::Button(PanelName(static_cast<PanelId>(i)))) {
                layout_.panelPinned[i] = !layout_.panelPinned[i];
                layout_.Save(layoutPath_);
            }
            ImGui::PopStyleColor();
            // cursor test, not IsItemHovered: the dock ignores input while click-through
            const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
            if (opt.cursorX >= lo.x && opt.cursorX <= hi.x && opt.cursorY >= lo.y && opt.cursorY <= hi.y)
                panelHoverUntil_[i] = (float)ImGui::GetTime() + 0.30f;
        }
        ImGui::PopStyleColor();
        dockPos_ = ImGui::GetWindowPos();
        dockSize_ = ImGui::GetWindowSize();
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void HudRenderer::DrawInfo(const WorldSnapshot& snap, const HudOptions& opt, double fps,
                           const char* sourceName) {
    if (!PanelOpen(PanelId::Info, opt, "Info")) return;
    ImGui::Text("D3Hud M0 | source: %s", sourceName);
    ImGui::Text("frame %llu  %.0f fps", static_cast<unsigned long long>(snap.frameIndex), fps);
    ImGui::Text("actors %u  items %u", snap.actorCount, snap.itemCount);
    ImGui::Text("player (%.1f, %.1f)", snap.playerPos.x, snap.playerPos.y);
    ImGui::Text("map orientation %d: %s", opt.mapOrientation,
                MapOrientationName(opt.mapOrientation));
    if (ReaderStatus* rs = opt.reader) {
        ImGui::Separator();
        if (rs->running) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "reader running (pid %lu)%s", rs->pid,
                               rs->connected ? "" : ", connecting...");
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop reader")) rs->stopClicked = true;
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "reader stopped");
            ImGui::SameLine();
            if (ImGui::SmallButton("Start reader")) rs->startClicked = true;
            if (!opt.inputInteractive) ImGui::TextDisabled("(F8 to click)");
        }
        if (rs->error[0] != '\0') ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", rs->error);
        if (rs->line[0] != '\0') ImGui::TextDisabled("%.70s", rs->line);
    }
    ImGui::Separator();
    if (opt.inputInteractive) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "INPUT CAPTURED (F8: release)");
    } else {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "INPUT PASS-THROUGH (F8: interact)");
    }
    {
        char k[4][32];
        for (int h = 0; h < 4; ++h) KeyName(layout_.hotkeys[h], k[h], sizeof(k[h]));
        ImGui::Text("%s map orientation | %s click-through | %s panels | %s quit", k[MinimapLayout::kHkMapOrientation],
                    k[MinimapLayout::kHkClickThrough], k[MinimapLayout::kHkPanels], k[MinimapLayout::kHkQuit]);
    }
    PanelEnd(PanelId::Info, true);
}

// Labels above legendary/set/bonus items lying in the world, like the game's own
// item labels but with what the item really is: "Ancient Puzzle Ring".
void HudRenderer::DrawGroundLabels(const WorldSnapshot& snap, const HudOptions& opt) {
    const LabelStyle& L = layout_.labels;
    if (!L.enabled) return;
    const Vec2 center = opt.haveLabelCenter ? opt.labelCenter : snap.playerPos;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * L.scale;
    for (uint16_t i = 0; i < snap.actorCount; ++i) {
        const ActorSnapshot& a = snap.actors[i];
        if (a.kind != ActorKind::GroundItem || a.itemRarity == ItemRarity::Unknown || a.name[0] == '\0')
            continue;
        const bool bonus = (a.itemFlags & kItemFlagBonus) != 0;
        const bool isSet = a.itemRarity == ItemRarity::Set, isLegendary = a.itemRarity == ItemRarity::Legendary;
        const bool anc = (a.itemFlags & (kItemFlagAncient | kItemFlagPrimal)) != 0;
        bool pass = bonus && L.showBonus;
        if (!pass) {
            switch (L.mode) {
                case 1:  pass = (isSet || isLegendary) && anc; break;
                case 2:  pass = isSet; break;
                default: pass = isSet || isLegendary; break;
            }
        }
        if (!pass) continue;
        Vec2 p;
        if (!WorldToScreen(a.world, 0.f, center, display.x, display.y, &p)) continue;
        char label[kNameLen + 16];
        snprintf(label, sizeof(label), "%s%s", (a.itemFlags & kItemFlagPrimal)    ? "Primal "
                                               : (a.itemFlags & kItemFlagAncient) ? "Ancient "
                                                                                  : "",
                 a.name);
        const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
        const ImVec2 tl(p.x - ts.x * 0.5f, p.y - ts.y - 6.0f);  // centred just above the item
        dl->AddRectFilled(ImVec2(tl.x - 4.0f, tl.y - 2.0f), ImVec2(tl.x + ts.x + 4.0f, tl.y + ts.y + 2.0f),
                          IM_COL32(0, 0, 0, static_cast<int>(L.bgAlpha * 255.f)), 3.0f);
        const uint32_t prgb = L.colPrimal;
        const ImU32 col = (a.itemFlags & kItemFlagPrimal)
                              ? IM_COL32((prgb >> 16) & 255, (prgb >> 8) & 255, prgb & 255, 255)
                              : DropColor(L, a.itemRarity, bonus);
        dl->AddText(font, fontSize, tl, col, label);
    }
}

// Record every legendary/set item the reader reports on the ground (once per item).
void HudRenderer::UpdateDrops(const WorldSnapshot& snap) {
    for (uint16_t i = 0; i < snap.itemCount; ++i) {
        const ItemSnapshot& it = snap.items[i];
        if (it.location != kItemLocationGround || dropIndex_.count(it.id)) continue;
        DropEntry e;
        e.item = it;
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        std::strftime(e.time, sizeof(e.time), "%H:%M:%S", &local);
        dropIndex_[it.id] = drops_.size();
        drops_.push_back(e);
        if (it.bonus) ++sessBonus_; else ++sessDrops_;
        sessAncient_ += it.ancient ? 1 : 0;
        sessPrimal_ += it.primal ? 1 : 0;
        if (haveRun_) {
            ++run_.drops;
            run_.ancient += it.ancient ? 1 : 0;
            run_.primal += it.primal ? 1 : 0;
        }
        const DropStyle& D = layout_.drops;
        const bool alert = (it.primal && D.alertPrimal) || (it.ancient && D.alertAncient);
        if (alert && ImGui::GetTime() > 3.0) {  // not for items already lying there at startup
            if (D.sound) MessageBeep(it.primal ? MB_ICONEXCLAMATION : MB_ICONASTERISK);
            if (D.flash) {
                flashUntil_ = ImGui::GetTime() + 1.2;
                flashRgb_ = it.primal ? layout_.labels.colPrimal : layout_.items.colAncient;
            }
        }
    }
    const size_t cap = static_cast<size_t>(std::max(0, layout_.drops.maxEntries));
    if (cap > 0 && drops_.size() > cap) {
        drops_.erase(drops_.begin(), drops_.begin() + static_cast<std::ptrdiff_t>(drops_.size() - cap));
        dropIndex_.clear();
        for (size_t k = 0; k < drops_.size(); ++k) dropIndex_[drops_[k].item.id] = k;
    }
}

static void FormatDuration(double seconds, char* out, size_t n) {
    const int s = static_cast<int>(std::max(0.0, seconds));
    if (s >= 3600) snprintf(out, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else snprintf(out, n, "%02d:%02d", s / 60, s % 60);
}

// A run is one visit to a world (rift level, bounty zone, town...): it ends when the
// world id changes. Visits shorter than minRunSeconds are not listed.
void HudRenderer::UpdateRuns(const WorldSnapshot& snap) {
    const double now = ImGui::GetTime();
    if (sessionStartT_ < 0.0) sessionStartT_ = now;
    if (snap.worldSnoId == 0) return;
    if (haveRun_ && snap.worldSnoId == run_.world) return;
    if (haveRun_) {
        run_.seconds = now - runStartT_;
        if (run_.seconds >= layout_.drops.minRunSeconds) {
            runs_.push_back(run_);
            if (runs_.size() > 100) runs_.erase(runs_.begin());
        }
    }
    run_ = RunEntry();
    run_.world = snap.worldSnoId;
    runStartT_ = now;
    haveRun_ = true;
    const std::time_t t = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &t);
    std::strftime(run_.start, sizeof(run_.start), "%H:%M:%S", &local);
}

void HudRenderer::ResetSession() {
    sessDrops_ = sessAncient_ = sessPrimal_ = sessBonus_ = 0;
    sessionStartT_ = ImGui::GetTime();
    runs_.clear();
    if (haveRun_) {
        run_.drops = run_.ancient = run_.primal = 0;
        runStartT_ = sessionStartT_;
    }
}

void HudRenderer::DrawRunTimer(const WorldSnapshot& snap) {
    const double now = ImGui::GetTime();
    // rift progress samples (about 1 per second, last 60 s) for the ETA
    if (snap.ui.riftOpen) {
        if (riftSamples_.empty() || now - riftSamples_.back().first >= 1.0) riftSamples_.push_back({now, snap.ui.riftProgress});
        while (riftSamples_.size() > 1 && now - riftSamples_.front().first > 60.0) riftSamples_.erase(riftSamples_.begin());
    } else {
        riftSamples_.clear();
    }
    // greater rift: the 15 minute limit, counted from when the bar first showed
    constexpr double kGreaterRiftSeconds = 900.0;
    if (snap.ui.riftOpen == 2) {
        if (grStartT_ < 0.0) grStartT_ = now;
    } else {
        grStartT_ = -1.0;
    }

    char lines[3][96];
    ImU32 cols[3];
    int n = 0;
    const ImU32 normal = IM_COL32(230, 230, 240, 255);
    if (layout_.drops.showRunTimer && haveRun_) {
        char t[24];
        FormatDuration(now - runStartT_, t, sizeof(t));
        snprintf(lines[n], sizeof(lines[0]), "Run %s", t);
        cols[n++] = normal;
    }
    double left = 0.0;
    const bool gr = layout_.drops.showRift && snap.ui.riftOpen == 2 && grStartT_ >= 0.0;
    if (gr) {
        left = kGreaterRiftSeconds - (now - grStartT_);
        char t[24];
        FormatDuration(std::max(0.0, left), t, sizeof(t));
        snprintf(lines[n], sizeof(lines[0]), "GR timer %s left (est.)", t);
        cols[n++] = left < 60.0 ? IM_COL32(255, 90, 90, 255) : left < 180.0 ? IM_COL32(255, 200, 70, 255) : normal;
    }
    if (layout_.drops.showRift && snap.ui.riftOpen) {
        char eta[24] = "--", spare[32] = "";
        double etaSec = -1.0;
        if (riftSamples_.size() >= 2) {
            const auto& a = riftSamples_.front();
            const auto& b = riftSamples_.back();
            const double dt = b.first - a.first, dp = b.second - a.second;
            if (dt >= 8.0 && dp > 0.002) {
                etaSec = (1.0 - b.second) / (dp / dt);
                FormatDuration(etaSec, eta, sizeof(eta));
            }
        }
        if (gr && etaSec >= 0.0) {  // at this pace: progress is full in `eta`, compared with the time left (boss fight not counted)
            char t[24];
            FormatDuration(std::fabs(left - etaSec), t, sizeof(t));
            snprintf(spare, sizeof(spare), "  %s%s", left >= etaSec ? "+" : "-", t);
        }
        snprintf(lines[n], sizeof(lines[0]), "Rift %.0f%%   ETA %s%s", snap.ui.riftProgress * 100.0f, eta, spare);
        cols[n++] = (gr && etaSec >= 0.0 && left < etaSec) ? IM_COL32(255, 120, 90, 255) : normal;
    }
    if (n == 0) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    float y = 6.0f;
    for (int i = 0; i < n; ++i) {
        const ImVec2 ts = ImGui::CalcTextSize(lines[i]);
        const ImVec2 at(ds.x * 0.5f - ts.x * 0.5f, y);
        dl->AddRectFilled(ImVec2(at.x - 6, at.y - 2), ImVec2(at.x + ts.x + 6, at.y + ts.y + 2), IM_COL32(0, 0, 0, 140), 4.f);
        dl->AddText(at, cols[i], lines[i]);
        y += ts.y + 6.0f;
    }
}

bool HudRenderer::ExportDropsCsv(const char* path) const {
    FILE* f = fopen(path, "w");
    if (f == nullptr) return false;
    fprintf(f, "time,name,type,rarity,ancient,primal,bonus%s", "\n");
    for (const DropEntry& d : drops_) {
        fprintf(f, "%s,\"%s\",%s,%s,%d,%d,%d%s", d.time, d.item.name, d.item.typeName, RarityName(d.item.rarity),
                d.item.ancient ? 1 : 0, d.item.primal ? 1 : 0, d.item.bonus ? 1 : 0, "\n");
    }
    fclose(f);
    return true;
}

// The drop log, newest first: still on the ground, picked up (now in the
// backpack/equipped) or gone (out of range, salvaged, despawned).
void HudRenderer::DrawDropsPanel(const WorldSnapshot& snap, const HudOptions& opt) {
    if (!PanelOpen(PanelId::Drops, opt, "Drops")) return;
    {
        const double now = ImGui::GetTime(), secs = now - sessionStartT_;
        char ses[24], run[24];
        FormatDuration(secs, ses, sizeof(ses));
        FormatDuration(haveRun_ ? now - runStartT_ : 0.0, run, sizeof(run));
        const double perHour = secs >= 60.0 ? sessDrops_ * 3600.0 / secs : 0.0;
        ImGui::Text("Session %s   current run %s", ses, run);
        ImGui::Text("%d legendary/set", sessDrops_);
        if (secs >= 60.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%.1f/h)", perHour);
        }
        ImGui::SameLine();
        ImGui::Text("| %d ancient", sessAncient_);
        if (sessDrops_ > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%.0f%%)", 100.0 * sessAncient_ / sessDrops_);
        }
        ImGui::SameLine();
        ImGui::Text("| %d primal | %d bonus", sessPrimal_, sessBonus_);
        if (ImGui::SmallButton("Clear log")) {
            drops_.clear();
            dropIndex_.clear();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset session stats")) ResetSession();
    }
    if (ImGui::BeginTable("drops", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable,
                          ImVec2(560.0f, 220.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Ancient", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableHeadersRow();
        for (size_t k = drops_.size(); k-- > 0;) {
            const ItemSnapshot& it = drops_[k].item;
            const char* status = "gone";
            for (uint16_t i = 0; i < snap.itemCount; ++i) {
                if (snap.items[i].id != it.id) continue;
                status = snap.items[i].location == kItemLocationGround ? "on ground" : "picked up";
                break;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(drops_[k].time);
            ImGui::TableNextColumn();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(DropColor(layout_.labels, it.rarity, it.bonus != 0)), "%s",
                               it.name);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", it.typeName);
            ImGui::TableNextColumn();
            if (it.primal)       ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "PRIMAL");
            else if (it.ancient) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Ancient");
            else                 ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (!strcmp(status, "on ground")) ImGui::TextUnformatted(status);
            else                              ImGui::TextDisabled("%s", status);
        }
        ImGui::EndTable();
    }
    if (ImGui::CollapsingHeader("Runs (world visits)")) {
        if (ImGui::BeginTable("runs", 6,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                              ImVec2(560.0f, 130.0f))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Start", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Drops", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn("Ancient", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Primal", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn("World", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            auto row = [&](const RunEntry& r, double seconds, bool live) {
                char t[24];
                FormatDuration(seconds, t, sizeof(t));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (live) ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "%s", r.start);
                else ImGui::TextUnformatted(r.start);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(t);
                ImGui::TableNextColumn();
                ImGui::Text("%d", r.drops);
                ImGui::TableNextColumn();
                ImGui::Text("%d", r.ancient);
                ImGui::TableNextColumn();
                ImGui::Text("%d", r.primal);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%08X%s", r.world, live ? "  (now)" : "");
            };
            if (haveRun_) row(run_, ImGui::GetTime() - runStartT_, true);
            for (size_t k = runs_.size(); k-- > 0;) row(runs_[k], runs_[k].seconds, false);
            ImGui::EndTable();
        }
    }
    PanelEnd(PanelId::Drops, true);
}

// "A" (ancient) / "P" (primal), with the augment rank in front ("125A"), in the
// top-left corner of every such item icon the
// game currently shows: backpack and equipment (inventory open) and the selected
// stash tab (stash open). Geometry comes from the game's own UI controls.
void HudRenderer::DrawItemBadges(const WorldSnapshot& snap) {
    const ItemStyle& S = layout_.items;
    if (!S.badges || (!snap.ui.inventoryOpen && !snap.ui.stashOpen)) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    for (uint16_t i = 0; i < snap.itemCount; ++i) {
        const ItemSnapshot& it = snap.items[i];
        if (!it.ancient && !it.primal) continue;
        UiRect ui;
        if (!ItemScreenRect(snap.ui, it.location, it.gridX, it.gridY, it.height, &ui)) continue;
        const UiRect r = UiToScreen(ui, display.x, display.y);
        const float cell = std::min(r.x1 - r.x0, r.y1 - r.y0);
        if (cell < 8.0f) continue;  // control not laid out (yet)
        const float size = std::clamp(cell * 0.32f, 12.0f, 28.0f) * S.badgeScale;
        // "A"/"P", prefixed by the Caldesann's augment gem rank if any ("125A")
        char label[8];
        if (it.augmentRank && S.badgeAugment) snprintf(label, sizeof(label), "%u%s", it.augmentRank, it.primal ? "P" : "A");
        else snprintf(label, sizeof(label), "%s", it.primal ? "P" : "A");
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
        const bool right = S.badgeCorner == 1 || S.badgeCorner == 3, bottom = S.badgeCorner >= 2;
        const ImVec2 p(right ? r.x1 - ts.x - 7.0f : r.x0 + 3.0f, bottom ? r.y1 - ts.y - 2.0f : r.y0 + 2.0f);
        const uint32_t rgb = it.primal ? S.colPrimal : S.colAncient;
        const ImU32 col = IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
        dl->AddRectFilled(p, ImVec2(p.x + ts.x + 4.0f, p.y + ts.y), IM_COL32(0, 0, 0, 170), 3.0f);
        dl->AddText(font, size, ImVec2(p.x + 2.0f, p.y), col, label);
    }
}

// Auto salvage: the safe area (green cells, never salvaged) whenever the
// inventory is open, the items that would be salvaged (red X) while the
// salvage tab is open, and a small control panel. Safe cells are toggled by
// clicking them in "edit" mode (F8: the overlay must take the clicks).
void HudRenderer::DrawAutoSalvage(const WorldSnapshot& snap, HudOptions& opt) {
    if (opt.salvage == nullptr || !snap.ui.inventoryOpen) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const UiRect grid = UiToScreen(snap.ui.backpack, display.x, display.y);
    const float cw = (grid.x1 - grid.x0) / kBackpackCols, ch = (grid.y1 - grid.y0) / kBackpackRows;
    if (cw < 4.0f || ch < 4.0f) return;  // not laid out
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    SafeArea& safe = layout_.safeArea;
    const bool showSafe = !layout_.safeOnlyAtAnvil || snap.ui.salvageOpen;  // see Settings > Auto salvage
    if (!showSafe) editSafeArea_ = false;

    // safe cells: edit by clicking (only while the overlay takes input)
    if (showSafe && editSafeArea_ && opt.mouseLive && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::GetIO().WantCaptureMouse) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const int cx = static_cast<int>((m.x - grid.x0) / cw), cy = static_cast<int>((m.y - grid.y0) / ch);
        if (m.x >= grid.x0 && m.y >= grid.y0 && cx < kBackpackCols && cy < kBackpackRows) {
            safe.Toggle(cx, cy);
            layoutStatus_ = layout_.Save(layoutPath_) ? "saved" : "save failed";
        }
    }
    if (showSafe) {
        for (int y = 0; y < kBackpackRows; ++y)
            for (int x = 0; x < kBackpackCols; ++x) {
                const ImVec2 a(grid.x0 + x * cw + 1, grid.y0 + y * ch + 1), b(grid.x0 + (x + 1) * cw - 1, grid.y0 + (y + 1) * ch - 1);
                if (safe.Has(x, y)) {
                    dl->AddRectFilled(a, b, IM_COL32(60, 220, 90, editSafeArea_ ? 90 : 45));
                    dl->AddRect(a, b, IM_COL32(60, 220, 90, 200), 0.f, 0, 1.5f);
                } else if (editSafeArea_) {
                    dl->AddRect(a, b, IM_COL32(255, 255, 255, 60));
                }
            }

        // toolbar under the backpack: the edit toggle works without F8 (hot rect, see main.cpp)
        ImGui::SetNextWindowPos(ImVec2(grid.x0, grid.y1 + 6.0f), ImGuiCond_Always);
        if (ImGui::Begin("##safe_toolbar", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav)) {
            if (ImGui::Button(editSafeArea_ ? "Done editing safe area" : "Edit safe area")) editSafeArea_ = !editSafeArea_;
            ImGui::SameLine();
            ImGui::TextDisabled(editSafeArea_ ? "click cells to toggle" : "%d safe cells", safe.Count());
            const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
            opt.AddHot(editSafeArea_ ? std::min(p.x, grid.x0) : p.x, editSafeArea_ ? std::min(p.y, grid.y0) : p.y,
                       editSafeArea_ ? std::max(p.x + s.x, grid.x1) : p.x + s.x, p.y + s.y);
        }
        ImGui::End();
    }

    // preview: what a salvage run would destroy
    int candidates = 0;
    for (uint16_t i = 0; i < snap.itemCount; ++i) {
        const ItemSnapshot& it = snap.items[i];
        if (!IsSalvageCandidate(it, safe, layout_.salvageRules, snap.items, snap.itemCount)) continue;
        ++candidates;
        if (!snap.ui.salvageOpen || !layout_.salvagePreview) continue;
        UiRect ui;
        if (!ItemScreenRect(snap.ui, it.location, it.gridX, it.gridY, it.height, &ui)) continue;
        const UiRect r = UiToScreen(ui, display.x, display.y);
        const ImU32 red = Rgba(layout_.salvagePreviewRgb, 210);
        dl->AddRect(ImVec2(r.x0 + 2, r.y0 + 2), ImVec2(r.x1 - 2, r.y1 - 2), red, 0.f, 0, 2.0f);
        const float m = std::min(r.x1 - r.x0, r.y1 - r.y0) * 0.25f;
        const ImVec2 c((r.x0 + r.x1) * 0.5f, (r.y0 + r.y1) * 0.5f);
        dl->AddLine(ImVec2(c.x - m, c.y - m), ImVec2(c.x + m, c.y + m), red, 2.0f);
        dl->AddLine(ImVec2(c.x - m, c.y + m), ImVec2(c.x + m, c.y - m), red, 2.0f);
    }

    // The salvage button sits under the game's own anvil button and only exists
    // while the blacksmith's salvage tab is open. The owner turns the overlay
    // interactive while the cursor is over it (salvageButton*), so no F8 needed.
    if (!snap.ui.salvageOpen || snap.ui.salvageButton.x1 <= snap.ui.salvageButton.x0) return;
    SalvageStatus& st = *opt.salvage;
    const UiRect anvil = UiToScreen(snap.ui.salvageButton, display.x, display.y);
    ImGui::SetNextWindowPos(ImVec2(anvil.x0, anvil.y1 + 6.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(anvil.x1 - anvil.x0, 160.0f), 0.0f),
                                        ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin("##salvage_button", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav)) {
        if (st.running) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "SALVAGING - Esc or move mouse to stop");
        } else if (candidates > 0) {
            char label[48];
            snprintf(label, sizeof(label), "Salvage %d item(s)", candidates);
            const bool armed = layout_.salvageConfirm && ImGui::GetTime() < salvageArmedUntil_;
            if (armed) snprintf(label, sizeof(label), "Click again: salvage %d!", candidates);
            if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f))) {
                if (!layout_.salvageConfirm || armed) {
                    st.startClicked = true;
                    salvageArmedUntil_ = 0.0;
                } else {
                    salvageArmedUntil_ = ImGui::GetTime() + 4.0;  // confirm within 4 s
                }
            }
        } else {
            ImGui::TextDisabled("nothing to salvage");
        }
        if (st.status[0] != '\0') ImGui::TextDisabled("%s", st.status);
        const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
        opt.AddHot(p.x, p.y, p.x + s.x, p.y + s.y);
    }
    ImGui::End();
}

// Settings > Auto salvage: what is protected and where (the button itself lives
// at the blacksmith).
void HudRenderer::DrawSalvageSettings() {
    SalvageRules& R = layout_.salvageRules;
    bool ch = false;
    ImGui::Checkbox("edit safe area", &editSafeArea_);
    ImGui::SameLine();
    ImGui::TextDisabled("(open the inventory, click backpack cells; %d safe cells)", layout_.safeArea.Count());
    ch |= ImGui::Checkbox("show the safe area and its editor only at the blacksmith's salvage tab", &layout_.safeOnlyAtAnvil);
    ch |= ImGui::Checkbox("keep ancient/primal of equipped", &R.protectEquippedAncients);
    ch |= ImGui::Checkbox("keep every ancient", &R.keepAncient);
    ImGui::SameLine();
    ch |= ImGui::Checkbox("keep every primal", &R.keepPrimal);

    ImGui::SeparatorText("Rarities that may be salvaged");
    static const char* kRarities[5] = {"Common", "Magic", "Rare", "Legendary", "Set"};
    for (int i = 0; i < 5; ++i) {
        bool on = ((R.rarityMask >> i) & 1) != 0;
        if (ImGui::Checkbox(kRarities[i], &on)) {
            R.rarityMask = on ? (R.rarityMask | (1u << i)) : (R.rarityMask & ~(1u << i));
            ch = true;
        }
        if (i < 4) ImGui::SameLine();
    }

    ImGui::SeparatorText("Never salvage these item types");
    for (int f = 0; f < kGearFamilyCount; ++f) {
        bool never = ((R.neverFamilies >> f) & 1) != 0;
        if (ImGui::Checkbox(GearFamilyName(f), &never)) {
            R.neverFamilies = never ? (R.neverFamilies | (1u << f)) : (R.neverFamilies & ~(1u << f));
            ch = true;
        }
        if (f % 5 != 4 && f + 1 < kGearFamilyCount) ImGui::SameLine();
    }

    ImGui::SeparatorText("Speed");
    ch |= ImGui::Checkbox("use the game's \"salvage all blue / yellow\" buttons when safe", &R.useGameSalvageAll);
    ImGui::TextDisabled("Pressed only if every blue/yellow item in the backpack may be salvaged\n(none in the safe area, no protected type); otherwise they are clicked one by one.");
    ch |= ImGui::Checkbox("fast mode: click ordinary items without waiting for each to vanish", &R.burst);
    ImGui::TextDisabled("Legendary and set items are always done one at a time (they ask for confirmation).");
    ImGui::SeparatorText("Run safety");
    ch |= ImGui::Checkbox("confirm before starting (click the anvil button twice)", &layout_.salvageConfirm);
    ImGui::SliderInt("max items per run (0 = no limit)", &R.maxPerRun, 0, 100);
    ch |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderInt("extra delay between clicks (ms)", &R.extraDelayMs, 0, 500);
    ch |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SeparatorText("Preview");
    ch |= ImGui::Checkbox("mark items a run would salvage", &layout_.salvagePreview);
    {
        float c[3] = {((layout_.salvagePreviewRgb >> 16) & 255) / 255.f, ((layout_.salvagePreviewRgb >> 8) & 255) / 255.f,
                      (layout_.salvagePreviewRgb & 255) / 255.f};
        ImGui::SameLine();
        if (ImGui::ColorEdit3("mark colour", c, ImGuiColorEditFlags_NoInputs)) {
            layout_.salvagePreviewRgb = (static_cast<uint32_t>(c[0] * 255.f + 0.5f) << 16) |
                                        (static_cast<uint32_t>(c[1] * 255.f + 0.5f) << 8) |
                                        static_cast<uint32_t>(c[2] * 255.f + 0.5f);
            ch = true;
        }
    }
    ImGui::TextDisabled("The Salvage button appears under the anvil when the blacksmith's salvage tab is open.");
    if (ch) layoutStatus_ = layout_.Save(layoutPath_) ? "saved" : "save failed";
}

// Two columns: the panels on the left, the selected one's settings on the right.
// Settings > Hotkeys: click an action, then press the new key. Polled with
// GetAsyncKeyState because a registered hotkey never reaches the window as a key event.
void HudRenderer::DrawHotkeySettings() {
    ImGui::TextDisabled("Click a key, then press the new one. Esc cancels; Backspace unbinds a panel toggle.");
    if (ImGui::BeginTable("hotkeys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        for (int h = 0; h < MinimapLayout::kHotkeyCount; ++h) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kHotkeyLabels[h]);
            ImGui::TableNextColumn();
            ImGui::PushID(h);
            char name[48];
            if (captureHotkey_ == h) snprintf(name, sizeof(name), "press a key...");
            else KeyName(layout_.hotkeys[h], name, sizeof(name));
            if (ImGui::Button(name, ImVec2(170.0f * layout_.uiScale, 0.0f))) {
                captureHotkey_ = captureHotkey_ == h ? -1 : h;
                captureReady_ = false;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (captureHotkey_ < 0) return;
    int pressed = 0;
    for (int vk = 8; vk < 255 && pressed == 0; ++vk) {
        const bool mouse = vk <= VK_XBUTTON2, mod = (vk >= VK_SHIFT && vk <= VK_MENU) || (vk >= VK_LSHIFT && vk <= VK_RMENU) ||
                           vk == VK_LWIN || vk == VK_RWIN;
        if (!mouse && !mod && (GetAsyncKeyState(vk) & 0x8000)) pressed = vk;
    }
    if (pressed == 0) {
        captureReady_ = true;  // wait until the key that started the capture is released
    } else if (captureReady_) {
        const int h = captureHotkey_;
        if (pressed == VK_BACK && h >= MinimapLayout::kHkPanelFirst) {
            layout_.hotkeys[h] = 0;
        } else if (pressed != VK_ESCAPE) {
            for (int o = 0; o < MinimapLayout::kHotkeyCount; ++o)  // one action per key
                if (o != h && layout_.hotkeys[o] == pressed) layout_.hotkeys[o] = 0;
            layout_.hotkeys[h] = static_cast<uint8_t>(pressed);
        }
        captureHotkey_ = -1;
        captureReady_ = false;
        hotkeysDirty_ = true;
        layoutStatus_ = layout_.Save(layoutPath_) ? "saved" : "save failed";
    }
}

// Settings > Profiles: whole setups (layout, markers, labels, hotkeys, safe area) as
// d3hud.<name>.ini files next to d3hud.ini.
void HudRenderer::DrawProfileSettings() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(layoutPath_).parent_path();
    auto pathOf = [&](const std::string& name) { return (dir / ("d3hud." + name + ".ini")).string(); };
    if (profilesDirty_) {
        profilesDirty_ = false;
        profiles_.clear();
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            const std::string fn = e.path().filename().string();  // d3hud.<name>.ini
            if (fn.size() > 10 && fn.rfind("d3hud.", 0) == 0 && fn.compare(fn.size() - 4, 4, ".ini") == 0)
                profiles_.push_back(fn.substr(6, fn.size() - 10));
        }
        std::sort(profiles_.begin(), profiles_.end());
    }
    ImGui::TextDisabled("A profile stores everything in d3hud.ini: panel layout, markers, labels, hotkeys, safe area.");
    ImGui::SetNextItemWidth(200.0f * layout_.uiScale);
    ImGui::InputText("##profile_name", profileName_, sizeof(profileName_));
    ImGui::SameLine();
    if (ImGui::Button("Save current as profile")) {
        std::string name;
        for (const char* c = profileName_; *c != '\0'; ++c)
            if (isalnum(static_cast<unsigned char>(*c)) || *c == '-' || *c == '_') name += *c;
        if (!name.empty()) {
            layoutStatus_ = layout_.Save(pathOf(name).c_str()) ? "profile saved" : "profile save failed";
            profilesDirty_ = true;
        }
    }
    ImGui::Separator();
    if (profiles_.empty()) ImGui::TextDisabled("no profiles yet");
    std::string toDelete;
    for (const std::string& name : profiles_) {
        ImGui::PushID(name.c_str());
        ImGui::TextUnformatted(name.c_str());
        ImGui::SameLine(220.0f * layout_.uiScale);
        if (ImGui::SmallButton("Load")) {
            MinimapLayout fresh;
            if (fresh.Load(pathOf(name).c_str())) {
                layout_ = fresh;
                hotkeysDirty_ = true;
                layoutStatus_ = layout_.Save(layoutPath_) ? "profile loaded" : "profile loaded (save failed)";
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) toDelete = name;
        ImGui::PopID();
    }
    if (!toDelete.empty()) {
        std::error_code ec;
        fs::remove(pathOf(toDelete), ec);
        profilesDirty_ = true;
    }
    ImGui::TextDisabled("%s", layoutStatus_);
}

// Settings > Reader / diagnostics, plus the housekeeping buttons.
void HudRenderer::DrawReaderSettings() {
    bool ch = false;
    ch |= ImGui::Checkbox("start the reader together with the overlay (live mode)", &layout_.readerAutostart);
    ImGui::SliderInt("reader publish rate (Hz)", &layout_.readerHz, 15, 120);
    ch |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::TextDisabled("The rate applies the next time the reader starts (Info panel: Stop, then Start reader).");
    ImGui::SeparatorText("On-screen diagnostics (bottom-left)");
    ch |= ImGui::Checkbox("fps and actor/item counts", &layout_.diagFps);
    ch |= ImGui::Checkbox("reader state, last log line and errors", &layout_.diagReader);
    ImGui::SeparatorText("Files");
    ImGui::TextDisabled("%s", layoutPath_);
    if (ImGui::Button("Open settings folder"))
        ShellExecuteA(nullptr, "open", std::filesystem::path(layoutPath_).parent_path().string().c_str(), nullptr,
                      nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    const bool armed = ImGui::GetTime() < resetArmedUntil_;
    ImGui::PushStyleColor(ImGuiCol_Button, armed ? ImVec4(0.75f, 0.2f, 0.2f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
    if (ImGui::Button(armed ? "Click again to reset EVERYTHING" : "Reset everything...")) {
        if (armed) {
            layout_ = MinimapLayout();
            hotkeysDirty_ = true;
            resetArmedUntil_ = 0.0;
            layoutStatus_ = layout_.Save(layoutPath_) ? "everything reset" : "reset (save failed)";
        } else {
            resetArmedUntil_ = ImGui::GetTime() + 4.0;
        }
    }
    ImGui::PopStyleColor();
    ImGui::TextDisabled("Reset also clears the safe area, panel layout and hotkeys (profiles are kept).");
    if (ch) layoutStatus_ = layout_.Save(layoutPath_) ? "saved" : "save failed";
}

// On-screen diagnostics, bottom-left: fps/counts and the reader's state.
void HudRenderer::DrawDiagnostics(const WorldSnapshot& snap, const HudOptions& opt, double fps) {
    if (!layout_.diagFps && !(layout_.diagReader && opt.reader != nullptr)) return;
    char lines[3][160];
    ImU32 cols[3];
    int n = 0;
    if (layout_.diagFps) {
        snprintf(lines[n], sizeof(lines[n]), "%.0f fps | actors %u | items %u | frame %llu", fps, snap.actorCount,
                 snap.itemCount, static_cast<unsigned long long>(snap.frameIndex));
        cols[n++] = IM_COL32(220, 220, 230, 255);
    }
    if (layout_.diagReader && opt.reader != nullptr) {
        const ReaderStatus& rs = *opt.reader;
        snprintf(lines[n], sizeof(lines[n]), "reader: %s", rs.running ? (rs.connected ? "running, connected" : "running, connecting...") : "stopped");
        cols[n++] = rs.running && rs.connected ? IM_COL32(110, 255, 130, 255) : IM_COL32(255, 170, 50, 255);
        if (rs.error[0] != '\0') {
            snprintf(lines[n], sizeof(lines[n]), "%.150s", rs.error);
            cols[n++] = IM_COL32(255, 100, 100, 255);
        } else if (rs.line[0] != '\0' && n < 3) {
            snprintf(lines[n], sizeof(lines[n]), "%.150s", rs.line);
            cols[n++] = IM_COL32(170, 170, 180, 255);
        }
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float lh = ImGui::GetTextLineHeightWithSpacing();
    float y = ds.y - 12.0f - lh * n;
    for (int i = 0; i < n; ++i, y += lh) {
        const ImVec2 ts = ImGui::CalcTextSize(lines[i]);
        dl->AddRectFilled(ImVec2(10.f, y - 1.f), ImVec2(14.f + ts.x, y + ts.y + 1.f), IM_COL32(0, 0, 0, 150), 3.f);
        dl->AddText(ImVec2(12.f, y), cols[i], lines[i]);
    }
}

// Stash search: a box above the stash grid. Words are matched against name, type,
// rarity and flags (see ItemSearch.h); matches get an outline, everything else is
// dimmed, in the stash tab and backpack that are on screen. Other tabs with matches
// are listed. Typing works through the overlay's keyboard hand-off (main.cpp).
void HudRenderer::UpdateStashCache(const WorldSnapshot& snap) {
    if (!snap.ui.stashOpen) {
        lastStashTab_ = -1;
        return;
    }
    if (snap.ui.stashPage >= 0 && snap.ui.stashPage < 4 && snap.ui.stashTabMask != 0)
        pageTabCount_[snap.ui.stashPage] = std::popcount(static_cast<unsigned>(snap.ui.stashTabMask));
    if (snap.ui.stashTab < 0) return;  // no tab selected for a moment (button animation): keep the state
    const double now = ImGui::GetTime();
    const int tab = std::min(63, static_cast<int>(snap.ui.stashTab));
    if (tab != lastStashTab_) {
        lastStashTab_ = tab;
        stashTabSince_ = now;
    }
    std::vector<ItemSnapshot> byTab[64];
    for (uint16_t i = 0; i < snap.itemCount; ++i)
        if (snap.items[i].location == kItemLocationStash)
            byTab[std::min(63, snap.items[i].gridY / kStashRowsPerTab)].push_back(snap.items[i]);
    for (int t = 0; t < 64; ++t)
        if (!byTab[t].empty()) {  // the reader reports this tab: it is complete
            stashCache_[t] = std::move(byTab[t]);
            stashSeen_[t] = true;
        }
    // an empty open tab is empty only once it has been shown for a moment (the reader may still be loading)
    constexpr double kDwell = 0.6;  // shown this long with no items reported: the tab is empty
    if (stashCache_[tab].empty() ? !stashSeen_[tab] && now - stashTabSince_ > kDwell
                                 : now - stashTabSince_ > kDwell && snap.itemCount > 0) {
        bool live = false;
        for (uint16_t i = 0; i < snap.itemCount && !live; ++i)
            live = snap.items[i].location == kItemLocationStash && snap.items[i].gridY / kStashRowsPerTab == tab;
        if (!live) {  // shown for a while and the reader reports nothing there: the tab is empty
            stashCache_[tab].clear();
            stashSeen_[tab] = true;
        }
    }
}

void HudRenderer::DrawItemSearch(const WorldSnapshot& snap, HudOptions& opt) {
    const ItemStyle& S = layout_.items;
    if (!S.search || (!snap.ui.stashOpen && !snap.ui.inventoryOpen)) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const bool active = searchQuery_[0] != '\0';
    const ImU32 outline = Rgba(S.searchRgb, 255), dim = IM_COL32(0, 0, 0, static_cast<int>(S.searchDim * 255.f));

    UpdateStashCache(snap);
    // matches per global stash tab (page * 5 + tab; the item's gridY / 10) from the cache, and
    // in the bags / on the body from the live snapshot
    int perTab[64] = {};
    int inBags = 0;
    if (active) {
        for (int t = 0; t < 64; ++t)
            for (const ItemSnapshot& it : stashCache_[t])
                if (ItemMatchesQuery(it, searchQuery_)) ++perTab[t];
        for (uint16_t i = 0; i < snap.itemCount; ++i)
            if (snap.items[i].location != kItemLocationStash && ItemMatchesQuery(snap.items[i], searchQuery_)) ++inBags;
    }

    if (snap.ui.stashOpen) {
        // the box sits BELOW the stash grid: the tab and page buttons are above it
        const UiRect grid = UiToScreen(snap.ui.stash, display.x, display.y);
        if (grid.x1 - grid.x0 > 8.0f) {
            ImGui::SetNextWindowPos(ImVec2(grid.x0, grid.y1 + 6.0f), ImGuiCond_Always, ImVec2(0.0f, 0.0f));
            if (ImGui::Begin("##stash_search", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
                ImGui::SetNextItemWidth(std::max(220.0f, grid.x1 - grid.x0 - 60.0f * layout_.uiScale));
                ImGui::InputTextWithHint("##q", "find: name, type, ancient, primal, set... (all words)", searchQuery_,
                                         sizeof(searchQuery_));
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) searchQuery_[0] = '\0';
                if (active) {
                    int total = inBags;
                    for (int c : perTab) total += c;
                    ImGui::TextDisabled("%d match(es) in the stash%s", total - inBags, inBags ? ", + some in the bags" : "");
                }
                const ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
                opt.AddHot(p.x, p.y, p.x + sz.x, p.y + sz.y);
            }
            ImGui::End();
        }
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    // Stash buttons: red fill = not indexed yet (a page: none of its five tabs seen; fainter when only
    // some were), yellow + count = matches. The game's button rects overlap their neighbours, so each
    // box is limited to the pitch between buttons.
    if (snap.ui.stashOpen) {
        auto screen = [&](const UiRect& ui) { return UiToScreen(ui, display.x, display.y); };
        auto pitch = [&](const UiRect* rects, int n, unsigned mask, bool horizontal) {
            float c[8], best = 0.0f;
            int m = 0;
            for (int i = 0; i < n; ++i) {
                if (((mask >> i) & 1) == 0) continue;  // button does not exist
                const UiRect r = screen(rects[i]);
                if (r.x1 - r.x0 >= 4.0f && r.y1 - r.y0 >= 4.0f) c[m++] = horizontal ? (r.x0 + r.x1) * 0.5f : (r.y0 + r.y1) * 0.5f;
            }
            std::sort(c, c + m);
            for (int i = 1; i < m; ++i) best = (best == 0.0f) ? c[i] - c[i - 1] : std::min(best, c[i] - c[i - 1]);
            return best;
        };
        const float pitchX = pitch(snap.ui.stashPageRect, 4, snap.ui.stashPageMask, true);
        const float pitchY = pitch(snap.ui.stashTabRect, 5, snap.ui.stashTabMask, false);
        auto box = [&](const UiRect& ui, float px, float py, float inset, ImVec2* a, ImVec2* b) {
            const UiRect r = screen(ui);
            if (r.x1 - r.x0 < 4.0f || r.y1 - r.y0 < 4.0f) return false;  // control not laid out
            const float cx = (r.x0 + r.x1) * 0.5f, cy = (r.y0 + r.y1) * 0.5f;
            const float w = (px > 0.0f ? std::min(r.x1 - r.x0, px - 4.0f) : r.x1 - r.x0) * 0.5f - inset;
            const float h = (py > 0.0f ? std::min(r.y1 - r.y0, py - 4.0f) : r.y1 - r.y0) * 0.5f - inset;
            *a = ImVec2(cx - w, cy - h);
            *b = ImVec2(cx + w, cy + h);
            return w > 1.0f && h > 1.0f;
        };
        auto mark = [&](const UiRect& ui, float px, float py, float unindexed, int count) {
            ImVec2 a, b;
            if (unindexed > 0.0f && S.markIndexed && box(ui, px, py, 2.0f, &a, &b))
                dl->AddRectFilled(a, b, IM_COL32(230, 50, 50, static_cast<int>(unindexed * 255.f)));
            if (count > 0 && box(ui, px, py, 1.0f, &a, &b)) {
                dl->AddRect(a, b, outline, 0.f, 0, 3.0f);
                char n[16];
                snprintf(n, sizeof(n), "%d", count);
                const ImVec2 ts = ImGui::CalcTextSize(n);
                const ImVec2 at(b.x - ts.x - 3.0f, a.y + 1.0f);
                dl->AddRectFilled(ImVec2(at.x - 2, at.y), ImVec2(at.x + ts.x + 2, at.y + ts.y), IM_COL32(0, 0, 0, 200), 3.f);
                dl->AddText(at, outline, n);
            }
        };
        const int shownPage = snap.ui.stashPage >= 0 ? snap.ui.stashPage : (snap.ui.stashTab >= 0 ? snap.ui.stashTab / 5 : 0);
        for (int pg = 0; pg < 4; ++pg) {
            if (((snap.ui.stashPageMask >> pg) & 1) == 0) continue;  // page not owned
            const int tabs = pageTabCount_[pg] > 0 ? pageTabCount_[pg] : 5;  // unknown until the page was shown
            int seen = 0, matches = 0;
            for (int t = 0; t < 5; ++t) {
                seen += stashSeen_[pg * 5 + t] ? 1 : 0;
                matches += perTab[pg * 5 + t];
            }
            mark(snap.ui.stashPageRect[pg], pitchX, 0.0f, seen == 0 ? 0.35f : seen < tabs ? 0.15f : 0.0f, matches);
        }
        for (int t = 0; t < 5; ++t) {
            if (((snap.ui.stashTabMask >> t) & 1) == 0) continue;  // this page has fewer tabs
            const int g = shownPage * 5 + t;
            mark(snap.ui.stashTabRect[t], 0.0f, pitchY, g < 64 && !stashSeen_[g] ? 0.35f : 0.0f, g < 64 ? perTab[g] : 0);
        }
    }
    if (!active) return;

    // items on screen: matches outlined, the rest dimmed
    for (uint16_t i = 0; i < snap.itemCount; ++i) {
        const ItemSnapshot& it = snap.items[i];
        UiRect ui;
        if (!ItemScreenRect(snap.ui, it.location, it.gridX, it.gridY, it.height, &ui)) continue;
        const UiRect r = UiToScreen(ui, display.x, display.y);
        if (ItemMatchesQuery(it, searchQuery_))
            dl->AddRect(ImVec2(r.x0 + 1, r.y0 + 1), ImVec2(r.x1 - 1, r.y1 - 1), outline, 0.f, 0, 3.0f);
        else
            dl->AddRectFilled(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), dim);
    }
}

// Settings > Drops: alerts, log size, CSV export.
void HudRenderer::DrawDropSettings() {
    DropStyle& D = layout_.drops;
    bool ch = false;
    ch |= ImGui::Checkbox("alert on a primal drop", &D.alertPrimal);
    ch |= ImGui::Checkbox("alert on an ancient drop", &D.alertAncient);
    ch |= ImGui::Checkbox("  play a sound", &D.sound);
    ch |= ImGui::Checkbox("  flash the screen border", &D.flash);
    ImGui::SliderInt("keep at most (0 = all)", &D.maxEntries, 0, 2000);
    ch |= ImGui::IsItemDeactivatedAfterEdit();
    ch |= ImGui::Checkbox("run timer at the top of the screen", &D.showRunTimer);
    ch |= ImGui::Checkbox("rift progress and ETA at the top of the screen", &D.showRift);
    ImGui::SliderInt("list runs longer than (seconds)", &D.minRunSeconds, 0, 300);
    ch |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::Button("Reset session stats")) ResetSession();
    ImGui::SameLine();
    if (ImGui::Button("Export session to CSV")) {
        const std::string path = (std::filesystem::path(layoutPath_).parent_path() / "d3hud_drops.csv").string();
        layoutStatus_ = ExportDropsCsv(path.c_str()) ? "exported d3hud_drops.csv next to d3hud.ini" : "export failed";
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear log")) {
        drops_.clear();
        dropIndex_.clear();
    }
    ImGui::TextDisabled("%zu entries   %s", drops_.size(), layoutStatus_);
    if (ch) layoutStatus_ = layout_.Save(layoutPath_) ? "saved" : "save failed";
}

// Settings > Items: A/P badges on icons and the hover card.
void HudRenderer::DrawItemSettings() {
    ItemStyle& S = layout_.items;
    bool ch = false;
    ch |= ImGui::Checkbox("A/P badges on inventory and stash icons", &S.badges);
    ImGui::SliderFloat("badge size", &S.badgeScale, 0.5f, 2.5f, "x%.2f");
    ch |= ImGui::IsItemDeactivatedAfterEdit();
    ch |= ImGui::Combo("badge corner", &S.badgeCorner, "Top left\0Top right\0Bottom left\0Bottom right\0");
    ch |= ImGui::Checkbox("show augment rank in front (125A)", &S.badgeAugment);
    auto colour = [&](const char* name, uint32_t* rgb) {
        float c[3] = {((*rgb >> 16) & 255) / 255.f, ((*rgb >> 8) & 255) / 255.f, (*rgb & 255) / 255.f};
        if (ImGui::ColorEdit3(name, c, ImGuiColorEditFlags_NoInputs)) {
            *rgb = (static_cast<uint32_t>(c[0] * 255.f + 0.5f) << 16) | (static_cast<uint32_t>(c[1] * 255.f + 0.5f) << 8) |
                   static_cast<uint32_t>(c[2] * 255.f + 0.5f);
            ch = true;
        }
    };
    colour("ancient badge", &S.colAncient);
    ImGui::SameLine();
    colour("primal badge", &S.colPrimal);
    ch |= ImGui::Checkbox("stash search box (highlights matching items)", &S.search);
    {
        float c[3] = {((S.searchRgb >> 16) & 255) / 255.f, ((S.searchRgb >> 8) & 255) / 255.f, (S.searchRgb & 255) / 255.f};
        if (ImGui::ColorEdit3("highlight colour", c, ImGuiColorEditFlags_NoInputs)) {
            S.searchRgb = (static_cast<uint32_t>(c[0] * 255.f + 0.5f) << 16) | (static_cast<uint32_t>(c[1] * 255.f + 0.5f) << 8) |
                          static_cast<uint32_t>(c[2] * 255.f + 0.5f);
            ch = true;
        }
    }
    ch |= ImGui::Checkbox("tint stash tabs and pages the search has not seen yet in red", &S.markIndexed);
    ImGui::SliderFloat("dim items that do not match", &S.searchDim, 0.0f, 0.9f, "%.2f");
    ch |= ImGui::IsItemDeactivatedAfterEdit();
    {
        int tabs = 0, cached = 0;
        for (int t = 0; t < 64; ++t) {
            tabs += stashSeen_[t] ? 1 : 0;
            cached += static_cast<int>(stashCache_[t].size());
        }
        ImGui::TextDisabled("the search remembers %d item(s) from %d stash tab(s) opened this session", cached, tabs);
        ImGui::SameLine();
        if (ImGui::SmallButton("Forget")) {
            for (int t = 0; t < 64; ++t) {
                stashCache_[t].clear();
                stashSeen_[t] = false;
            }
        }
    }
    ImGui::SeparatorText("Hover card");
    ch |= ImGui::Combo("show the card for", &S.hoverMode, "Unidentified items only\0Every item\0Off\0");
    ImGui::SliderFloat("card opacity", &S.hoverAlpha, 0.2f, 1.0f, "%.2f");
    ch |= ImGui::IsItemDeactivatedAfterEdit();
    if (ch) layoutStatus_ = layout_.Save(layoutPath_) ? "saved" : "save failed";
}

// Settings > Ground labels.
void HudRenderer::DrawLabelSettings() {
    LabelStyle& L = layout_.labels;
    ImGui::Checkbox("label items on the ground", &L.enabled);
    ImGui::Combo("which items", &L.mode, "Legendary + set\0Ancient / primal only\0Sets only\0");
    ImGui::Checkbox("always label bonus drops", &L.showBonus);
    ImGui::SliderFloat("text size", &L.scale, 0.6f, 2.5f, "x%.2f");
    ImGui::SliderFloat("background darkness", &L.bgAlpha, 0.0f, 1.0f, "%.2f");
    auto colour = [](const char* name, uint32_t* rgb) {
        float c[3] = {((*rgb >> 16) & 255) / 255.f, ((*rgb >> 8) & 255) / 255.f, (*rgb & 255) / 255.f};
        if (ImGui::ColorEdit3(name, c, ImGuiColorEditFlags_NoInputs))
            *rgb = (static_cast<uint32_t>(c[0] * 255.f + 0.5f) << 16) | (static_cast<uint32_t>(c[1] * 255.f + 0.5f) << 8) |
                   static_cast<uint32_t>(c[2] * 255.f + 0.5f);
    };
    colour("legendary", &L.colLegendary);
    colour("set", &L.colSet);
    colour("primal", &L.colPrimal);
    colour("bonus drop", &L.colBonus);
    ImGui::TextDisabled("Colours also apply to the Drops panel.");
    if (ImGui::Button("Save")) layoutStatus_ = layout_.Save(layoutPath_) ? "saved" : "save FAILED";
    ImGui::SameLine();
    if (ImGui::Button("Reset labels")) {
        L = LabelStyle();
        layoutStatus_ = "labels reset (not saved)";
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", layoutStatus_);
}

void HudRenderer::DrawSettings(const WorldSnapshot&, const HudOptions& opt) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (!PanelOpen(PanelId::Settings, opt, "Settings",
                   ImVec2(std::max(780.0f, display.x * 0.42f), std::max(540.0f, display.y * 0.62f))))
        return;
    // entries: the four panel pages (PanelId order) sit together; the rest are feature pages
    enum Entry { kGeneral, kHotkeys, kProfiles, kInfo, kDrops, kItems, kActors, kMinimap, kLabels, kSalvage, kReader, kEntryCount };
    static const char* kEntries[kEntryCount] = {"General", "Hotkeys", "Profiles", "Info", "Drops", "Items", "Actors",
                                                "Minimap", "Ground labels", "Auto salvage", "Reader / diagnostics"};
    ImGui::BeginChild("##settings_list", ImVec2(150.0f * layout_.uiScale, 0.0f), true);
    for (int i = 0; i < kEntryCount; ++i)
        if (ImGui::Selectable(kEntries[i], settingsTab_ == i)) settingsTab_ = i;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##settings_detail", ImVec2(0.0f, 0.0f), true);  // fills the window
    if (settingsTab_ != kHotkeys) captureHotkey_ = -1;
    ImGui::TextUnformatted(kEntries[settingsTab_]);
    ImGui::Separator();
    if (settingsTab_ == kGeneral) {
        ImGui::SliderFloat("UI scale (text and widgets)", &layout_.uiScale, 0.75f, 2.5f, "x%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) layoutStatus_ = layout_.Save(layoutPath_) ? "saved" : "save failed";
        ImGui::TextDisabled("The Settings window can be resized by dragging its bottom-right corner (F8).");
        ImGui::SeparatorText("Dock");
        bool ch = ImGui::Combo("corner", &layout_.dockCorner, "Bottom right\0Bottom left\0Top right\0Top left\0");
        ch |= ImGui::Checkbox("auto-hide (show near the cursor or while a panel is open)", &layout_.dockAutoHide);
        ImGui::SliderFloat("dock opacity", &layout_.dockAlpha, 0.0f, 1.0f, "%.2f");
        ch |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SeparatorText("Panels");
        ImGui::SliderFloat("panel opacity", &layout_.panelAlpha, 0.2f, 1.0f, "%.2f");
        ch |= ImGui::IsItemDeactivatedAfterEdit();
        ch |= ImGui::Checkbox("compact (tighter padding)", &layout_.compact);
        if (ch) layoutStatus_ = layout_.Save(layoutPath_) ? "saved" : "save failed";
    } else if (settingsTab_ == kHotkeys) {
        DrawHotkeySettings();
    } else if (settingsTab_ == kProfiles) {
        DrawProfileSettings();
    } else if (settingsTab_ >= kInfo && settingsTab_ <= kActors) {
        const int i = settingsTab_ - kInfo;
        bool changed = false;
        bool shown = !layout_.panelHidden[i];
        changed |= ImGui::Checkbox("show in dock", &shown);
        layout_.panelHidden[i] = !shown;
        changed |= ImGui::Checkbox("keep open (pinned)", &layout_.panelPinned[i]);
        if (ImGui::Button("reset position")) {
            layout_.panelX[i] = layout_.panelY[i] = -1.f;
            changed = true;
        }
        ImGui::TextDisabled("Hover the dock tab to peek; F8 + click pins.%sDrag the title bar (F8) to move.", "\n");
        if (changed) layoutStatus_ = layout_.Save(layoutPath_) ? "saved" : "save failed";
        if (settingsTab_ == kDrops) {
            ImGui::SeparatorText("Drop log and alerts");
            DrawDropSettings();
        }
        if (settingsTab_ == kItems) {
            ImGui::SeparatorText("Inventory overlays");
            DrawItemSettings();
        }
    } else if (settingsTab_ == kMinimap) {
        DrawMinimapSettings();
    } else if (settingsTab_ == kReader) {
        DrawReaderSettings();
    } else if (settingsTab_ == kLabels) {
        DrawLabelSettings();
    } else {
        DrawSalvageSettings();
    }
    ImGui::EndChild();
    PanelEnd(PanelId::Settings, true);
}

// While the game shows the tooltip of an UNIDENTIFIED item: a compact card left
// of the cursor with what the item really is (name, set/legendary, ancient/primal).
// Identified items are skipped - the game's own tooltip already says it all.
void HudRenderer::DrawHoveredItem(const WorldSnapshot& snap, const HudOptions& opt) {
    if (snap.hoveredItemPlus1 == 0 || snap.hoveredItemPlus1 > snap.itemCount || opt.cursorX < 0.f) return;
    const ItemSnapshot& it = snap.items[snap.hoveredItemPlus1 - 1];
    if (layout_.items.hoverMode == 2 || (it.identified && layout_.items.hoverMode == 0)) return;
    // left of the cursor, vertically centred on it (pivot = right-middle edge),
    // flipped to the right only if it would leave the screen
    ImVec2 pos(opt.cursorX - 18.0f, opt.cursorY);
    ImVec2 pivot(1.0f, 0.5f);
    if (opt.cursorX < 280.0f) {
        pos.x = opt.cursorX + 24.0f;
        pivot.x = 0.0f;
    }
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(layout_.items.hoverAlpha);
    if (!ImGui::Begin("##hovered_item", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                          ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    ImVec4 col(0.85f, 0.85f, 0.85f, 1.0f);
    switch (it.rarity) {
        case ItemRarity::Legendary: col = ImVec4(1.0f, 0.55f, 0.1f, 1.0f); break;
        case ItemRarity::Set:       col = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); break;
        case ItemRarity::Rare:      col = ImVec4(1.0f, 1.0f, 0.3f, 1.0f); break;
        case ItemRarity::Magic:     col = ImVec4(0.45f, 0.55f, 1.0f, 1.0f); break;
        default: break;
    }
    ImGui::TextColored(col, "%s", it.name);
    ImGui::TextDisabled("%s %s%s", RarityName(it.rarity), it.typeName, it.identified ? "" : " - unidentified");
    if (it.primal)       ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "PRIMAL ANCIENT");
    else if (it.ancient) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Ancient");
    else if (it.rarity == ItemRarity::Legendary || it.rarity == ItemRarity::Set)
        ImGui::TextDisabled("not ancient");
    ImGui::End();
}

void HudRenderer::Draw(const WorldSnapshot& snap, HudOptions& opt, double fps, const char* sourceName) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontScaleMain = layout_.uiScale;
    style.WindowPadding = layout_.compact ? ImVec2(4.f, 3.f) : ImVec2(8.f, 8.f);
    style.FramePadding = layout_.compact ? ImVec2(3.f, 1.f) : ImVec2(4.f, 3.f);
    style.ItemSpacing = layout_.compact ? ImVec2(4.f, 2.f) : ImVec2(8.f, 4.f);
    for (int i = 0; i < kPanels; ++i)
        if (opt.panelToggleMask & (1u << i)) {
            layout_.panelPinned[i] = !layout_.panelPinned[i];
            layout_.Save(layoutPath_);
        }
    UpdateRuns(snap);
    UpdateDrops(snap);           // the log keeps recording while the panels are hidden
    DrawRunTimer(snap);
    if (!opt.showPanels)
        for (ImVec2& s : panelSize_) s = ImVec2(0.f, 0.f);  // F9 off: nothing to hover
    for (int i = 0; i < kPanels; ++i)
        if (layout_.panelHidden[i]) panelSize_[i] = ImVec2(0.f, 0.f);  // hidden in Settings
    if (opt.showPanels) {
        DrawDock(opt);  // first: its tabs set the hover state the panels read
        auto visible = [&](PanelId id) { return !layout_.panelHidden[static_cast<int>(id)]; };
        if (visible(PanelId::Info)) DrawInfo(snap, opt, fps, sourceName);
        if (visible(PanelId::Drops)) DrawDropsPanel(snap, opt);
        if (visible(PanelId::Items)) DrawSalvagePanel(snap, opt);
        if (visible(PanelId::Actors)) DrawActorPanel(snap, opt);
        DrawSettings(snap, opt);  // always reachable
    }
    if (opt.showMinimap) DrawMinimap(snap, opt);
    DrawGroundLabels(snap, opt); // independent of F9, like the game's own item labels
    DrawItemBadges(snap);        // independent of F9: only while the game shows inventory/stash
    DrawAutoSalvage(snap, opt);  // safe area + salvage preview while the inventory is open
    DrawItemSearch(snap, opt);   // stash search box + highlights
    DrawHoveredItem(snap, opt);  // independent of F9: it only appears while the game shows a tooltip
    DrawDiagnostics(snap, opt, fps);
    if (ImGui::GetTime() < flashUntil_) {  // drop alert: a fading border around the screen
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        const float k = static_cast<float>((flashUntil_ - ImGui::GetTime()) / 1.2);
        ImGui::GetForegroundDrawList()->AddRect(ImVec2(0, 0), ds, Rgba(flashRgb_, static_cast<int>(k * 220.f)), 0.f, 0, 14.0f);
    }
}

}  // namespace d3hud
