// app.cpp : message window, tray, hotkeys, settings UI, main loop
#include "common.h"
#include <mmsystem.h>
#include <vector>

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
typedef HANDLE ROACH_DPI_CTX;
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((ROACH_DPI_CTX)-4)
#endif

static void EnablePerMonitorDpi() {
    typedef BOOL (WINAPI *PFN)(HANDLE);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u) return;
    PFN f = (PFN)GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (f) { f((HANDLE)DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); return; }
    typedef BOOL (WINAPI *PFN2)(void);
    PFN2 g = (PFN2)GetProcAddress(u, "SetProcessDPIAware");
    if (g) g();
}

RECT g_curBounds = {0, 0, 1920, 1080};

static const wchar_t* kMsgClass = L"RoachPetMsgWnd";
static HWND  g_msgWnd = nullptr;
static HWND  g_setWnd = nullptr;
static HICON g_trayIcon = nullptr;
static bool  g_trayAdded = false;
static bool  g_paused = false;
static bool  g_wantSettings = false;
static UINT  g_taskbarCreated = 0;

#define WM_TRAY   (WM_APP + 1)
#define ID_TRAY   1

// settings control ids
enum {
    IDC_FPS = 1001, IDC_IDLE, IDC_MAXBUG, IDC_SCALE, IDC_OPACITY, IDC_DEBUGBG,
    IDC_LANG, IDC_TRAY, IDC_THROUGH, IDC_HOVER, IDC_EAT, IDC_POOP, IDC_FULLSCR,
    IDC_AUTOSTART, IDC_MONITOR, IDC_BOUNDS, IDC_HIDDENCNT, IDC_RESTORE,
    IDC_OPENLOG, IDC_OPENCFG, IDC_ABOUT2,
    IDC_OK, IDC_CANCEL, IDC_APPLY, IDC_PAUSE, IDC_LOGBTN, IDC_STARVE,
    IDC_HIDEONAPP, IDC_FLEESPD, IDC_FLEESPDLBL,
    IDC_HK_TOGGLE, IDC_HK_SETTINGS, IDC_HK_QUIT
};

static HWND hFps, hIdle, hMaxBug, hScale, hOpacity, hDebugBg, hLang, hTray,
            hThrough, hHover, hEat, hPoop, hFullscr, hAutostart, hMonitor,
            hBounds, hHiddenCnt, hRestore, hOpenLog, hOpenCfg,
            hAbout2, hOK, hCancel, hApply, hPause, hLogBtn, hStarve,
            hHideOnApp, hFleeSpd;
static HFONT g_uiFont = nullptr;
static HFONT g_boldFont = nullptr;
static HFONT g_smallFont = nullptr;
// The dialog is laid out in 96-dpi units and scaled to the real monitor DPI,
// otherwise the controls grow with the font but the columns do not.
static float g_uiScale = 1.0f;
static int   Sc(int v) { return (int)(v * g_uiScale + 0.5f); }

// =========================================================================
// settings theme — "specimen card"
//
// The pet is a red-brown cockroach; the interface borrows the language of an
// entomology specimen drawer rather than the usual grey Windows sheet: one
// sheet of white card laid on a warm paper-grey desk, each section flagged
// with a short carapace-brown marker, sections separated by hairlines.
// Nothing here is decoration for its own sake — every colour has a job.
// =========================================================================
static const COLORREF kDesk      = RGB(0xED, 0xE7, 0xDD);   // desk behind the card
static const COLORREF kCard      = RGB(0xFF, 0xFF, 0xFF);   // the card
static const COLORREF kCardEdge  = RGB(0xE4, 0xDA, 0xCC);   // card hairline
static const COLORREF kRule      = RGB(0xE7, 0xDD, 0xCE);   // section rule
static const COLORREF kCarapace  = RGB(0x7A, 0x2E, 0x12);   // section marker, primary button
static const COLORREF kCarapaceD = RGB(0x5E, 0x22, 0x0C);   // primary button, pressed
static const COLORREF kCarapaceH = RGB(0x8E, 0x3A, 0x1A);   // primary button, hot
static const COLORREF kInk       = RGB(0x2E, 0x24, 0x20);   // section headings
static const COLORREF kBody      = RGB(0x4A, 0x3F, 0x38);   // labels, values
static const COLORREF kFieldEdge = RGB(0xD5, 0xCA, 0xBD);   // control borders
static const COLORREF kBtnEdge   = RGB(0xD5, 0xCA, 0xBD);   // secondary buttons
static const COLORREF kBtnHot    = RGB(0xF6, 0xF1, 0xE9);   // secondary buttons, hot

// ---- custom hotkeys: click a row, then press the combination ----
struct HkRow { HWND btn = nullptr; UINT mod = 0, key = 0; bool changed = false; int which = 0; };
static HkRow g_hkRows[3];
static HWND  g_hkCapture = nullptr;      // button waiting for a new combination

static void HkText(UINT mod, UINT key, wchar_t* buf, size_t cch) {
    if (!key) { swprintf(buf, cch, L"%ls", T(S_HK_PRESS)); return; }
    wchar_t name[64] = L"?";
    UINT sc = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    if (sc) {
        LONG lsc = (LONG)sc << 16;
        if ((key >= 0x21 && key <= 0x28) || key == 0x2D || key == 0x2E) lsc |= 0x1000000;
        GetKeyNameTextW(lsc, name, 64);
    }
    wchar_t out[200] = L"";
    if (mod & MOD_CONTROL) wcscat(out, L"Ctrl + ");
    if (mod & MOD_SHIFT)   wcscat(out, L"Shift + ");
    if (mod & MOD_ALT)     wcscat(out, L"Alt + ");
    wcscat(out, name);
    swprintf(buf, cch, L"%ls", out);
}

static void HkScanCapture(HWND h) {
    if (!g_hkCapture) return;
    UINT mod = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mod |= MOD_CONTROL;
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mod |= MOD_SHIFT;
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) mod |= MOD_ALT;
    // Escape without modifiers cancels
    if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) && !mod) {
        g_hkCapture = nullptr;
        KillTimer(h, 2);
        return;
    }
    for (UINT vk = 0x08; vk <= 0xFE; vk++) {
        if (vk == VK_ESCAPE || vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU ||
            vk == VK_LWIN || vk == VK_RWIN || vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LMENU || vk == VK_RMENU ||
            vk == VK_PACKET) continue;
        if (GetAsyncKeyState((int)vk) & 0x8000) {
            for (auto& r : g_hkRows) {
                if (r.btn == g_hkCapture) {
                    r.mod = mod; r.key = vk; r.changed = true;
                    wchar_t buf[200]; HkText(mod, vk, buf, 200);
                    SetWindowTextW(r.btn, buf);
                    InvalidateRect(r.btn, nullptr, TRUE);
                }
            }
            g_hkCapture = nullptr;
            KillTimer(h, 2);
            return;
        }
    }
}

struct SetSection { const wchar_t* title; int titleY; int ruleY; };
static std::vector<SetSection> g_secs;
static std::vector<HWND>       g_chks;      // owner-drawn checkboxes
static int    g_cardTop = 0, g_cardBot = 0;

static HBRUSH g_brCard = nullptr, g_brDesk = nullptr;

