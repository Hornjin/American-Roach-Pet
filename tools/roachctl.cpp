// roachctl.cpp : drive a running RoachPet instance from the command line
// usage: roachctl <toggle|settings|pause|clean|restore|quit>
#include <windows.h>
#include <stdio.h>

int wmain(int argc, wchar_t** argv) {
    HWND h = FindWindowW(L"RoachPetMsgWnd", nullptr);
    if (!h) { printf("RoachPet not running\n"); return 1; }
    if (argc < 2) { printf("usage: roachctl <toggle|settings|pause|clean|restore|quit>\n"); return 1; }

    int id = 0;
    if (!wcscmp(argv[1], L"toggle"))        id = 1;
    else if (!wcscmp(argv[1], L"settings")) id = 2;
    else if (!wcscmp(argv[1], L"pause"))    id = 3;
    else if (!wcscmp(argv[1], L"clean"))    id = 4;
    else if (!wcscmp(argv[1], L"restore"))  id = 5;
    else if (!wcscmp(argv[1], L"quit"))     id = 8;
    if (!id) { printf("unknown command\n"); return 1; }

    PostMessageW(h, WM_COMMAND, (WPARAM)id, 0);
    printf("sent %d to %p\n", id, (void*)h);
    return 0;
}
