// zorder.cpp : "what is on top of the desktop right now?"
//
// The pets are layered TOPMOST windows so they are never buried by a stale
// window, but that also means they would float above every application.  The
// trick is to treat the desktop as the surface the insects live on: any normal
// application window is an opaque object lying on that surface, so the insects
// have to vanish behind it.
//
// This module keeps a small cache of the rectangles of all windows that
// currently cover the desktop.  Bug::Present() then clips its sprite against
// them (or hides the window entirely when nothing is left), which makes a
// cockroach slide out from behind an app window edge exactly like it would
// slide out from behind a book.
#include "common.h"
#include <dwmapi.h>

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

namespace {

std::vector<RECT> s_occ;        // covering rectangles, screen coords
bool              s_valid = false;

// Windows that belong to the wallpaper / shell furniture: they never occlude.
bool SkipClass(const wchar_t* c) {
    static const wchar_t* kSkip[] = {
        L"Progman",
        L"WorkerW",
        L"SHELLDLL_DefView",
        L"SysListView32",
        L"Shell_TrayWnd",
        L"Shell_SecondaryTrayWnd",
        L"RoachPetBugWnd",
        L"RoachPetPoopWnd",
        L"RoachPetMsgWnd",
        L"tooltips_class32",
        L"#32768",              // popup menus
        L"TaskListThumbnailWnd",
    };
    for (const wchar_t* s : kSkip) if (!wcscmp(c, s)) return true;
    return false;
}

BOOL CALLBACK EnumOcc(HWND h, LPARAM lp) {
    auto* out = (std::vector<RECT>*)lp;

    if (!IsWindowVisible(h)) return TRUE;
    if (IsIconic(h)) return TRUE;

    wchar_t cls[128] = {0};
    GetClassNameW(h, cls, 127);
    if (SkipClass(cls)) return TRUE;

    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TRANSPARENT) return TRUE;      // click-through overlay

    // UWP shells keep their windows around but "cloaked" when not on screen
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;

    // fully transparent layered overlays do not hide anything
    if (ex & WS_EX_LAYERED) {
        BYTE alpha = 255;
        DWORD flags = 0;
        COLORREF key = 0;
        if (GetLayeredWindowAttributes(h, &key, &alpha, &flags)) {
            if ((flags & LWA_ALPHA) && alpha < 8) return TRUE;
        }
    }

    RECT r;
    if (!GetWindowRect(h, &r)) return TRUE;
    if (r.right - r.left < 2 || r.bottom - r.top < 2) return TRUE;

    out->push_back(r);
    return TRUE;
}

} // namespace

void ZOrderRefresh() {
    std::vector<RECT> now;
    now.reserve(64);
    EnumWindows(EnumOcc, (LPARAM)&now);

    if (s_valid && now.size() == s_occ.size()) {
        bool same = true;
        for (size_t i = 0; i < now.size(); i++) {
            const RECT& a = now[i];
            const RECT& b = s_occ[i];
            if (a.left != b.left || a.top != b.top || a.right != b.right || a.bottom != b.bottom) {
                same = false;
                break;
            }
        }
        if (same) return;
    }
    s_occ.swap(now);
    s_valid = true;
    PoopMarkDirty();
    if (getenv("ROACH_ZORDER_LOG")) {
        for (auto& r : s_occ)
            LogLine(L"occ %ld,%ld %ldx%ld", (long)r.left, (long)r.top,
                    (long)(r.right - r.left), (long)(r.bottom - r.top));
    }
}

int ZOrderCollect(const RECT& box, RECT* out, int maxN, bool* fullyCovered) {
    if (fullyCovered) *fullyCovered = false;
    if (s_occ.empty() || !out || maxN <= 0) return 0;

    int n = 0;
    bool full = false;
    for (const RECT& r : s_occ) {
        if (r.right <= box.left || r.left >= box.right ||
            r.bottom <= box.top || r.top >= box.bottom) continue;
        if (r.left <= box.left && r.top <= box.top &&
            r.right >= box.right && r.bottom >= box.bottom) full = true;
        if (n < maxN) out[n++] = r;
    }
    if (fullyCovered) *fullyCovered = full;
    return n;
}

bool ZOrderPointCovered(float x, float y) {
    for (const RECT& r : s_occ) {
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return true;
    }
    return false;
}
