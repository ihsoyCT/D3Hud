// D3Hud.Overlay — transparent click-through DX11/ImGui overlay (M0).
// Sources: synthetic scene (default), replay file (--replay), recorder (--record).
// Modes: --dcomp (default, per-pixel alpha) or --colorkey (fallback).
#include "Win32.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"  // ClearActiveID
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#include "HudRenderer.h"
#include "PluginHost.h"
#include "ReplayIO.h"
#include "SceneGen.h"
#include "Paths.h"
#include "ReaderProcess.h"
#include "SalvageRunner.h"
#include "SharedRing.h"
#include "SnapshotInterp.h"

// Modern imgui backends guard this in #if 0 — forward-declare it per the docs.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

using namespace d3hud;

// ---------------------------------------------------------------------------
static const wchar_t* kClassName = L"D3HudOverlayClass";
static HWND g_hwnd = nullptr;
static HWND g_targetHwnd = nullptr;
static wchar_t g_targetSub[256] = {};      // title substring of the window to cover (empty = whole screen)
static bool g_waitingForTarget = false;    // that window is not there (yet): overlay hidden, searching
static bool g_running = true;
static bool g_clickThrough = true;
static bool g_textFocus = false;  // an ImGui text field is active: the overlay holds the keyboard
static ULONGLONG g_textFocusAt = 0;
static bool g_hotkeysSuspended = false;  // a key is being captured in Settings > Hotkeys
static unsigned g_panelToggleMask = 0;   // panel hotkeys pressed since the last frame
static bool g_hotButton = false;  // click-through lifted only while the cursor is over the salvage button
static bool g_showPanels = true;
static int g_mapOrientation = 0;  // see OrientForMap; F7 cycles
static bool g_useDComp = true;

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static IDXGISwapChain1* g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static IDCompositionDevice* g_dcompDevice = nullptr;
static IDCompositionTarget* g_dcompTarget = nullptr;
static IDCompositionVisual* g_dcompVisual = nullptr;

// selftest extras
static bool g_hitTest = false;     // --selftest-hit: verify click-through toggling
static bool g_visualMark = false;  // --selftest-visual: draw a big marker square
// ImGui's built-in font is a 13 px bitmap-style font: small, and blurry when plugins draw it larger. Use a system
// TrueType font instead (ImGui 1.92+ renders any size from it sharply); the built-in one stays as the fallback.
static void LoadUiFont() {
    wchar_t windir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windir, MAX_PATH) == 0) return;
    for (const wchar_t* name : {L"segoeui.ttf", L"tahoma.ttf", L"arial.ttf"}) {
        const std::filesystem::path font = std::filesystem::path(windir) / L"Fonts" / name;
        if (!std::filesystem::exists(font)) continue;
        if (ImGui::GetIO().Fonts->AddFontFromFileTTF(Utf8(font).c_str(), 16.0f) != nullptr) return;
    }
}

static bool g_matrix = false;      // --matrix: cycle exstyle combos for external probing
static bool g_baseNoRedir = true;  // matrix base window type
static int g_selftestResult = -1;

struct StylePhase {
    DWORD extra;         // ORed onto the base style
    const char* name;
};
static const StylePhase kPhases[] = {
    {WS_EX_TRANSPARENT, "transparent-only"},
    {WS_EX_LAYERED | WS_EX_TRANSPARENT, "layered+transparent"},
    {WS_EX_LAYERED | WS_EX_TRANSPARENT, "layered+transparent+slwa-alpha"},
};
static void ApplyMatrixPhase(int idx) {
    DWORD ex = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | kPhases[idx].extra;
    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
    if (strstr(kPhases[idx].name, "slwa-alpha"))
        SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
    std::print("PHASE {} {}\n", idx, kPhases[idx].name);
    fflush(stdout);
}

#define CHK(hr, msg)                                                                  \
    do {                                                                              \
        HRESULT _hr = (hr);                                                           \
        if (FAILED(_hr)) {                                                            \
            std::print(stderr, "FATAL: {} failed with 0x{:08X}\n", msg, (unsigned long)_hr); \
            return false;                                                             \
        }                                                                             \
    } while (0)

