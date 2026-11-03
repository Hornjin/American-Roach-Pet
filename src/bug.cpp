// bug.cpp : individual insects, behaviour state machine, layered windows, frass
#include "common.h"

static const wchar_t* kBugClass  = L"RoachPetBugWnd";
static const wchar_t* kPoopClass = L"RoachPetPoopWnd";
static bool  s_classReady = false;
static float s_eatCooldown = 12.0f;
static std::set<int> s_eatingIcons;

static std::vector<Bug*>& g_bugs_impl() {
    static std::vector<Bug*> v;
    return v;
}
std::vector<Bug*>& Bug::All() { return g_bugs_impl(); }

// ---------------------------------------------------------------- sprite size
static int StageSprite(Stage st) {
    float s = g_cfg.scale;
    switch (st) {
        case Stage::Adult:     return (int)(150 * s);
        case Stage::Juvenile:  return (int)(104 * s);
        case Stage::Hatchling: return (int)(72  * s);
        default:               return (int)(56  * s);
    }
}
static float StageBodyLen(Stage st, int S) {
    if (st == Stage::Ootheca) return S * 0.46f;
    return S * 0.30f;
}
static float StageSpeed(Stage st) {
    switch (st) {
        case Stage::Adult:     return 1.00f;
        case Stage::Juvenile:  return 0.78f;
        case Stage::Hatchling: return 0.58f;
        default:               return 0.0f;
    }
}

// =========================================================================
Bug::Bug(Stage st, float x, float y, int mon, bool autoHide) : m_stage(st), m_x(x), m_y(y), m_mon(mon) {
    m_ang = frand(0.0f, 6.2831853f);
    m_speed = 0;
    m_t = frand(0, 100.0f);
    m_gait = frand(0, 6.28f);
    m_antPhase = frand(0, 6.28f);
    m_tx = x; m_ty = y;
    m_restT = frand(0.2f, 2.5f);
    resizeWindowForStage();
    createWindow();
    // egg cases and hatchlings start concealed under a file
    if (autoHide && (st == Stage::Ootheca || st == Stage::Hatchling)) {
        if (g_desk.Ready() && !g_desk.icons().empty()) {
            int idx = g_desk.RandomIcon();
            if (idx >= 0) enterHideUnder(idx);
        }
    }
    g_bugs_impl().push_back(this);
}

Bug::~Bug() {
    DestroyWindowSafe();
}

void Bug::GlobalInit() { RegisterWndClass(); }

void Bug::RegisterWndClass() {
    if (s_classReady) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = Bug::WndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kBugClass;
    RegisterClassExW(&wc);
    s_classReady = true;
}

void Bug::UnregisterWndClass() {
    if (!s_classReady) return;
    ::UnregisterClassW(kBugClass, g_hInst);
    s_classReady = false;
}

LRESULT CALLBACK Bug::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND:    return 1;
        case WM_NCHITTEST:     return g_cfg.clickThrough ? HTTRANSPARENT : HTCLIENT;
        case WM_DESTROY:       return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void Bug::setShown(bool v) {
    if (!m_hwnd) return;
    bool hideAll = (m_state == BState::Hidden && m_stage == Stage::Ootheca);
    if (v && !hideAll) {
        if (!IsWindowVisible(m_hwnd)) ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    } else {
        if (IsWindowVisible(m_hwnd)) ShowWindow(m_hwnd, SW_HIDE);
    }
}

void Bug::createWindow() {
    DWORD ex = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
    if (g_cfg.clickThrough) ex |= WS_EX_TRANSPARENT;
    m_hwnd = CreateWindowExW(ex, kBugClass, L"", WS_POPUP,
                             (int)m_x, (int)m_y, m_bw, m_bh,
                             nullptr, nullptr, g_hInst, nullptr);
    if (m_hwnd && g_visible) ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
}

void Bug::refreshWindowStyle() {
    if (!m_hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if (g_cfg.clickThrough) ex |= WS_EX_TRANSPARENT;
    else                    ex &= ~WS_EX_TRANSPARENT;
    ex |= WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex);
}

void Bug::DestroyWindowSafe() {
    if (m_memDC) { DeleteDC(m_memDC); m_memDC = nullptr; }
    if (m_bmp)   { DeleteObject(m_bmp); m_bmp = nullptr; }
    m_bits = nullptr;
    if (m_hwnd)  { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}

void Bug::resizeWindowForStage() {
    int S = StageSprite(m_stage);
    m_S = S;
    m_bodyLen = StageBodyLen(m_stage, S);
    if (m_bw == S && m_bmp) return;
    m_bw = m_bh = S;

    if (m_memDC) { DeleteDC(m_memDC); m_memDC = nullptr; }
    if (m_bmp)   { DeleteObject(m_bmp); m_bmp = nullptr; }

    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = S;
    bi.bV5Height = -S;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    m_memDC = CreateCompatibleDC(nullptr);
    m_bmp = CreateDIBSection(m_memDC, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &m_bits, nullptr, 0);
    if (m_bmp) SelectObject(m_memDC, m_bmp);
    if (m_hwnd) SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, S, S, SWP_NOMOVE | SWP_NOACTIVATE);
}

