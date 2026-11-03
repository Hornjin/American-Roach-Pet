// maxwin.cpp : maximize every window whose class contains the given substring
// usage: maxwin.exe <classSubstring> [restore]
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static const wchar_t* g_cls = L"";
static int g_cmd = SW_MAXIMIZE;

BOOL CALLBACK EnumProc(HWND h, LPARAM) {
    if (!IsWindowVisible(h)) return TRUE;
    wchar_t cls[256] = {0};
    GetClassNameW(h, cls, 255);
    if (!wcsstr(cls, g_cls)) return TRUE;
    ShowWindow(h, g_cmd);
    SetForegroundWindow(h);
    printf("done %p [%ls]\n", (void*)h, cls);
    return FALSE;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { printf("usage: maxwin <classSubstr> [restore]\n"); return 1; }
    g_cls = argv[1];
    if (argc > 2 && !wcscmp(argv[2], L"restore")) g_cmd = SW_RESTORE;
    EnumWindows(EnumProc, 0);
    return 0;
}
