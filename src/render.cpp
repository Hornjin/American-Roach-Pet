// render.cpp : procedural vector artwork for Periplaneta americana
// Body space: origin = body centre, +X = head, +Y = the insect's right side.
// L = total body length (head to wing tip).  All coordinates are fractions of L.
//
// NOTE: Gdiplus::Color's 4-arg ctor is Color(a, r, g, b) - alpha FIRST.
// Everything here goes through RGBA() to avoid that trap.
#include "common.h"

using namespace Gdiplus;

namespace {

inline Color RGBA(int r, int g, int b, int a = 255) {
    return Color((BYTE)a, (BYTE)r, (BYTE)g, (BYTE)b);
}

// ---- palette (Periplaneta americana, reddish mahogany) -------------------
const Color kTegHi    = RGBA(162,  92,  44);
const Color kTegLo    = RGBA( 84,  38,  14);
const Color kTegEdge  = RGBA( 44,  17,   5);
const Color kAbdo     = RGBA(106,  52,  22);
const Color kPronDark = RGBA( 64,  26,   9);
const Color kPronHi   = RGBA( 98,  48,  20);
const Color kHead     = RGBA( 56,  23,   8);
const Color kLegDark  = RGBA( 68,  31,  11);
const Color kLegLight = RGBA(140,  82,  40);
const Color kAnt      = RGBA( 46,  20,   7);
const Color kNymphB   = RGBA( 94,  45,  18);
const Color kNymphPale= RGBA(196, 158, 106);
const Color kHatchB   = RGBA( 42,  19,   7);
const Color kHatchPale= RGBA(220, 208, 182);
const Color kOothLo   = RGBA( 36,  14,   4);
const Color kOothHi   = RGBA(120,  62,  28);

void EllipseP(GraphicsPath& p, float cx, float cy, float rx, float ry) {
    p.AddEllipse(cx - rx, cy - ry, rx * 2.0f, ry * 2.0f);
}

void Bez(GraphicsPath& p, float x1, float y1, float x2, float y2,
         float x3, float y3, float x4, float y4) {
    p.AddBezier(x1, y1, x2, y2, x3, y3, x4, y4);
}

// soft contact shadow so the insect reads as standing ON the surface
void DrawShadow(Graphics& g, float L, float halfW, float alpha) {
    for (int i = 0; i < 2; i++) {
        float k = 1.0f + i * 0.12f;
        GraphicsPath p;
        EllipseP(p, -0.06f * L, 0.012f * L, 0.46f * L * k, halfW * 1.02f * k);
        SolidBrush b(RGBA(0, 0, 0, (int)(alpha / (i + 1.7f))));
        g.FillPath(&b, &p);
    }
}

// ---- limbs ---------------------------------------------------------------
void DrawLeg(Graphics& g, float rx, float ry, float fx, float fy,
             float lift, float L, float w, int pair) {
    float dx = fx - rx, dy = fy - ry;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) len = 0.001f;
    float ux = dx / len, uy = dy / len;
    float nx = -uy, ny = ux;
    if (ny * ry < 0) { nx = -nx; ny = -ny; }      // always bulge away from the midline

    float kneeOut = (pair == 2 ? 0.110f : (pair == 0 ? 0.080f : 0.095f)) * L;
    float kx = rx + ux * len * 0.46f + nx * kneeOut + (-uy) * lift;
    float ky = ry + uy * len * 0.46f + ny * kneeOut + ( ux) * lift;

    Pen femur(kLegDark, w);
    femur.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    g.DrawLine(&femur, rx, ry, kx, ky);

    Pen tibia(kLegDark, w * 0.72f);
    tibia.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    g.DrawLine(&tibia, kx, ky, fx, fy);

    float tx = fx + ux * 0.040f * L, ty = fy + uy * 0.040f * L;
    Pen tar(kLegDark, w * 0.42f);
    tar.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    g.DrawLine(&tar, fx, fy, tx, ty);

    // fine tibial spines, outer side only - kept short so they read as hairs
    // rather than as beads on the shin
    Pen spine(RGBA(168, 108, 58, 120), std::max(0.35f, w * 0.18f));
    for (int i = 1; i <= 4; i++) {
        float tt = i / 5.0f;
        float sx = kx + (fx - kx) * tt, sy = ky + (fy - ky) * tt;
        g.DrawLine(&spine, sx, sy, sx + nx * 0.011f * L, sy + ny * 0.011f * L);
    }
}