// ---------------------------------------------------------------- behaviour
void Bug::enterHideUnder(int iconIdx) {
    m_state = BState::Hidden;
    m_hideIcon = iconIdx;
    m_hideCell = (iconIdx >= 0 && iconIdx < (int)g_desk.icons().size())
                     ? g_desk.CellOf(g_desk.icons()[iconIdx].pos) : -1;
    m_speed = 0;
    m_stateT = 0;

    if (iconIdx >= 0 && iconIdx < (int)g_desk.icons().size()) {
        const IconItem& it = g_desk.icons()[iconIdx];
        // the bitmap of the icon, not the whole cell: that is the piece of
        // "paper" the antennae have to slide out from under
        RECT ir = it.ir;
        if (ir.right - ir.left < 6 || ir.bottom - ir.top < 6) ir = it.rc;

        float cx = (float)(ir.left + ir.right) * 0.5f;
        float cy = (float)(ir.top + ir.bottom) * 0.5f;
        float hw = (float)(ir.right - ir.left) * 0.5f - 3.0f;
        float hh = (float)(ir.bottom - ir.top) * 0.5f - 3.0f;
        if (hw < 4) hw = 4;
        if (hh < 4) hh = 4;
        // aim the head at the rim of the icon so the antennae poke out from
        // underneath the file instead of sitting in the middle of it
        float a = frand(0.0f, 6.2831853f);
        m_x = cx + cosf(a) * hw;
        m_y = cy + sinf(a) * hh;
        m_ang = a + frand(-0.3f, 0.3f);
        m_coverRc = ir;
        // silhouette of the artwork inside that rectangle (may be empty, in
        // which case the rectangle itself is used as the occluder)
        bool sameRc = (ir.left == it.ir.left && ir.top == it.ir.top &&
                       ir.right == it.ir.right && ir.bottom == it.ir.bottom);
        m_coverSpan  = sameRc ? it.span : std::vector<std::pair<short, short>>();
        m_coverMaskW = sameRc ? it.maskW : 0;
        m_coverMaskH = sameRc ? it.maskH : 0;
        if (getenv("ROACH_MASK_LOG"))
            LogLine(L"hide under icon=%d name=%ls masked=%d mask=%dx%d", iconIdx,
                    it.name.c_str(), (int)!m_coverSpan.empty(), m_coverMaskW, m_coverMaskH);
    }
    if (m_stage == Stage::Ootheca) m_life = -frand(70.0f, 90.0f);   // hatching timer
}

void Bug::reveal() {
    if (m_state != BState::Hidden) return;
    bool wasEgg = (m_stage == Stage::Ootheca);
    m_hideIcon = -1;
    m_hideCell = -1;
    m_state = BState::Emerge;
    m_stateT = 0;
    m_speed = frand(150.0f, 230.0f) * StageSpeed(m_stage);
    m_ang = frand(0.0f, 6.2831853f);
    // an egg case does not run and does not start a stampede
    if (!wasEgg) BugPanicAll(m_x, m_y, 260.0f, 0.9f);
}

void Bug::burstOut() {
    m_state = BState::Emerge;
    m_stateT = 0;
    m_speed = frand(140.0f, 210.0f) * StageSpeed(m_stage);
    m_ang = frand(0.0f, 6.2831853f);
    m_resting = false;
}

void Bug::enterEat(int iconIdx) {
    m_state = BState::Eat;
    m_eatIcon = iconIdx;
    m_eatHold = 0;
    m_stateT = 0;
    m_speed = 0;
    m_chew = 0;
    s_eatingIcons.insert(iconIdx);
}

void Bug::finishEat() {
    if (m_eatIcon >= 0) {
        std::wstring p = g_desk.PathOfIcon(m_eatIcon);
        if (!p.empty()) g_desk.HideFile(p);
        s_eatingIcons.erase(m_eatIcon);
    }
    m_eatIcon = -1;
    m_state = BState::Poop;
    m_stateT = 0;
}

void Bug::spawnPoop() {
    if (!g_cfg.poopEnabled) return;
    int n = irand(1, 3);
    for (int i = 0; i < n; i++) {
        float a = frand(0.0f, 6.2831853f);
        float r = frand(8.0f, 26.0f);
        PoopAdd(m_x + cosf(a) * r, m_y + sinf(a) * r, frand(5.0f, 8.0f) * g_cfg.scale);
    }
}

