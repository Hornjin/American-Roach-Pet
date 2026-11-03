// postcmd.cpp : post WM_COMMAND to a window found by class substring
// usage: postcmd.exe <classSubstr> <commandId>
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static const wchar_t* g_cls = L"";
static HWND g_found = nullptr;

BOOL CALLBACK EnumProc(HWND h, LPARAM) {
    wchar_t cls[256] = {0};
    GetClassNameW(h, cls, 255);
    if (!wcsstr(cls, g_cls)) return TRUE;
    if (!IsWindowVisible(h)) return TRUE;
    g_found = h;
    return FALSE;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { printf("usage: postcmd <classSubstr> <id>\n"); return 1; }
    g_cls = argv[1];
    int id = _wtoi(argv[2]);
    EnumWindows(EnumProc, 0);
    if (!g_found) { printf("window not found\n"); return 1; }
    PostMessageW(g_found, WM_COMMAND, (WPARAM)id, 0);
    printf("posted %d to %p\n", id, (void*)g_found);
    return 0;
}