static bool IsOwnerCheck(HWND h) {
    for (HWND c : g_chks) if (c == h) return true;
    return false;
}

static void FillRoundRectG(HDC dc, const RECT& r, int rad, COLORREF fill, COLORREF edge) {
    HBRUSH  br = CreateSolidBrush(fill);
    HPEN    pn = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pn);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad * 2, rad * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pn);
}

static void FillRectC(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    FillRect(dc, &r, br);
    DeleteObject(br);
}

// 15px box, carapace fill when on, a tick drawn with two strokes.
static void DrawOwnerCheck(HDC dc, const RECT& rc, const wchar_t* text,
                           bool on, bool pressed, bool focus) {
    FillRectC(dc, rc, kCard);

    const int box = Sc(15);
    const int cy  = (rc.top + rc.bottom) / 2;
    RECT b = { rc.left + Sc(1), cy - box / 2, rc.left + Sc(1) + box, cy - box / 2 + box };
    COLORREF fill = on ? (pressed ? kCarapaceD : kCarapace) : (pressed ? kBtnHot : kCard);
    FillRoundRectG(dc, b, Sc(3), fill, on ? fill : kFieldEdge);

    if (on) {
        HPEN p = CreatePen(PS_SOLID, Sc(2), kCard);
        HGDIOBJ op = SelectObject(dc, p);
        MoveToEx(dc, b.left + box / 4, cy + Sc(1), nullptr);
        LineTo(dc, b.left + box / 2 - Sc(1), b.bottom - box / 3);
        LineTo(dc, b.right - box / 4, b.top + box / 3);
        SelectObject(dc, op);
        DeleteObject(p);
    }

    RECT tr = rc;
    tr.left = b.right + Sc(8);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kBody);
    DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (focus) {
        RECT f = { b.left - Sc(2), b.top - Sc(2), tr.right, tr.bottom + Sc(2) };
        DrawFocusRect(dc, &f);
    }
}

static void DrawOwnerPush(HDC dc, const RECT& rc, const wchar_t* text,
                          bool primary, bool pressed, bool focus) {
    COLORREF fill, edge, ink;
    if (primary) {
        fill = pressed ? kCarapaceD : kCarapace;
        edge = fill;
        ink  = kCard;
    } else {
        fill = pressed ? kBtnHot : kCard;
        edge = kBtnEdge;
        ink  = kBody;
    }
    FillRoundRectG(dc, rc, Sc(5), fill, edge);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ink);
    RECT tr = rc;
    DrawTextW(dc, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (focus) {
        RECT f = rc;
        InflateRect(&f, -Sc(3), -Sc(3));
        DrawFocusRect(dc, &f);
    }
}

// =========================================================================
// helpers
// =========================================================================
static void OpenFolderOf(const std::wstring& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void AppNotifyBalloon(const wchar_t* title, const wchar_t* text) {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_msgWnd;
    nid.uID = ID_TRAY;
    nid.uFlags = NIF_INFO;
    wcsncpy(nid.szInfoTitle, title, 63);
    wcsncpy(nid.szInfo, text, 255);
    nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// =========================================================================
// bounds / monitors
// =========================================================================
struct MonInfo { RECT full; RECT work; };
static std::vector<MonInfo> g_mons;

static BOOL CALLBACK MonEnum(HMONITOR h, HDC, LPRECT, LPARAM lp) {
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(h, &mi)) {
        MonInfo m;
        m.full = mi.rcMonitor;
        m.work = mi.rcWork;
        ((std::vector<MonInfo>*)lp)->push_back(m);
    }
    return TRUE;
}

static void RefreshMonitors() {
    g_mons.clear();
    EnumDisplayMonitors(nullptr, nullptr, MonEnum, (LPARAM)&g_mons);
    if (g_mons.empty()) {
        MonInfo m;
        m.full.left = 0; m.full.top = 0;
        m.full.right = GetSystemMetrics(SM_CXSCREEN);
        m.full.bottom = GetSystemMetrics(SM_CYSCREEN);
        m.work = m.full;
        g_mons.push_back(m);
    }
}

RECT ComputeBounds() {
    RefreshMonitors();
    if (g_cfg.monitor > 0 && g_cfg.monitor <= (int)g_mons.size()) {
        const MonInfo& m = g_mons[g_cfg.monitor - 1];
        return g_cfg.boundsMode ? m.full : m.work;
    }
    RECT r = { INT_MAX, INT_MAX, -INT_MAX, -INT_MAX };
    for (auto& m : g_mons) {
        const RECT& s = g_cfg.boundsMode ? m.full : m.work;
        r.left = std::min(r.left, s.left);
        r.top = std::min(r.top, s.top);
        r.right = std::max(r.right, s.right);
        r.bottom = std::max(r.bottom, s.bottom);
    }
    if (r.right <= r.left || r.bottom <= r.top) {
        r.left = 0; r.top = 0;
        r.right = GetSystemMetrics(SM_CXSCREEN);
        r.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    return r;
}

bool IsFullscreenForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    if (fg == g_setWnd || fg == g_msgWnd) return false;
    wchar_t cls[128] = {0};
    GetClassNameW(fg, cls, 127);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") ||
        !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Windows.UI.Core.CoreWindow"))
        return false;
    if (!IsWindowVisible(fg) || IsIconic(fg)) return false;
    // a merely maximised window is NOT fullscreen
    if (IsZoomed(fg)) return false;
    LONG_PTR style = GetWindowLongPtrW(fg, GWL_STYLE);
    if (style & WS_CAPTION) return false;      // has a title bar -> windowed

    RECT wr;
    if (!GetWindowRect(fg, &wr)) return false;
    HMONITOR hm = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(hm, &mi)) return false;
    const RECT& mr = mi.rcMonitor;
    return wr.left <= mr.left + 2 && wr.top <= mr.top + 2 &&
           wr.right >= mr.right - 2 && wr.bottom >= mr.bottom - 2;
}

// =========================================================================
// tray
// =========================================================================
static void TrayAdd() {
    if (!g_cfg.showTray) return;
    if (g_trayAdded) return;
    if (!g_trayIcon) g_trayIcon = R::MakeTrayIcon(GetSystemMetrics(SM_CXSMICON));
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_msgWnd;
    nid.uID = ID_TRAY;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = g_trayIcon;
    wcsncpy(nid.szTip, T(S_TRAY_TIP), 127);
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != 0;
}

static void TrayRemove() {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_msgWnd;
    nid.uID = ID_TRAY;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = false;
}

void AppRefreshTray() {
    TrayRemove();
    TrayAdd();
}

static void ShowTrayMenu() {
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, g_visible ? T(S_MENU_HIDE) : T(S_MENU_SHOW));
    AppendMenuW(m, MF_STRING, 2, T(S_MENU_SETTINGS));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_paused ? MF_CHECKED : 0), 3, T(S_MENU_PAUSE));
    AppendMenuW(m, MF_STRING, 4, T(S_MENU_CLEANPOOP));
    AppendMenuW(m, MF_STRING, 5, T(S_MENU_RESTORE));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 6, T(S_MENU_OPENCFG));
    AppendMenuW(m, MF_STRING, 7, T(S_MENU_ABOUT));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 8, T(S_MENU_QUIT));
    SetForegroundWindow(g_msgWnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_msgWnd, nullptr);
    DestroyMenu(m);
}