void Bug::panicFrom(float px, float py, float radius, float power) {
    if (m_state == BState::Eat) { if (m_eatIcon >= 0) s_eatingIcons.erase(m_eatIcon); m_eatIcon = -1; }
    if (m_state == BState::Hidden) return;
    float dx = m_x - px, dy = m_y - py;
    float d = sqrtf(dx * dx + dy * dy);
    if (d > radius) return;
    if (d < 1.0f) { dx = frand(-1, 1); dy = frand(-1, 1); d = 1.0f; }
    m_ang = atan2f(dy, dx) + frand(-0.35f, 0.35f);
    m_state = BState::Flee;
    m_stateT = 0;
    m_speed = (150.0f + 140.0f * power) * StageSpeed(m_stage) * frand(0.8f, 1.25f);
    m_resting = false;
}

void Bug::pickWanderTarget(const RECT& bounds) {
    float bx0 = (float)bounds.left + 12, bx1 = (float)bounds.right - 12;
    float by0 = (float)bounds.top + 12,  by1 = (float)bounds.bottom - 12;
    if (bx1 <= bx0) bx1 = bx0 + 1;
    if (by1 <= by0) by1 = by0 + 1;

    // 72 % of the time head for a spot among the icons, otherwise roam empty space
    if (!g_desk.icons().empty() && frand() < 0.72f) {
        const auto& v = g_desk.icons();
        const IconItem& it = v[g_rng() % v.size()];
        float px = (float)(it.rc.left + it.rc.right) * 0.5f + frand(-150, 150);
        float py = (float)(it.rc.top + it.rc.bottom) * 0.5f + frand(-120, 120);
        m_tx = clampf(px, bx0, bx1);
        m_ty = clampf(py, by0, by1);
    } else {
        m_tx = frand(bx0, bx1);
        m_ty = frand(by0, by1);
    }
}

void Bug::steerTo(float tx, float ty, float dt, float turnRate) {
    float want = atan2f(ty - m_y, tx - m_x);
    float diff = want - m_ang;
    while (diff > 3.14159265f)  diff -= 6.2831853f;
    while (diff < -3.14159265f) diff += 6.2831853f;
    float maxTurn = turnRate * dt;
    if (diff > maxTurn) diff = maxTurn;
    if (diff < -maxTurn) diff = -maxTurn;
    m_ang += diff;
}

void Bug::clampToBounds(const RECT& bounds) {
    float pad = m_bodyLen * 0.55f;
    float x0 = bounds.left + pad, x1 = bounds.right - pad;
    float y0 = bounds.top + pad,  y1 = bounds.bottom - pad;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    bool hit = false;
    if (m_x < x0) { m_x = x0; hit = true; }
    if (m_x > x1) { m_x = x1; hit = true; }
    if (m_y < y0) { m_y = y0; hit = true; }
    if (m_y > y1) { m_y = y1; hit = true; }
    if (hit && m_stage != Stage::Ootheca) m_ang += 2.4f + frand(-0.5f, 0.5f);
}

