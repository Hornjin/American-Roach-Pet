// bugpos.cpp : dump RoachPet window rects without touching other processes
// usage: bugpos.exe [tag]
#include <windows.h>
#include <stdio.h>

BOOL CALLBACK EnumProc(HWND h, LPARAM) {
    wchar_t cls[128] = {0};
    GetClassNameW(h, cls, 127);
    if (wcsncmp(cls, L"RoachPet", 8)) return TRUE;
    RECT r = {0};
    GetWindowRect(h, &r);
    printf("%ls %d %d %d %d vis=%d\n", cls, (int)r.left, (int)r.top,
           (int)(r.right - r.left), (int)(r.bottom - r.top),
           IsWindowVisible(h) ? 1 : 0);
    return TRUE;
}

int wmain(int, wchar_t**) {
    EnumWindows(EnumProc, 0);
    return 0;
}