// ---------------------------------------------------------------------------
struct FindCtx {
    const wchar_t* substr;
    HWND best;
};
static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp) {
    FindCtx* ctx = reinterpret_cast<FindCtx*>(lp);
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t title[256];
    int n = GetWindowTextW(hwnd, title, 256);
    if (n <= 0) return TRUE;
    if (wcsstr(title, ctx->substr) != nullptr) {
        ctx->best = hwnd;
        return FALSE;
    }
    return TRUE;
}
static HWND FindTargetWindow(const wchar_t* substr) {
    FindCtx ctx{substr, nullptr};
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.best;
}

static void GetOverlayRect(RECT* r) {
    if (g_targetHwnd != nullptr && IsWindow(g_targetHwnd)) {
        GetClientRect(g_targetHwnd, r);
        POINT tl{0, 0};
        ClientToScreen(g_targetHwnd, &tl);
        OffsetRect(r, tl.x, tl.y);
    } else {
        r->left = 0;
        r->top = 0;
        r->right = GetSystemMetrics(SM_CXSCREEN);
        r->bottom = GetSystemMetrics(SM_CYSCREEN);
    }
}

static void SetClickThrough(HWND hwnd, bool on) {
    g_clickThrough = on;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (on) ex |= WS_EX_TRANSPARENT;
    else ex &= ~WS_EX_TRANSPARENT;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
}

// (Re)register the global hotkeys: id = 1 + action, vk 0 = unbound.
static void RegisterHotkeys(const uint8_t* vks) {
    for (int i = 0; i < MinimapLayout::kHotkeyCount; ++i) {
        UnregisterHotKey(g_hwnd, i + 1);
        if (vks[i] != 0 && !RegisterHotKey(g_hwnd, i + 1, 0, vks[i]))
            std::print(stderr, "warning: hotkey {} (vk {}) register failed\n", i, vks[i]);
    }
}

// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 0;
    switch (msg) {
        case WM_DESTROY:
            g_running = false;
            return 0;
        case WM_HOTKEY:
            if (g_hotkeysSuspended) return 0;
            // ids are 1 + MinimapLayout::HotkeyAction; the keys are user-configurable
            if (wp >= 5) g_panelToggleMask |= 1u << (wp - 5);
            switch (static_cast<int>(wp)) {
                case 1:  // click-through (default F8)
                    g_hotButton = false;
                    SetClickThrough(hwnd, !g_clickThrough);
                    break;
                case 2:  // panels (default F9)
                    g_showPanels = !g_showPanels;
                    break;
                case 3:  // quit (default END)
                    g_running = false;
                    break;
                case 4:  // cycle map orientation (default F7)
                    g_mapOrientation = (g_mapOrientation + 1) % kMapOrientations;
                    break;
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

static bool CreateGraphics(HWND hwnd, int w, int h) {
    IDXGIFactory1* factory1 = nullptr;
    CHK(CreateDXGIFactory1(IID_PPV_ARGS(&factory1)), "CreateDXGIFactory1");
    IDXGIFactory2* factory = nullptr;
    CHK(factory1->QueryInterface(IID_PPV_ARGS(&factory)), "QI IDXGIFactory2");

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                   D3D11_SDK_VERSION, &g_device, &fl, &g_ctx);
    if (FAILED(hr)) {  // fall back to WARP (software) — enough for ImGui
        CHK(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                              D3D11_SDK_VERSION, &g_device, &fl, &g_ctx),
            "D3D11CreateDevice(WARP)");
    }
    factory1->Release();

    DXGI_SWAP_CHAIN_DESC1 sc = {};
    sc.Width = static_cast<UINT>(w);
    sc.Height = static_cast<UINT>(h);
    sc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sc.SampleDesc = {1, 0};
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = 2;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sc.Scaling = DXGI_SCALING_STRETCH;

    if (g_useDComp) {
        sc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        CHK(factory->CreateSwapChainForComposition(g_device, &sc, nullptr, &g_swap), "CreateSwapChainForComposition");

        IDXGIDevice* dxgiDev = nullptr;
        CHK(g_device->QueryInterface(IID_PPV_ARGS(&dxgiDev)), "QI IDXGIDevice");
        CHK(DCompositionCreateDevice(dxgiDev, IID_PPV_ARGS(&g_dcompDevice)), "DCompositionCreateDevice");
        dxgiDev->Release();
        CHK(g_dcompDevice->CreateTargetForHwnd(hwnd, TRUE, &g_dcompTarget), "CreateTargetForHwnd");
        CHK(g_dcompDevice->CreateVisual(&g_dcompVisual), "CreateVisual");
        g_dcompVisual->SetContent(g_swap);
        g_dcompTarget->SetRoot(g_dcompVisual);
        CHK(g_dcompDevice->Commit(), "DComp Commit");
    } else {
        sc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        CHK(factory->CreateSwapChainForHwnd(g_device, hwnd, &sc, nullptr, nullptr, &g_swap), "CreateSwapChainForHwnd");
        SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_COLORKEY);
    }
    factory->Release();
    return true;
}