void DrawAllLegs(Graphics& g, float L, float gaitPhase, float amp) {
    static const float rootX[3] = { 0.315f, 0.150f, -0.020f };
    static const float footX[3] = { 0.480f,  0.115f, -0.390f };
    static const float footY[3] = { 0.415f,  0.500f,  0.545f };
    for (int side = 0; side < 2; side++) {
        float s = side ? 1.0f : -1.0f;
        for (int i = 0; i < 3; i++) {
            float off = (i == 1) ? (side ? 0.0f : 3.14159f)
                                 : (side ? 3.14159f : 0.0f);
            float ph = gaitPhase + off;
            float swing = sinf(ph) * 0.075f * amp;
            float lift  = cosf(ph) * 0.030f * amp;
            float fx = (footX[i] + swing) * L;
            float fy = s * (footY[i] + fabsf(cosf(ph)) * 0.018f * amp) * L;
            float w = (i == 2 ? 1.70f : 1.45f) * (L / 46.0f);
            DrawLeg(g, rootX[i] * L, s * 0.130f * L, fx, fy, lift * L, L, w, i);
        }
    }
}

// ---- antennae ------------------------------------------------------------
// Filiform: very thin at the tip, gently arcing forward and outwards, with the
// small irregular kinks a live roach keeps tapping around with.
void DrawOneAntenna(Graphics& g, float L, float s, float phase, float amp) {
    const int N = 30;
    float x = 0.488f * L, y = s * 0.030f * L;
    float baseAng = s * 0.20f;
    float step = (1.02f * L) / N;

    for (int i = 0; i < N; i++) {
        float t = (float)i / N;
        float a = baseAng
                + sinf(t * 3.1f + phase + s * 1.2f) * 0.30f * amp
                + sinf(t * 8.3f - phase * 1.7f) * 0.075f * amp
                + s * 0.52f * t * t;
        float nx2 = x + cosf(a) * step;
        float ny2 = y + sinf(a) * step;
        float w = std::max(0.30f, 1.00f * (1.0f - t * 0.82f)) * (L / 46.0f);
        Pen p(kAnt, w);
        p.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawLine(&p, x, y, nx2, ny2);
        x = nx2; y = ny2;
    }
}

void DrawAntennae(Graphics& g, float L, float phase, float amp) {
    DrawOneAntenna(g, L, -1.0f, phase, amp);
    DrawOneAntenna(g, L,  1.0f, phase + 0.85f, amp);
}

// ---- body parts ----------------------------------------------------------
void DrawCerci(Graphics& g, float L) {
    for (int i = 0; i < 2; i++) {
        float s = i ? 1.0f : -1.0f;
        Pen pen(RGBA(50, 22, 8), 1.9f * (L / 46.0f));
        pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawLine(&pen, -0.455f * L, s * 0.062f * L, -0.545f * L, s * 0.125f * L);
        Pen tip(RGBA(78, 38, 15), 1.1f * (L / 46.0f));
        tip.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawLine(&tip, -0.545f * L, s * 0.125f * L, -0.578f * L, s * 0.160f * L);
    }
}

void DrawAbdomen(Graphics& g, float L, float w, bool striped, int bands, Color base) {
    GraphicsPath p;
    EllipseP(p, -0.140f * L, 0, 0.375f * L, w * L);

    Color hi = RGBA(base.GetR(), base.GetG(), base.GetB(), 255);
    Color lo = RGBA((int)(base.GetR() * 0.48f), (int)(base.GetG() * 0.48f),
                    (int)(base.GetB() * 0.48f), 255);
    LinearGradientBrush br(PointF(0, -w * L), PointF(0, w * L), hi, lo);
    g.FillPath(&br, &p);

    Pen seg(RGBA(34, 14, 4, 70), 0.9f * (L / 46.0f));
    for (int i = 0; i < 6; i++) {
        float xx = (0.10f - i * 0.098f) * L;
        float rel = (xx + 0.140f * L) / (0.375f * L);
        if (rel <= -1.0f || rel >= 1.0f) continue;
        float half = w * L * sqrtf(1.0f - rel * rel) * 0.97f;
        g.DrawLine(&seg, xx, -half, xx, half);
    }

    if (striped) {
        for (int i = 0; i < bands; i++) {
            float xx = (0.075f - i * 0.105f) * L;
            float rel = (xx + 0.140f * L) / (0.375f * L);
            if (rel <= -1.0f || rel >= 1.0f) continue;
            float half = w * L * sqrtf(1.0f - rel * rel);
            GraphicsPath band;
            band.AddEllipse(xx - 0.010f * L, -half * 0.88f, 0.022f * L, half * 1.76f);
            SolidBrush b(RGBA(kNymphPale.GetR(), kNymphPale.GetG(), kNymphPale.GetB(), 130));
            g.FillPath(&b, &band);
        }
        Pen lat(RGBA(kNymphPale.GetR(), kNymphPale.GetG(), kNymphPale.GetB(), 95),
                1.1f * (L / 46.0f));
        g.DrawEllipse(&lat, -0.140f * L - 0.375f * L * 0.99f, -w * L * 0.88f,
                      0.375f * L * 1.98f, w * L * 1.76f);
    }
}

