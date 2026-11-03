// config.cpp : configuration, localisation, paths, autostart
#include "common.h"
#include <cstdarg>

Config       g_cfg;
HINSTANCE    g_hInst = nullptr;
std::mt19937 g_rng((unsigned)GetTickCount64());
bool         g_visible = true;
bool         g_quitting = false;

// ------------------------------------------------------------------ strings
struct LStr { const wchar_t* zh; const wchar_t* en; };

static const LStr kStr[] = {
/*S_APPNAME*/        { L"美洲大蠊桌宠", L"American Roach Pet" },
/*S_MENU_SHOW*/      { L"显示桌宠", L"Show pet" },
/*S_MENU_HIDE*/      { L"隐藏桌宠", L"Hide pet" },
/*S_MENU_SETTINGS*/  { L"设置...", L"Settings..." },
/*S_MENU_RESTORE*/   { L"还原被吃掉的文档...", L"Restore eaten files..." },
/*S_MENU_CLEANPOOP*/ { L"清理粪便", L"Clean up frass" },
/*S_MENU_OPENCFG*/   { L"打开配置文件", L"Open config file" },
/*S_MENU_ABOUT*/     { L"关于", L"About" },
/*S_MENU_QUIT*/      { L"退出", L"Quit" },
/*S_MENU_PAUSE*/     { L"暂停活动", L"Pause activity" },
/*S_SET_TITLE*/      { L"美洲大蠊桌宠 - 设置", L"American Roach Pet - Settings" },
/*S_SEC_PERF*/       { L"性能", L"Performance" },
/*S_SEC_LOOK*/       { L"外观", L"Appearance" },
/*S_SEC_BEHAVE*/     { L"行为", L"Behaviour" },
/*S_SEC_DISPLAY*/    { L"显示与多显示器", L"Display & monitors" },
/*S_SEC_FILE*/       { L"文件（可逆）", L"Files (reversible)" },
/*S_SEC_HOTKEY*/     { L"快捷键", L"Hotkeys" },
/*S_SEC_MISC*/       { L"其它", L"Misc" },
/*S_FPS*/            { L"帧率上限", L"FPS cap" },
/*S_IDLE*/           { L"闲置自动降帧", L"Idle throttling" },
/*S_MAXBUG*/         { L"繁殖数量上限", L"Population cap" },
/*S_SCALE*/          { L"缩放", L"Scale" },
/*S_OPACITY*/        { L"不透明度", L"Opacity" },
/*S_DEBUGBG*/        { L"调试背景色", L"Debug background" },
/*S_LANG*/           { L"语言", L"Language" },
/*S_TRAY*/           { L"显示托盘图标", L"Tray icon" },
/*S_THROUGH*/        { L"鼠标穿透（点击穿过）", L"Click-through" },
/*S_HOVER*/          { L"鼠标悬停惊逃", L"Flee on hover" },
/*S_EAT*/            { L"允许啃食文件", L"Allow eating files" },
/*S_POOP*/           { L"产生粪便", L"Produce frass" },
/*S_FULLSCR*/        { L"全屏时自动隐藏", L"Hide on fullscreen" },
/*S_AUTOSTART*/      { L"开机自启", L"Start with Windows" },
/*S_MONITOR*/        { L"限定显示器", L"Monitor" },
/*S_BOUNDS*/         { L"活动边界", L"Walk bounds" },
/*S_MON_ALL*/        { L"全部显示器", L"All monitors" },
/*S_BOUND_WORK*/     { L"工作区（避开任务栏）", L"Work area" },
/*S_BOUND_FULL*/     { L"整块屏幕", L"Full monitor" },
/*S_HIDDEN_CNT*/     { L"已隐藏文件：", L"Hidden files: " },
/*S_RESTORE_ALL*/    { L"还原全部", L"Restore all" },
/*S_OPENLOG*/        { L"打开记录", L"Open log" },
/*S_OPENCELL*/       { L"打开所在文件夹", L"Open folder" },
/*S_OPENCFG*/        { L"打开配置", L"Open config" },
/*S_CHECKUPD*/       { L"检查更新", L"Check update" },
/*S_REPO*/           { L"仓库", L"Repository" },
/*S_ABOUT*/          { L"关于", L"About" },
/*S_BTNOK*/             { L"确定", L"OK" },
/*S_BTNCANCEL*/         { L"取消", L"Cancel" },
/*S_APPLY*/          { L"应用", L"Apply" },
/*S_CLOSE*/          { L"关闭", L"Close" },
/*S_LANG_ZH*/        { L"简体中文", L"简体中文" },
/*S_LANG_EN*/        { L"English", L"English" },
/*S_ALREADY_LATEST*/ { L"当前已是最新版本 v1.1.0", L"You are on the latest version v1.1.0" },
/*S_RESTORE_OK*/     { L"已还原 %d 个文件。", L"%d file(s) restored." },
/*S_RESTORE_NONE*/   { L"没有被隐藏的文件。", L"No hidden files." },
/*S_RESTORE_CONFIRM*/{ L"将把本程序隐藏的所有文件恢复为正常属性（attrib -s -h）。\n是否继续？",
                       L"Restore the normal attributes of every file hidden by this app?\nContinue?" },
/*S_ABOUT_TEXT*/     { L"美洲大蠊桌宠 v1.1.0\n\n"
                         L"原生 Win32 + GDI+ 实现，单文件、无运行时依赖、低占用。\n"
                         L"全部形象由矢量代码实时绘制（非位图素材），因此触须与步足可逐帧摆动。\n\n"
                         L"啃食文件仅通过设置 隐藏+系统 属性实现（等同 attrib +s +h），\n"
                         L"不移动、不删除、不改内容，可随时一键还原。",
                       L"American Roach Pet v1.1.0\n\n"
                         L"Native Win32 + GDI+, single binary, no runtime, low footprint.\n"
                         L"All artwork is drawn procedurally in vector code, so antennae and legs\n"
                         L"animate every frame.\n\n"
                         L"Eating a file only sets the Hidden+System attributes (same as\n"
                         L"attrib +s +h). Nothing is moved, deleted or modified." },
/*S_HK_TIP*/         { L"显示/隐藏 Ctrl+Alt+R    设置 Ctrl+Alt+S    退出 Ctrl+Alt+Q",
                       L"Show/Hide Ctrl+Alt+R    Settings Ctrl+Alt+S    Quit Ctrl+Alt+Q" },
/*S_NEED_ADMIN*/     { L"需要管理员权限才能隐藏该文件。", L"Administrator rights required." },
/*S_TRAY_TIP*/       { L"美洲大蠊桌宠\n双击打开设置", L"American Roach Pet\nDouble-click for settings" },
/*S_PAUSE*/          { L"暂停全部动画", L"Pause all motion" },
/*S_OPENLOGFILE*/    { L"打开日志", L"Open log file" },
/*S_VER*/            { L"v1.1.0 · 原生 Win32 + GDI+ · 单文件", L"v1.1.0 · native Win32 + GDI+ · single file" },
/*S_EATEN_TIP*/      { L"啃食 = 仅设置 隐藏+系统 属性（等同 attrib +s +h），文件可正常使用，可一键还原。",
                       L"Eating only sets Hidden+System attributes (same as attrib +s +h). Files stay usable and fully restorable." },
/*S_HIDEONAPP*/      { L"窗口遮挡时隐藏（不开窗时浮于桌面）", L"Hide behind app windows" },
/*S_HIDEONAPP_TIP*/  { L"被窗口盖住的地方自动隐藏",
                       L"Hidden wherever a window covers them" },
};

