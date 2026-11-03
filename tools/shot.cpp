// shot.cpp : tiny screen capture helper (build-time only, not shipped)
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <cstdio>

using namespace Gdiplus;

static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    ImageCodecInfo* pInfo = (ImageCodecInfo*)malloc(size);
    if (!pInfo) return -1;
    GetImageEncoders(num, size, pInfo);
    int found = -1;
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(pInfo[i].MimeType, format) == 0) { *pClsid = pInfo[i].Clsid; found = (int)i; break; }
    }
    free(pInfo);
    return found;
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* out = (argc > 1) ? argv[1] : L"shot.png";
    int cropX = -1, cropY = -1, cropW = 0, cropH = 0;
    if (argc > 5) { cropX = _wtoi(argv[2]); cropY = _wtoi(argv[3]); cropW = _wtoi(argv[4]); cropH = _wtoi(argv[5]); }

    GdiplusStartupInput gi; ULONG_PTR tok;
    GdiplusStartup(&tok, &gi, nullptr);

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, vw, vh);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, vw, vh, screen, vx, vy, SRCCOPY);
    SelectObject(mem, old);

    {
        Bitmap full(bmp, nullptr);
        Bitmap* outBmp = &full;
        Bitmap* crop = nullptr;
        if (cropX >= 0) {
            crop = new Bitmap(cropW, cropH, PixelFormat32bppARGB);
            Graphics g(crop);
            g.DrawImage(&full, Rect(0, 0, cropW, cropH), cropX - vx, cropY - vy, cropW, cropH, UnitPixel);
            outBmp = crop;
        }
        CLSID png;
        GetEncoderClsid(L"image/png", &png);
        outBmp->Save(out, &png, nullptr);
        delete crop;
    }

    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    GdiplusShutdown(tok);
    return 0;
}
