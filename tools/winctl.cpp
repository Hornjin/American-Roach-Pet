// winctl.cpp : bring a window to front (debug helper)
// usage: winctl.exe <classSubstring> [top|activate|move x y]
#include <windows.h>
#include <stdio.h>

static const wchar_t* g_cls = L"";
static int g_mode = 0;
static int g_x = 0, g_y = 0;

BOOL CALLBACK EnumProc(HWND h, LPARAM) {
    wchar_t cls[256] = {0};
    GetClassNameW(h, cls, 255);
    if (!wcsstr(cls, g_cls)) return TRUE;

    ShowWindow(h, SW_SHOW);

    if (g_mode == 1) {
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    } else if (g_mode == 2) {
        HWND fg = GetForegroundWindow();
        DWORD fgTid = GetWindowThreadProcessId(fg, nullptr);
        DWORD myTid = GetCurrentThreadId();
        AttachThreadInput(myTid, fgTid, TRUE);
        SetForegroundWindow(h);
        BringWindowToTop(h);
        AttachThreadInput(myTid, fgTid, FALSE);
    } else if (g_mode == 3) {
        SetWindowPos(h, HWND_TOPMOST, g_x, g_y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    } else if (g_mode == 4) {
        PostMessageW(h, WM_CLOSE, 0, 0);
    }
    printf("done %p [%ls] vis=%d\n", (void*)h, cls, IsWindowVisible(h) ? 1 : 0);
    return FALSE;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { printf("usage: winctl <classSubstr> [top|activate|move x y]\n"); return 1; }
    g_cls = argv[1];
    if (argc > 2) {
        if (!wcscmp(argv[2], L"top")) g_mode = 1;
        else if (!wcscmp(argv[2], L"activate")) g_mode = 2;
        else if (!wcscmp(argv[2], L"close")) g_mode = 4;
        else if (!wcscmp(argv[2], L"move")) {
            g_mode = 3;
            g_x = (argc > 3) ? _wtoi(argv[3]) : 0;
            g_y = (argc > 4) ? _wtoi(argv[4]) : 0;
        }
    }
    EnumWindows(EnumProc, 0);
    return 0;
}