void DrawTegmina(Graphics& g, float L, float halfW) {
    for (int i = 0; i < 2; i++) {
        float s = i ? 1.0f : -1.0f;
        GraphicsPath p;
        p.StartFigure();
        p.AddLine(0.240f * L, s * 0.018f * L, 0.205f * L, s * 0.120f * L);
        Bez(p, 0.160f * L, s * 0.205f * L, -0.020f * L, s * 0.250f * L,
               -0.230f * L, s * 0.243f * L, -0.355f * L, s * 0.203f * L);
        Bez(p, -0.440f * L, s * 0.170f * L, -0.520f * L, s * 0.118f * L,
               -0.545f * L, s * 0.044f * L, -0.548f * L, s * 0.010f * L);
        p.AddLine(-0.548f * L, s * 0.010f * L, 0.240f * L, s * 0.018f * L);
        p.CloseFigure();

        LinearGradientBrush br(PointF(0.16f * L, s * 0.02f * L), PointF(-0.45f * L, s * halfW * L),
                               kTegHi, kTegLo);
        g.FillPath(&br, &p);

        // the folded lateral field reads darker
        Pen field(RGBA(40, 16, 6, 70), 0.075f * L);
        GraphicsPath fp;
        Bez(fp, 0.190f * L, s * 0.135f * L, 0.000f * L, s * 0.205f * L,
              -0.230f * L, s * 0.200f * L, -0.400f * L, s * 0.155f * L);
        g.DrawPath(&field, &fp);

        Pen edge(kTegEdge, 0.9f * (L / 46.0f));
        g.DrawPath(&edge, &p);

        Pen vein(RGBA(88, 34, 14, 110), 0.7f * (L / 46.0f));
        for (int v = 1; v <= 3; v++) {
            float tt = v / 4.0f;
            GraphicsPath vp;
            Bez(vp, 0.05f * L, s * 0.028f * L,
                   -0.10f * L, s * (0.02f + tt * 0.12f) * L,
                   -0.28f * L, s * (0.03f + tt * 0.10f) * L,
                   -0.44f * L, s * (0.05f + tt * 0.040f) * L);
            g.DrawPath(&vein, &vp);
        }

        Pen gloss(RGBA(255, 228, 190, 42), 1.1f * (L / 46.0f));
        GraphicsPath gp;
        Bez(gp, 0.175f * L, s * 0.070f * L, 0.095f * L, s * 0.140f * L,
              -0.050f * L, s * 0.162f * L, -0.190f * L, s * 0.158f * L);
        g.DrawPath(&gloss, &gp);
    }
    // dorsal seam where the two tegmina overlap
    Pen seam(RGBA(38, 14, 4, 150), 0.9f * (L / 46.0f));
    g.DrawLine(&seam, 0.235f * L, 0.0f, -0.540f * L, 0.0f);
}

void DrawPronotum(Graphics& g, float L) {
    // broad shield, wider than long, with a narrow cream marginal band
    GraphicsPath outer;
    EllipseP(outer, 0.315f * L, 0, 0.135f * L, 0.250f * L);
    LinearGradientBrush rimBr(PointF(0.19f * L, -0.25f * L), PointF(0.45f * L, 0.25f * L),
                              RGBA(214, 180, 130), RGBA(132, 96, 54));
    g.FillPath(&rimBr, &outer);

    GraphicsPath inner;
    EllipseP(inner, 0.318f * L, 0, 0.122f * L, 0.234f * L);
    LinearGradientBrush inBr(PointF(0.21f * L, -0.23f * L), PointF(0.44f * L, 0.23f * L),
                             kPronHi, kPronDark);
    g.FillPath(&inBr, &inner);

    // the rear margin of the shield lies on top of the wing bases -> contact shadow
    Pen rear(RGBA(26, 10, 3, 105), 2.4f * (L / 46.0f));
    GraphicsPath rp;
    rp.AddArc(0.315f * L - 0.135f * L, -0.250f * L, 0.270f * L, 0.500f * L, 100.0f, 160.0f);
    g.DrawPath(&rear, &rp);

    Pen e(RGBA(28, 11, 3, 170), 0.8f * (L / 46.0f));
    g.DrawPath(&e, &outer);

    Pen sh(RGBA(240, 208, 162, 60), 1.0f * (L / 46.0f));
    GraphicsPath sp;
    Bez(sp, 0.250f * L, -0.150f * L, 0.295f * L, -0.190f * L,
           0.365f * L, -0.185f * L, 0.405f * L, -0.135f * L);
    g.DrawPath(&sh, &sp);
}

