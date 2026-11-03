// RoachPet - American cockroach desktop pet
// common.h : shared definitions
#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objidl.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <random>
#include <set>

// ---------------------------------------------------------------- lifecycle
enum class Stage : int {
    Ootheca   = 0,   // egg case (ootheca)
    Hatchling = 1,   // instar 1
    Juvenile  = 2,   // wingless nymph
    Adult     = 3    // winged imago
};

enum class BState : int {
    Wander = 0,
    Flee,
    Eat,
    Poop,
    Hidden,      // under an icon, body not drawn
    Emerge       // just revealed, running out
};

// ---------------------------------------------------------------- config
struct Config {
    // general
    int  lang            = 0;      // 0 = chinese, 1 = english
    bool autostart       = false;
    bool showTray        = true;
    bool clickThrough    = false;
    int  opacity         = 100;    // 30..100
    float scale          = 1.0f;   // 0.6 .. 1.8

    // display
    int  monitor         = 0;      // 0 = all monitors, 1..N
    int  boundsMode      = 0;      // 0 = work area, 1 = full monitor
    bool hideFullscreen  = true;
    bool debugBg         = false;

    // performance
    int  fps             = 24;     // 10/15/24/30/60
    bool idleThrottle    = true;
    int  maxBugs         = 26;
    bool lowPriority     = true;

    // behaviour
    bool hoverFlee       = true;
    bool eatEnabled      = true;
    bool poopEnabled     = true;
    bool hideOnApp       = true;   // disappear behind normal app windows
    int  eatIntervalSec  = 40;     // min seconds between two meals
    int  maxHiddenFiles  = 200;

    // hotkeys (virtual key + modifiers)
    UINT hkToggleMod     = MOD_CONTROL | MOD_ALT;
    UINT hkToggleKey     = 'R';
    UINT hkSettingsMod   = MOD_CONTROL | MOD_ALT;
    UINT hkSettingsKey   = 'S';
    UINT hkQuitMod       = MOD_CONTROL | MOD_ALT;
    UINT hkQuitKey       = 'Q';
};

extern Config      g_cfg;
extern HINSTANCE   g_hInst;
extern std::mt19937 g_rng;
extern bool        g_visible;      // pet shown / hidden
extern bool        g_quitting;

inline float frand()                       { return (float)(g_rng() & 0xFFFFFF) / 16777216.0f; }
inline float frand(float a, float b)       { return a + (b - a) * frand(); }
inline int   irand(int a, int b)           { return a + (int)(g_rng() % (unsigned)(b - a + 1)); }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// ---------------------------------------------------------------- paths
std::wstring AppDataDir();          // %APPDATA%\RoachPet  (created on demand)
std::wstring ConfigPath();
std::wstring EatenLogPath();
std::wstring LogPath();

void LogLine(const wchar_t* fmt, ...);

// ---------------------------------------------------------------- config io
void ConfigLoad();
void ConfigSave();
void ApplyAutostart(bool on);
bool QueryAutostart();

// ---------------------------------------------------------------- strings
enum StrId {
    S_APPNAME = 0,
    S_MENU_SHOW, S_MENU_HIDE, S_MENU_SETTINGS, S_MENU_RESTORE, S_MENU_CLEANPOOP,
    S_MENU_OPENCFG, S_MENU_ABOUT, S_MENU_QUIT, S_MENU_PAUSE,
    S_SET_TITLE, S_SEC_PERF, S_SEC_LOOK, S_SEC_BEHAVE, S_SEC_DISPLAY, S_SEC_FILE, S_SEC_HOTKEY, S_SEC_MISC,
    S_FPS, S_IDLE, S_MAXBUG, S_SCALE, S_OPACITY, S_DEBUGBG, S_LANG, S_TRAY,
    S_THROUGH, S_HOVER, S_EAT, S_POOP, S_FULLSCR, S_AUTOSTART,
    S_MONITOR, S_BOUNDS, S_MON_ALL, S_BOUND_WORK, S_BOUND_FULL,
    S_HIDDEN_CNT, S_RESTORE_ALL, S_OPENLOG, S_OPENCELL,
    S_OPENCFG, S_CHECKUPD, S_REPO, S_ABOUT, S_BTNOK, S_BTNCANCEL, S_APPLY, S_CLOSE,
    S_LANG_ZH, S_LANG_EN, S_ALREADY_LATEST, S_RESTORE_OK, S_RESTORE_NONE, S_RESTORE_CONFIRM,
    S_ABOUT_TEXT, S_HK_TIP, S_NEED_ADMIN, S_TRAY_TIP,
    S_PAUSE, S_OPENLOGFILE, S_EATEN_TIP,
    S_HIDEONAPP, S_HIDEONAPP_TIP, S_ABOUT_LINK,
    S_COUNT,
};
const wchar_t* T(int id);