void AppRebuildTrayMenu() { /* menu is built on demand */ }

void AppToggleVisible() {
    g_visible = !g_visible;
    BugForceShow(g_visible);
}

void AppQuit() {
    g_quitting = true;
    ConfigSave();
    PostQuitMessage(0);
}

// =========================================================================
// about
// =========================================================================
static HFONT UiFont(int pt, bool bold);     // defined with the settings UI

static const wchar_t* kRepoUrl = L"https://github.com/HuangJHong/American-Roach-Pet";
static HWND g_aboutWnd = nullptr;

static void OpenRepo() {
    ShellExecuteW(nullptr, L"open", kRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

static LRESULT CALLBACK AboutProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) DestroyWindow(h);
        return 0;
    case WM_NOTIFY: {
        LPNMHDR nh = (LPNMHDR)l;
        if (nh && (nh->code == NM_CLICK || nh->code == NM_RETURN)) {
            OpenRepo();
            return 0;
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_aboutWnd = nullptr;
        if (g_setWnd && IsWindow(g_setWnd)) EnableWindow(g_setWnd, TRUE);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void AppShowAbout() {
    if (g_aboutWnd && IsWindow(g_aboutWnd)) { SetForegroundWindow(g_aboutWnd); return; }

    static bool clsReady = false;
    if (!clsReady) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = AboutProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"RoachPetAbout";
        RegisterClassExW(&wc);
        clsReady = true;
    }

    float s = 1.0f;
    {
        HDC dc = GetDC(nullptr);
        int dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
        if (dpi > 0) s = dpi / 96.0f;
        if (s < 0.8f) s = 0.8f;
    }
    auto S = [s](int v) { return (int)(v * s + 0.5f); };

    HFONT fBody = UiFont(9, false);

    const int pad = S(20);
    const int cw  = S(470) - pad * 2;

    // the about box carries exactly one thing: the repository address
    const int linkY = S(18);
    const int linkH = S(22);
    const int btnY  = linkY + linkH + S(18);
    const int btnH  = S(28);
    const int winH  = btnY + btnH + S(16);

    std::wstring title = std::wstring(T(S_APPNAME)) + L" v1.1.2";
    RECT rc = { 0, 0, S(470), winH };
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int wx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
    int wy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;

    g_aboutWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"RoachPetAbout", title.c_str(),
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                 wx, wy, ww, wh, g_setWnd, nullptr, g_hInst, nullptr);
    if (!g_aboutWnd) return;

    // --- repository link (SysLink: clickable, keyboard reachable)
    HWND link = CreateWindowExW(0, L"SysLink", T(S_ABOUT_LINK),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                pad, linkY, cw, linkH, g_aboutWnd, nullptr, g_hInst, nullptr);
    if (link) SendMessageW(link, WM_SETFONT, (WPARAM)fBody, TRUE);

    HWND ok = CreateWindowExW(0, L"BUTTON", T(S_BTNOK),
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                              S(470) - pad - S(88), btnY, S(88), btnH,
                              g_aboutWnd, (HMENU)IDOK, g_hInst, nullptr);
    if (ok) SendMessageW(ok, WM_SETFONT, (WPARAM)fBody, TRUE);

    if (g_setWnd && IsWindow(g_setWnd)) EnableWindow(g_setWnd, FALSE);
    ShowWindow(g_aboutWnd, SW_SHOW);
    SetForegroundWindow(g_aboutWnd);
    if (ok) SetFocus(ok);
}

void AppRestoreFiles() {
    if (MessageBoxW(g_setWnd ? g_setWnd : g_msgWnd, T(S_RESTORE_CONFIRM), T(S_MENU_RESTORE),
                    MB_YESNO | MB_ICONWARNING) != IDYES) return;
    std::wstring msg;
    g_desk.RestoreAll(&msg);
    MessageBoxW(g_setWnd ? g_setWnd : g_msgWnd, msg.c_str(), T(S_MENU_RESTORE), MB_OK | MB_ICONINFORMATION);
    if (hHiddenCnt) {
        wchar_t b[128];
        swprintf(b, 128, L"%ls%d", T(S_HIDDEN_CNT), g_desk.HiddenCount());
        SetWindowTextW(hHiddenCnt, b);
    }
}

void AppCleanPoop() {
    PoopClear();
    PoopPresent();
}

// =========================================================================
// hotkeys
// =========================================================================
static void RegisterHotkeys() {
    UnregisterHotKey(g_msgWnd, 1);
    UnregisterHotKey(g_msgWnd, 2);
    UnregisterHotKey(g_msgWnd, 3);
    RegisterHotKey(g_msgWnd, 1, g_cfg.hkToggleMod, g_cfg.hkToggleKey);
    RegisterHotKey(g_msgWnd, 2, g_cfg.hkSettingsMod, g_cfg.hkSettingsKey);
    RegisterHotKey(g_msgWnd, 3, g_cfg.hkQuitMod, g_cfg.hkQuitKey);
}

// =========================================================================
// settings window
// =========================================================================
static HFONT UiFont(int pt, bool bold) {
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    HDC dc = GetDC(nullptr);
    int dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    if (dpiY <= 0) dpiY = 96;
    lf.lfHeight = -(int)(pt * dpiY * g_uiScale / 72.0f + 0.5f);
    if (bold) lf.lfWeight = FW_SEMIBOLD;
    return CreateFontIndirectW(&lf);
}

static HWND Mk(const wchar_t* cls, const wchar_t* text, DWORD style,
               int x, int y, int w, int h, int id, DWORD exStyle = 0) {
    HWND c = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                             Sc(x), Sc(y), Sc(w), Sc(h), g_setWnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    if (c && g_uiFont) SendMessageW(c, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    return c;
}

// section headings are painted directly in WM_PAINT (see SetWndProc)

// BS_OWNERDRAW buttons ignore BM_SETCHECK/BM_GETCHECK entirely, so the
// checked state lives in GWLP_USERDATA; the checkbox repaints from there.
static void SetCheck(HWND h, bool v) {
    if (!h) return;
    SetWindowLongPtrW(h, GWLP_USERDATA, v ? 1 : 0);
    InvalidateRect(h, nullptr, TRUE);
}
static bool GetCheck(HWND h) { return h && GetWindowLongPtrW(h, GWLP_USERDATA) != 0; }

static int ComboSel(HWND h) { return (int)SendMessageW(h, CB_GETCURSEL, 0, 0); }
static void ComboSet(HWND h, int i) { SendMessageW(h, CB_SETCURSEL, i, 0); }

static void FillFpsCombo(HWND h) {
    const wchar_t* items[] = { L"10", L"15", L"24", L"30", L"45", L"60" };
    for (auto* s : items) SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)s);
    // the last entry means "follow the monitor refresh rate"
    SendMessageW(h, CB_INSERTSTRING, 6, (LPARAM)T(S_FPS_SYNC));
}
static int FpsToIndex(int fps) {
    const int v[] = { 10, 15, 24, 30, 45, 60 };
    if (fps == 0) return 6;
    int best = 2;
    for (int i = 0; i < 6; i++) if (v[i] == fps) best = i;
    return best;
}
static int IndexToFps(int i) {
    const int v[] = { 10, 15, 24, 30, 45, 60 };
    if (i == 6) return 0;               // 0 = follow the monitor
    if (i < 0 || i > 5) return 24;
    return v[i];
}