void Bug::Update(float dt, const RECT& bounds) {
    if (!m_alive) return;

    // scale / stage changed?
    if (StageSprite(m_stage) != m_S) resizeWindowForStage();

    m_t += dt;
    m_stateT += dt;

    // -------- concealment bookkeeping
    if (m_state == BState::Hidden) {
        // Which file is still on top of us?  Icon indices shift whenever the
        // desktop changes, so the owning file is looked up by position.
        int idx = -1;
        const auto& icons = g_desk.icons();
        for (size_t i = 0; i < icons.size(); i++) {
            const RECT& rc = icons[i].rc;
            if (m_x >= (float)rc.left - 2 && m_x <= (float)rc.right + 2 &&
                m_y >= (float)rc.top - 2 && m_y <= (float)rc.bottom + 2) { idx = (int)i; break; }
        }
        if (idx < 0) {
            reveal();     // the user moved the file away
        } else {
            m_hideIcon = idx;
            const IconItem& it = icons[idx];
            m_coverRc = (it.ir.right - it.ir.left >= 6) ? it.ir : it.rc;

            // idle antennae twitch
            m_antPhase += dt * 2.2f;
            m_chew = 0;
            if (m_stage == Stage::Ootheca) {
                m_life += dt;
                if (m_life >= 0 && BugCount() < g_cfg.maxBugs) {
                    // the nymph chews its way out and scuttles off
                    Bug* b = new Bug(Stage::Hatchling, m_x, m_y, m_mon, false);
                    b->burstOut();
                    m_alive = false;    // the ootheca is spent
                }
            } else {
                m_life += dt;
                if (m_stage == Stage::Hatchling && m_life > 55.0f) {
                    m_stage = Stage::Juvenile; m_life = 0;
                }
            }
            return;
        }
    }

    float spdFactor = StageSpeed(m_stage);

    switch (m_state) {
    case BState::Wander: {
        if (m_stage == Stage::Hatchling) { m_life += dt; if (m_life > 55.0f) { m_stage = Stage::Juvenile; m_life = 0; } }
        if (m_stage == Stage::Juvenile)  { m_life += dt; if (m_life > 70.0f) { m_stage = Stage::Adult;   m_life = 0; } }

        // scuttling towards a file to hide under
        if (m_seekIcon >= 0) {
            if (m_seekIcon >= (int)g_desk.icons().size()) {
                m_seekIcon = -1;
            } else {
                const RECT& rc = g_desk.icons()[m_seekIcon].rc;
                float cx = (float)(rc.left + rc.right) * 0.5f;
                float cy = (float)(rc.top + rc.bottom) * 0.5f;
                m_tx = cx; m_ty = cy;
                m_resting = false;
                m_speed = std::max(m_speed, 110.0f * spdFactor);
                if (hypotf(cx - m_x, cy - m_y) < 18.0f) {
                    enterHideUnder(m_seekIcon);
                    m_seekIcon = -1;
                    return;
                }
            }
        }

        if (m_resting) {
            m_speed = 0;
            m_restT -= dt;
            if (m_restT <= 0) {
                m_resting = false;
                pickWanderTarget(bounds);
                m_speed = frand(70.0f, 150.0f) * spdFactor;
                m_restT = frand(0.35f, 1.6f);   // burst duration
            }
        } else {
            m_restT -= dt;
            steerTo(m_tx, m_ty, dt, 5.5f);
            if (m_stage != Stage::Ootheca)
                m_ang += sinf(m_t * 6.0f + m_antPhase) * 0.9f * dt;   // slight waddle
            float dx = m_tx - m_x, dy = m_ty - m_y;
            if (dx * dx + dy * dy < 900.0f || m_restT <= 0) {
                m_resting = true;
                m_restT = frand(0.25f, 2.2f);
            }
        }
        // decide to scuttle under a nearby file
        if (m_stage != Stage::Adult && m_stage != Stage::Juvenile && frand() < 0.10f * dt) {
            // small nymphs look for cover
        }
        // decide to eat
        if (g_cfg.eatEnabled && s_eatCooldown <= 0 &&
            (m_stage == Stage::Adult || m_stage == Stage::Juvenile) &&
            g_desk.HiddenCount() < g_cfg.maxHiddenFiles &&
            !g_desk.icons().empty() && frand() < 0.55f * dt) {
            int idx = g_desk.PickEatableIcon(s_eatingIcons);
            if (idx >= 0) {
                m_eatIcon = idx;
                const IconItem& it = g_desk.icons()[idx];
                m_tx = (float)(it.rc.left + it.rc.right) * 0.5f;
                m_ty = (float)(it.rc.top + it.rc.bottom) * 0.5f;
                m_state = BState::Eat;
                m_eatHold = 0;
                m_stateT = 0;
                s_eatingIcons.insert(idx);
                s_eatCooldown = (float)g_cfg.eatIntervalSec;
            }
        }
        break;
    }
    case BState::Eat: {
        if (m_eatIcon < 0 || m_eatIcon >= (int)g_desk.icons().size()) { m_state = BState::Wander; break; }
        const IconItem& it = g_desk.icons()[m_eatIcon];
        float cx = (float)(it.rc.left + it.rc.right) * 0.5f;
        float cy = (float)(it.rc.top + it.rc.bottom) * 0.5f;
        float dx = cx - m_x, dy = cy - m_y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > 22.0f) {
            m_speed = 95.0f * spdFactor;
            steerTo(cx, cy, dt, 7.0f);
            m_x += cosf(m_ang) * m_speed * dt;
            m_y += sinf(m_ang) * m_speed * dt;
            m_eatHold = 0;
        } else {
            m_speed = 0;
            m_ang = atan2f(0.0f, 1.0f) + frand(-0.1f, 0.1f);
            m_eatHold += dt;
            m_chew = m_eatHold;
            if (m_eatHold > 3.2f) finishEat();
        }
        break;
    }
    case BState::Poop: {
        m_speed = 0;
        m_chew += dt;
        if (m_stateT > 0.85f) {
            spawnPoop();
            s_eatingIcons.erase(m_eatIcon);
            m_eatIcon = -1;
            m_state = BState::Wander;
            m_resting = false;
            m_restT = frand(0.3f, 1.0f);
            pickWanderTarget(bounds);
            m_speed = frand(90.0f, 160.0f) * spdFactor;
        }
        break;
    }
    case BState::Flee:
    case BState::Emerge: {
        float maxT = (m_state == BState::Emerge) ? 1.4f : 1.0f;
        if (m_stage != Stage::Ootheca) m_ang += sinf(m_t * 11.0f) * 1.6f * dt;
        if (m_stateT > maxT) {
            // some dive back under cover, the rest keep running in the open
            if (m_state == BState::Emerge && frand() < 0.62f && !g_desk.icons().empty()) {
                for (int k = 0; k < 12; k++) {
                    int idx = g_desk.RandomIcon();
                    if (idx < 0) break;
                    const RECT& rc = g_desk.icons()[idx].rc;
                    float cx = (float)(rc.left + rc.right) * 0.5f;
                    float cy = (float)(rc.top + rc.bottom) * 0.5f;
                    if (hypotf(cx - m_x, cy - m_y) < 70.0f) continue;
                    m_seekIcon = idx;
                    m_tx = cx; m_ty = cy;
                    m_state = BState::Wander;
                    m_speed = frand(120, 190) * spdFactor;
                    m_restT = 6.0f;
                    m_resting = false;
                    goto flee_done;
                }
            }
            m_state = BState::Wander;
            m_resting = false;
            m_restT = frand(0.3f, 1.2f);
            pickWanderTarget(bounds);
            m_speed = frand(60, 110) * spdFactor;
        flee_done:
            ;
        }
        break;
    }
    default: break;
    }

    // integrate
    if (m_state != BState::Eat) {
        m_x += cosf(m_ang) * m_speed * dt;
        m_y += sinf(m_ang) * m_speed * dt;
    } else if (m_eatHold > 0) {
        // nibbling jitter
        m_x += cosf(m_ang) * sinf(m_chew * 18.0f) * 6.0f * dt;
    }

    clampToBounds(bounds);

    float moving = std::min(1.0f, m_speed / 60.0f);
    m_gait += (m_speed * 0.085f + 1.2f * moving) * dt;
    m_antPhase += (1.8f + m_speed * 0.02f) * dt;

    // hover panic
    if (g_cfg.hoverFlee && m_state != BState::Flee && m_state != BState::Eat) {
        POINT mp; GetCursorPos(&mp);
        float dx = m_x - mp.x, dy = m_y - mp.y;
        if (dx * dx + dy * dy < 34.0f * 34.0f) {
            m_ang = atan2f(dy, dx) + frand(-0.3f, 0.3f);
            m_state = BState::Flee;
            m_stateT = 0;
            m_speed = frand(210.0f, 300.0f) * spdFactor;
            m_resting = false;
        }
    }
}

