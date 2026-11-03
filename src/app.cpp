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
    IDC_OPENLOG, IDC_OPENCFG, IDC_CHECKUPD, IDC_REPO, IDC_ABOUT2,
    IDC_OK, IDC_CANCEL, IDC_APPLY, IDC_OPACITYLBL, IDC_PAUSE, IDC_LOGBTN,
    IDC_HIDEONAPP
};

static HWND hFps, hIdle, hMaxBug, hScale, hOpacity, hDebugBg, hLang, hTray,
            hThrough, hHover, hEat, hPoop, hFullscr, hAutostart, hMonitor,
            hBounds, hHiddenCnt, hRestore, hOpenLog, hOpenCfg, hCheckUpd,
            hRepo, hAbout2, hOK, hCancel, hApply, hOpacityLbl, hPause, hLogBtn,
            hHideOnApp;
static HFONT g_uiFont = nullptr;
static HFONT g_boldFont = nullptr;
static HFONT g_smallFont = nullptr;
// The dialog is laid out in 96-dpi units and scaled to the real monitor DPI,
// otherwise the controls grow with the font but the columns do not.
static float g_uiScale = 1.0f;
static int   Sc(int v) { return (int)(v * g_uiScale + 0.5f); }

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

void AppShowAbout() {
    MessageBoxW(g_setWnd ? g_setWnd : g_msgWnd, T(S_ABOUT_TEXT), T(S_ABOUT),
                MB_OK | MB_ICONINFORMATION);
}