const wchar_t* T(int id) {
    if (id < 0 || id >= S_COUNT) return L"";
    return g_cfg.lang ? kStr[id].en : kStr[id].zh;
}

// ------------------------------------------------------------------ paths
std::wstring AppDataDir() {
    static std::wstring cached;
    if (!cached.empty()) return cached;
    wchar_t buf[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf))) {
        cached = buf;
        cached += L"\\RoachPet";
        CreateDirectoryW(cached.c_str(), nullptr);
    } else {
        cached = L".";
    }
    return cached;
}
std::wstring ConfigPath()  { return AppDataDir() + L"\\config.ini"; }
std::wstring EatenLogPath(){ return AppDataDir() + L"\\eaten.tsv"; }
std::wstring LogPath()     { return AppDataDir() + L"\\roachpet.log"; }

void LogLine(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf(buf, 2047, fmt, ap);
    va_end(ap);
    buf[2047] = 0;
    FILE* f = _wfopen(LogPath().c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fwprintf(f, L"[%04d-%02d-%02d %02d:%02d:%02d] %ls\n",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, buf);
    fclose(f);
}

// ------------------------------------------------------------------ ini
static std::wstring g_ini;

static int  IniGetInt(const wchar_t* sec, const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(sec, key, def, g_ini.c_str());
}
static void IniSetInt(const wchar_t* sec, const wchar_t* key, int v) {
    wchar_t b[32]; swprintf(b, 32, L"%d", v);
    WritePrivateProfileStringW(sec, key, b, g_ini.c_str());
}

void ConfigLoad() {
    g_ini = ConfigPath();
    // create default file so "open config" always works
    bool fresh = false;
    if (GetFileAttributesW(g_ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        FILE* f = _wfopen(g_ini.c_str(), L"w, ccs=UTF-8");
        if (f) { fwprintf(f, L"; RoachPet configuration\n"); fclose(f); fresh = true; }
    }
    g_cfg.lang           = IniGetInt(L"general", L"lang", 0);
    g_cfg.autostart      = IniGetInt(L"general", L"autostart", 0) != 0;
    g_cfg.showTray       = IniGetInt(L"general", L"tray", 1) != 0;
    g_cfg.clickThrough   = IniGetInt(L"general", L"click_through", 0) != 0;
    g_cfg.opacity        = IniGetInt(L"general", L"opacity", 100);
    g_cfg.scale          = IniGetInt(L"general", L"scale_pct", 100) / 100.0f;

    g_cfg.monitor        = IniGetInt(L"display", L"monitor", 0);
    g_cfg.boundsMode     = IniGetInt(L"display", L"bounds", 0);
    g_cfg.hideFullscreen = IniGetInt(L"display", L"hide_fullscreen", 1) != 0;
    g_cfg.debugBg        = IniGetInt(L"display", L"debug_bg", 0) != 0;

    g_cfg.fps            = IniGetInt(L"perf", L"fps", 24);
    g_cfg.idleThrottle   = IniGetInt(L"perf", L"idle_throttle", 1) != 0;
    g_cfg.maxBugs        = IniGetInt(L"perf", L"max_bugs", 26);
    g_cfg.lowPriority    = IniGetInt(L"perf", L"low_priority", 1) != 0;

    g_cfg.hoverFlee      = IniGetInt(L"behave", L"hover_flee", 1) != 0;
    g_cfg.eatEnabled     = IniGetInt(L"behave", L"eat_files", 1) != 0;
    g_cfg.poopEnabled    = IniGetInt(L"behave", L"poop", 1) != 0;
    g_cfg.hideOnApp      = IniGetInt(L"behave", L"hide_on_app", 1) != 0;
    g_cfg.eatIntervalSec = IniGetInt(L"behave", L"eat_interval", 40);
    g_cfg.maxHiddenFiles = IniGetInt(L"behave", L"max_hidden", 200);

    g_cfg.hkToggleMod    = (UINT)IniGetInt(L"hotkey", L"toggle_mod", MOD_CONTROL | MOD_ALT);
    g_cfg.hkToggleKey    = (UINT)IniGetInt(L"hotkey", L"toggle_key", 'R');
    g_cfg.hkSettingsMod  = (UINT)IniGetInt(L"hotkey", L"settings_mod", MOD_CONTROL | MOD_ALT);
    g_cfg.hkSettingsKey  = (UINT)IniGetInt(L"hotkey", L"settings_key", 'S');
    g_cfg.hkQuitMod      = (UINT)IniGetInt(L"hotkey", L"quit_mod", MOD_CONTROL | MOD_ALT);
    g_cfg.hkQuitKey      = (UINT)IniGetInt(L"hotkey", L"quit_key", 'Q');

    if (g_cfg.opacity < 30)  g_cfg.opacity = 30;
    if (g_cfg.opacity > 100) g_cfg.opacity = 100;
    if (g_cfg.fps < 5)       g_cfg.fps = 5;
    if (g_cfg.fps > 60)      g_cfg.fps = 60;
    if (g_cfg.maxBugs < 3)   g_cfg.maxBugs = 3;
    if (g_cfg.maxBugs > 80)  g_cfg.maxBugs = 80;
    if (g_cfg.scale < 0.6f)  g_cfg.scale = 0.6f;
    if (g_cfg.scale > 1.8f)  g_cfg.scale = 1.8f;

    // on first run dump every key with its default so the file is editable
    if (fresh) ConfigSave();
}

void ConfigSave() {
    g_ini = ConfigPath();
    IniSetInt(L"general", L"lang", g_cfg.lang);
    IniSetInt(L"general", L"autostart", g_cfg.autostart ? 1 : 0);
    IniSetInt(L"general", L"tray", g_cfg.showTray ? 1 : 0);
    IniSetInt(L"general", L"click_through", g_cfg.clickThrough ? 1 : 0);
    IniSetInt(L"general", L"opacity", g_cfg.opacity);
    IniSetInt(L"general", L"scale_pct", (int)(g_cfg.scale * 100 + 0.5f));

    IniSetInt(L"display", L"monitor", g_cfg.monitor);
    IniSetInt(L"display", L"bounds", g_cfg.boundsMode);
    IniSetInt(L"display", L"hide_fullscreen", g_cfg.hideFullscreen ? 1 : 0);
    IniSetInt(L"display", L"debug_bg", g_cfg.debugBg ? 1 : 0);

    IniSetInt(L"perf", L"fps", g_cfg.fps);
    IniSetInt(L"perf", L"idle_throttle", g_cfg.idleThrottle ? 1 : 0);
    IniSetInt(L"perf", L"max_bugs", g_cfg.maxBugs);
    IniSetInt(L"perf", L"low_priority", g_cfg.lowPriority ? 1 : 0);

    IniSetInt(L"behave", L"hover_flee", g_cfg.hoverFlee ? 1 : 0);
    IniSetInt(L"behave", L"eat_files", g_cfg.eatEnabled ? 1 : 0);
    IniSetInt(L"behave", L"poop", g_cfg.poopEnabled ? 1 : 0);
    IniSetInt(L"behave", L"hide_on_app", g_cfg.hideOnApp ? 1 : 0);
    IniSetInt(L"behave", L"eat_interval", g_cfg.eatIntervalSec);
    IniSetInt(L"behave", L"max_hidden", g_cfg.maxHiddenFiles);

    IniSetInt(L"hotkey", L"toggle_mod", (int)g_cfg.hkToggleMod);
    IniSetInt(L"hotkey", L"toggle_key", (int)g_cfg.hkToggleKey);
    IniSetInt(L"hotkey", L"settings_mod", (int)g_cfg.hkSettingsMod);
    IniSetInt(L"hotkey", L"settings_key", (int)g_cfg.hkSettingsKey);
    IniSetInt(L"hotkey", L"quit_mod", (int)g_cfg.hkQuitMod);
    IniSetInt(L"hotkey", L"quit_key", (int)g_cfg.hkQuitKey);
}

// ------------------------------------------------------------------ autostart
static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunVal = L"RoachPet";

void ApplyAutostart(bool on) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring q = L"\"";
        q += exe; q += L"\"";
        RegSetValueExW(k, kRunVal, 0, REG_SZ, (const BYTE*)q.c_str(),
                       (DWORD)((q.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, kRunVal);
    }
    RegCloseKey(k);
}

bool QueryAutostart() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return false;
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD sz = sizeof(buf), type = 0;
    bool ok = RegQueryValueExW(k, kRunVal, nullptr, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok && sz > 2;
}