// ---------------------------------------------------------------- present
void Bug::drawInto(Gdiplus::Graphics& g, int ox, int oy) {
    bool hideAll = (m_state == BState::Hidden && m_stage == Stage::Ootheca);
    if (hideAll) return;

    float cx = m_bw * 0.5f, cy = m_bh * 0.5f;
    float L = m_bodyLen;

    if (m_state == BState::Hidden) {
        // Only the antennae poke out from under the icon.  The file itself is
        // not part of this layered window, so the part of the antennae that
        // would lie on top of the icon is cut away - what is left really does
        // look like it is emerging from underneath the file.
        if (m_coverRc.right > m_coverRc.left && m_coverRc.bottom > m_coverRc.top) {
            int irW = m_coverRc.right - m_coverRc.left;
            int irH = m_coverRc.bottom - m_coverRc.top;
            bool masked = (m_coverMaskW > 0 && m_coverMaskH > 0 &&
                           (int)m_coverSpan.size() == m_coverMaskH);

            if (masked) {
                // Exclude the icon's actual silhouette instead of its bounding
                // box, so the antennae run right up to the drawn artwork and
                // stop there - not at an invisible rectangle edge.
                Gdiplus::GraphicsPath path;
                int y = 0;
                while (y < m_coverMaskH) {
                    short x0 = m_coverSpan[y].first, x1 = m_coverSpan[y].second;
                    int run = 1;
                    while (y + run < m_coverMaskH &&
                           m_coverSpan[y + run].first == x0 &&
                           m_coverSpan[y + run].second == x1) run++;

                    if (x1 > x0) {
                        int py0 = m_coverRc.top + y * irH / m_coverMaskH;
                        int py1 = m_coverRc.top + (y + run) * irH / m_coverMaskH;
                        int px0 = m_coverRc.left + x0 * irW / m_coverMaskW;
                        int px1 = m_coverRc.left + x1 * irW / m_coverMaskW;
                        if (py1 > py0 && px1 > px0)
                            path.AddRectangle(Gdiplus::Rect(px0 - ox, py0 - oy,
                                                            px1 - px0, py1 - py0));
                    }
                    y += run;
                }
                if (path.GetPointCount() > 0)
                    g.SetClip(&path, Gdiplus::CombineModeExclude);
            } else {
                Gdiplus::Rect hole(m_coverRc.left - ox, m_coverRc.top - oy, irW, irH);
                g.SetClip(hole, Gdiplus::CombineModeExclude);
            }
        }
        float amp = 0.9f + 0.35f * sinf(m_t * 0.7f);
        R::DrawAntennaeOnly(g, cx, cy, m_ang, L, m_t, m_antPhase, amp);
        return;
    }

    float gaitAmp = 0.35f + 0.75f * std::min(1.0f, m_speed / 90.0f);
    bool eating = (m_state == BState::Eat && m_eatHold > 0.15f) || m_state == BState::Poop;
    float chew = (m_state == BState::Eat) ? m_eatHold : (m_state == BState::Poop ? m_stateT : 0.0f);
    float amp = 0.85f + 0.4f * sinf(m_t * 0.9f + m_antPhase * 0.2f);

    R::DrawBug(g, cx, cy, m_ang, m_stage, L, m_t, m_gait, gaitAmp,
               eating, chew, m_antPhase, amp);
}

