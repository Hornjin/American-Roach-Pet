// winstate.cpp : dump placement info for a window class
#include <windows.h>
#include <stdio.h>

static const wchar_t* g_cls = L"";

BOOL CALLBACK EnumProc(HWND h, LPARAM) {
    wchar_t cls[256] = {0};
    GetClassNameW(h, cls, 255);
    if (!wcsstr(cls, g_cls)) return TRUE;

    WINDOWPLACEMENT wp = { sizeof(wp) };
    GetWindowPlacement(h, &wp);
    RECT r = {0};
    GetWindowRect(h, &r);
    LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    printf("hwnd=%p\n", (void*)h);
    printf("  visible=%d iconic=%d zoomed=%d\n", IsWindowVisible(h), IsIconic(h), IsZoomed(h));
    printf("  showCmd=%u flags=%u\n", wp.showCmd, wp.flags);
    printf("  normal=%ld,%ld %ldx%ld  min=%ld,%ld  max=%ld,%ld\n",
           (long)wp.rcNormalPosition.left, (long)wp.rcNormalPosition.top,
           (long)(wp.rcNormalPosition.right - wp.rcNormalPosition.left),
           (long)(wp.rcNormalPosition.bottom - wp.rcNormalPosition.top),
           (long)wp.ptMinPosition.x, (long)wp.ptMinPosition.y,
           (long)wp.ptMaxPosition.x, (long)wp.ptMaxPosition.y);
    printf("  rect=%ld,%ld %ldx%ld style=0x%08lX ex=0x%08lX\n",
           (long)r.left, (long)r.top, (long)(r.right - r.left), (long)(r.bottom - r.top),
           (unsigned long)style, (unsigned long)ex);
    return FALSE;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { printf("usage: winstate <classSubstr>\n"); return 1; }
    g_cls = argv[1];
    EnumWindows(EnumProc, 0);
    return 0;
}
