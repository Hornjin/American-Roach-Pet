// mkicon.cpp : build res/app.ico from the tray-roach artwork (build-time only)
// usage: mkicon.exe <out.ico>
#include "../src/common.h"
#include <objidl.h>

Config       g_cfg;
HINSTANCE    g_hInst = nullptr;
std::mt19937 g_rng(1234);
bool         g_visible = true;
bool         g_quitting = false;
RECT         g_curBounds = {0,0,100,100};

const wchar_t* T(int) { return L""; }

using namespace Gdiplus;

static CLSID FindPngEncoder() {
    UINT count = 0, size = 0;
    GetImageEncodersSize(&count, &size);
    if (!size) return {};
    std::vector<BYTE> buf(size);
    ImageCodecInfo* infos = (ImageCodecInfo*)buf.data();
    GetImageEncoders(count, size, infos);
    CLSID cls = {};
    for (UINT i = 0; i < count; i++)
        if (!wcscmp(infos[i].MimeType, L"image/png")) cls = infos[i].Clsid;
    return cls;
}

static std::vector<BYTE> PngOf(int size, CLSID pngCls) {
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    // a little margin so the roach does not kiss the edge
    R::DrawTrayRoach(g, size * 0.5f, size * 0.5f, size * 0.92f);

    IStream* stm = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stm) != S_OK) return {};
    bmp.Save(stm, &pngCls, nullptr);
    STATSTG st = {};
    stm->Stat(&st, STATFLAG_NONAME);
    std::vector<BYTE> png((size_t)st.cbSize.QuadPart);
    LARGE_INTEGER zero = {};
    stm->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG got = 0;
    stm->Read(png.data(), (ULONG)png.size(), &got);
    stm->Release();
    png.resize(got);
    return png;
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* out = (argc > 1) ? argv[1] : L"res/app.ico";
    GdiplusStartupInput gi; ULONG_PTR tok;
    GdiplusStartup(&tok, &gi, nullptr);

    CLSID png = FindPngEncoder();
    if (png.Data1 == 0) { wprintf(L"no png encoder\n"); return 1; }

    const int sizes[] = { 16, 24, 32, 48, 64, 128, 256 };
    const int n = 7;
    std::vector<std::vector<BYTE>> blobs;
    for (int i = 0; i < n; i++) {
        auto p = PngOf(sizes[i], png);
        if (p.empty()) { wprintf(L"render %d failed\n", sizes[i]); return 1; }
        blobs.push_back(std::move(p));
        wprintf(L"%4d px  %6zu bytes\n", sizes[i], p.size());
    }

    // ICO: ICONDIR + ICONDIRENTRY per image, PNG blobs as the entries
    FILE* f = nullptr;
    if (_wfopen_s(&f, out, L"wb") != 0 || !f) { wprintf(L"cannot write %s\n", out); return 1; }
    BYTE hdr[6] = { 0, 0, 1, 0, (BYTE)n, 0 };
    fwrite(hdr, 1, 6, f);
    DWORD off = 6 + 16 * (DWORD)n;
    for (int i = 0; i < n; i++) {
        int s = sizes[i];
        BYTE e[16] = {};
        e[0] = (BYTE)(s >= 256 ? 0 : s);
        e[1] = (BYTE)(s >= 256 ? 0 : s);
        e[2] = 0; e[3] = 0;
        e[4] = 1; e[5] = 0;                       // planes
        e[6] = 32; e[7] = 0;                      // bpp
        DWORD sz = (DWORD)blobs[i].size();
        memcpy(e + 8, &sz, 4);
        memcpy(e + 12, &off, 4);
        fwrite(e, 1, 16, f);
        off += sz;
    }
    for (int i = 0; i < n; i++) fwrite(blobs[i].data(), 1, blobs[i].size(), f);
    fclose(f);
    wprintf(L"wrote %s (%lu bytes)\n", out, off);
    return 0;
}