void Bug::Present() {
    if (!m_hwnd || !m_memDC || !m_bits) return;

    bool hideAll = (m_state == BState::Hidden && m_stage == Stage::Ootheca);
    if (hideAll || !g_visible) {
        if (IsWindowVisible(m_hwnd)) ShowWindow(m_hwnd, SW_HIDE);
        return;
    }

    // The insect lives on the desktop; application windows are solid objects
    // on top of that surface.  Clip the sprite against them (or drop the
    // window entirely when it is fully covered) so nothing - not even a
    // single antenna tip - is ever painted over somebody else's window.
    RECT sprite;
    sprite.left   = (LONG)floorf(m_x - m_bw * 0.5f);
    sprite.top    = (LONG)floorf(m_y - m_bh * 0.5f);
    sprite.right  = sprite.left + m_bw;
    sprite.bottom = sprite.top + m_bh;

    RECT cov[16];
    bool fullyCovered = false;
    int nCov = g_cfg.hideOnApp ? ZOrderCollect(sprite, cov, 16, &fullyCovered) : 0;
    if (!fullyCovered && nCov > 1) {
        // several windows can cover the sprite together even when none of them
        // does it alone - sample the box corners instead of compositing an
        // image that would come out completely transparent
        const float px[5] = { (float)sprite.left, (float)sprite.right - 1, (float)sprite.left,
                              (float)sprite.right - 1, (float)(sprite.left + sprite.right) / 2 };
        const float py[5] = { (float)sprite.top, (float)sprite.top, (float)sprite.bottom - 1,
                              (float)sprite.bottom - 1, (float)(sprite.top + sprite.bottom) / 2 };
        bool all = true;
        for (int k = 0; k < 5 && all; k++) {
            bool hit = false;
            for (int i = 0; i < nCov; i++) {
                if (px[k] >= cov[i].left && px[k] <= cov[i].right &&
                    py[k] >= cov[i].top && py[k] <= cov[i].bottom) { hit = true; break; }
            }
            if (!hit) all = false;
        }
        fullyCovered = all;
    }
    if (fullyCovered) {
        if (IsWindowVisible(m_hwnd)) ShowWindow(m_hwnd, SW_HIDE);
        return;
    }

    if (!IsWindowVisible(m_hwnd)) ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

    {
        Gdiplus::Bitmap bmp(m_bw, m_bh, m_bw * 4, PixelFormat32bppPARGB, (BYTE*)m_bits);
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        if (g_cfg.debugBg) {
            Gdiplus::SolidBrush b(Gdiplus::Color(90, 40, 160, 255));
            g.FillRectangle(&b, 0, 0, m_bw, m_bh);
        }
        for (int i = 0; i < nCov; i++) {
            Gdiplus::Rect r(cov[i].left - sprite.left, cov[i].top - sprite.top,
                            cov[i].right - cov[i].left, cov[i].bottom - cov[i].top);
            g.SetClip(r, Gdiplus::CombineModeExclude);
        }
        drawInto(g, sprite.left, sprite.top);
    }

    POINT dst = { sprite.left, sprite.top };
    SIZE  sz  = { m_bw, m_bh };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(g_cfg.opacity * 255 / 100), AC_SRC_ALPHA };
    UpdateLayeredWindow(m_hwnd, nullptr, &dst, &sz, m_memDC, &src, 0, &bf, ULW_ALPHA);
}

// ---------------------------------------------------------------- globals
void BugTickAll(float dt, const RECT& bounds) {
    if (s_eatCooldown > 0) s_eatCooldown -= dt;
    auto& v = Bug::All();
    for (auto* b : v) if (b->alive()) b->Update(dt, bounds);

    // reap dead
    for (size_t i = 0; i < v.size();) {
        if (!v[i]->alive()) { delete v[i]; v.erase(v.begin() + i); }
        else i++;
    }

    // ootheca laying by adults
    static float layT = 40.0f;
    layT -= dt;
    if (layT <= 0) {
        layT = frand(55.0f, 110.0f);
        if ((int)v.size() < g_cfg.maxBugs) {
            int adults = 0;
            for (auto* b : v) if (b->stage() == Stage::Adult) adults++;
            if (adults >= 2 && !g_desk.icons().empty()) {
                for (int k = 0; k < 16; k++) {
                    const IconItem& it = g_desk.icons()[g_rng() % g_desk.icons().size()];
                    int cell = g_desk.CellOf(it.pos);
                    if (!g_desk.CellOccupied(cell)) continue;
                    POINT c = g_desk.CellCenter(cell);
                    new Bug(Stage::Ootheca, (float)c.x, (float)c.y, 0);
                    break;
                }
            }
        }
    }
}

void BugPresentAll() {
    for (auto* b : Bug::All()) if (b->alive()) b->Present();
}