static void BuildSettingsUI();

// Paint the frame in the card's own palette so the title bar does not look
// bolted on. Every attribute is best-effort: older Windows just ignores it.
static void StyleCaption(HWND h) {
    typedef HRESULT (WINAPI *PFN)(HWND, DWORD, LPCVOID, DWORD);
    static PFN pSet = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        HMODULE m = LoadLibraryW(L"dwmapi.dll");
        if (m) pSet = (PFN)GetProcAddress(m, "DwmSetWindowAttribute");
    }
    if (!pSet) return;
    BOOL dark = FALSE;                          // DWMWA_USE_IMMERSIVE_DARK_MODE
    pSet(h, 20, &dark, sizeof(dark));
    COLORREF cap = kDesk, txt = kInk;           // DWMWA_CAPTION_COLOR / DWMWA_TEXT_COLOR
    pSet(h, 35, &cap, sizeof(cap));
    pSet(h, 36, &txt, sizeof(txt));
}

static LRESULT CALLBACK SetWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND:
        return 1;                       // WM_PAINT owns the whole client area

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);

        HDC     mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob  = SelectObject(mem, bmp);

        FillRectC(mem, rc, kDesk);

        RECT cr = { Sc(14), Sc(g_cardTop), rc.right - Sc(14), Sc(g_cardBot) };
        FillRoundRectG(mem, cr, Sc(10), kCard, kCardEdge);

        for (const auto& s : g_secs) {
            // short carapace marker beside the heading, like a specimen flag
            RECT mk = { cr.left + Sc(20), Sc(s.titleY) + Sc(5),
                        cr.left + Sc(23), Sc(s.titleY) + Sc(19) };
            FillRectC(mem, mk, kCarapace);

            RECT tr = { cr.left + Sc(31), Sc(s.titleY), cr.right - Sc(20), Sc(s.titleY) + Sc(22) };
            SetBkMode(mem, TRANSPARENT);
            SetTextColor(mem, kInk);
            HGDIOBJ of = SelectObject(mem, g_boldFont);
            DrawTextW(mem, s.title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(mem, of);

            if (s.ruleY > 0) {
                RECT rl = { cr.left + Sc(20), Sc(s.ruleY), cr.right - Sc(20), Sc(s.ruleY) + 1 };
                FillRectC(mem, rl, kRule);
            }
        }

        // hairline above the button row
        {
            int y = Sc(g_cardBot) + Sc(12);
            RECT rl = { Sc(14), y, rc.right - Sc(14), y + 1 };
            FillRectC(mem, rl, kCardEdge);
        }

        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC cdc = (HDC)w;
        SetBkMode(cdc, TRANSPARENT);
        SetTextColor(cdc, kBody);
        return (LRESULT)GetStockObject(HOLLOW_BRUSH);
    }

    case WM_CTLCOLOREDIT: {
        HDC cdc = (HDC)w;
        SetBkColor(cdc, kCard);
        SetTextColor(cdc, kBody);
        return (LRESULT)g_brCard;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)l;
        if (di->CtlType != ODT_BUTTON) break;
        wchar_t txt[160] = {0};
        GetWindowTextW(di->hwndItem, txt, 160);
        bool pressed = (di->itemState & ODS_SELECTED) != 0;
        bool focus   = (di->itemState & ODS_FOCUS) != 0;
        if (IsOwnerCheck(di->hwndItem)) {
            bool on = GetCheck(di->hwndItem);
            DrawOwnerCheck(di->hDC, di->rcItem, txt, on, pressed, focus);
        } else {
            DrawOwnerPush(di->hDC, di->rcItem, txt, di->CtlID == IDC_OK, pressed, focus);
        }
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(w);
        int code = HIWORD(w);
        HWND ctl = (HWND)l;
        // BS_OWNERDRAW checkboxes do not toggle themselves
        if (code == BN_CLICKED && IsOwnerCheck(ctl)) {
            SetCheck(ctl, !GetCheck(ctl));
        }
        if (code == BN_CLICKED && (id == IDC_HK_TOGGLE || id == IDC_HK_SETTINGS || id == IDC_HK_QUIT)) {
            g_hkCapture = ctl;
            SetWindowTextW(ctl, T(S_HK_PRESS));
            InvalidateRect(ctl, nullptr, TRUE);
            SetTimer(h, 2, 25, nullptr);
            return 0;
        }
        if (id == IDC_OK || id == IDC_APPLY) {
            // ---- read back
            int oldLang = g_cfg.lang;
            int oldMax = g_cfg.maxBugs;
            float oldScale = g_cfg.scale;

            g_cfg.fps          = IndexToFps(ComboSel(hFps));
            g_cfg.idleThrottle = GetCheck(hIdle);
            g_cfg.maxBugs      = GetDlgItemInt(h, IDC_MAXBUG, nullptr, FALSE);
            if (g_cfg.maxBugs < 3)  g_cfg.maxBugs = 3;
            if (g_cfg.maxBugs > 1000) g_cfg.maxBugs = 1000;
            SetDlgItemInt(h, IDC_MAXBUG, g_cfg.maxBugs, FALSE);

            const float scales[] = { 0.7f, 0.85f, 1.0f, 1.2f, 1.45f, 1.7f };
            int si = ComboSel(hScale);
            if (si >= 0 && si < 6) g_cfg.scale = scales[si];

            g_cfg.opacity     = GetDlgItemInt(h, IDC_OPACITY, nullptr, FALSE);
            if (g_cfg.opacity < 30)  g_cfg.opacity = 30;
            if (g_cfg.opacity > 100) g_cfg.opacity = 100;
            SetDlgItemInt(h, IDC_OPACITY, g_cfg.opacity, FALSE);
            g_cfg.starveWhenNoFood = GetCheck(hStarve);
            g_cfg.debugBg     = GetCheck(hDebugBg);
            g_cfg.lang        = ComboSel(hLang) == 1 ? 1 : 0;
            g_cfg.showTray    = GetCheck(hTray);
            g_cfg.clickThrough = GetCheck(hThrough);
            g_cfg.hoverFlee   = GetCheck(hHover);
            g_cfg.eatEnabled  = GetCheck(hEat);
            g_cfg.poopEnabled = GetCheck(hPoop);
            g_cfg.hideOnApp   = GetCheck(hHideOnApp);
            for (const auto& r : g_hkRows) {
                if (!r.changed) continue;
                UINT* mods[3] = { &g_cfg.hkToggleMod,   &g_cfg.hkSettingsMod,   &g_cfg.hkQuitMod };
                UINT* keys[3] = { &g_cfg.hkToggleKey,   &g_cfg.hkSettingsKey,   &g_cfg.hkQuitKey };
                *mods[r.which] = r.mod;
                *keys[r.which] = r.key;
            }
            g_cfg.fleeSpeed   = GetDlgItemInt(h, IDC_FLEESPD, nullptr, FALSE);
            if (g_cfg.fleeSpeed < 50)  g_cfg.fleeSpeed = 50;
            if (g_cfg.fleeSpeed > 300) g_cfg.fleeSpeed = 300;
            SetDlgItemInt(h, IDC_FLEESPD, g_cfg.fleeSpeed, FALSE);
            g_cfg.hideFullscreen = GetCheck(hFullscr);
            g_cfg.autostart   = GetCheck(hAutostart);
            g_cfg.monitor     = ComboSel(hMonitor);
            if (g_cfg.monitor < 0) g_cfg.monitor = 0;
            g_cfg.boundsMode  = ComboSel(hBounds) == 1 ? 1 : 0;

            ConfigSave();
            ApplyAutostart(g_cfg.autostart);
            RegisterHotkeys();
            AppRefreshTray();
            g_curBounds = ComputeBounds();
            ZOrderRefresh();
            PoopMarkDirty();
            for (auto* b : Bug::All()) b->refreshWindowStyle();

            if (g_cfg.maxBugs < oldMax) {
                auto& v = Bug::All();
                while ((int)v.size() > g_cfg.maxBugs) {
                    v.back()->kill();
                    v.pop_back();
                }
            } else if (g_cfg.maxBugs > oldMax) {
                // let the colony grow naturally
            }
            (void)oldScale;

            if (g_cfg.lang != oldLang) {
                DestroyWindow(h);
                g_setWnd = nullptr;
                BuildSettingsUI();
                return 0;
            }
            if (id == IDC_OK) { ShowWindow(h, SW_HIDE); BugSetTopmost(true); }
            return 0;
        }
        if (id == IDC_CANCEL) {
            ShowWindow(h, SW_HIDE);
            BugSetTopmost(true);
            return 0;
        }
        if (id == IDC_PAUSE && code == BN_CLICKED) {
            g_paused = GetCheck(hPause);
            return 0;
        }
        if (id == IDC_RESTORE)  { AppRestoreFiles(); return 0; }
        if (id == IDC_OPENLOG)  { OpenFolderOf(AppDataDir()); return 0; }
        if (id == IDC_LOGBTN)   { ShellExecuteW(nullptr, L"open", LogPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0; }
        if (id == IDC_OPENCFG)  { ShellExecuteW(nullptr, L"open", ConfigPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0; }
        if (id == IDC_ABOUT2)   { AppShowAbout(); return 0; }
        break;
    }
    case WM_TIMER:
        if (w == 2) HkScanCapture(h);
        return 0;

    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        BugSetTopmost(true);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 2);
        g_hkCapture = nullptr;
        g_setWnd = nullptr;
        BugSetTopmost(true);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void BuildSettingsUI() {
    static bool clsReg = false;
    static float forcedScale = 0.0f;
    if (!clsReg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SetWndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;    // WM_PAINT paints the desk
        wc.lpszClassName = L"RoachPetSettings";
        wc.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
        RegisterClassExW(&wc);
        clsReg = true;
    }

    {
        typedef UINT (WINAPI *PFN_DPI)(HWND);
        PFN_DPI gdw = (PFN_DPI)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        HWND ref = GetForegroundWindow();
        UINT dpi = (gdw && ref) ? gdw(ref) : 96;
        if (!dpi) dpi = 96;
        g_uiScale = forcedScale > 0 ? forcedScale : dpi / 96.0f;
        if (g_uiScale < 0.8f) g_uiScale = 0.8f;
    }

    if (g_uiFont)   { DeleteObject(g_uiFont);   g_uiFont = nullptr; }
    if (g_boldFont) { DeleteObject(g_boldFont); g_boldFont = nullptr; }
    if (g_smallFont){ DeleteObject(g_smallFont); g_smallFont = nullptr; }
    g_uiFont = UiFont(9, false);
    g_boldFont = UiFont(10, true);
    g_smallFont = UiFont(8, false);

    if (g_brCard) DeleteObject(g_brCard);
    if (g_brDesk) DeleteObject(g_brDesk);
    g_brCard = CreateSolidBrush(kCard);
    g_brDesk = CreateSolidBrush(kDesk);
    g_secs.clear();
    g_chks.clear();

    const int W = 660;
    RECT wa = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

    const DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    g_setWnd = CreateWindowExW(WS_EX_TOPMOST, L"RoachPetSettings", T(S_SET_TITLE),
                               kStyle, 0, 0, Sc(W), 300, nullptr, nullptr, g_hInst, nullptr);
    if (!g_setWnd) return;

    // card inset 14, content inset 20 inside the card
    const int C1 = 34, L1 = 146, W1 = 130;     // left column
    const int C2 = 352, L2 = 464, W2 = 160;    // right column

    auto lbl = [&](const wchar_t* t, int x, int yy, int w) {
        return Mk(L"STATIC", t, SS_LEFT, x, yy + 4, w, 18, 0);
    };
    auto chk = [&](const wchar_t* t, int x, int yy, int id) {
        HWND c = Mk(L"BUTTON", t, BS_OWNERDRAW | WS_TABSTOP, x, yy, 268, 24, id);
        if (c) g_chks.push_back(c);
        return c;
    };
    auto cmb = [&](int x, int yy, int w, int id) {
        return Mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, yy, w, 240, id);
    };
    auto hint = [&](const wchar_t* t, int x, int yy, int w, int hh = 18) {
        HWND s = Mk(L"STATIC", t, SS_LEFT, x, yy, w, hh, 0);
        if (s && g_smallFont) SendMessageW(s, WM_SETFONT, (WPARAM)g_smallFont, TRUE);
        return s;
    };
    // section headings are painted by WM_PAINT, not owned by a control
    auto section = [&](const wchar_t* t, int yy) {
        if (!g_secs.empty()) g_secs.back().ruleY = yy - 9;
        g_secs.push_back({ t, yy, 0 });
    };

    g_cardTop = 14;
    int y = g_cardTop + 12;

    // ---------- 性能 ----------
    section(T(S_SEC_PERF), y); y += 24;
    lbl(T(S_FPS), C1, y, L1 - C1 - 4);
    hFps = cmb(L1, y, W1, IDC_FPS);
    FillFpsCombo(hFps);
    ComboSet(hFps, FpsToIndex(g_cfg.fps));
    hIdle = chk(T(S_IDLE), C2, y, IDC_IDLE);
    SetCheck(hIdle, g_cfg.idleThrottle);
    y += 28;
    lbl(T(S_MAXBUG), C1, y, L1 - C1 - 4);
    hMaxBug = Mk(L"EDIT", L"", ES_NUMBER | WS_BORDER | WS_TABSTOP, L1, y, 56, 22, IDC_MAXBUG);
    SetWindowTextW(hMaxBug, std::to_wstring(g_cfg.maxBugs).c_str());
    SendMessageW(hMaxBug, EM_SETLIMITTEXT, 4, 0);
    {
        HWND sp = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT |
                                  UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                                  L1, y, 20, 22, g_setWnd, (HMENU)(INT_PTR)(IDC_MAXBUG + 100), g_hInst, nullptr);
        SendMessageW(sp, UDM_SETBUDDY, (WPARAM)hMaxBug, 0);
        SendMessageW(sp, UDM_SETRANGE32, 3, 1000);
    }
    hPause = chk(T(S_PAUSE), C2, y, IDC_PAUSE);
    SetCheck(hPause, g_paused);
    y += 34;

    // ---------- 外观 ----------
    section(T(S_SEC_LOOK), y); y += 24;
    lbl(T(S_OPACITY), C1, y, L1 - C1 - 4);
    // typed like the other numeric fields: 30..100, no trackbar
    hOpacity = Mk(L"EDIT", L"", ES_NUMBER | WS_BORDER | WS_TABSTOP, L1, y, 56, 22, IDC_OPACITY);
    SetWindowTextW(hOpacity, std::to_wstring(g_cfg.opacity).c_str());
    SendMessageW(hOpacity, EM_SETLIMITTEXT, 3, 0);
    Mk(L"STATIC", L"%", SS_LEFT, L1 + 62, y + 4, 16, 18, 0);
    hDebugBg = chk(T(S_DEBUGBG), C2, y, IDC_DEBUGBG);
    SetCheck(hDebugBg, g_cfg.debugBg);
    y += 28;
    lbl(T(S_SCALE), C1, y, L1 - C1 - 4);
    hScale = cmb(L1, y, W1, IDC_SCALE);
    {
        const wchar_t* s[] = { L"70%", L"85%", L"100%", L"120%", L"145%", L"170%" };
        for (auto* t : s) SendMessageW(hScale, CB_ADDSTRING, 0, (LPARAM)t);
        const float sc[] = { 0.7f, 0.85f, 1.0f, 1.2f, 1.45f, 1.7f };
        int bi = 2; float bd = 9;
        for (int i = 0; i < 6; i++) { float d = fabsf(sc[i] - g_cfg.scale); if (d < bd) { bd = d; bi = i; } }
        ComboSet(hScale, bi);
    }
    hTray = chk(T(S_TRAY), C2, y, IDC_TRAY);
    SetCheck(hTray, g_cfg.showTray);
    y += 28;
    lbl(T(S_LANG), C1, y, L1 - C1 - 4);
    hLang = cmb(L1, y, W1, IDC_LANG);
    SendMessageW(hLang, CB_ADDSTRING, 0, (LPARAM)T(S_LANG_ZH));
    SendMessageW(hLang, CB_ADDSTRING, 0, (LPARAM)T(S_LANG_EN));
    ComboSet(hLang, g_cfg.lang);
    y += 34;

    // ---------- 行为 ----------
    section(T(S_SEC_BEHAVE), y); y += 24;
    hThrough = chk(T(S_THROUGH), C1, y, IDC_THROUGH);
    SetCheck(hThrough, g_cfg.clickThrough);
    hHover = chk(T(S_HOVER), C2, y, IDC_HOVER);
    SetCheck(hHover, g_cfg.hoverFlee);
    y += 28;
    hEat = chk(T(S_EAT), C1, y, IDC_EAT);
    SetCheck(hEat, g_cfg.eatEnabled);
    hPoop = chk(T(S_POOP), C2, y, IDC_POOP);
    SetCheck(hPoop, g_cfg.poopEnabled);
    y += 28;
    hFullscr = chk(T(S_FULLSCR), C1, y, IDC_FULLSCR);
    SetCheck(hFullscr, g_cfg.hideFullscreen);
    hAutostart = chk(T(S_AUTOSTART), C2, y, IDC_AUTOSTART);
    SetCheck(hAutostart, g_cfg.autostart);
    y += 28;
    hHideOnApp = chk(T(S_HIDEONAPP), C1, y, IDC_HIDEONAPP);
    SetCheck(hHideOnApp, g_cfg.hideOnApp);
    hint(T(S_HIDEONAPP_TIP), C2, y + 3, 626 - C2);
    y += 28;
    lbl(T(S_FLEESPD), C1, y, L1 - C1 - 4);
    // a trackbar dragged badly on the self-drawn card, so the value is typed
    hFleeSpd = Mk(L"EDIT", L"", ES_NUMBER | WS_BORDER | WS_TABSTOP, L1, y, 56, 22, IDC_FLEESPD);
    SetWindowTextW(hFleeSpd, std::to_wstring(g_cfg.fleeSpeed).c_str());
    SendMessageW(hFleeSpd, EM_SETLIMITTEXT, 3, 0);
    hint(T(S_FLEESPD_TIP), C2, y + 3, 626 - C2);
    y += 28;
    hStarve = chk(T(S_STARVE), C1, y, IDC_STARVE);
    SetCheck(hStarve, g_cfg.starveWhenNoFood);
    y += 34;

    // ---------- 显示 ----------
    section(T(S_SEC_DISPLAY), y); y += 24;
    lbl(T(S_MONITOR), C1, y, L1 - C1 - 4);
    hMonitor = cmb(L1, y, W1, IDC_MONITOR);
    SendMessageW(hMonitor, CB_ADDSTRING, 0, (LPARAM)T(S_MON_ALL));
    RefreshMonitors();
    for (int i = 0; i < (int)g_mons.size(); i++) {
        wchar_t b[64]; swprintf(b, 64, L"#%d  %dx%d", i + 1,
                                g_mons[i].full.right - g_mons[i].full.left,
                                g_mons[i].full.bottom - g_mons[i].full.top);
        SendMessageW(hMonitor, CB_ADDSTRING, 0, (LPARAM)b);
    }
    ComboSet(hMonitor, g_cfg.monitor <= (int)g_mons.size() ? g_cfg.monitor : 0);
    lbl(T(S_BOUNDS), C2, y, L2 - C2 - 6);
    hBounds = cmb(L2, y, W2, IDC_BOUNDS);
    SendMessageW(hBounds, CB_ADDSTRING, 0, (LPARAM)T(S_BOUND_WORK));
    SendMessageW(hBounds, CB_ADDSTRING, 0, (LPARAM)T(S_BOUND_FULL));
    ComboSet(hBounds, g_cfg.boundsMode ? 1 : 0);
    y += 34;

    // ---------- 文件 ----------
    section(T(S_SEC_FILE), y); y += 24;
    {
        wchar_t b[160];
        swprintf(b, 160, L"%ls%d", T(S_HIDDEN_CNT), g_desk.HiddenCount());
        hHiddenCnt = Mk(L"STATIC", b, SS_LEFT, C1, y + 5, 190, 18, IDC_HIDDENCNT);
    }
    hRestore = Mk(L"BUTTON", T(S_RESTORE_ALL), BS_OWNERDRAW | WS_TABSTOP, 232, y, 110, 26, IDC_RESTORE);
    hOpenLog = Mk(L"BUTTON", T(S_OPENLOG), BS_OWNERDRAW | WS_TABSTOP, 348, y, 110, 26, IDC_OPENLOG);
    hOpenCfg = Mk(L"BUTTON", T(S_OPENCFG), BS_OWNERDRAW | WS_TABSTOP, 464, y, 110, 26, IDC_OPENCFG);
    y += 28;
    hint(T(S_EATEN_TIP), C1, y, 626 - C1, g_cfg.lang ? 34 : 18);
    y += g_cfg.lang ? 40 : 24;

    // ---------- 快捷键 ----------
    section(T(S_SEC_HOTKEY), y); y += 24;
    // three capture buttons: click one, then press the combination you want
    for (auto& r : g_hkRows) r = HkRow{};
    int hkIdx = 0;
    auto hkRow = [&](const wchar_t* label, int id, int which, UINT mod, UINT key) {
        lbl(label, C1, y, L1 - C1 - 4);
        wchar_t buf[200]; HkText(mod, key, buf, 200);
        HWND b = Mk(L"BUTTON", buf, BS_OWNERDRAW | WS_TABSTOP, L1, y, W1, 24, id);
        if (b) g_hkRows[hkIdx] = { b, mod, key, false, which };
        y += 26;
        hkIdx++;
    };
    hkRow(T(S_HK_TOGGLE),   IDC_HK_TOGGLE,   0, g_cfg.hkToggleMod,   (UINT)g_cfg.hkToggleKey);
    hkRow(T(S_HK_SETTINGS), IDC_HK_SETTINGS, 1, g_cfg.hkSettingsMod, (UINT)g_cfg.hkSettingsKey);
    hkRow(T(S_HK_QUIT),     IDC_HK_QUIT,     2, g_cfg.hkQuitMod,     (UINT)g_cfg.hkQuitKey);
    y += 8;

    // ---------- 其它 ----------
    section(T(S_SEC_MISC), y); y += 24;
    hLogBtn   = Mk(L"BUTTON", T(S_OPENLOGFILE), BS_OWNERDRAW | WS_TABSTOP, C1, y, 110, 26, IDC_LOGBTN);
    hAbout2   = Mk(L"BUTTON", T(S_ABOUT), BS_OWNERDRAW | WS_TABSTOP, C1 + 116, y, 110, 26, IDC_ABOUT2);
    y += 34;

    // ---------- 底部按钮 ----------
    // the card ends here; the button row sits on the desk below a hairline
    g_cardBot = y + 2;
    const int clientH = g_cardBot + 12 + 12 + 28 + 12;
    const int by = clientH - 12 - 28;
    hOK     = Mk(L"BUTTON", T(S_BTNOK), BS_OWNERDRAW | WS_TABSTOP, W - 310, by, 92, 28, IDC_OK);
    hCancel = Mk(L"BUTTON", T(S_BTNCANCEL), BS_OWNERDRAW | WS_TABSTOP, W - 208, by, 92, 28, IDC_CANCEL);
    hApply  = Mk(L"BUTTON", T(S_APPLY), BS_OWNERDRAW | WS_TABSTOP, W - 106, by, 92, 28, IDC_APPLY);

    // ---------- 尺寸与居中 ----------
    RECT rc = { 0, 0, Sc(W), Sc(clientH) };
    AdjustWindowRectEx(&rc, kStyle, FALSE, WS_EX_TOPMOST);
    int outW = rc.right - rc.left, outH = rc.bottom - rc.top;
    int sx = wa.left + ((wa.right - wa.left) - outW) / 2;
    int sy = wa.top + ((wa.bottom - wa.top) - outH) / 2;
    if (sx < wa.left) sx = wa.left;
    if (sy < wa.top)  sy = wa.top;

    // high-DPI screens are not always tall enough for the whole sheet: shrink
    // the layout once instead of letting the buttons fall off the bottom
    int availH = (wa.bottom - wa.top) - 16;
    if (outH > availH && forcedScale <= 0.0f) {
        forcedScale = g_uiScale * ((float)availH / (float)outH) * 0.99f;
        DestroyWindow(g_setWnd);
        g_setWnd = nullptr;
        BuildSettingsUI();
        return;
    }
    // The very first ShowWindow() in a process may be overridden by the
    // STARTUPINFO the parent passed in, so force a normal state afterwards.
    ShowWindow(g_setWnd, SW_SHOWNORMAL);
    if (IsIconic(g_setWnd)) ShowWindow(g_setWnd, SW_RESTORE);
    SetWindowPos(g_setWnd, HWND_TOPMOST, sx, sy, outW, outH, SWP_SHOWWINDOW);
    StyleCaption(g_setWnd);
    SetForegroundWindow(g_setWnd);
    UpdateWindow(g_setWnd);
    BugSetTopmost(false);      // keep the pets below the dialog
}

