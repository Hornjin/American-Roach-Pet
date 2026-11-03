// winlist.cpp : enumerate top-level windows (debug helper)
#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>

static std::vector<std::wstring> g_want;
static int g_total = 0;

BOOL CALLBACK EnumProc(HWND h, LPARAM) {
    g_total++;
    wchar_t cls[256] = {0}, txt[256] = {0};
    GetClassNameW(h, cls, 255);
    GetWindowTextW(h, txt, 255);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);

    bool match = false;
    if (g_want.empty()) {
        if (wcsstr(cls, L"Roach")) match = true;
    } else {
        for (auto& w : g_want) if (wcsstr(cls, w.c_str()) || wcsstr(txt, w.c_str())) match = true;
    }
    if (!match) return TRUE;

    RECT r = {0};
    GetWindowRect(h, &r);
    printf("hwnd=%p pid=%lu vis=%d title=\"%ls\" rect=%ld,%ld %ldx%ld\n",
           (void*)h, (unsigned long)pid, IsWindowVisible(h) ? 1 : 0, txt,
           (long)r.left, (long)r.top, (long)(r.right - r.left), (long)(r.bottom - r.top));
    printf("    class=%ls\n", cls);
    fflush(stdout);
    return TRUE;
}

int wmain(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; i++) g_want.push_back(argv[i]);
    EnumWindows(EnumProc, 0);
    printf("-- total top-level windows enumerated: %d\n", g_total);
    return 0;
}
