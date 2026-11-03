// test_eat.cpp : round-trip test for the reversible "eating" (attrib +s +h)
#include "common.h"
#include <cstdio>

static void Dump(const wchar_t* tag, const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    wprintf(L"%-10ls %-6ls H=%d S=%d R=%d A=%d\n", tag, p.c_str(),
            (a & FILE_ATTRIBUTE_HIDDEN) ? 1 : 0,
            (a & FILE_ATTRIBUTE_SYSTEM) ? 1 : 0,
            (a & FILE_ATTRIBUTE_READONLY) ? 1 : 0,
            (a & FILE_ATTRIBUTE_ARCHIVE) ? 1 : 0);
}

int wmain() {
    wchar_t tmp[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"roach_eat_test";
    CreateDirectoryW(dir.c_str(), nullptr);

    std::vector<std::wstring> files;
    for (int i = 0; i < 3; i++) {
        std::wstring p = dir + L"\\sample_" + std::to_wstring(i) + L".txt";
        FILE* f = _wfopen(p.c_str(), L"w");
        if (f) { fwprintf(f, L"hello %d\n", i); fclose(f); }
        files.push_back(p);
    }

    wprintf(L"--- before ---\n");
    for (auto& p : files) Dump(L"before", p);
    wprintf(L"journal entries before: %d\n\n", g_desk.HiddenCount());

    int ok = 0;
    for (auto& p : files) if (g_desk.HideFile(p)) ok++;
    wprintf(L"hide ok=%d\n", ok);

    wprintf(L"--- after hide ---\n");
    for (auto& p : files) Dump(L"hidden", p);
    wprintf(L"journal entries now: %d\n\n", g_desk.HiddenCount());

    std::wstring msg;
    int n = g_desk.RestoreAll(&msg);
    wprintf(L"restore returned %d : %ls\n", n, msg.c_str());

    wprintf(L"--- after restore ---\n");
    for (auto& p : files) Dump(L"restored", p);
    wprintf(L"journal entries after: %d\n", g_desk.HiddenCount());

    for (auto& p : files) _wremove(p.c_str());
    RemoveDirectoryW(dir.c_str());
    return 0;
}