void AppCheckUpdate() {
    std::wstring u = AppDataDir() + L"\\update_url.txt";
    if (GetFileAttributesW(u.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ShellExecuteW(nullptr, L"open", u.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    MessageBoxW(g_setWnd ? g_setWnd : g_msgWnd,
                L"v1.1.0\n\n当前为本地构建，未配置远程更新源。\n如需启用，请在配置目录放置 update_url.txt。\n\n"
                L"v1.1.0\n\nLocal build, no remote update source configured.\n"
                L"Drop an update_url.txt into the config folder to enable it.",
                T(S_CHECKUPD), MB_OK | MB_ICONINFORMATION);
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

static void SectionLabel(const wchar_t* text, int x, int y, int w) {
    HWND h = Mk(L"STATIC", text, SS_LEFT, x, y, w, 20, 0);
    if (h && g_boldFont) SendMessageW(h, WM_SETFONT, (WPARAM)g_boldFont, TRUE);
}

static void SetCheck(HWND h, bool v) { SendMessageW(h, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0); }
static bool GetCheck(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }

static int ComboSel(HWND h) { return (int)SendMessageW(h, CB_GETCURSEL, 0, 0); }
static void ComboSet(HWND h, int i) { SendMessageW(h, CB_SETCURSEL, i, 0); }

static void FillFpsCombo(HWND h) {
    const wchar_t* items[] = { L"10", L"15", L"24", L"30", L"45", L"60" };
    for (auto* s : items) SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)s);
}
static int FpsToIndex(int fps) {
    const int v[] = { 10, 15, 24, 30, 45, 60 };
    int best = 2;
    for (int i = 0; i < 6; i++) if (v[i] == fps) best = i;
    return best;
}
static int IndexToFps(int i) {
    const int v[] = { 10, 15, 24, 30, 45, 60 };
    if (i < 0 || i > 5) return 24;
    return v[i];
}

static void BuildSettingsUI();

static LRESULT CALLBACK SetWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w);
        int code = HIWORD(w);
        if (id == IDC_OPACITY && code == 0) {
            int v = (int)SendMessageW(hOpacity, TBM_GETPOS, 0, 0);
            wchar_t b[32]; swprintf(b, 32, L"%d%%", v);
            SetWindowTextW(hOpacityLbl, b);
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
            if (g_cfg.maxBugs > 80) g_cfg.maxBugs = 80;
            SetDlgItemInt(h, IDC_MAXBUG, g_cfg.maxBugs, FALSE);

            const float scales[] = { 0.7f, 0.85f, 1.0f, 1.2f, 1.45f, 1.7f };
            int si = ComboSel(hScale);
            if (si >= 0 && si < 6) g_cfg.scale = scales[si];

            g_cfg.opacity     = (int)SendMessageW(hOpacity, TBM_GETPOS, 0, 0);
            g_cfg.debugBg     = GetCheck(hDebugBg);
            g_cfg.lang        = ComboSel(hLang) == 1 ? 1 : 0;
            g_cfg.showTray    = GetCheck(hTray);
            g_cfg.clickThrough = GetCheck(hThrough);
            g_cfg.hoverFlee   = GetCheck(hHover);
            g_cfg.eatEnabled  = GetCheck(hEat);
            g_cfg.poopEnabled = GetCheck(hPoop);
            g_cfg.hideOnApp   = GetCheck(hHideOnApp);
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
        if (id == IDC_CHECKUPD) { AppCheckUpdate(); return 0; }
        if (id == IDC_REPO) {
            wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
            std::wstring dir = exe;
            size_t p = dir.find_last_of(L"\\/");
            if (p != std::wstring::npos) dir = dir.substr(0, p);
            OpenFolderOf(dir);
            return 0;
        }
        if (id == IDC_ABOUT2)   { AppShowAbout(); return 0; }
        break;
    }
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        BugSetTopmost(true);
        return 0;
    case WM_DESTROY:
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
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"RoachPetSettings";
        wc.hIcon = g_trayIcon ? g_trayIcon : LoadIcon(nullptr, IDI_APPLICATION);
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

    const int W = 660;
    RECT wa = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

    const DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    g_setWnd = CreateWindowExW(WS_EX_TOPMOST, L"RoachPetSettings", T(S_SET_TITLE),
                               kStyle, 0, 0, Sc(W), 300, nullptr, nullptr, g_hInst, nullptr);
    if (!g_setWnd) return;

    const int C1 = 16, L1 = 118, W1 = 132;     // left column
    const int C2 = 340, L2 = 452, W2 = 160;    // right column

    auto lbl = [&](const wchar_t* t, int x, int yy, int w) {
        return Mk(L"STATIC", t, SS_LEFT, x, yy + 4, w, 18, 0);
    };
    auto chk = [&](const wchar_t* t, int x, int yy, int id) {
        return Mk(L"BUTTON", t, BS_AUTOCHECKBOX | WS_TABSTOP, x, yy, 280, 22, id);
    };
    auto cmb = [&](int x, int yy, int w, int id) {
        return Mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, yy, w, 240, id);
    };

    int y = 10;

    // ---------- 性能 ----------
    SectionLabel(T(S_SEC_PERF), C1, y, 300); y += 22;
    lbl(T(S_FPS), C1, y, L1 - C1 - 4);
    hFps = cmb(L1, y, W1, IDC_FPS);
    FillFpsCombo(hFps);
    ComboSet(hFps, FpsToIndex(g_cfg.fps));
    hIdle = chk(T(S_IDLE), C2, y, IDC_IDLE);
    SetCheck(hIdle, g_cfg.idleThrottle);
    y += 30;
    lbl(T(S_MAXBUG), C1, y, L1 - C1 - 4);
    hMaxBug = Mk(L"EDIT", L"", ES_NUMBER | WS_BORDER | WS_TABSTOP, L1, y, 56, 22, IDC_MAXBUG);
    SetWindowTextW(hMaxBug, std::to_wstring(g_cfg.maxBugs).c_str());
    SendMessageW(hMaxBug, EM_SETLIMITTEXT, 2, 0);
    {
        HWND sp = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT |
                                  UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                                  L1, y, 20, 22, g_setWnd, (HMENU)(INT_PTR)(IDC_MAXBUG + 100), g_hInst, nullptr);
        SendMessageW(sp, UDM_SETBUDDY, (WPARAM)hMaxBug, 0);
        SendMessageW(sp, UDM_SETRANGE32, 3, 80);
    }
    hPause = chk(T(S_PAUSE), C2, y, IDC_PAUSE);
    SetCheck(hPause, g_paused);
    y += 34;

    // ---------- 外观 ----------
    SectionLabel(T(S_SEC_LOOK), C1, y, 300); y += 22;
    lbl(T(S_OPACITY), C1, y, L1 - C1 - 4);
    hOpacity = Mk(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, L1, y, W1, 24, IDC_OPACITY);
    SendMessageW(hOpacity, TBM_SETRANGE, TRUE, MAKELONG(30, 100));
    SendMessageW(hOpacity, TBM_SETPOS, TRUE, g_cfg.opacity);
    hOpacityLbl = Mk(L"STATIC", L"", SS_LEFT, L1 + W1 + 6, y + 5, 44, 18, IDC_OPACITYLBL);
    {
        wchar_t b[32]; swprintf(b, 32, L"%d%%", g_cfg.opacity);
        SetWindowTextW(hOpacityLbl, b);
    }
    hDebugBg = chk(T(S_DEBUGBG), C2, y, IDC_DEBUGBG);
    SetCheck(hDebugBg, g_cfg.debugBg);
    y += 34;
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
    y += 30;
    lbl(T(S_LANG), C1, y, L1 - C1 - 4);
    hLang = cmb(L1, y, W1, IDC_LANG);
    SendMessageW(hLang, CB_ADDSTRING, 0, (LPARAM)T(S_LANG_ZH));
    SendMessageW(hLang, CB_ADDSTRING, 0, (LPARAM)T(S_LANG_EN));
    ComboSet(hLang, g_cfg.lang);
    y += 34;

    // ---------- 行为 ----------
    SectionLabel(T(S_SEC_BEHAVE), C1, y, 300); y += 22;
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
    {
        HWND t = Mk(L"STATIC", T(S_HIDEONAPP_TIP), SS_LEFT, C2, y + 3, W - C2 - C1, 18, 0);
        if (t && g_smallFont) SendMessageW(t, WM_SETFONT, (WPARAM)g_smallFont, TRUE);
    }
    y += 32;

    // ---------- 显示 ----------
    SectionLabel(T(S_SEC_DISPLAY), C1, y, 300); y += 22;
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
    SectionLabel(T(S_SEC_FILE), C1, y, 300); y += 22;
    {
        wchar_t b[160];
        swprintf(b, 160, L"%ls%d", T(S_HIDDEN_CNT), g_desk.HiddenCount());
        hHiddenCnt = Mk(L"STATIC", b, SS_LEFT, C1, y + 5, 190, 18, IDC_HIDDENCNT);
    }
    hRestore = Mk(L"BUTTON", T(S_RESTORE_ALL), BS_PUSHBUTTON | WS_TABSTOP, 214, y, 104, 26, IDC_RESTORE);
    hOpenLog = Mk(L"BUTTON", T(S_OPENLOG), BS_PUSHBUTTON | WS_TABSTOP, 326, y, 104, 26, IDC_OPENLOG);
    hOpenCfg = Mk(L"BUTTON", T(S_OPENCFG), BS_PUSHBUTTON | WS_TABSTOP, 438, y, 104, 26, IDC_OPENCFG);
    y += 32;
    {
        HWND t = Mk(L"STATIC", T(S_EATEN_TIP), SS_LEFT, C1, y, W - C1 * 2, 18, 0);
        if (t && g_smallFont) SendMessageW(t, WM_SETFONT, (WPARAM)g_smallFont, TRUE);
    }
    y += 26;

    // ---------- 快捷键 ----------
    SectionLabel(T(S_SEC_HOTKEY), C1, y, 300); y += 22;
    Mk(L"STATIC", T(S_HK_TIP), SS_LEFT, C1, y, W - C1 * 2, 18, 0);
    y += 26;

    // ---------- 其它 ----------
    SectionLabel(T(S_SEC_MISC), C1, y, 300); y += 22;
    hCheckUpd = Mk(L"BUTTON", T(S_CHECKUPD), BS_PUSHBUTTON | WS_TABSTOP, C1, y, 104, 26, IDC_CHECKUPD);
    hLogBtn   = Mk(L"BUTTON", T(S_OPENLOGFILE), BS_PUSHBUTTON | WS_TABSTOP, C1 + 112, y, 104, 26, IDC_LOGBTN);
    hRepo     = Mk(L"BUTTON", T(S_REPO), BS_PUSHBUTTON | WS_TABSTOP, C1 + 224, y, 104, 26, IDC_REPO);
    hAbout2   = Mk(L"BUTTON", T(S_ABOUT), BS_PUSHBUTTON | WS_TABSTOP, C1 + 336, y, 104, 26, IDC_ABOUT2);
    y += 34;
    {
        HWND t = Mk(L"STATIC", T(S_VER), SS_LEFT, C1, y, W - C1 * 2, 16, 0);
        if (t && g_smallFont) SendMessageW(t, WM_SETFONT, (WPARAM)g_smallFont, TRUE);
    }
    y += 24;

    // ---------- 底部按钮 ----------
    const int clientH = y + 46;
    hOK     = Mk(L"BUTTON", T(S_BTNOK), BS_DEFPUSHBUTTON | WS_TABSTOP, W - 312, clientH - 38, 90, 28, IDC_OK);
    hCancel = Mk(L"BUTTON", T(S_BTNCANCEL), BS_PUSHBUTTON | WS_TABSTOP, W - 212, clientH - 38, 90, 28, IDC_CANCEL);
    hApply  = Mk(L"BUTTON", T(S_APPLY), BS_PUSHBUTTON | WS_TABSTOP, W - 112, clientH - 38, 90, 28, IDC_APPLY);

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

        // ---- desktop poll (~every 600 ms)
        g_pollAcc += (int)(dt * 1000);
        if (g_pollAcc >= 600) {
            g_pollAcc = 0;
            if (g_desk.Poll()) {
                LogLine(L"desktop changed: %d icons", (int)g_desk.icons().size());
            }
        }

        // ---- what is lying on top of the desktop right now (~every 180 ms)
        g_occAcc += (int)(dt * 1000);
        if (g_occAcc >= 180) {
            g_occAcc = 0;
            if (g_cfg.hideOnApp) ZOrderRefresh();
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
            BugTickAll(dt, g_curBounds);
            if (g_visible) {
                BugPresentAll();
                PoopPresent();
            }
        }

        // ---- frame pacing / idle throttling
        int target = g_cfg.fps;
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

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_UPDOWN_CLASS };
    InitCommonControlsEx(&icc);

    ConfigLoad();
    if (g_cfg.lowPriority) SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);

    // make sure the config dir + log exist
    AppDataDir();
    LogLine(L"---- start v1.1.0 ----");

    int rc = RunApp();
    (void)0;

    if (mtx) CloseHandle(mtx);
    Gdiplus::GdiplusShutdown(gdipToken);
    return rc;
}