// ---------------------------------------------------------------- desktop
struct IconItem {
    std::wstring name;
    RECT         rc;       // screen rect, whole cell (icon + label)
    RECT         ir;       // screen rect, icon bitmap only
    POINT        pos;      // screen top-left of the icon
    int          cell;     // grid cell index

    // Silhouette of the icon bitmap: one [x0,x1) span per mask row, in mask
    // pixels.  A desktop icon is a rectangle on screen but the artwork inside
    // it is any shape at all, so clipping a hiding insect against the whole
    // rectangle would cut its antennae off in mid air.  Empty = unknown, and
    // callers fall back to the plain rectangle.
    std::vector<std::pair<short, short>> span;
    int          maskW = 0, maskH = 0;
};

class DesktopWatcher {
public:
    bool  Poll();                                   // true if icon layout changed
    const std::vector<IconItem>& icons() const { return m_icons; }
    bool  Ready() const { return m_ready; }
    // cell grid (for "is this spot still covered by a file?")
    bool  CellOccupied(int cell) const;
    // true while some icon rectangle still covers this screen point
    bool  AnyIconCovers(float x, float y) const;
    int   RandomIcon() const;
    int   CellOf(POINT screenPt) const;
    POINT CellCenter(int cell) const;
    // pick a random icon to nibble, returns index or -1
    int   PickEatableIcon(const std::set<int>& skip) const;
    std::wstring PathOfIcon(int idx) const;
    int   PathCount() const {
        int n = 0;
        for (auto& p : m_paths) if (!p.empty()) n++;
        return n;
    }
    void  Invalidate() { m_dirty = true; }

    // file hide / restore
    bool  HideFile(const std::wstring& path);
    int   RestoreAll(std::wstring* outMsg);
    int   HiddenCount();
private:
    std::vector<IconItem> m_icons;
    std::vector<POINT>    m_cellPos;   // cell index -> center
    std::set<int>         m_occupied;
    bool  m_ready = false;
    bool  m_dirty = true;
    std::vector<std::wstring> m_paths; // parallel to m_icons (may be empty)
    int   m_hiddenCount = -1;          // cached journal line count (-1 = unknown)
    void  BuildPaths();
    static void BuildIconMask(const std::wstring& path, IconItem& it);
};

extern DesktopWatcher g_desk;
extern const int CELL;    // grid cell size in px

// ---------------------------------------------------------------- z-order
// The pets live on the desktop layer: everything the wallpaper owns is fair
// game, but a normal application window is opaque and the insects must
// disappear behind it instead of floating on top of it.
void ZOrderRefresh();                                   // re-scan, true if set changed
int  ZOrderCollect(const RECT& box, RECT* out, int maxN, bool* fullyCovered);
bool ZOrderPointCovered(float x, float y);

// ---------------------------------------------------------------- render
namespace R {
    void DrawBug(Gdiplus::Graphics& g, float cx, float cy, float ang,
                 Stage st, float bodyLen, float t,
                 float gaitPhase, float gaitAmp,
                 bool eating, float chew, float antPhase, float antAmp);
    void DrawAntennaeOnly(Gdiplus::Graphics& g, float hx, float hy, float ang,
                          float bodyLen, float t, float antPhase, float antAmp);
    void DrawPoop(Gdiplus::Graphics& g, float x, float y, float size, float rot);
    void DrawTrayRoach(Gdiplus::Graphics& g, float cx, float cy, float size);
    HICON MakeTrayIcon(int size);
}

// ---------------------------------------------------------------- bug
class Bug {
public:
    Bug(Stage st, float x, float y, int mon, bool autoHide = true);
    ~Bug();

    void  Update(float dt, const RECT& bounds);
    void  Present();                    // UpdateLayeredWindow
    void  DestroyWindowSafe();