void AppOpenSettings() {
    if (g_setWnd && IsWindow(g_setWnd)) {
        ShowWindow(g_setWnd, SW_SHOW);
        SetForegroundWindow(g_setWnd);
        BugSetTopmost(false);
        return;
    }
    BuildSettingsUI();
}

// =========================================================================
// message window
// =========================================================================
static LRESULT CALLBACK MsgWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TRAY:
        if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_CONTEXTMENU) {
            ShowTrayMenu();
        } else if (LOWORD(l) == WM_LBUTTONDBLCLK) {
            AppOpenSettings();
        }
        return 0;
    case WM_HOTKEY:
        if (w == 1) AppToggleVisible();
        else if (w == 2) AppOpenSettings();
        else if (w == 3) AppQuit();
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(w);
        switch (id) {
        case 1: AppToggleVisible(); break;
        case 2: AppOpenSettings(); break;
        case 3: g_paused = !g_paused; break;
        case 4: AppCleanPoop(); break;
        case 5: AppRestoreFiles(); break;
        case 6: ShellExecuteW(nullptr, L"open", ConfigPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case 7: AppShowAbout(); break;
        case 8: AppQuit(); break;
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    if (m == g_taskbarCreated && g_taskbarCreated) {
        g_trayAdded = false;
        TrayAdd();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// =========================================================================
// main loop
// =========================================================================
static HANDLE g_timer = nullptr;

static void InitPacer() {
    g_timer = CreateWaitableTimerExW(nullptr, nullptr, 2 /*HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
    if (!g_timer) {
        timeBeginPeriod(1);
        g_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
}

static int  g_pollAcc = 0;
static int  g_fsAcc = 0;
static int  g_statAcc = 0;
static int  g_occAcc = 0;
static bool g_fsHidden = false;
static bool g_perfLog = false;      // ROACH_PERF_LOG=1 -> log frame stalls

static double MsSince(LARGE_INTEGER a, LARGE_INTEGER b, LARGE_INTEGER f) {
    return (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
}

int RunApp() {
    LogLine(L"run: refresh monitors");
    RefreshMonitors();
    g_curBounds = ComputeBounds();

    LogLine(L"run: register msg class");
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MsgWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = kMsgClass;
    RegisterClassExW(&wc);
    g_msgWnd = CreateWindowExW(0, kMsgClass, L"RoachPetMsg", 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, g_hInst, nullptr);

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (!g_trayIcon) g_trayIcon = R::MakeTrayIcon(GetSystemMetrics(SM_CXSMICON));

    TrayAdd();
    RegisterHotkeys();

    LogLine(L"run: bug global init");
    Bug::GlobalInit();
    LogLine(L"run: desk poll");
    g_desk.Invalidate();
    g_desk.Poll();
    LogLine(L"run: z-order scan");
    ZOrderRefresh();
    LogLine(L"run: restore population");
    BugRestorePopulation();
    LogLine(L"run: population=%d", (int)Bug::All().size());
    if (g_wantSettings) AppOpenSettings();
    LogLine(L"run: init pacer");

    InitPacer();
    g_perfLog = getenv("ROACH_PERF_LOG") != nullptr;
    LogLine(L"run: loop enter");

    LARGE_INTEGER freq, prev;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    int idleFrames = 0;

    while (!g_quitting) {
        // ---- pump
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_quitting = true; break; }
            if (g_setWnd && IsWindow(g_setWnd) && IsDialogMessageW(g_setWnd, &msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_quitting) break;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - prev.QuadPart) / (double)freq.QuadPart);
        prev = now;
        if (dt > 0.25f) dt = 0.25f;
        if (dt < 0.0005f) dt = 0.0005f;
        if (g_perfLog && dt > 0.10f) LogLine(L"perf: frame %.0f ms", dt * 1000.0f);

        // ---- desktop poll (~every 600 ms)
        g_pollAcc += (int)(dt * 1000);
        if (g_pollAcc >= 600) {
            g_pollAcc = 0;
            LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
            bool changed = g_desk.Poll();
            QueryPerformanceCounter(&b);
            if (g_perfLog) {
                double ms = MsSince(a, b, freq);
                if (ms > 40.0) LogLine(L"perf: desk poll %.0f ms", ms);
            }
            if (changed) {
                LogLine(L"desktop changed: %d icons", (int)g_desk.icons().size());
            }
        }

        // ---- what is lying on top of the desktop right now (~every 180 ms)
        g_occAcc += (int)(dt * 1000);
        if (g_occAcc >= 180) {
            g_occAcc = 0;
            if (g_cfg.hideOnApp) {
                LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
                ZOrderRefresh();
                QueryPerformanceCounter(&b);
                if (g_perfLog) {
                    double ms = MsSince(a, b, freq);
                    if (ms > 40.0) LogLine(L"perf: zorder %.0f ms", ms);
                }
            }
        }

        // ---- periodic colony statistics
        g_statAcc += (int)(dt * 1000);
        if (g_statAcc >= 5000) {
            g_statAcc = 0;
            BugLogStats();
        }

        // ---- fullscreen check (~every 900 ms)
        g_fsAcc += (int)(dt * 1000);
        if (g_fsAcc >= 900) {
            g_fsAcc = 0;
            if (g_cfg.hideFullscreen) {
                bool fs = IsFullscreenForeground();
                if (fs && !g_fsHidden) {
                    g_fsHidden = true;
                    g_visible = false;
                    extern void BugForceShow(bool);
                    BugForceShow(false);
                } else if (!fs && g_fsHidden) {
                    g_fsHidden = false;
                    g_visible = true;
                    extern void BugForceShow(bool);
                    BugForceShow(true);
                }
            } else if (g_fsHidden) {
                g_fsHidden = false;
                g_visible = true;
                extern void BugForceShow(bool);
                BugForceShow(true);
            }
            RECT nb = ComputeBounds();
            if (nb.left != g_curBounds.left || nb.top != g_curBounds.top ||
                nb.right != g_curBounds.right || nb.bottom != g_curBounds.bottom) {
                g_curBounds = nb;
                for (auto* b : Bug::All()) b->setBounds(nb);
            }
        }

        // ---- simulation + paint
        if (!g_paused) {
            LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
            BugTickAll(dt, g_curBounds);
            if (g_visible) {
                BugPresentAll();
                PoopPresent();
            }
            QueryPerformanceCounter(&b);
            if (g_perfLog) {
                double ms = MsSince(a, b, freq);
                if (ms > 60.0) LogLine(L"perf: paint %.0f ms", ms);
            }
        }

        // ---- frame pacing / idle throttling
        int target = g_cfg.fps;
        if (target == 0) {
            // follow the monitor refresh rate, resolved once
            static int hz = 0;
            if (!hz) {
                DEVMODEW dm = { sizeof(dm) };
                EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm);
                hz = (int)dm.dmDisplayFrequency;
                if (hz < 30) hz = 60;
            }
            target = hz;
        }
        if (g_cfg.idleThrottle) {
            bool busy = false;
            for (auto* b : Bug::All()) {
                if (b->state() != BState::Hidden) { busy = true; break; }
            }
            if (!busy || !g_visible) target = 8;
        }
        if (target < 5) target = 5;

        double frameMs = 1000.0 / target;
        static double acc = 0;
        acc += dt * 1000.0;
        double wait = frameMs - acc;
        if (wait > 0.5) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)(wait * 10000.0);
            SetWaitableTimer(g_timer, &due, 0, nullptr, nullptr, FALSE);
            MsgWaitForMultipleObjectsEx(1, &g_timer, 120, QS_ALLINPUT, 0);
            acc = 0;
        } else {
            acc = 0;
        }
    }

    // ---- shutdown
    UnregisterHotKey(g_msgWnd, 1);
    UnregisterHotKey(g_msgWnd, 2);
    UnregisterHotKey(g_msgWnd, 3);
    TrayRemove();
    PoopDestroy();
    BugDestroyAll();
    Bug::UnregisterWndClass();
    if (g_timer) CloseHandle(g_timer);
    if (g_trayIcon) DestroyIcon(g_trayIcon);
    return 0;
}

// =========================================================================
// WinMain
// =========================================================================
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    void* addr = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    LogLine(L"!! CRASH code=0x%08lX addr=%p", (unsigned long)code, addr);
    return EXCEPTION_EXECUTE_HANDLER;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int) {
    g_hInst = hInst;
    SetUnhandledExceptionFilter(CrashFilter);

    g_wantSettings = false;
    bool wantRestore = false, quiet = false;
    {
        std::wstring cmd = lpCmdLine ? lpCmdLine : L"";
        if (cmd.find(L"--settings") != std::wstring::npos) g_wantSettings = true;
        if (cmd.find(L"--restore")  != std::wstring::npos) wantRestore = true;
        if (cmd.find(L"--quiet")    != std::wstring::npos) quiet = true;
    }

    // recovery path: put every eaten file back without touching the UI
    if (wantRestore) {
        Gdiplus::GdiplusStartupInput gi0;
        ULONG_PTR tok0 = 0;
        Gdiplus::GdiplusStartup(&tok0, &gi0, nullptr);
        ConfigLoad();
        std::wstring msg;
        int n = g_desk.RestoreAll(&msg);
        LogLine(L"restore-cli: %ls (%d)", msg.c_str(), n);
        if (!quiet) MessageBoxW(nullptr, msg.c_str(), L"RoachPet", MB_OK | MB_ICONINFORMATION);
        Gdiplus::GdiplusShutdown(tok0);
        return 0;
    }

    // single instance
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Global\\RoachPet_SingleInstance");
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(kMsgClass, nullptr);
        if (prev) PostMessageW(prev, WM_COMMAND, 2, 0);
        CloseHandle(mtx);
        return 0;
    }

    EnablePerMonitorDpi();

    Gdiplus::GdiplusStartupInput gi;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gi, nullptr);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES |
                                              ICC_UPDOWN_CLASS | ICC_LINK_CLASS |
                                              ICC_HOTKEY_CLASS };
    InitCommonControlsEx(&icc);

    ConfigLoad();
    if (g_cfg.lowPriority) SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);

    // make sure the config dir + log exist
    AppDataDir();
    LogLine(L"---- start v1.1.2 ----");

    int rc = RunApp();
    (void)0;

    if (mtx) CloseHandle(mtx);
    Gdiplus::GdiplusShutdown(gdipToken);
    return rc;
}
