#!/usr/bin/env python3
"""matrix_probe.py — measures click-through + on-screen visibility for each
exstyle phase reported by D3Hud.Overlay --matrix."""
import ctypes
import subprocess
import sys
import time
import ctypes.wintypes as wt

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
user32.WindowFromPoint.argtypes = [wt.POINT]
user32.WindowFromPoint.restype = wt.HWND

def magenta_count():
    sw, sh = user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)
    cx, cy = sw // 2, sh // 2
    w = h = 400
    hdc = user32.GetDC(0)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    gdi32.BitBlt(mem, 0, 0, w, h, hdc, cx - 200, cy - 200, 0x00CC0020)
    class BMIH(ctypes.Structure):
        _fields_ = [("biSize", ctypes.c_uint32), ("biWidth", ctypes.c_int32), ("biHeight", ctypes.c_int32),
                    ("biPlanes", ctypes.c_uint16), ("biBitCount", ctypes.c_uint16), ("biCompression", ctypes.c_uint32),
                    ("biSizeImage", ctypes.c_uint32), ("biXPelsPerMeter", ctypes.c_int32), ("biYPelsPerMeter", ctypes.c_int32),
                    ("biClrUsed", ctypes.c_uint32), ("biClrImportant", ctypes.c_uint32)]
    class BMI(ctypes.Structure):
        _fields_ = [("bmiHeader", BMIH), ("biColors", ctypes.c_uint32 * 3)]
    bmi = BMI()
    bmi.bmiHeader = BMIH(ctypes.sizeof(BMIH), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = (ctypes.c_uint8 * (w * h * 4))()
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bmi), 0)
    gdi32.DeleteObject(bmp); gdi32.DeleteDC(mem); user32.ReleaseDC(0, hdc)
    n = 0
    for i in range(0, w * h * 4, 4):
        if buf[i+2] > 180 and buf[i] > 180 and buf[i+1] < 90:
            n += 1
    return n

def probe(base):
    exe = r"C:\Users\arne\Projects\D3Hud\build\Release\D3Hud.Overlay.exe"
    p = subprocess.Popen([exe, "--matrix", "--matrix-base", base], stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, bufsize=1)
    results = []
    seen = 0
    for line in p.stdout:
        line = line.strip()
        if not line.startswith("PHASE"):
            continue
        seen += 1
        if seen == 4:  # phases wrapped (0,1,2,0) — one full cycle done
            break
        phase_name = line
        time.sleep(1.1)  # let the style settle and frames render
        hwnd = user32.FindWindowW(None, "D3Hud Overlay")
        if not hwnd:
            results.append((base, phase_name, "no-hwnd", -1))
            continue
        r = wt.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(r))
        pt = wt.POINT((r.left + r.right) // 2, (r.top + r.bottom) // 2)
        hit_self = user32.WindowFromPoint(pt) == hwnd
        m = magenta_count()
        results.append((base, phase_name, "blocked" if hit_self else "passthrough", m))
    p.kill()
    return results

def main():
    all_results = []
    for base in ("noredir", "normal"):
        print(f"--- base={base} (a magenta square will flash on screen) ---", flush=True)
        all_results += probe(base)
    print()
    print(f"{'base':8s} {'phase':45s} {'clicks':12s} {'magenta px':>10s}")
    winners = []
    for base, phase, clicks, m in all_results:
        print(f"{base:8s} {phase:45s} {clicks:12s} {m:>10d}")
        if clicks == "passthrough" and m > 3000:
            winners.append((base, phase))
    print()
    if winners:
        print("WORKING COMBOS:", winners)
    else:
        print("NO combo gives both pass-through and visibility — needs a different approach")

if __name__ == "__main__":
    main()
