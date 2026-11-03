// preview.cpp : offscreen art sheet (build-time only)
#include "../src/common.h"

Config       g_cfg;
HINSTANCE    g_hInst = nullptr;
std::mt19937 g_rng(1234);
bool         g_visible = true;
bool         g_quitting = false;
RECT         g_curBounds = {0,0,100,100};

const wchar_t* T(int) { return L""; }

using namespace Gdiplus;

int wmain(int argc, wchar_t** argv) {
    const wchar_t* out = (argc > 1) ? argv[1] : L"preview.png";
    GdiplusStartupInput gi; ULONG_PTR tok;
    GdiplusStartup(&tok, &gi, nullptr);

    const int W = 1280, H = 900;
    {
    Bitmap bmp(W, H, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);

    // background: light desktop-like surface
    LinearGradientBrush bg(Point(0, 0), Point(0, H), Color(255, 238, 240, 244), Color(255, 208, 214, 222));
    g.FillRectangle(&bg, 0, 0, W, H);
    SolidBrush dark(Color(255, 30, 32, 36));
    g.FillRectangle(&dark, 0, H - 220, W, 220);
    SolidBrush white(Color(255, 245, 245, 245));
    Font font(L"Segoe UI", 15);
    Font small(L"Segoe UI", 11);

    float L = 210.0f;
    // --- adults
    R::DrawBug(g, 250, 190, 0.55f, Stage::Adult, L, 0, 0.6f, 0.8f, false, 0, 0.0f, 1.0f);
    R::DrawBug(g, 640, 190, 2.1f, Stage::Adult, L, 0, 2.4f, 0.9f, true, 1.2f, 1.1f, 1.0f);
    R::DrawBug(g, 1010, 190, -0.3f, Stage::Adult, L * 0.92f, 0, 4.0f, 0.2f, false, 0, 2.0f, 1.2f);
    g.DrawString(L"Adult  (Periplaneta americana)", -1, &font, PointF(60, 330), &dark);

    // --- juvenile
    R::DrawBug(g, 250, 500, 0.2f, Stage::Juvenile, L * 0.62f, 0, 1.2f, 0.8f, false, 0, 0.5f, 1.0f);
    g.DrawString(L"Juvenile", -1, &small, PointF(200, 570), &dark);

    // --- hatchling
    R::DrawBug(g, 520, 500, -0.9f, Stage::Hatchling, L * 0.40f, 0, 3.0f, 0.8f, false, 0, 1.5f, 1.0f);
    g.DrawString(L"Hatchling", -1, &small, PointF(470, 570), &dark);

    // --- ootheca
    R::DrawBug(g, 720, 500, 0.0f, Stage::Ootheca, L * 0.62f, 0, 0, 0, false, 0, 0, 0);
    g.DrawString(L"Ootheca", -1, &small, PointF(670, 570), &dark);

    // --- frass
    for (int i = 0; i < 7; i++)
        R::DrawPoop(g, 880 + (i % 4) * 42 + (i / 4) * 16, 480 + (i / 4) * 40 + (i % 3) * 9, 34, i * 37.0f);
    g.DrawString(L"Frass", -1, &small, PointF(880, 570), &dark);

    // --- dark strip : how they read on a dark wallpaper
    SolidBrush w2(Color(255, 235, 235, 235));
    R::DrawBug(g, 220, H - 110, 0.9f, Stage::Adult, L * 0.9f, 0, 1.0f, 0.7f, false, 0, 0.3f, 1.0f);
    R::DrawBug(g, 640, H - 110, 2.6f, Stage::Adult, L * 0.9f, 0, 3.3f, 0.7f, false, 0, 1.7f, 1.0f);
    R::DrawBug(g, 1020, H - 110, 0.1f, Stage::Adult, L * 0.9f, 0, 5.1f, 0.7f, true, 0.8f, 2.4f, 1.0f);
    g.DrawString(L"on a dark surface", -1, &small, PointF(60, H - 200), &w2);

    CLSID png;
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    ImageCodecInfo* info = (ImageCodecInfo*)malloc(size);
    GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; i++)
        if (!wcscmp(info[i].MimeType, L"image/png")) png = info[i].Clsid;
    free(info);

    bmp.Save(out, &png, nullptr);
    }
    GdiplusShutdown(tok);
    return 0;
}