    Stage  stage()  const { return m_stage; }
    BState state()  const { return m_state; }
    float  x()      const { return m_x; }
    float  y()      const { return m_y; }
    bool   alive()  const { return m_alive; }
    void   kill()         { m_alive = false; }
    void   panicFrom(float px, float py, float radius, float power);
    void   setBounds(const RECT& r) { m_bounds = r; }
    void   reveal();                    // force emerge from under a file
    void   burstOut();                  // run out of cover (freshly hatched)
    int    hideCell() const { return m_hideCell; }
    int    hideIcon() const { return m_hideIcon; }
    bool   hidden() const { return m_state == BState::Hidden; }
    void   refreshWindowStyle();
    void   setShown(bool v);
    HWND   hwnd() const { return m_hwnd; }
    int    spriteSize() const { return m_S; }

    static void RegisterWndClass();
    static void UnregisterWndClass();
    static void GlobalInit();
    static void GlobalShutdown();
    static std::vector<Bug*>& All();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    void   createWindow();
    void   resizeWindowForStage();
    void   pickWanderTarget(const RECT& bounds);
    void   enterHideUnder(int iconIdx);
    void   enterEat(int iconIdx);
    void   finishEat();
    void   spawnPoop();
    void   steerTo(float tx, float ty, float dt, float turnRate);
    void   clampToBounds(const RECT& bounds);
    void   drawInto(Gdiplus::Graphics& g, int ox, int oy);

    Stage   m_stage;
    BState  m_state = BState::Wander;
    bool    m_alive = true;

    float   m_x, m_y;          // body center, screen px
    float   m_ang;             // heading
    float   m_speed;           // current speed px/s
    float   m_tx, m_ty;        // wander target
    float   m_t;               // global anim clock
    float   m_stateT = 0;      // time in current state
    float   m_gait;            // gait phase
    float   m_antPhase;
    float   m_chew = 0;
    float   m_life = 0;
    int     m_mon = 0;

    int     m_hideCell = -1;
    int     m_hideIcon = -1;   // icon currently covering this bug
    RECT    m_coverRc = {0,0,0,0};  // screen rect of that file's icon bitmap
    // opaque silhouette of that icon (see IconItem::span); empty -> use m_coverRc
    std::vector<std::pair<short, short>> m_coverSpan;
    int     m_coverMaskW = 0, m_coverMaskH = 0;
    int     m_seekIcon = -1;   // icon this bug is scuttling towards
    int     m_eatIcon  = -1;
    float   m_eatHold  = 0;
    float   m_restT    = 0;    // idle rest timer
    bool    m_resting  = false;

    int     m_S = 150;         // sprite box size (px)
    float   m_bodyLen = 45;
    int     m_scaleKey = 100;
    RECT    m_bounds = { 0, 0, 1920, 1080 };

    HWND    m_hwnd = nullptr;
    HDC     m_memDC = nullptr;
    HBITMAP m_bmp = nullptr;
    void*   m_bits = nullptr;
    int     m_bw = 0, m_bh = 0;
    bool    m_bmpDirty = true;
    ULONGLONG m_lastPresent = 0;
};

void BugTickAll(float dt, const RECT& bounds);
void BugPresentAll();
void BugPanicAll(float px, float py, float radius, float power);
void BugDestroyAll();
void BugSetVisible(bool vis);
void BugForceShow(bool vis);
void BugSetTopmost(bool on);
void PoopSetVisible(bool vis);
void BugRestorePopulation();
int  BugCount();
void BugLogStats();

// ---------------------------------------------------------------- poop
void PoopAdd(float x, float y, float size);
void PoopClear();
void PoopPresent();
void PoopMarkDirty();
void PoopDestroy();
HWND PoopHwnd();
int  PoopCount();

// ---------------------------------------------------------------- app
void AppOpenSettings();
void AppToggleVisible();
void AppQuit();
void AppRefreshTray();
void AppShowAbout();
void AppCheckUpdate();
void AppRebuildTrayMenu();
void AppOnConfigChanged();
void AppRestoreFiles();
void AppCleanPoop();
void AppNotifyBalloon(const wchar_t* title, const wchar_t* text);
extern RECT g_curBounds;
RECT ComputeBounds();
bool IsFullscreenForeground();
