// icontest.cpp : dump the opaque silhouette of every desktop icon
// usage: icontest.exe
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <vector>
#include <string>

static void One(const std::wstring& path) {
    SHFILEINFOW sfi = {};
    if (!SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                        SHGFI_ICON | SHGFI_LARGEICON) || !sfi.hIcon) {
        wprintf(L"  [no icon err=%lu]\n", (unsigned long)GetLastError());
        return;
    }
    ICONINFO ii = {};
    GetIconInfo(sfi.hIcon, &ii);
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
    bool ok = w > 0 && h > 0 && GetDIBits(dc, ii.hbmColor, 0, h, px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);

    if (!ok) {
        wprintf(L"  [no dib]\n");
    } else {
        int partial = 0, empty = 0;
        for (int y = 0; y < h; y++) {
            int x0 = -1, x1 = -1;
            for (int x = 0; x < w; x++)
                if ((px[(size_t)y * w + x] >> 24) >= 24) { if (x0 < 0) x0 = x; x1 = x + 1; }
            if (x0 < 0) empty++;
            else if (x0 > 0 || x1 < w) partial++;
        }
        wprintf(L"  mask %dx%d  rows with inset edge=%d  fully empty rows=%d\n", w, h, partial, empty);
    }

    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask)  DeleteObject(ii.hbmMask);
    DestroyIcon(sfi.hIcon);
}

int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t dir[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, dir);
    wprintf(L"dir: %ls\n", dir);

    WIN32_FIND_DATAW fd;
    std::wstring pat = std::wstring(dir) + L"\\*";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { wprintf(L"empty\n"); return 1; }
    int n = 0;
    do {
        if (fd.cFileName[0] == L'.') continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        wprintf(L"%ls\n", fd.cFileName);
        One(std::wstring(dir) + L"\\" + fd.cFileName);
        if (++n >= 8) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}