void DrawHead(Graphics& g, float L, float chew, bool eating) {
    float bob = eating ? sinf(chew * 22.0f) * 0.010f * L : 0.0f;
    GraphicsPath p;
    EllipseP(p, 0.440f * L + bob, 0, 0.060f * L, 0.080f * L);
    SolidBrush b(kHead);
    g.FillPath(&b, &p);

    SolidBrush eye(RGBA(16, 7, 3));
    GraphicsPath e1; EllipseP(e1, 0.458f * L + bob, -0.050f * L, 0.019f * L, 0.025f * L);
    GraphicsPath e2; EllipseP(e2, 0.458f * L + bob,  0.050f * L, 0.019f * L, 0.025f * L);
    g.FillPath(&eye, &e1);
    g.FillPath(&eye, &e2);

    if (eating) {
        float open = 0.5f + 0.5f * sinf(chew * 26.0f);
        Pen m(RGBA(28, 11, 4), 1.3f * (L / 46.0f));
        m.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawLine(&m, 0.486f * L + bob, -0.010f * L, 0.520f * L + bob, (-0.026f - open * 0.018f) * L);
        g.DrawLine(&m, 0.486f * L + bob,  0.010f * L, 0.520f * L + bob, ( 0.026f + open * 0.018f) * L);
        Pen pal(RGBA(56, 24, 8), 0.85f * (L / 46.0f));
        g.DrawLine(&pal, 0.478f * L, -0.040f * L, 0.512f * L, -0.068f * L);
        g.DrawLine(&pal, 0.478f * L,  0.040f * L, 0.512f * L,  0.068f * L);
    }
}

// ---- ootheca -------------------------------------------------------------
void DrawOotheca(Graphics& g, float L, float t) {
    float w = 0.40f * L, h = 0.94f * L;
    float r = w * 0.42f;

    GraphicsPath body;
    body.StartFigure();
    body.AddArc(-w * 0.5f, -h * 0.5f, w, r * 2.0f, 180.0f, 180.0f);
    body.AddLine(w * 0.5f, -h * 0.5f + r, w * 0.5f, h * 0.5f - r);
    body.AddArc(-w * 0.5f, h * 0.5f - r * 2.0f, w, r * 2.0f, 0.0f, 180.0f);
    body.AddLine(-w * 0.5f, h * 0.5f - r, -w * 0.5f, -h * 0.5f + r);
    body.CloseFigure();

    LinearGradientBrush br(PointF(-w * 0.5f, 0), PointF(w * 0.5f, 0), kOothHi, kOothLo);
    g.FillPath(&br, &body);
    Pen edge(RGBA(24, 9, 2), 1.0f * (L / 46.0f));
    g.DrawPath(&edge, &body);

    Pen ridge(RGBA(20, 8, 3, 120), 0.9f * (L / 46.0f));
    for (int i = 1; i <= 8; i++) {
        float yy = -h * 0.5f + h * (i / 9.0f);
        g.DrawLine(&ridge, -w * 0.46f, yy, w * 0.46f, yy);
    }

    Pen keel(RGBA(138, 82, 38), 2.3f * (L / 46.0f));
    keel.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    g.DrawLine(&keel, -w * 0.30f, -h * 0.44f, -w * 0.30f, h * 0.44f);
    Pen keel2(RGBA(54, 22, 6), 0.8f * (L / 46.0f));
    g.DrawLine(&keel2, -w * 0.30f, -h * 0.44f, -w * 0.30f, h * 0.44f);

    Pen tooth(RGBA(48, 20, 6), 0.8f * (L / 46.0f));
    for (int i = 1; i <= 7; i++) {
        float yy = -h * 0.40f + h * (i / 8.0f);
        g.DrawLine(&tooth, -w * 0.36f, yy, -w * 0.24f, yy);
    }

    Pen gl(RGBA(255, 234, 200, 55), 1.0f * (L / 46.0f));
    g.DrawLine(&gl, w * 0.18f, -h * 0.30f, w * 0.20f, h * 0.10f);
}

} // namespace

