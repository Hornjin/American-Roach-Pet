// snapwin.cpp : capture a single window by class substring via PrintWindow
// usage: snapwin.exe <classSubstr> <out.png> [full]
#include <windows.h>
#include <gdiplus.h>
#include <stdio.h>

using namespace Gdiplus;

static const wchar_t* g_cls = L"";
static const wchar_t* g_out = L"out.png";
static bool g_full = false;

static bool GetEncoderClsid(const WCHAR* mime, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return false;
    auto* info = (ImageCodecInfo*)malloc(size);
    if (!info) return false;
    GetImageEncoders(num, size, info);
    bool ok = false;
    for (UINT i = 0; i < num; i++) {
        if (!wcscmp(info[i].MimeType, mime)) { *pClsid = info[i].Clsid; ok = true; break; }
    }
    free(info);
    return ok;
}

BOOL CALLBACK EnumProc(HWND h, LPARAM) {
    wchar_t cls[256] = {0};
    GetClassNameW(h, cls, 255);
    if (!wcsstr(cls, g_cls)) return TRUE;

    RECT r;
    GetWindowRect(h, &r);
    int w = r.right - r.left, ht = r.bottom - r.top;
    if (w <= 0 || ht <= 0) return TRUE;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, ht);
    HGDIOBJ old = SelectObject(mem, bmp);

    BOOL ok = PrintWindow(h, mem, g_full ? 2 : 0);
    if (!ok) {
        // fallback: BitBlt from screen
        BitBlt(mem, 0, 0, w, ht, screen, r.left, r.top, SRCCOPY);
    }

    Bitmap out(bmp, nullptr);
    CLSID png;
    GetEncoderClsid(L"image/png", &png);
    out.Save(g_out, &png, nullptr);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    wprintf(L"captured %p [%ls] %dx%d ok=%d -> %ls\n", (void*)h, cls, w, ht, ok ? 1 : 0, g_out);
    return FALSE;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { wprintf(L"usage: snapwin <classSubstr> <out.png> [full]\n"); return 1; }
    {
        typedef BOOL (WINAPI *PFN)(HANDLE);
        HMODULE u = GetModuleHandleW(L"user32.dll");
        PFN f = u ? (PFN)GetProcAddress(u, "SetProcessDpiAwarenessContext") : nullptr;
        if (f) f((HANDLE)-4);
    }
    g_cls = argv[1];
    g_out = argv[2];
    g_full = (argc > 3);

    GdiplusStartupInput gi;
    ULONG_PTR tok = 0;
    GdiplusStartup(&tok, &gi, nullptr);
    EnumWindows(EnumProc, 0);
    GdiplusShutdown(tok);
    return 0;
}
