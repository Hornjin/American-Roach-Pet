// desktop.cpp : desktop icon tracking (cross-process SysListView32) + reversible file hiding
#include "common.h"
#include <map>
#include <cstdint>

const int CELL = 80;

DesktopWatcher g_desk;

static std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

// ---------------------------------------------------------------- remote memory
namespace {
class RemoteMem {
public:
    RemoteMem(HANDLE hp, SIZE_T sz) : m_hp(hp), m_sz(sz) {
        m_p = VirtualAllocEx(hp, nullptr, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    ~RemoteMem() { if (m_p) VirtualFreeEx(m_hp, m_p, 0, MEM_RELEASE); }
    void*  ptr() const { return m_p; }
    LPARAM at(SIZE_T off) const { return (LPARAM)((BYTE*)m_p + off); }
    bool   write(const void* d, SIZE_T n, SIZE_T off = 0) const {
        return m_p && WriteProcessMemory(m_hp, (BYTE*)m_p + off, d, n, nullptr) != 0;
    }
    bool   read(void* d, SIZE_T n, SIZE_T off = 0) const {
        return m_p && ReadProcessMemory(m_hp, (BYTE*)m_p + off, d, n, nullptr) != 0;
    }
private:
    HANDLE m_hp; void* m_p = nullptr; SIZE_T m_sz;
};

LRESULT SendT(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT timeout = 400) {
    DWORD_PTR r = 0;
    if (!SendMessageTimeoutW(h, msg, wp, lp, SMTO_ABORTIFHUNG | SMTO_BLOCK, timeout, &r))
        return -1;
    return (LRESULT)r;
}

HWND FindDesktopListView() {
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
} // namespace

// ---------------------------------------------------------------- paths on desktop
static void CollectDesktopFiles(std::map<std::wstring, std::wstring>& byStem) {
    byStem.clear();
    wchar_t user[MAX_PATH] = {0}, pub[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, user);
    SHGetFolderPathW(nullptr, CSIDL_COMMON_DESKTOPDIRECTORY, nullptr, 0, pub);

    const wchar_t* dirs[2] = { user, pub };
    for (int d = 0; d < 2; d++) {
        if (!dirs[d][0]) continue;
        std::wstring pat = std::wstring(dirs[d]) + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pat.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.cFileName[0] == L'.') continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;   // already invisible
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) continue;
            std::wstring full = std::wstring(dirs[d]) + L"\\" + fd.cFileName;
            std::wstring name = fd.cFileName;
            std::wstring stem = name;
            size_t dot = stem.find_last_of(L'.');
            if (dot != std::wstring::npos && dot > 0) stem = stem.substr(0, dot);
            byStem[Lower(stem)] = full;
            byStem[Lower(name)] = full;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

void DesktopWatcher::BuildPaths() {
    std::map<std::wstring, std::wstring> byStem;
    CollectDesktopFiles(byStem);
    m_paths.assign(m_icons.size(), std::wstring());
    for (size_t i = 0; i < m_icons.size(); i++) {
        auto it = byStem.find(Lower(m_icons[i].name));
        if (it != byStem.end()) m_paths[i] = it->second;
    }
}

// ---------------------------------------------------------------- poll
bool DesktopWatcher::Poll() {
    HWND lv = FindDesktopListView();
    if (!lv) {
        bool had = m_ready || !m_icons.empty();
        m_icons.clear(); m_occupied.clear(); m_paths.clear(); m_ready = false;
        return had;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(lv, &pid);
    HANDLE hp = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
                            PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hp) return false;

    int count = (int)SendT(lv, LVM_GETITEMCOUNT, 0, 0);
    if (count < 0) { CloseHandle(hp); return false; }
    if (count > 4000) count = 4000;

    RemoteMem mem(hp, 8192);
    if (!mem.ptr()) { CloseHandle(hp); return false; }

    std::vector<IconItem> items;
    items.reserve(count);

    for (int i = 0; i < count; i++) {
        // --- position (listview client coords -> screen)
        POINT pt = {0, 0};
        if (SendT(lv, LVM_GETITEMPOSITION, (WPARAM)i, mem.at(0)) < 0) continue;
        if (!mem.read(&pt, sizeof(pt), 0)) continue;
        MapWindowPoints(lv, HWND_DESKTOP, &pt, 1);

        // --- bounds
        RECT rc = {0, 0, 0, 0};
        rc.left = LVIR_BOUNDS;
        mem.write(&rc, sizeof(rc), 32);
        if (SendT(lv, LVM_GETITEMRECT, (WPARAM)i, mem.at(32)) >= 0)
            mem.read(&rc, sizeof(rc), 32);
        else
            SetRect(&rc, pt.x, pt.y, pt.x + 75, pt.y + 75);
        MapWindowPoints(lv, HWND_DESKTOP, (POINT*)&rc, 2);

        // --- the icon bitmap alone (the "sheet of paper" the file looks like)
        RECT ir = rc;
        ir.left = LVIR_ICON;                 // LVM_GETITEMRECT reads the type from .left
        mem.write(&ir, sizeof(ir), 48);
        if (SendT(lv, LVM_GETITEMRECT, (WPARAM)i, mem.at(48)) >= 0 &&
            mem.read(&ir, sizeof(ir), 48) &&
            ir.left != LVIR_ICON && ir.right - ir.left >= 8 && ir.bottom - ir.top >= 8) {
            MapWindowPoints(lv, HWND_DESKTOP, (POINT*)&ir, 2);
        } else {
            ir = rc;
        }

        // --- label
        LVITEMW it = {};
        it.iSubItem = 0;
        it.cchTextMax = 260;
        it.pszText = (LPWSTR)mem.at(1024);
        mem.write(&it, sizeof(it), 64);
        wchar_t text[300] = {0};
        if (SendT(lv, LVM_GETITEMTEXTW, (WPARAM)i, mem.at(64)) > 0) {
            mem.read(text, sizeof(wchar_t) * 259, 1024);
            text[259] = 0;
        }

        IconItem item;
        item.name = text;
        item.rc = rc;
        item.ir = ir;
        item.pos = pt;
        item.cell = -1;
        items.push_back(item);
    }
    CloseHandle(hp);

    // --- occupancy grid
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    std::set<int> occ;
    std::vector<POINT> centers;
    for (auto& it : items) {
        int cx0 = (it.rc.left - vx) / CELL;
        int cx1 = (it.rc.right - vx) / CELL;
        int cy0 = (it.rc.top - vy) / CELL;
        int cy1 = (it.rc.bottom - vy) / CELL;
        for (int gy = cy0; gy <= cy1; gy++)
            for (int gx = cx0; gx <= cx1; gx++) {
                int idx = gy * 4096 + gx;
                occ.insert(idx);
            }
        POINT c = { (it.rc.left + it.rc.right) / 2, (it.rc.top + it.rc.bottom) / 2 };
        it.cell = ((c.y - vy) / CELL) * 4096 + ((c.x - vx) / CELL);
        centers.push_back(c);
    }

    bool changed = m_dirty;
    m_dirty = false;
    if (!changed) {
        if (items.size() != m_icons.size()) changed = true;
        else {
            for (size_t i = 0; i < items.size(); i++) {
                if (items[i].name != m_icons[i].name ||
                    items[i].pos.x != m_icons[i].pos.x ||
                    items[i].pos.y != m_icons[i].pos.y) { changed = true; break; }
            }
        }
    }

    m_icons.swap(items);
    m_occupied.swap(occ);
    m_cellPos.swap(centers);
    m_ready = true;
    if (changed) BuildPaths();
    return changed;
}

bool DesktopWatcher::CellOccupied(int cell) const {
    if (cell < 0) return false;
    return m_occupied.count(cell) != 0;
}

bool DesktopWatcher::AnyIconCovers(float x, float y) const {
    for (const auto& it : m_icons) {
        if (x >= it.rc.left - 3 && x <= it.rc.right + 3 &&
            y >= it.rc.top - 3 && y <= it.rc.bottom + 3) return true;
    }
    return false;
}

int DesktopWatcher::RandomIcon() const {
    if (m_icons.empty()) return -1;
    return (int)(g_rng() % m_icons.size());
}

int DesktopWatcher::CellOf(POINT p) const {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return ((p.y - vy) / CELL) * 4096 + ((p.x - vx) / CELL);
}

POINT DesktopWatcher::CellCenter(int cell) const {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    POINT p;
    p.x = vx + (cell % 4096) * CELL + CELL / 2;
    p.y = vy + (cell / 4096) * CELL + CELL / 2;
    return p;
}

int DesktopWatcher::PickEatableIcon(const std::set<int>& skip) const {
    std::vector<int> cand;
    for (size_t i = 0; i < m_icons.size() && i < m_paths.size(); i++) {
        if (m_paths[i].empty()) continue;
        if (skip.count((int)i)) continue;
        cand.push_back((int)i);
    }
    if (cand.empty()) return -1;
    return cand[g_rng() % cand.size()];
}

std::wstring DesktopWatcher::PathOfIcon(int idx) const {
    if (idx < 0 || idx >= (int)m_paths.size()) return std::wstring();
    return m_paths[idx];
}

// ---------------------------------------------------------------- hide / restore
bool DesktopWatcher::HideFile(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) return false;
    if (attr & FILE_ATTRIBUTE_READONLY) return false;   // don't fight read-only media

    // equivalent of: attrib +s +h "path"
    if (!SetFileAttributesW(path.c_str(), attr | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
        return false;

    FILE* f = _wfopen(EatenLogPath().c_str(), L"a, ccs=UTF-8");
    if (f) {
        fwprintf(f, L"%ls\t%lu\n", path.c_str(), (unsigned long)attr);
        fclose(f);
    } else {
        // could not journal it -> undo immediately, never hide unlogged files
        SetFileAttributesW(path.c_str(), attr);
        return false;
    }

    // make Explorer drop the icon right away
    wchar_t desk[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk);
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, desk, nullptr);
    m_dirty = true;
    m_hiddenCount = HiddenCount() + 1;
    LogLine(L"eaten: %ls", path.c_str());
    return true;
}

// strip a UTF-8 BOM that the CRT may have written at the head of the journal
static void StripBom(std::wstring& s) {
    if (!s.empty() && s[0] == 0xFEFF) s.erase(s.begin());
}

int DesktopWatcher::HiddenCount() {
    if (m_hiddenCount >= 0) return m_hiddenCount;
    FILE* f = _wfopen(EatenLogPath().c_str(), L"r, ccs=UTF-8");
    if (!f) { m_hiddenCount = 0; return 0; }
    wchar_t line[2048];
    int n = 0;
    bool first = true;
    while (fgetws(line, 2048, f)) {
        std::wstring s = line;
        if (first) { StripBom(s); first = false; }
        bool blank = true;
        for (auto c : s) if (c != L'\n' && c != L'\r' && c != L' ' && c != L'\t') { blank = false; break; }
        if (!blank) n++;
    }
    fclose(f);
    m_hiddenCount = n;
    return n;
}

int DesktopWatcher::RestoreAll(std::wstring* outMsg) {
    std::vector<std::wstring> paths;
    std::vector<DWORD> attrs;
    FILE* f = _wfopen(EatenLogPath().c_str(), L"r, ccs=UTF-8");
    if (f) {
        wchar_t line[2048];
        bool first = true;
        while (fgetws(line, 2048, f)) {
            std::wstring s = line;
            if (first) { StripBom(s); first = false; }
            while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
            if (s.empty()) continue;
            size_t tab = s.find(L'\t');
            if (tab == std::wstring::npos) { paths.push_back(s); attrs.push_back(FILE_ATTRIBUTE_NORMAL); }
            else { paths.push_back(s.substr(0, tab)); attrs.push_back((DWORD)_wtoi(s.c_str() + tab + 1)); }
        }
        fclose(f);
    }

    int done = 0;
    for (size_t i = 0; i < paths.size(); i++) {
        DWORD cur = GetFileAttributesW(paths[i].c_str());
        if (cur == INVALID_FILE_ATTRIBUTES) continue;      // gone, skip
        DWORD target = attrs[i] & ~(FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        if (target == 0) target = FILE_ATTRIBUTE_NORMAL;
        // equivalent of: attrib -s -h "path"
        if (SetFileAttributesW(paths[i].c_str(), target)) done++;
    }

    _wremove(EatenLogPath().c_str());
    m_hiddenCount = 0;
    wchar_t desk[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk);
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, desk, nullptr);
    m_dirty = true;

    if (outMsg) {
        wchar_t b[256];
        if (done > 0) swprintf(b, 256, T(S_RESTORE_OK), done);
        else          swprintf(b, 256, L"%ls", T(S_RESTORE_NONE));
        *outMsg = b;
    }
    LogLine(L"restored %d file(s)", done);
    return done;
}
