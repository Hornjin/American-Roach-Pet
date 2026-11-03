// deskicon.cpp : list / move desktop icons (test helper for the reveal behaviour)
// usage: deskicon list
//        deskicon move <index> <x> <y>
//        deskicon find <nameSubstring>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <string>
#include <vector>

struct Item { int idx; POINT pos; std::wstring name; };

static HWND FindDesktopListView() {
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

LRESULT SendT(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    DWORD_PTR r = 0;
    if (!SendMessageTimeoutW(h, msg, wp, lp, SMTO_ABORTIFHUNG | SMTO_BLOCK, 800, &r)) return -1;
    return (LRESULT)r;
}

static std::vector<Item> ReadIcons(HWND lv, HANDLE hp) {
    std::vector<Item> out;
    int count = (int)SendT(lv, LVM_GETITEMCOUNT, 0, 0);
    if (count < 0 || count > 4000) return out;

    void* remote = VirtualAllocEx(hp, nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return out;

    for (int i = 0; i < count; i++) {
        Item it; it.idx = i;
        POINT pt = {0, 0};
        if (SendT(lv, LVM_GETITEMPOSITION, (WPARAM)i, (LPARAM)remote) >= 0) {
            ReadProcessMemory(hp, remote, &pt, sizeof(pt), nullptr);
        }
        it.pos = pt;

        LVITEMW item = {};
        item.iItem = i;
        item.iSubItem = 0;
        item.cchTextMax = 260;
        item.pszText = (LPWSTR)((BYTE*)remote + 1024);
        WriteProcessMemory(hp, remote, &item, sizeof(item), nullptr);
        wchar_t buf[300] = {0};
        if (SendT(lv, LVM_GETITEMTEXTW, (WPARAM)i, (LPARAM)remote) > 0) {
            ReadProcessMemory(hp, (BYTE*)remote + 1024, buf, sizeof(wchar_t) * 259, nullptr);
            buf[259] = 0;
        }
        it.name = buf;
        out.push_back(it);
    }
    VirtualFreeEx(hp, remote, 0, MEM_RELEASE);
    return out;
}

int wmain(int argc, wchar_t** argv) {
    HWND lv = FindDesktopListView();
    if (!lv) { printf("desktop listview not found\n"); return 1; }

    DWORD pid = 0;
    GetWindowThreadProcessId(lv, &pid);
    HANDLE hp = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
                            PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hp) { printf("cannot open explorer (err=%lu)\n", GetLastError()); return 1; }

    if (argc < 2) { printf("usage: deskicon list|find <sub>|move <idx> <x> <y>\n"); return 1; }

    if (!wcscmp(argv[1], L"list") || !wcscmp(argv[1], L"find")) {
        auto v = ReadIcons(lv, hp);
        for (auto& it : v) {
            if (argc > 2 && wcsstr(it.name.c_str(), argv[2]) == nullptr) continue;
            wprintf(L"%3d  (%4ld,%4ld)  %ls\n", it.idx, (long)it.pos.x, (long)it.pos.y, it.name.c_str());
        }
    } else if (!wcscmp(argv[1], L"move") && argc >= 5) {
        int idx = _wtoi(argv[2]);
        int x = _wtoi(argv[3]), y = _wtoi(argv[4]);
        LRESULT r = SendT(lv, LVM_SETITEMPOSITION, (WPARAM)idx, MAKELPARAM(x, y));
        wprintf(L"move idx=%d -> (%d,%d) result=%ld\n", idx, x, y, (long)r);
    }
    CloseHandle(hp);
    return 0;
}
