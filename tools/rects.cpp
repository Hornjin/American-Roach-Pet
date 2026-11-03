// rects.cpp : dump LVIR_BOUNDS / LVIR_ICON / LVIR_LABEL of desktop items
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>

static HWND FindLV() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    HWND def = nullptr;
    if (progman) def = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
    if (!def) {
        HWND w = nullptr;
        while ((w = FindWindowExW(nullptr, w, L"WorkerW", nullptr)) != nullptr) {
            HWND d = FindWindowExW(w, nullptr, L"SHELLDLL_DefView", nullptr);
            if (d) { def = d; break; }
        }
    }
    if (!def) return nullptr;
    return FindWindowExW(def, nullptr, L"SysListView32", nullptr);
}

int wmain(int argc, wchar_t** argv) {
    HWND lv = FindLV();
    if (!lv) { printf("no listview\n"); return 1; }
    DWORD pid = 0;
    GetWindowThreadProcessId(lv, &pid);
    HANDLE hp = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
                            PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hp) { printf("open explorer failed %lu\n", GetLastError()); return 1; }
    void* rm = VirtualAllocEx(hp, nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!rm) { printf("alloc failed\n"); return 1; }

    int n = (int)SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0);
    printf("items=%d\n", n);
    for (int i = 0; i < n && i < 6; i++) {
        for (int kind = 0; kind <= 2; kind++) {
            RECT r = { kind, 0, 0, 0 };
            WriteProcessMemory(hp, rm, &r, sizeof(r), nullptr);
            LRESULT ok = SendMessageW(lv, LVM_GETITEMRECT, (WPARAM)i, (LPARAM)rm);
            ReadProcessMemory(hp, rm, &r, sizeof(r), nullptr);
            POINT pts[2] = { {r.left, r.top}, {r.right, r.bottom} };
            MapWindowPoints(lv, HWND_DESKTOP, pts, 2);
            printf("  item %d kind=%d ok=%ld -> %ld,%ld %ldx%ld\n", i, kind, (long)ok,
                   (long)pts[0].x, (long)pts[0].y,
                   (long)(pts[1].x - pts[0].x), (long)(pts[1].y - pts[0].y));
        }
    }
    VirtualFreeEx(hp, rm, 0, MEM_RELEASE);
    CloseHandle(hp);
    return 0;
}
