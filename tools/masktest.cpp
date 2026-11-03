// masktest.cpp : side-by-side proof of the icon-silhouette clip (build helper)
//
//   left  : old behaviour - the antennae are clipped by the icon's rectangle
//   right : new behaviour - clipped by the opaque silhouette of the artwork
//
// usage: masktest.exe <out.png> [iconPath]
#include "../src/common.h"
#include <shlobj.h>

Config       g_cfg;
HINSTANCE    g_hInst = nullptr;
std::mt19937 g_rng(1234);
bool         g_visible = true;
bool         g_quitting = false;
RECT         g_curBounds = {0, 0, 100, 100};

const wchar_t* T(int) { return L""; }

using namespace Gdiplus;

struct Mask { std::vector<std::pair<short, short>> span; int w = 0, h = 0; };

static bool LoadMask(const std::wstring& path, Mask& m) {
    SHFILEINFOW sfi = {};
    if (!SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                        SHGFI_ICON | SHGFI_LARGEICON) || !sfi.hIcon) return false;
    ICONINFO ii = {};
    if (!GetIconInfo(sfi.hIcon, &ii)) { DestroyIcon(sfi.hIcon); return false; }
    BITMAP bm = {};
    GetObjectW(ii.hbmColor, sizeof(bm), &bm);
    int w = bm.bmWidth, h = bm.bmHeight;
    std::vector<DWORD> px((size_t)w * h, 0);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr);
    bool ok = GetDIBits(dc, ii.hbmColor, 0, h, px.data(), &bi, DIB_RGB_COLORS) != 0;
    ReleaseDC(nullptr, dc);
    if (ok) {
        m.span.resize(h);
        for (int y = 0; y < h; y++) {
            int x0 = -1, x1 = -1;
            for (int x = 0; x < w; x++)
                if ((px[(size_t)y * w + x] >> 24) >= 24) { if (x0 < 0) x0 = x; x1 = x + 1; }
            m.span[y] = (x0 < 0) ? std::make_pair((short)0, (short)0)
                                 : std::make_pair((short)x0, (short)x1);
        }
        m.w = w; m.h = h;
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask)  DeleteObject(ii.hbmMask);
    DestroyIcon(sfi.hIcon);
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* out = (argc > 1) ? argv[1] : L"masktest.png";

    std::wstring path;
    if (argc > 2) path = argv[2];
    if (path.empty()) {
        wchar_t dir[MAX_PATH] = {0};
        SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, dir);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((std::wstring(dir) + L"\\*.lnk").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) { path = std::wstring(dir) + L"\\" + fd.cFileName; FindClose(h); }
    }

    Mask m;
    if (!LoadMask(path, m)) { wprintf(L"no mask for %ls\n", path.c_str()); return 1; }

    GdiplusStartupInput gi; ULONG_PTR tok;
    GdiplusStartup(&tok, &gi, nullptr);

    const int W = 900, H = 520;
    {
        Bitmap bmp(W, H, PixelFormat32bppARGB);
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

        SolidBrush bg(Color(255, 236, 238, 242));
        g.FillRectangle(&bg, 0, 0, W, H);
        SolidBrush dark(Color(255, 30, 32, 36));
        Font font(L"Segoe UI", 15);
        Font small(L"Segoe UI", 12);

        // the icon, blown up so the edge is obvious
        const int ICON = 220;
        int rects[2] = { 120, 560 };
        for (int side = 0; side < 2; side++) {
            int ox = rects[side], oy = 110;
            Gdiplus::Rect ir(ox, oy, ICON, ICON);

            // --- the file itself
            SHFILEINFOW sfi = {};
            SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                           SHGFI_ICON | SHGFI_LARGEICON);
            if (sfi.hIcon) {
                Bitmap icon(sfi.hIcon);
                g.DrawImage(&icon, ir);
                DestroyIcon(sfi.hIcon);
            }

            // --- antennae emerging from the middle, heading up-right
            Region saved;
            g.GetClip(&saved);
            if (side == 0) {
                g.SetClip(ir, CombineModeExclude);            // old: whole rectangle
            } else {
                GraphicsPath p;
                for (int y = 0; y < m.h; y++) {
                    short x0 = m.span[y].first, x1 = m.span[y].second;
                    if (x1 <= x0) continue;
                    int py0 = ir.Y + y * ir.Height / m.h;
                    int py1 = ir.Y + (y + 1) * ir.Height / m.h;
                    int px0 = ir.X + x0 * ir.Width / m.w;
                    int px1 = ir.X + x1 * ir.Width / m.w;
                    if (py1 > py0 && px1 > px0)
                        p.AddRectangle(Gdiplus::Rect(px0, py0, px1 - px0, py1 - py0));
                }
                g.SetClip(&p, CombineModeExclude);            // new: silhouette
            }
            R::DrawAntennaeOnly(g, (float)(ir.X + ICON / 2), (float)(ir.Y + ICON / 2),
                                -0.7f, (float)ICON * 1.25f, 0.0f, 0.0f, 1.0f);
            g.SetClip(&saved, CombineModeReplace);

            // --- the invisible bounding box, drawn for reference
            Pen box(Color(210, 220, 60, 60), 1.5f);
            box.SetDashStyle(DashStyleDash);
            g.DrawRectangle(&box, ir);

            g.DrawString(side == 0 ? L"before: clipped by the rectangle"
                                   : L"after: clipped by the silhouette",
                         -1, &font, PointF((REAL)(ox - 60), (REAL)(oy + ICON + 30)), &dark);
        }

        g.DrawString(L"dashed red = the icon rectangle the shell reports", -1, &small,
                     PointF(120, 30), &dark);

        CLSID png;
        UINT n = 0, sz = 0;
        GetImageEncodersSize(&n, &sz);
        auto* info = (ImageCodecInfo*)malloc(sz);
        GetImageEncoders(n, sz, info);
        for (UINT i = 0; i < n; i++)
            if (!wcscmp(info[i].MimeType, L"image/png")) png = info[i].Clsid;
        free(info);
        bmp.Save(out, &png, nullptr);
    }
    GdiplusShutdown(tok);
    return 0;
}
