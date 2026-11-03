// drag.cpp : simulate dragging a window by its title bar (build-time helper)
// usage: drag.exe <classSubstring> <dx> <dy> [steps] [msPerStep]
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static const wchar_t* g_cls = L"";
static HWND g_found = nullptr;

BOOL CALLBACK EnumProc(HWND h, LPARAM) {
    if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
    wchar_t cls[256] = {0};
    GetClassNameW(h, cls, 255);
    if (!wcsstr(cls, g_cls)) return TRUE;
    g_found = h;
    return FALSE;
}

static void MouseBtn(bool down) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(in));
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) { printf("usage: drag <classSubstr> <dx> <dy> [steps] [ms]\n"
                           "       drag @ <x> <y> <dx> <dy> [steps] [ms]\n"); return 1; }

    bool abs = !wcscmp(argv[1], L"@");
    int dx, dy, steps, ms, sx, sy;

    if (abs) {
        if (argc < 6) { printf("usage: drag @ <x> <y> <dx> <dy> [steps] [ms]\n"); return 1; }
        sx = _wtoi(argv[2]); sy = _wtoi(argv[3]);
        dx = _wtoi(argv[4]); dy = _wtoi(argv[5]);
        steps = (argc > 6) ? _wtoi(argv[6]) : 40;
        ms = (argc > 7) ? _wtoi(argv[7]) : 40;
        printf("drag from %d,%d by %d,%d\n", sx, sy, dx, dy);
    } else {
        g_cls = argv[1];
        dx = _wtoi(argv[2]); dy = _wtoi(argv[3]);
        steps = (argc > 4) ? _wtoi(argv[4]) : 40;
        ms = (argc > 5) ? _wtoi(argv[5]) : 40;

        EnumWindows(EnumProc, 0);
        if (!g_found) { printf("window not found\n"); return 1; }

        RECT r;
        GetWindowRect(g_found, &r);
        sx = r.left + (r.right - r.left) / 2;
        sy = r.top + 14;                       // title bar
        printf("drag %p from %d,%d by %d,%d\n", (void*)g_found, sx, sy, dx, dy);
    }
    if (steps < 2) steps = 2;

    SetCursorPos(sx, sy);
    Sleep(150);
    MouseBtn(true);
    Sleep(150);
    for (int i = 1; i <= steps; i++) {
        SetCursorPos(sx + dx * i / steps, sy + dy * i / steps);
        Sleep(ms);
    }
    Sleep(150);
    MouseBtn(false);
    printf("done\n");
    return 0;
}
