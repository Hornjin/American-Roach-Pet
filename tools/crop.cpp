// crop.cpp : crop + nearest/high-quality zoom of a PNG (build-time helper)
// usage: crop.exe <in.png> <out.png> <x> <y> <w> <h> [scale]
#include <windows.h>
#include <gdiplus.h>
#include <stdlib.h>
#include <stdio.h>

using namespace Gdiplus;

static bool Encoder(const WCHAR* mime, CLSID* clsid) {
    UINT n = 0, sz = 0;
    GetImageEncodersSize(&n, &sz);
    if (!sz) return false;
    auto* info = (ImageCodecInfo*)malloc(sz);
    if (!info) return false;
    GetImageEncoders(n, sz, info);
    bool ok = false;
    for (UINT i = 0; i < n; i++)
        if (!wcscmp(info[i].MimeType, mime)) { *clsid = info[i].Clsid; ok = true; break; }
    free(info);
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 7) { wprintf(L"usage: crop <in> <out> <x> <y> <w> <h> [scale]\n"); return 1; }
    int x = _wtoi(argv[3]), y = _wtoi(argv[4]);
    int w = _wtoi(argv[5]), h = _wtoi(argv[6]);
    int sc = (argc > 7) ? _wtoi(argv[7]) : 1;
    if (sc < 1) sc = 1;

    GdiplusStartupInput gi; ULONG_PTR tok;
    GdiplusStartup(&tok, &gi, nullptr);

    {
        Bitmap src(argv[1]);
        if (src.GetLastStatus() != Ok) { wprintf(L"cannot open %ls\n", argv[1]); GdiplusShutdown(tok); return 1; }
        Bitmap dst(w * sc, h * sc, PixelFormat32bppARGB);
        Graphics g(&dst);
        g.SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.DrawImage(&src, Rect(0, 0, w * sc, h * sc), x, y, w, h, UnitPixel);
        CLSID png;
        Encoder(L"image/png", &png);
        dst.Save(argv[2], &png, nullptr);
    }
    GdiplusShutdown(tok);
    return 0;
}