// =========================================================================
namespace R {

void DrawBug(Graphics& g, float cx, float cy, float ang, Stage st, float L,
             float t, float gaitPhase, float gaitAmp,
             bool eating, float chew, float antPhase, float antAmp) {
    GraphicsState gs = g.Save();
    g.TranslateTransform(cx, cy);
    g.RotateTransform(ang * 57.29578f);

    if (st == Stage::Ootheca) {
        DrawShadow(g, L * 0.55f, 0.17f * L, 34);
        DrawOotheca(g, L, t);
        g.Restore(gs);
        return;
    }

    bool adult = (st == Stage::Adult);
    bool hatch = (st == Stage::Hatchling);
    float w = adult ? 0.235f : (hatch ? 0.180f : 0.200f);

    DrawShadow(g, L, w, hatch ? 26 : 36);
    DrawAllLegs(g, L, gaitPhase, gaitAmp);

    Color abdo = hatch ? kHatchB : (adult ? kAbdo : kNymphB);
    DrawAbdomen(g, L, w, !adult, hatch ? 5 : 4, abdo);
    DrawCerci(g, L);

    if (adult) DrawTegmina(g, L, w);

    DrawHead(g, L, chew, eating);
    DrawPronotum(g, L);

    if (hatch) {
        Pen stripe(RGBA(kHatchPale.GetR(), kHatchPale.GetG(), kHatchPale.GetB(), 200),
                   1.0f * (L / 46.0f));
        stripe.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawLine(&stripe, -0.30f * L, 0.0f, -0.05f * L, 0.0f);
    }

    DrawAntennae(g, L, antPhase, antAmp);
    g.Restore(gs);
}

void DrawAntennaeOnly(Graphics& g, float hx, float hy, float ang, float L,
                      float t, float antPhase, float antAmp) {
    GraphicsState gs = g.Save();
    g.TranslateTransform(hx, hy);
    g.RotateTransform(ang * 57.29578f);
    g.TranslateTransform(-0.488f * L, 0.0f);
    DrawAntennae(g, L, antPhase, antAmp);
    g.Restore(gs);
}

void DrawPoop(Graphics& g, float x, float y, float size, float rot) {
    GraphicsState gs = g.Save();
    g.TranslateTransform(x, y);
    g.RotateTransform(rot);
    float w = size, h = size * 0.40f;
    GraphicsPath p;
    p.AddEllipse(-w * 0.5f, -h * 0.5f, w, h);
    LinearGradientBrush br(PointF(0, -h * 0.5f), PointF(0, h * 0.5f),
                           RGBA(56, 40, 26), RGBA(20, 14, 8));
    g.FillPath(&br, &p);
    Pen e(RGBA(12, 8, 4, 190), std::max(0.45f, size * 0.04f));
    g.DrawPath(&e, &p);

    Pen ridge(RGBA(118, 92, 62, 120), std::max(0.4f, size * 0.035f));
    for (int i = -2; i <= 2; i++) {
        float yy = i * h * 0.19f;
        float dx = w * 0.5f * sqrtf(std::max(0.0f, 1.0f - (yy / (h * 0.5f)) * (yy / (h * 0.5f))));
        g.DrawLine(&ridge, -dx * 0.78f, yy, dx * 0.78f, yy);
    }
    Pen gl(RGBA(170, 142, 108, 55), std::max(0.4f, size * 0.04f));
    g.DrawLine(&gl, -w * 0.18f, -h * 0.20f, w * 0.06f, -h * 0.24f);
    g.Restore(gs);
}

void DrawTrayRoach(Graphics& g, float cx, float cy, float size) {
    DrawBug(g, cx, cy, -0.55f, Stage::Adult, size, 0.0f, 0.0f, 0.35f, false, 0.0f, 0.0f, 0.9f);
}

HICON MakeTrayIcon(int size) {
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC hdc = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!color) return nullptr;

    {
        Bitmap bmp(size, size, size * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.Clear(Color(0, 0, 0, 0));
        DrawTrayRoach(g, size * 0.47f, size * 0.53f, size * 0.66f);
    }

    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return ic;
}

} // namespace R