void BugPanicAll(float px, float py, float radius, float power) {
    for (auto* b : Bug::All()) if (b->alive()) b->panicFrom(px, py, radius, power);
}

void BugDestroyAll() {
    auto& v = Bug::All();
    for (auto* b : v) delete b;
    v.clear();
    s_eatingIcons.clear();
}

void BugSetVisible(bool vis) {
    for (auto* b : Bug::All()) if (b->alive()) b->setShown(vis);
}

void BugForceShow(bool vis) {
    for (auto* b : Bug::All()) if (b->alive()) b->setShown(vis);
    PoopSetVisible(vis);
}

// While the settings dialog is open the pets must not cover it, so drop them
// below normal windows; restore TOPMOST when the dialog closes.
void BugSetTopmost(bool on) {
    HWND ins = on ? HWND_TOPMOST : HWND_NOTOPMOST;
    for (auto* b : Bug::All()) {
        if (!b->alive()) continue;
        HWND h = b->hwnd();
        if (!h) continue;
        SetWindowPos(h, ins, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    HWND ph = PoopHwnd();
    if (ph) SetWindowPos(ph, ins, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void BugRestorePopulation() {
    int want = g_cfg.maxBugs;
    // mostly grown insects out in the open, a few egg cases tucked under files
    int adults = want * 32 / 100; if (adults < 3) adults = 3;
    int juv    = want * 20 / 100; if (juv < 2) juv = 2;
    int hatch  = want * 12 / 100; if (hatch < 1) hatch = 1;
    int ooth   = want * 14 / 100; if (ooth < 2) ooth = 2;

    RECT b = g_curBounds;
    for (int i = 0; i < adults; i++) {
        float x = frand((float)b.left + 40, (float)b.right - 40);
        float y = frand((float)b.top + 40, (float)b.bottom - 40);
        new Bug(Stage::Adult, x, y, 0);
    }
    for (int i = 0; i < juv; i++) {
        float x = frand((float)b.left + 40, (float)b.right - 40);
        float y = frand((float)b.top + 40, (float)b.bottom - 40);
        new Bug(Stage::Juvenile, x, y, 0);
    }
    for (int i = 0; i < hatch; i++) {
        float x = frand((float)b.left + 40, (float)b.right - 40);
        float y = frand((float)b.top + 40, (float)b.bottom - 40);
        new Bug(Stage::Hatchling, x, y, 0);
    }
    for (int i = 0; i < ooth; i++) {
        float x = frand((float)b.left + 40, (float)b.right - 40);
        float y = frand((float)b.top + 40, (float)b.bottom - 40);
        new Bug(Stage::Ootheca, x, y, 0);
    }
}

int BugCount() { return (int)Bug::All().size(); }

void BugLogStats() {
    int nAdult = 0, nJuv = 0, nHatch = 0, nOoth = 0;
    int nHidden = 0, nEat = 0, nFlee = 0, nWander = 0;
    for (auto* b : Bug::All()) {
        if (!b->alive()) continue;
        switch (b->stage()) {
            case Stage::Adult: nAdult++; break;
            case Stage::Juvenile: nJuv++; break;
            case Stage::Hatchling: nHatch++; break;
            default: nOoth++; break;
        }
        if (b->hidden()) nHidden++;
        else switch (b->state()) {
            case BState::Eat: nEat++; break;
            case BState::Flee: case BState::Emerge: nFlee++; break;
            default: nWander++; break;
        }
    }
    LogLine(L"stats: total=%d adult=%d juv=%d hatch=%d ooth=%d | hidden=%d eat=%d flee=%d wander=%d | poop=%d icons=%d eatable=%d hiddenFiles=%d | vis=%d",
            BugCount(), nAdult, nJuv, nHatch, nOoth,
            nHidden, nEat, nFlee, nWander,
            PoopCount(), (int)g_desk.icons().size(), g_desk.PathCount(), g_desk.HiddenCount(),
            g_visible ? 1 : 0);
    for (auto* b : Bug::All()) {
        if (!b->alive() || !b->hidden()) continue;
        LogLine(L"  hidden bug at cell=%d icon=%d pos=%.0f,%.0f stage=%d",
                b->hideCell(), b->hideIcon(), b->x(), b->y(), (int)b->stage());
    }
}

// =========================================================================
// frass (faeces) layer : one shared layered window, repainted only on change
// =========================================================================
namespace {
struct Poop { float x, y, size, rot; };
std::vector<Poop> s_poop;
HWND   s_poopWnd = nullptr;
HDC    s_poopDC = nullptr;
HBITMAP s_poopBmp = nullptr;
void*  s_poopBits = nullptr;
int    s_pw = 0, s_ph = 0;
bool   s_poopDirty = true;

LRESULT CALLBACK PoopProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, m, w, l);
}

void PoopEnsureWindow() {
    if (s_poopWnd) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PoopProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = kPoopClass;
    RegisterClassExW(&wc);
    DWORD ex = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
    if (g_cfg.clickThrough) ex |= WS_EX_TRANSPARENT;
    s_poopWnd = CreateWindowExW(ex, kPoopClass, L"", WS_POPUP, 0, 0, 1, 1,
                                nullptr, nullptr, g_hInst, nullptr);
    ShowWindow(s_poopWnd, SW_SHOWNOACTIVATE);
}
} // namespace

void PoopAdd(float x, float y, float size) {
    if (s_poop.size() > 160) s_poop.erase(s_poop.begin());
    Poop p;
    p.x = x; p.y = y; p.size = size; p.rot = frand(0.0f, 360.0f);
    s_poop.push_back(p);
    s_poopDirty = true;
}

void PoopClear() { s_poop.clear(); s_poopDirty = true; }
int  PoopCount() { return (int)s_poop.size(); }
void PoopMarkDirty() { s_poopDirty = true; }

void PoopPresent() {
    PoopEnsureWindow();
    if (!s_poopDirty) return;
    s_poopDirty = false;

    if (s_poop.empty() || !g_visible) {
        if (s_poopWnd && IsWindowVisible(s_poopWnd)) ShowWindow(s_poopWnd, SW_HIDE);
        return;
    }

    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (auto& p : s_poop) {
        x0 = std::min(x0, p.x - p.size); x1 = std::max(x1, p.x + p.size);
        y0 = std::min(y0, p.y - p.size); y1 = std::max(y1, p.y + p.size);
    }
    int ox = (int)floorf(x0) - 2, oy = (int)floorf(y0) - 2;
    int w = (int)(x1 - x0) + 8, h = (int)(y1 - y0) + 8;
    if (w < 4) w = 4;
    if (h < 4) h = 4;
    if (w > 8000) w = 8000;
    if (h > 8000) h = 8000;

    if (w != s_pw || h != s_ph) {
        if (s_poopDC) { DeleteDC(s_poopDC); s_poopDC = nullptr; }
        if (s_poopBmp) { DeleteObject(s_poopBmp); s_poopBmp = nullptr; }
        BITMAPV5HEADER bi = {};
        bi.bV5Size = sizeof(bi);
        bi.bV5Width = w; bi.bV5Height = -h;
        bi.bV5Planes = 1; bi.bV5BitCount = 32;
        bi.bV5Compression = BI_BITFIELDS;
        bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00;
        bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
        s_poopDC = CreateCompatibleDC(nullptr);
        s_poopBmp = CreateDIBSection(s_poopDC, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &s_poopBits, nullptr, 0);
        if (s_poopBmp) SelectObject(s_poopDC, s_poopBmp);
        s_pw = w; s_ph = h;
    }

    // frass lies on the desktop too: an application window hides it
    RECT box = { ox, oy, ox + w, oy + h };
    RECT cov[16];
    bool fullyCovered = false;
    int nCov = g_cfg.hideOnApp ? ZOrderCollect(box, cov, 16, &fullyCovered) : 0;
    if (fullyCovered) {
        if (IsWindowVisible(s_poopWnd)) ShowWindow(s_poopWnd, SW_HIDE);
        return;
    }

    {
        Gdiplus::Bitmap bmp(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)s_poopBits);
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        for (int i = 0; i < nCov; i++) {
            Gdiplus::Rect r(cov[i].left - ox, cov[i].top - oy,
                            cov[i].right - cov[i].left, cov[i].bottom - cov[i].top);
            g.SetClip(r, Gdiplus::CombineModeExclude);
        }
        for (auto& p : s_poop) R::DrawPoop(g, p.x - ox, p.y - oy, p.size, p.rot);
    }

    POINT dst = { ox, oy };
    SIZE sz = { w, h };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(g_cfg.opacity * 255 / 100), AC_SRC_ALPHA };
    UpdateLayeredWindow(s_poopWnd, nullptr, &dst, &sz, s_poopDC, &src, 0, &bf, ULW_ALPHA);
    if (!IsWindowVisible(s_poopWnd)) ShowWindow(s_poopWnd, SW_SHOWNOACTIVATE);
}

void PoopDestroy() {
    s_poop.clear();
    if (s_poopDC) { DeleteDC(s_poopDC); s_poopDC = nullptr; }
    if (s_poopBmp) { DeleteObject(s_poopBmp); s_poopBmp = nullptr; }
    s_pw = s_ph = 0;
    if (s_poopWnd) { DestroyWindow(s_poopWnd); s_poopWnd = nullptr; }
}

void PoopSetVisible(bool vis) {
    if (!s_poopWnd) return;
    if (vis) {
        if (!s_poop.empty()) {
            s_poopDirty = true;
            PoopPresent();
        }
    } else if (IsWindowVisible(s_poopWnd)) {
        ShowWindow(s_poopWnd, SW_HIDE);
    }
}

HWND PoopHwnd() { return s_poopWnd; }