static bool MakeRtv() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    ID3D11Texture2D* back = nullptr;
    CHK(g_swap->GetBuffer(0, IID_PPV_ARGS(&back)), "GetBuffer");
    HRESULT hr = g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    CHK(hr, "CreateRenderTargetView");
    return true;
}

static void Resize(int w, int h) {
    if (g_swap == nullptr || w <= 0 || h <= 0) return;
    g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    g_swap->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, 0);
    MakeRtv();
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);  // live console output, also when redirected to a file
    const char* targetSubstr = nullptr;
    const char* recordPath = nullptr;
    const char* replayPath = nullptr;
    int selftestFrames = 0;
    const char* settingsPage = nullptr;  // --settings-page <id>: open the Settings window on that page
    double recordSeconds = 10.0;
    bool live = false;
    bool startReader = false;
    bool demo = false;       // synthetic scene instead of the live game
    bool noReader = false;   // do not manage the reader (run D3Hud.Reader by hand)

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--target") && i + 1 < argc) targetSubstr = argv[++i];
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) recordPath = argv[++i];
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc) replayPath = argv[++i];
        else if (!strcmp(argv[i], "--live")) live = true;
        else if (!strcmp(argv[i], "--demo")) demo = true;
        else if (!strcmp(argv[i], "--no-reader")) noReader = true;
        else if (!strcmp(argv[i], "--start-reader")) live = startReader = true;
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) recordSeconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--selftest") && i + 1 < argc) selftestFrames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--settings-page") && i + 1 < argc) settingsPage = argv[++i];
        else if (!strcmp(argv[i], "--selftest-hit")) { selftestFrames = 80; g_hitTest = true; }
        else if (!strcmp(argv[i], "--selftest-visual")) { g_visualMark = true; }
        else if (!strcmp(argv[i], "--matrix")) { g_matrix = true; g_visualMark = true; selftestFrames = 20000; }
        else if (!strcmp(argv[i], "--matrix-base") && i + 1 < argc) g_baseNoRedir = (strcmp(argv[++i], "noredir") == 0);
        else if (!strcmp(argv[i], "--colorkey")) g_useDComp = false;
        else if (!strcmp(argv[i], "--help")) {
            std::print("D3Hud.Overlay\n"
                   "  (no options)                cover Diablo III, start the reader, show live data\n"
                   "  --target <title substring>  cover that window instead of \"Diablo III\"\n"
                   "  --no-reader                 do not start/manage D3Hud.Reader (run it yourself)\n"
                   "  --demo                      synthetic demo scene instead of the game\n"
                   "  --replay <file>             play a recorded .bin\n"
                   "  --record <file> [--seconds N]  record synthetic scene (default 10s)\n"
                   "  --live / --start-reader     explicit live mode (default already)\n"
                   "  --colorkey                  color-key fallback instead of DirectComposition\n"
                   "  --selftest <frames>         run N frames and exit\n"
                   "Hotkeys (rebindable in Settings): F7 map orientation, F8 click-through, F9 panels, END quit\n");
            return 0;
        }
    }

    // Started without options (double-click): live data over Diablo III, reader managed by us.
    const bool otherSource = replayPath != nullptr || recordPath != nullptr || selftestFrames > 0 || g_matrix || demo;
    if (!otherSource && !live) {
        live = true;
        startReader = !noReader;
    }
    if (live && noReader) startReader = false;
    if (live && targetSubstr == nullptr) targetSubstr = "Diablo III";

    // --- window -------------------------------------------------------------
    ImGui_ImplWin32_EnableDpiAwareness();
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    // WS_EX_TRANSPARENT only yields click-through when combined with WS_EX_LAYERED
    // (plain TRANSPARENT affects same-thread windows only). DComp windows keep
    // WS_EX_NOREDIRECTIONBITMAP for per-pixel alpha; no SetLayeredWindowAttributes
    // call is made, so the layered style only enables the hit-test behavior.
    DWORD exStyle;
    if (g_matrix) {
        exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | kPhases[0].extra;
        if (g_baseNoRedir) exStyle |= WS_EX_NOREDIRECTIONBITMAP;
    } else {
        // NOTE: measured on Win11 26300 — LAYERED breaks DComp visuals on a
        // NOREDIRECTIONBITMAP window, and TRANSPARENT without LAYERED does not
        // pass clicks. The matrix run picks the working combo; default here is
        // provisional until then.
        exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                  WS_EX_TOOLWINDOW;
        if (g_useDComp) exStyle |= WS_EX_NOREDIRECTIONBITMAP;
    }

    if (targetSubstr) {
        MultiByteToWideChar(CP_UTF8, 0, targetSubstr, -1, g_targetSub, 256);
        g_targetHwnd = FindTargetWindow(g_targetSub);
        if (g_targetHwnd) {
            std::print("tracking window {}\n", static_cast<void*>(g_targetHwnd));
        } else if (live) {
            g_waitingForTarget = true;  // start the game whenever you like: we hide until it appears
            std::print("waiting for a window titled '{}' (start the game)...\n", targetSubstr);
        } else {
            std::print("no window matches '{}' - covering primary screen\n", targetSubstr);
        }
    }

    RECT r;
    GetOverlayRect(&r);
    const int w0 = static_cast<int>(r.right - r.left), h0 = static_cast<int>(r.bottom - r.top);
    g_hwnd = CreateWindowExW(exStyle, kClassName, L"D3Hud Overlay", WS_POPUP, r.left, r.top, w0, h0,
                             nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) { std::print(stderr, "FATAL: CreateWindowExW failed\n"); return 1; }
    if (!CreateGraphics(g_hwnd, w0, h0)) return 1;
    if (!MakeRtv()) return 1;
    ShowWindow(g_hwnd, g_waitingForTarget ? SW_HIDE : SW_SHOWNOACTIVATE);


    // --- imgui --------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    LoadUiFont();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_ctx);

    // --- data source ----------------------------------------------------------
    SceneGen scene(1);
    ReplayReader replay;
    ReplayWriter recorder;
    SharedRing liveRing;
    const char* sourceName = "synthetic";
    if (live) {
        std::print("waiting for live ring '{}' (start D3Hud.Reader --publish)...\n", SharedRing::kName);
        // no fallback: the ring appears whenever a reader starts (Info panel
        // button or by hand); the main loop keeps trying to open it
        sourceName = "live";
        g_mapOrientation = 2;  // "rotate -135": confirmed in-game (F7 to adjust)
    }
    if (!live && replayPath) {
        if (replay.Open(replayPath)) {
            sourceName = replayPath;
            std::print("replay: {} frames @ {:.1f} ms\n", replay.frameCount(), replay.frameIntervalMs());
        } else {
            std::print(stderr, "cannot open replay '{}' — falling back to synthetic\n", replayPath);
            replayPath = nullptr;
        }
    }
    if (recordPath && !recorder.Open(recordPath, static_cast<float>(scene.frameIntervalMs()))) {
        std::print(stderr, "cannot open '{}' for recording\n", recordPath);
        recordPath = nullptr;
    }

    d3hud::WorldSnapshot snap;
    scene.Generate(&snap);  // first frame immediately
    if (g_matrix) ApplyMatrixPhase(0);
    DWORD lastPhaseTick = GetTickCount();

    HudRenderer renderer;
    PluginHost plugins(renderer);
    {
        renderer.SetLayoutPath(ExeDir() / "d3hud.ini");
        // map prediction shapes, next to the exe like d3hud.ini (copied by the build)
        if (!renderer.LoadSceneCells(ExeDir() / "scene_cells.bin")) std::print(stderr, "no scene_cells.bin - map prediction off\n");
    }
    plugins.LoadAll(ExeDir() / "plugins", ExeDir() / "plugins.ini");
    if (settingsPage != nullptr && !renderer.OpenSettingsPage(settingsPage)) std::print(stderr, "no settings page '{}'\n", settingsPage);
    HudOptions opts;
    opts.showPanels = g_showPanels;
    ReaderProcess reader;  // Info panel "Start reader" (live mode); ends with the overlay
    ReaderStatus readerStatus;
    SalvageRunner salvageRunner;  // auto salvage (live mode): real clicks into the game
    SalvageStatus salvageStatus;
    bool ringOpen = false;
    ULONGLONG ringTryAt = 0;
    if (live) {
        opts.reader = &readerStatus;
        opts.salvage = &salvageStatus;
    }
    // The reader is started once the game window exists, restarted if the game is restarted, and left
    // alone after the user pressed Stop or when it keeps dying right away.
    bool readerUserStopped = false, readerWasRunning = false;
    ULONGLONG readerNextTry = 0, readerStartedAt = 0;
    int readerQuickExits = 0;
    std::string readerAutoError;

    LARGE_INTEGER freq, tPrev, tNow;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&tPrev);
    double srcAcc = 0.0;
    double fpsSmooth = 60.0;
    DWORD lastTrack = 0;
    RECT lastRect = r;
    uint64_t framesDrawn = 0;

    // --- main loop ------------------------------------------------------------
    while (g_running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running) break;

        QueryPerformanceCounter(&tNow);
        const double dt = static_cast<double>(tNow.QuadPart - tPrev.QuadPart) / freq.QuadPart;
        tPrev = tNow;
        fpsSmooth = fpsSmooth * 0.9 + (1.0 / (dt > 0 ? dt : 1e-9)) * 0.1;

        // Track the target window 4x/s.
        if (g_matrix && GetTickCount() - lastPhaseTick > 2500) {
            static int phase = 0;
            lastPhaseTick = GetTickCount();
            phase = (phase + 1) % 3;
            ApplyMatrixPhase(phase);
        }
        if (GetTickCount() - lastTrack > 250) {
            lastTrack = GetTickCount();
            if (g_targetSub[0] != L'\0' && (g_targetHwnd == nullptr || !IsWindow(g_targetHwnd))) {
                g_targetHwnd = FindTargetWindow(g_targetSub);  // the game was (re)started
                if (g_targetHwnd != nullptr) {
                    std::print("tracking window {}\n", static_cast<void*>(g_targetHwnd));
                    g_waitingForTarget = false;
                    lastRect = RECT{};  // force a reposition below
                    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
                } else if (live && !g_waitingForTarget) {
                    g_waitingForTarget = true;
                    std::print("the game window is gone - waiting for it...\n");
                    ShowWindow(g_hwnd, SW_HIDE);
                }
            }
            RECT nr;
            GetOverlayRect(&nr);
            if (!EqualRect(&nr, &lastRect)) {
                lastRect = nr;
                SetWindowPos(g_hwnd, HWND_TOPMOST, nr.left, nr.top, nr.right - nr.left,
                             nr.bottom - nr.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
                Resize(nr.right - nr.left, nr.bottom - nr.top);
            }
        }

        if (g_waitingForTarget) {  // nothing to draw on: idle until the game window shows up
            Sleep(100);
            continue;
        }

        // Advance the data source at its native cadence.
        if (live) {
            // Keep the two newest snapshots with their arrival times and blend
            // between them (one interval of latency, ~16 ms) so dots glide
            // instead of stepping at the reader's rate.
            static WorldSnapshot liveA, liveB, polled;
            static double arriveA = 0.0, arriveB = 0.0;
            const double nowS = static_cast<double>(tNow.QuadPart) / freq.QuadPart;
            int got = 0;
            while (liveRing.Poll(&polled)) {
                liveA = liveB;
                liveB = polled;
                got++;
            }
            if (got > 0) {
                arriveA = arriveB;
                arriveB = nowS;
            }
            if (arriveA <= 0.0) {
                snap = liveB;  // fewer than two snapshots yet: nothing to blend from
            } else {
                const double interval = std::min(0.2, std::max(0.005, arriveB - arriveA));
                InterpolateSnapshot(liveA, liveB, static_cast<float>((nowS - arriveB) / interval),
                                    &snap);
            }
            // World labels sit on the 3D scene, which the game draws at the player's
            // CURRENT position: anchor them to the newest position extrapolated to
            // now (reader stamps are QPC ms, the same clock as nowS), not to the
            // deliberately delayed interpolation used for the minimap.
            opts.haveLabelCenter = false;
            const double tsA = liveA.timestampMs, tsB = liveB.timestampMs;
            if (arriveA > 0.0 && tsB > tsA && tsB - tsA < 200.0) {
                const double ahead = std::clamp(nowS * 1000.0 - tsB, 0.0, 50.0);
                const double k = ahead / (tsB - tsA);
                opts.labelCenter = {
                    liveB.playerPos.x + static_cast<float>((liveB.playerPos.x - liveA.playerPos.x) * k),
                    liveB.playerPos.y + static_cast<float>((liveB.playerPos.y - liveA.playerPos.y) * k)};
                opts.haveLabelCenter = true;
            }
        } else if (replayPath) {
            srcAcc += dt * 1000.0;
            const double interval = replay.frameIntervalMs();
            uint32_t guard = 0;
            while (srcAcc >= interval && guard++ < 8) {
                static uint32_t rp = 0;
                if (!replay.Read(rp, &snap)) rp = 0;  // loop
                else rp++;
                srcAcc -= interval;
            }
        } else {
            srcAcc += dt * 1000.0;
            const double interval = scene.frameIntervalMs();
            uint32_t guard = 0;
            while (srcAcc >= interval && guard++ < 8) {
                scene.Generate(&snap);
                if (recordPath) recorder.Append(snap);
                srcAcc -= interval;
            }
            if (recordPath && snap.timestampMs >= recordSeconds * 1000.0) {
                recorder.Close();
                std::print("recorded {} frames to {}\n", recorder.count(), recordPath);
                recordPath = nullptr;  // continue rendering synthetic
            }
        }

        // Render.
        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        opts.showPanels = g_showPanels;
        opts.inputInteractive = !g_clickThrough && !g_hotButton;
        opts.mouseLive = !g_clickThrough;
        opts.mapOrientation = g_mapOrientation;
        g_hotkeysSuspended = renderer.capturingHotkey() || g_textFocus;
        if (renderer.TakeHotkeysDirty()) RegisterHotkeys(renderer.hotkeys());
        opts.hotCount = 0;  // refilled by Draw
        opts.panelToggleMask = g_panelToggleMask;
        g_panelToggleMask = 0;
        {
            // Win32 cursor: ImGui gets no mouse input while the overlay is click-through
            POINT cur;
            const bool okCur = GetCursorPos(&cur) && ScreenToClient(g_hwnd, &cur);
            opts.cursorX = okCur ? static_cast<float>(cur.x) : -1.f;
            opts.cursorY = okCur ? static_cast<float>(cur.y) : -1.f;
        }
        if (live) {
            if (!ringOpen && GetTickCount64() - ringTryAt > 500) {
                ringTryAt = GetTickCount64();
                ringOpen = liveRing.Open();
                if (ringOpen) std::print("live ring connected\n");
            }
            const ULONGLONG nowMs = GetTickCount64();
            const bool readerRunning = reader.Running();
            if (readerWasRunning && !readerRunning)
                readerQuickExits = nowMs - readerStartedAt < 15000 ? readerQuickExits + 1 : 0;
            readerWasRunning = readerRunning;
            const bool wantReader = startReader || renderer.readerAutostart();
            const bool gameUp = g_targetHwnd != nullptr && IsWindow(g_targetHwnd);
            if (wantReader && !readerRunning && !readerUserStopped && gameUp && readerQuickExits < 4 &&
                nowMs >= readerNextTry) {
                readerNextTry = nowMs + 5000;
                if (reader.Start(renderer.readerHz())) readerStartedAt = nowMs;
                else std::print(stderr, "{}\n", reader.lastError());
            }
            readerAutoError = readerQuickExits >= 4 ? "the reader keeps stopping right away - see reader.log; press Start reader to retry"
                                                    : reader.lastError();
            readerStatus.running = readerRunning;
            readerStatus.connected = ringOpen && liveRing.latestIndex() > 0;
            readerStatus.pid = reader.pid();
            readerStatus.line = reader.LastLogLine().c_str();
            readerStatus.error = readerAutoError.c_str();
        }
        if (live) {
            salvageRunner.Tick(snap, renderer.safeArea(), renderer.salvageRules());
            salvageStatus.running = salvageRunner.Active();
            salvageStatus.status = salvageRunner.status().c_str();
        }
        plugins.OnFrame(snap, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
        renderer.Draw(snap, opts, fpsSmooth, sourceName);
        {
            // Typing: the overlay is WS_EX_NOACTIVATE, so keys go to the game. While a text
            // field is active take the keyboard (and give it back to the game afterwards).
            const bool wantText = ImGui::GetIO().WantTextInput;
            if (wantText != g_textFocus) {
                g_textFocus = wantText;
                g_textFocusAt = GetTickCount64();
                LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
                if (wantText) {
                    ex &= ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
                    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
                    SetForegroundWindow(g_hwnd);
                    SetFocus(g_hwnd);
                } else {
                    ex |= WS_EX_NOACTIVATE;
                    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
                    if (g_targetHwnd != nullptr && IsWindow(g_targetHwnd)) SetForegroundWindow(g_targetHwnd);
                }
            } else if (g_textFocus && GetTickCount64() - g_textFocusAt > 400 && GetForegroundWindow() != g_hwnd) {
                ImGui::ClearActiveID();  // the user clicked back into the game: end the edit
            }
        }
        if (salvageStatus.stopClicked) salvageRunner.Stop("stopped by user");
        if (salvageStatus.startClicked) {
            // clicks must reach the game: back to click-through first
            g_hotButton = false;
            if (!g_clickThrough) SetClickThrough(g_hwnd, true);
            salvageRunner.Start(g_hwnd, g_targetHwnd);
        }
        salvageStatus.startClicked = salvageStatus.stopClicked = false;
        {
            // the anvil's salvage button works without F8: the overlay takes input only
            // while the cursor is over it, and goes back to click-through right after
            bool inBtn = false;
            for (int i = 0; i < opts.hotCount; ++i) {
                const HudOptions::HotRect& h = opts.hot[i];
                inBtn |= opts.cursorX >= h.x0 && opts.cursorX <= h.x1 && opts.cursorY >= h.y0 && opts.cursorY <= h.y1;
            }
            if (inBtn && g_clickThrough && !salvageRunner.Active()) {
                SetClickThrough(g_hwnd, false);
                g_hotButton = true;
            } else if (g_hotButton && !inBtn) {
                g_hotButton = false;
                SetClickThrough(g_hwnd, true);
            }
        }
        if (readerStatus.startClicked) {
            readerUserStopped = false;
            readerQuickExits = 0;
            if (reader.Start(renderer.readerHz())) readerStartedAt = GetTickCount64();
        }
        if (readerStatus.stopClicked) {
            readerUserStopped = true;  // do not bring it back by itself
            reader.Stop();
        }
        readerStatus.startClicked = readerStatus.stopClicked = false;
        if (g_visualMark && framesDrawn >= 5 && (g_matrix || framesDrawn < 55)) {
            // big saturated marker so an external screen capture can verify compositing
            const ImVec2 ds = ImGui::GetIO().DisplaySize;
            ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(ds.x * 0.5f - 150.f, ds.y * 0.5f - 150.f),
                ImVec2(ds.x * 0.5f + 150.f, ds.y * 0.5f + 150.f), IM_COL32(255, 0, 255, 255));
        }
        ImGui::Render();

        const float clearDcomp[4] = {0.f, 0.f, 0.f, 0.f};
        const float clearKeyed[4] = {0.f, 0.f, 0.f, 1.f};  // opaque black = color key
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, g_useDComp ? clearDcomp : clearKeyed);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(selftestFrames > 0 ? 0 : 1, 0);
        framesDrawn++;

        if (g_hitTest && framesDrawn == 60) {
            RECT wr;
            GetWindowRect(g_hwnd, &wr);
            const LONG w = wr.right - wr.left, h = wr.bottom - wr.top;
            POINT pts[4] = {
                {wr.left + w / 2, wr.top + h / 2},                            // center
                {wr.left + static_cast<LONG>(w * 0.8f), wr.top + static_cast<LONG>(h * 0.25f)},  // salvage
                {wr.right - 150, wr.bottom - 150},                            // minimap
                {wr.left + 100, wr.top + 60},                                 // info box
            };
            const char* names[4] = {"center", "salvage", "minimap", "info"};
            bool passAll = true, interAll = true;
            for (int i = 0; i < 4; ++i) {
                const bool pass = (WindowFromPoint(pts[i]) != g_hwnd);
                passAll &= pass;
                std::print("selftest-hit: passthrough[{}]={}\n", names[i], pass ? "OK" : "FAIL");
            }
            LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE) & ~WS_EX_TRANSPARENT;
            SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
            Sleep(150);
            for (int i = 0; i < 4; ++i) {
                const bool cap = (WindowFromPoint(pts[i]) == g_hwnd);
                interAll &= cap;
                std::print("selftest-hit: interactive[{}]={}\n", names[i], cap ? "OK" : "FAIL");
            }
            std::print("selftest-hit: {}\n", (passAll && interAll) ? "ALL OK" : "FAILURES PRESENT");
            g_selftestResult = (passAll && interAll) ? 0 : 2;
        }

        if (selftestFrames > 0 && framesDrawn >= static_cast<uint64_t>(selftestFrames)) {
            std::print("selftest: {} frames rendered OK (dcomp={}) source={} player=({:.1f}, {:.1f})\n",
                   static_cast<unsigned long long>(framesDrawn), g_useDComp ? 1 : 0, sourceName,
                   snap.playerPos.x, snap.playerPos.y);
            g_running = false;
        }
    }

    // --- teardown --------------------------------------------------------------
    recorder.Close();
    replay.Close();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    for (int i = 0; i < MinimapLayout::kHotkeyCount; ++i) UnregisterHotKey(g_hwnd, i + 1);
    if (g_rtv) g_rtv->Release();
    if (g_swap) g_swap->Release();
    if (g_dcompVisual) g_dcompVisual->Release();
    if (g_dcompTarget) g_dcompTarget->Release();
    if (g_dcompDevice) g_dcompDevice->Release();
    if (g_ctx) g_ctx->Release();
    if (g_device) g_device->Release();
    DestroyWindow(g_hwnd);
    UnregisterClassW(kClassName, hInst);
    std::print("D3Hud.Overlay exited cleanly ({} frames)\n", static_cast<unsigned long long>(framesDrawn));
    return g_selftestResult == -1 ? 0 : g_selftestResult;
}
