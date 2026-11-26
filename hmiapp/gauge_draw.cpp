// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// See gauge_draw.hpp. Each function follows its namesake in hmiapp/gauge_art.py line by line (Python's round() is
// round-half-even: nearbyint here; Python's int() truncates: a cast here), so both draw the same pixels.
#include "gauge_draw.hpp"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace evo {
namespace gauges {
namespace draw {

Picture::~Picture() {
    free(rgba);
}

namespace {

// ---- the sheets (CarGauges_Glyphs.bin) ------------------------------------------------------------------------------
struct Glyph {
    unsigned size, code, x, y, w, h;
    int ink;
    double advance;
};
struct IconPlace {
    unsigned icon, size, x, y;
};
struct Sheets {
    unsigned fw, fh;
    unsigned char* falpha;
    Glyph* glyphs;
    unsigned glyphCount;
    unsigned sizes[16];
    unsigned sizeCount;
    unsigned iw, ih;
    unsigned char* ialpha;
    IconPlace* icons;
    unsigned iconCount;
    unsigned short* entryIcon;  // per catalogue entry: icon index, 0xFFFF none
    unsigned entryCount;
};
Sheets g_sheets;
bool g_loaded = false;

struct Reader {
    const unsigned char* p;
    size_t n, at;
    bool ok;
    unsigned u32() {
        if (at + 4 > n) {
            ok = false;
            return 0;
        }
        const unsigned v = p[at] | (p[at + 1] << 8) | (p[at + 2] << 16) | (static_cast<unsigned>(p[at + 3]) << 24);
        at += 4;
        return v;
    }
    unsigned u16() {
        if (at + 2 > n) {
            ok = false;
            return 0;
        }
        const unsigned v = p[at] | (p[at + 1] << 8);
        at += 2;
        return v;
    }
    float f32() {
        const unsigned v = u32();
        float f;
        memcpy(&f, &v, 4);
        return f;
    }
    unsigned char* bytes(size_t k) {
        if (!ok || at + k > n) {
            ok = false;
            return nullptr;
        }
        unsigned char* out = static_cast<unsigned char*>(malloc(k ? k : 1));
        if (out) {
            memcpy(out, p + at, k);
        }
        at += k;
        return out;
    }
};

// ---- coverage masks (gauge_art.Mask) --------------------------------------------------------------------------------
struct Mask {
    int w, h;
    float* a;
    int x0, y0, x1, y1;
    bool any;
    Mask(int w_, int h_)
        : w(w_),
          h(h_),
          a(static_cast<float*>(calloc(static_cast<size_t>(w_) * h_, sizeof(float)))),
          x0(0),
          y0(0),
          x1(0),
          y1(0),
          any(false) {}
    ~Mask() { free(a); }
    Mask(const Mask&) = delete;
    Mask& operator=(const Mask&) = delete;

    void grow(int ax, int ay, int bx, int by) {
        if (!any) {
            x0 = ax, y0 = ay, x1 = bx, y1 = by, any = true;
            return;
        }
        x0 = ax < x0 ? ax : x0;
        y0 = ay < y0 ? ay : y0;
        x1 = bx > x1 ? bx : x1;
        y1 = by > y1 ? by : y1;
    }
    void cover(int x, int y, double c) {
        if (c > 0 && x >= 0 && x < w && y >= 0 && y < h) {
            float& v = a[y * w + x];
            if (c > v) {
                v = static_cast<float>(c > 1 ? 1.0 : c);
            }
        }
    }
    void stroke(double xa, double ya, double xb, double yb, double r1, double r2) {
        const double rm = (r1 > r2 ? r1 : r2) + 1;
        int sx0 = static_cast<int>((xa < xb ? xa : xb) - rm), sxe = static_cast<int>((xa > xb ? xa : xb) + rm) + 1;
        int sy0 = static_cast<int>((ya < yb ? ya : yb) - rm), sye = static_cast<int>((ya > yb ? ya : yb) + rm) + 1;
        sx0 = sx0 < 0 ? 0 : sx0;
        sy0 = sy0 < 0 ? 0 : sy0;
        sxe = sxe > w - 1 ? w - 1 : sxe;
        sye = sye > h - 1 ? h - 1 : sye;
        if (sx0 > sxe || sy0 > sye) {
            return;
        }
        const double dx = xb - xa, dy = yb - ya, ll = dx * dx + dy * dy;
        for (int y = sy0; y <= sye; ++y) {
            const double py = y + 0.5 - ya;
            for (int x = sx0; x <= sxe; ++x) {
                const double px = x + 0.5 - xa;
                double t = ll == 0 ? 0.0 : (px * dx + py * dy) / ll;
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                const double ex = px - t * dx, ey = py - t * dy;
                cover(x, y, r1 + (r2 - r1) * t + 0.5 - sqrt(ex * ex + ey * ey));
            }
        }
        grow(sx0, sy0, sxe, sye);
    }
    void stroke(double xa, double ya, double xb, double yb, double r) { stroke(xa, ya, xb, yb, r, r); }
    void polyline(const double (*pts)[2], int n, double r) {
        for (int i = 0; i + 1 < n; ++i) {
            stroke(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], r, r);
        }
    }
    // A convex polygon, anti-aliased by the distance to the nearest edge.
    void polygon(const double (*pts)[2], int n) {
        if (n < 3) {
            return;
        }
        double area = 0;
        for (int i = 0; i < n; ++i) {
            const int j = (i + 1) % n;
            area += pts[i][0] * pts[j][1] - pts[j][0] * pts[i][1];
        }
        const double sgn = area > 0 ? 1 : -1;
        double edges[16][4];
        int ne = 0;
        for (int i = 0; i < n && ne < 16; ++i) {
            const int j = (i + 1) % n;
            const double ln = hypot(pts[j][0] - pts[i][0], pts[j][1] - pts[i][1]);
            if (ln > 1e-9) {
                edges[ne][0] = pts[i][0];
                edges[ne][1] = pts[i][1];
                edges[ne][2] = sgn * -(pts[j][1] - pts[i][1]) / ln;
                edges[ne][3] = sgn * (pts[j][0] - pts[i][0]) / ln;
                ++ne;
            }
        }
        if (ne < 3 || fabs(area) < 1e-6) {
            return;
        }
        double mnx = pts[0][0], mxx = pts[0][0], mny = pts[0][1], mxy = pts[0][1];
        for (int i = 1; i < n; ++i) {
            mnx = pts[i][0] < mnx ? pts[i][0] : mnx;
            mxx = pts[i][0] > mxx ? pts[i][0] : mxx;
            mny = pts[i][1] < mny ? pts[i][1] : mny;
            mxy = pts[i][1] > mxy ? pts[i][1] : mxy;
        }
        int px0 = static_cast<int>(mnx), px1 = static_cast<int>(mxx) + 1;
        int py0 = static_cast<int>(mny), py1 = static_cast<int>(mxy) + 1;
        px0 = px0 < 0 ? 0 : px0;
        py0 = py0 < 0 ? 0 : py0;
        px1 = px1 > w - 1 ? w - 1 : px1;
        py1 = py1 > h - 1 ? h - 1 : py1;
        for (int y = py0; y <= py1; ++y) {
            for (int x = px0; x <= px1; ++x) {
                double d = 1e30;
                for (int k = 0; k < ne; ++k) {
                    const double v = (x + 0.5 - edges[k][0]) * edges[k][2] + (y + 0.5 - edges[k][1]) * edges[k][3];
                    d = v < d ? v : d;
                }
                cover(x, y, d + 0.5);
            }
        }
        grow(px0, py0, px1, py1);
    }
};

// UTF-8 to code points; n: how many.
unsigned codepoints(const char* s, unsigned* out, unsigned cap) {
    unsigned n = 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    while (*p && n < cap) {
        unsigned c = *p++;
        if (c >= 0xC0 && c < 0xE0 && (*p & 0xC0) == 0x80) {
            c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
        } else if (c >= 0xE0 && c < 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
            c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
            p += 2;
        }
        out[n++] = c;
    }
    return n;
}

unsigned sizeFor(double height) {
    unsigned best = g_sheets.sizes[0];
    for (unsigned i = 1; i < g_sheets.sizeCount; ++i) {
        if (fabs(g_sheets.sizes[i] - height) < fabs(best - height)) {
            best = g_sheets.sizes[i];
        }
    }
    return best;
}

const Glyph* glyph(unsigned size, unsigned code) {
    for (unsigned i = 0; i < g_sheets.glyphCount; ++i) {
        if (g_sheets.glyphs[i].size == size && g_sheets.glyphs[i].code == code) {
            return &g_sheets.glyphs[i];
        }
    }
    return nullptr;
}

double textWidth(const char* s, double height) {
    const unsigned size = sizeFor(height);
    unsigned cps[64];
    const unsigned n = codepoints(s, cps, 64);
    double w = 0;
    for (unsigned i = 0; i < n; ++i) {
        const Glyph* g = glyph(size, cps[i]);
        w += g ? g->advance : 0;
    }
    return w + 0.6 * (static_cast<double>(n) - 1);
}

enum Anchor { kCentre, kLeft, kRight };

void text(Mask& m, const char* s, double x, double y, double height, Anchor anchor = kCentre) {
    const unsigned size = sizeFor(height);
    const double width = textWidth(s, height);
    double left = anchor == kLeft ? x : anchor == kRight ? x - width : x - width / 2;
    left = left < m.w - width ? left : m.w - width;
    left = left > 0.0 ? left : 0.0;
    const int top = static_cast<int>(nearbyint(y - size / 2.0));
    unsigned cps[64];
    const unsigned n = codepoints(s, cps, 64);
    double pen = left;
    for (unsigned i = 0; i < n; ++i) {
        const Glyph* g = glyph(size, cps[i]);
        if (!g) {
            continue;
        }
        const int ox = static_cast<int>(nearbyint(pen + g->ink));
        for (unsigned j = 0; j < g->h; ++j) {
            for (unsigned k = 0; k < g->w; ++k) {
                m.cover(ox + static_cast<int>(k), top + static_cast<int>(j),
                        g_sheets.falpha[(g->y + j) * g_sheets.fw + g->x + k] / 255.0);
            }
        }
        pen += g->advance + 0.6;
    }
    m.grow(left > 0 ? static_cast<int>(left) : 0, top > 0 ? top : 0,
           static_cast<int>(left + width) + 1 < m.w - 1 ? static_cast<int>(left + width) + 1 : m.w - 1,
           top + static_cast<int>(size) < m.h - 1 ? top + static_cast<int>(size) : m.h - 1);
}

void drawIcon(Mask& m, unsigned entry, double cx, double cy, unsigned size) {
    if (entry >= g_sheets.entryCount || g_sheets.entryIcon[entry] == 0xFFFF) {
        return;
    }
    const unsigned icon = g_sheets.entryIcon[entry];
    const IconPlace* best = nullptr;
    for (unsigned i = 0; i < g_sheets.iconCount; ++i) {
        const IconPlace& p = g_sheets.icons[i];
        if (p.icon == icon &&
            (!best || fabs(static_cast<double>(p.size) - size) < fabs(static_cast<double>(best->size) - size))) {
            best = &p;
        }
    }
    if (!best) {
        return;
    }
    const unsigned s = best->size;
    const int left = static_cast<int>(nearbyint(cx - s / 2.0)), top = static_cast<int>(nearbyint(cy - s / 2.0));
    for (unsigned j = 0; j < s; ++j) {
        for (unsigned i = 0; i < s; ++i) {
            m.cover(left + static_cast<int>(i), top + static_cast<int>(j),
                    g_sheets.ialpha[(best->y + j) * g_sheets.iw + best->x + i] / 255.0);
        }
    }
    m.grow(left > 0 ? left : 0, top > 0 ? top : 0,
           left + static_cast<int>(s) < m.w - 1 ? left + static_cast<int>(s) : m.w - 1,
           top + static_cast<int>(s) < m.h - 1 ? top + static_cast<int>(s) : m.h - 1);
}

const unsigned char kWhite[3] = {235, 235, 235}, kRed[3] = {229, 57, 53}, kTrack[3] = {92, 94, 100};

void paint(Picture& p, const Mask& m, const unsigned char* colour) {
    if (!m.any) {
        return;
    }
    for (int y = m.y0; y <= m.y1; ++y) {
        for (int x = m.x0; x <= m.x1; ++x) {
            const double ca = m.a[y * m.w + x];
            if (ca <= 0) {
                continue;
            }
            unsigned char* px = p.rgba + (static_cast<size_t>(y) * p.w + x) * 4;
            const double da = px[3] / 255.0;
            const double oa = ca + da * (1 - ca);
            for (int k = 0; k < 3; ++k) {
                px[k] =
                    static_cast<unsigned char>(static_cast<int>((colour[k] * ca + px[k] * da * (1 - ca)) / oa + 0.5));
            }
            px[3] = static_cast<unsigned char>(static_cast<int>(oa * 255 + 0.5));
        }
    }
}

bool newPicture(Picture* p, int w, int h) {
    free(p->rgba);
    p->rgba = static_cast<unsigned char*>(calloc(static_cast<size_t>(w) * h, 4));
    p->w = p->rgba ? w : 0;
    p->h = p->rgba ? h : 0;
    return p->rgba != nullptr;
}

// ---- scales ---------------------------------------------------------------------------------------------------------
// The numbered ticks: the finest 1/2/2.5/5 step with at most `most` intervals; the minor step.
unsigned ticks(double lo, double hi, int most, double* out, unsigned cap, double* minor) {
    static const double kM[4] = {1, 2, 2.5, 5};
    for (int k = -3; k < 8; ++k) {
        for (int i = 0; i < 4; ++i) {
            const double step = kM[i] * pow(10.0, k);
            const double first = ceil(lo / step - 1e-9) * step;
            const int n = static_cast<int>(floor((hi - first) / step + 1e-9)) + 1;
            if (n - 1 <= most) {
                double mn = step / (kM[i] == 2 ? 4 : 5);
                while ((hi - lo) / mn > 48) {
                    mn *= 2;
                }
                *minor = mn;
                unsigned c = 0;
                for (int j = 0; j < n && c < cap; ++j) {
                    out[c++] = first + j * step;
                }
                return c;
            }
        }
    }
    *minor = (hi - lo) / 10;
    return 0;
}

double divisorOf(double lo, double hi) {
    return (fabs(lo) > fabs(hi) ? fabs(lo) : fabs(hi)) >= 2000 ? 1000 : 1;
}

void figure(double v, double div, int decimals, char* out, size_t cap) {
    if (decimals >= 0) {
        snprintf(out, cap, "%.*f", decimals, v / div);
        return;
    }
    snprintf(out, cap, "%.2f", v / div);
    size_t n = strlen(out);
    while (n && out[n - 1] == '0') {
        out[--n] = 0;
    }
    if (n && out[n - 1] == '.') {
        out[--n] = 0;
    }
    if (!strcmp(out, "-0") || !out[0]) {
        snprintf(out, cap, "0");
    }
}

void unitText(const char* unit, char* out, size_t cap) {
    snprintf(out, cap, "%s",
             !strcmp(unit, "C") ? "\xc2\xb0"
                                  "C"
                                : unit);
}

// ---- dials ----------------------------------------------------------------------------------------------------------
struct DialSpec {
    int w, h;
    double cx, cy, radius, a0, a1, major, minor, gap, figH, figGap;
    int most;
};
const double kPi = 3.14159265358979323846;
const DialSpec kSmallDial = {189, 180, 94.5, 100, 76, kPi, 0, 9, 5, 3, 13, 3, 5};
const DialSpec kWideDial = {386, 180, 193, 128, 116, kPi, 0, 13, 7, 4, 18, 4, 8};
const DialSpec kSmallArc = {189, 180, 94.5, -40, 160, 240 * kPi / 180, 300 * kPi / 180, 9, 5, 3, 13, 3, 4};
const DialSpec kWideArc = {386, 180, 193, -150, 300, 234 * kPi / 180, 306 * kPi / 180, 13, 7, 4, 18, 4, 6};

struct Dial {
    const DialSpec& s;
    unsigned iconSize;
    double mark0[2], mark1[2], titleAt[2];
    explicit Dial(const DialSpec& spec) : s(spec), iconSize(spec.w < 300 ? 20 : 26) {
        if (s.a0 > s.a1) {
            const double below = s.cy + (s.w < 300 ? 20 : 18);
            mark0[0] = s.cx, mark0[1] = below + 24;
            mark1[0] = s.cx - iconSize * 0.4, mark1[1] = below;
            titleAt[0] = s.cx, titleAt[1] = below + 26;
        } else {
            point(0.5, s.radius * 0.78, mark0);
            point(0.5, s.radius * 0.62, mark1);
            mark1[0] -= iconSize * 0.4;
            titleAt[0] = s.cx, titleAt[1] = s.w < 300 ? 16 : 18;
        }
    }
    double angle(double t) const { return s.a0 + (s.a1 - s.a0) * t; }
    void point(double t, double r, double* out) const {
        const double a = angle(t);
        out[0] = s.cx + r * cos(a);
        out[1] = s.cy - r * sin(a);
    }

    bool face(const Scale& sc, unsigned entry, const char* title, const char* unit, Picture* out) const {
        const double lo = sc.lo, hi = sc.hi;
        if (!(hi > lo) || !newPicture(out, s.w, s.h)) {
            return false;
        }
        Mask white(s.w, s.h), reds(s.w, s.h);
        if (!white.a || !reds.a) {
            return false;
        }
        const double tRed = sc.red ? (sc.redFrom - lo) / (hi - lo) : 1.0;
        const int steps = 120;
        double rim[steps + 1][2];
        for (int i = 0; i <= steps; ++i) {
            point(static_cast<double>(i) / steps, s.radius, rim[i]);
        }
        int cut = static_cast<int>(nearbyint(tRed * steps));
        cut = cut < 0 ? 0 : cut > steps ? steps : cut;
        white.polyline(rim, cut + 1, 1.1);
        reds.polyline(rim + cut, steps + 1 - cut, 1.1);
        const double turn = s.major * 0.9;
        double ends[2][2];
        point(0, s.radius, ends[0]);
        point(0, s.radius - turn, ends[1]);
        white.polyline(ends, 2, 1.1);
        point(1, s.radius, ends[0]);
        point(1, s.radius - turn, ends[1]);
        (sc.red ? reds : white).polyline(ends, 2, 1.1);

        const double div = divisorOf(lo, hi);
        const double fh = s.figH;
        const double inner = s.radius - s.gap - s.major;
        double numbered[64], boxes[64][3], minor = 1;
        unsigned count = 0;
        for (int most = s.most; most > 1; --most) {
            count = ticks(lo, hi, most, numbered, 64, &minor);
            for (unsigned i = 0; i < count; ++i) {
                const double a = angle((numbered[i] - lo) / (hi - lo));
                char f[32];
                figure(numbered[i], div, -1, f, sizeof(f));
                const double tw = textWidth(f, fh);
                const double r = inner - s.figGap - fabs(cos(a)) * tw / 2 - fabs(sin(a)) * fh / 2;
                double x = s.cx + r * cos(a), y = s.cy - r * sin(a);
                if (fabs(sin(a)) < 0.2) {
                    const double above = s.cy - r * sin(a) - fh / 2 - 4;
                    y = y < above ? y : above;
                }
                boxes[i][0] = x, boxes[i][1] = y, boxes[i][2] = tw;
            }
            bool clear = true;
            for (unsigned i = 0; i + 1 < count && clear; ++i) {
                clear = fabs(boxes[i][0] - boxes[i + 1][0]) >= (boxes[i][2] + boxes[i + 1][2]) / 2 + 4 ||
                        fabs(boxes[i][1] - boxes[i + 1][1]) >= fh + 3;
            }
            if (clear) {
                break;
            }
        }
        const double firstMinor = ceil(lo / minor - 1e-9), lastMinor = floor(hi / minor + 1e-9);
        for (double i = firstMinor; i <= lastMinor; i += 1) {
            const double v = i * minor, t = (v - lo) / (hi - lo);
            bool major = false;
            for (unsigned k = 0; k < count && !major; ++k) {
                major = fabs(numbered[k] - v) < 1e-9;
            }
            if (major || t < 1e-6 || t > 1 - 1e-6) {
                continue;
            }
            double a[2], b[2];
            point(t, s.radius - s.gap, a);
            point(t, s.radius - s.gap - s.minor, b);
            (sc.red && v >= sc.redFrom - 1e-9 ? reds : white).stroke(a[0], a[1], b[0], b[1], 0.8);
        }
        for (unsigned i = 0; i < count; ++i) {
            const double v = numbered[i], t = (v - lo) / (hi - lo);
            Mask& m = sc.red && v >= sc.redFrom - 1e-9 ? reds : white;
            if (t > 1e-6 && t < 1 - 1e-6) {
                double a[2], b[2];
                point(t, s.radius - s.gap, a);
                point(t, inner, b);
                m.stroke(a[0], a[1], b[0], b[1], 1.1);
            }
            char f[32];
            figure(v, div, -1, f, sizeof(f));
            text(m, f, boxes[i][0], boxes[i][1], fh);
        }
        if (div != 1) {
            text(white, "x1000", mark0[0], mark0[1], fh * 0.7);
        }
        const bool icon = entry < g_sheets.entryCount && g_sheets.entryIcon[entry] != 0xFFFF;
        char u[32] = "";
        if (unit && unit[0]) {
            unitText(unit, u, sizeof(u));
        }
        if (icon) {
            drawIcon(white, entry, mark1[0], mark1[1], iconSize);
            if (u[0]) {
                text(white, u, mark1[0] + iconSize * 0.7, mark1[1], fh * 0.85, kLeft);
            }
        } else if (u[0]) {
            text(white, u, s.cx, mark1[1], fh * 0.85);
        }
        if (!icon && title && title[0]) {
            text(white, title, titleAt[0], titleAt[1], fh * 0.85);
        }
        paint(*out, white, sc.alert ? kRed : kWhite);
        paint(*out, reds, kRed);
        return true;
    }
};

// ---- bars -----------------------------------------------------------------------------------------------------------
struct Bar {
    bool tall;
    int w, h;
    double len, thin, thick;
    int count;
    void xy(double u, double v, double* out) const {
        if (tall) {
            out[0] = 56 - v, out[1] = 354 - u;
        } else {
            out[0] = u + 13, out[1] = 127 - v;
        }
    }
    bool track(const Scale& sc, unsigned entry, const char* title, const char* unit, Picture* out) const {
        const double lo = sc.lo, hi = sc.hi;
        if (!(hi > lo) || !newPicture(out, w, h)) {
            return false;
        }
        Mask grey(w, h), white(w, h), reds(w, h);
        if (!grey.a || !white.a || !reds.a) {
            return false;
        }
        const double uRed = sc.red ? len * (sc.redFrom - lo) / (hi - lo) : len;
        const double line = 2.5;
        double q[4][2];
        xy(0, 0, q[0]), xy(uRed, 0, q[1]), xy(uRed, line, q[2]), xy(0, line, q[3]);
        grey.polygon(q, 4);
        if (sc.red) {
            xy(uRed, 0, q[0]), xy(len, 0, q[1]), xy(len, line + 1.5, q[2]), xy(uRed, line + 1.5, q[3]);
            reds.polygon(q, 4);
        }
        double a[2], b[2];
        xy(-2.0, -3.0, a);
        xy(4.0, thick + 2, b);
        reds.stroke(a[0], a[1], b[0], b[1], 1.4);
        const double gap = 3.0, size = (len - gap * (count - 1)) / count;
        for (int k = 1; k <= count; ++k) {
            const double u = k < count ? k * (size + gap) - gap / 2 : len;
            if (tall) {
                white.stroke(58, 354 - u, 63, 354 - u + 1.5, 0.8);
            } else {
                white.stroke(u + 13, 129, u + 10.5, 134, 0.8);
            }
        }
        const double div = divisorOf(lo, hi);
        const double three[3] = {lo / div, (lo + hi) / 2 / div, hi / div};
        int decimals = 2;
        for (int d = 0; d <= 1; ++d) {
            bool exact = true;
            for (int i = 0; i < 3; ++i) {
                const double x = three[i] * pow(10.0, d);
                exact = exact && fabs(x - nearbyint(x)) < 1e-6;
            }
            if (exact) {
                decimals = d;
                break;
            }
        }
        if ((hi - lo) / div < 10 && decimals < 1) {
            decimals = 1;
        }
        static const Anchor kWideAnchors[3] = {kLeft, kCentre, kRight};
        for (int i = 0; i < 3; ++i) {
            const double frac = i / 2.0, v = lo + (hi - lo) * frac, u = frac * len;
            char f[32];
            figure(v, div, decimals, f, sizeof(f));
            if (tall) {
                text(white, f, 68, 354 - u, 15, kLeft);
            } else {
                text(white, f, u + 14, 145, 15, kWideAnchors[i]);
            }
        }
        const double ix = tall ? 164 : 22, iy = tall ? 114 : 96;
        const bool icon = entry < g_sheets.entryCount && g_sheets.entryIcon[entry] != 0xFFFF;
        if (icon) {
            drawIcon(white, entry, ix, iy, 26);
        } else if (title && title[0]) {
            if (ix > w / 2.0) {
                text(white, title, w - 11, iy, 15, kRight);
            } else {
                text(white, title, ix + 20, iy, 15, kLeft);
            }
        }
        if (unit && unit[0]) {
            char u[32];
            unitText(unit, u, sizeof(u));
            text(white, u, tall ? 178 : 372, tall ? 354 : 96, 18, kRight);
        }
        paint(*out, grey, kTrack);
        paint(*out, white, sc.alert ? kRed : kWhite);
        paint(*out, reds, kRed);
        return true;
    }
};
const Bar kWideBar = {false, 386, 180, 360, 6, 14, 6};
const Bar kTallBar = {true, 189, 368, 240, 6, 14, 6};

// ---- PNG ------------------------------------------------------------------------------------------------------------
uint32_t g_crc[256];
void crcInit() {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) {
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        g_crc[n] = c;
    }
}
uint32_t crc(uint32_t c, const unsigned char* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        c = g_crc[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return c;
}
void be32(unsigned char* p, uint32_t v) {
    p[0] = static_cast<unsigned char>(v >> 24), p[1] = static_cast<unsigned char>(v >> 16);
    p[2] = static_cast<unsigned char>(v >> 8), p[3] = static_cast<unsigned char>(v);
}
bool chunk(FILE* f, const char* type, const unsigned char* data, size_t n) {
    unsigned char head[8];
    be32(head, static_cast<uint32_t>(n));
    memcpy(head + 4, type, 4);
    uint32_t c = crc(0xFFFFFFFFu, head + 4, 4);
    c = crc(c, data, n) ^ 0xFFFFFFFFu;
    unsigned char tail[4];
    be32(tail, c);
    return fwrite(head, 1, 8, f) == 8 && (n == 0 || fwrite(data, 1, n, f) == n) && fwrite(tail, 1, 4, f) == 4;
}

}  // namespace

bool loadSheets(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* data = size > 0 ? static_cast<unsigned char*>(malloc(static_cast<size_t>(size))) : nullptr;
    const bool read = data && fread(data, 1, static_cast<size_t>(size), f) == static_cast<size_t>(size);
    fclose(f);
    if (!read || memcmp(data, "GGLY", 4) != 0) {
        free(data);
        return false;
    }
    Reader r = {data, static_cast<size_t>(size), 4, true};
    Sheets s;
    memset(&s, 0, sizeof(s));
    r.u32();  // version
    s.fw = r.u32(), s.fh = r.u32();
    s.falpha = r.bytes(static_cast<size_t>(s.fw) * s.fh);
    s.glyphCount = r.u32();
    s.glyphs = r.ok && s.glyphCount < 100000 ? static_cast<Glyph*>(calloc(s.glyphCount + 1, sizeof(Glyph))) : nullptr;
    for (unsigned i = 0; s.glyphs && i < s.glyphCount; ++i) {
        Glyph& g = s.glyphs[i];
        g.size = r.u16(), g.code = r.u32(), g.x = r.u16(), g.y = r.u16(), g.w = r.u16(), g.h = r.u16();
        g.ink = static_cast<short>(r.u16());
        g.advance = r.f32();
        bool known = false;
        for (unsigned k = 0; k < s.sizeCount; ++k) {
            known = known || s.sizes[k] == g.size;
        }
        if (!known && s.sizeCount < 16) {
            s.sizes[s.sizeCount++] = g.size;
        }
    }
    s.iw = r.u32(), s.ih = r.u32();
    s.ialpha = r.bytes(static_cast<size_t>(s.iw) * s.ih);
    s.iconCount = r.u32();
    s.icons =
        r.ok && s.iconCount < 100000 ? static_cast<IconPlace*>(calloc(s.iconCount + 1, sizeof(IconPlace))) : nullptr;
    for (unsigned i = 0; s.icons && i < s.iconCount; ++i) {
        IconPlace& p = s.icons[i];
        p.icon = r.u16(), p.size = r.u16(), p.x = r.u16(), p.y = r.u16();
    }
    s.entryCount = r.u32();
    s.entryIcon = r.ok && s.entryCount < 10000
                      ? static_cast<unsigned short*>(calloc(s.entryCount + 1, sizeof(unsigned short)))
                      : nullptr;
    for (unsigned i = 0; s.entryIcon && i < s.entryCount; ++i) {
        s.entryIcon[i] = static_cast<unsigned short>(r.u16());
    }
    free(data);
    // the glyphs' sizes sorted, as gauge_art.Figures.sizes (the nearest size, the smaller one on a tie)
    for (unsigned i = 0; i < s.sizeCount; ++i) {
        for (unsigned j = i + 1; j < s.sizeCount; ++j) {
            if (s.sizes[j] < s.sizes[i]) {
                const unsigned t = s.sizes[i];
                s.sizes[i] = s.sizes[j], s.sizes[j] = t;
            }
        }
    }
    if (!r.ok || !s.falpha || !s.glyphs || !s.ialpha || !s.icons || !s.entryIcon || !s.sizeCount) {
        free(s.falpha), free(s.glyphs), free(s.ialpha), free(s.icons), free(s.entryIcon);
        return false;
    }
    g_sheets = s;
    g_loaded = true;
    crcInit();
    return true;
}

bool sheetsLoaded() {
    return g_loaded;
}

bool render(Kind kind, const Scale& scale, unsigned entry, const char* title, const char* unit, Picture* out) {
    if (!g_loaded || !out) {
        return false;
    }
    switch (kind) {
        case kFace:
            return Dial(kSmallDial).face(scale, entry, title, unit, out);
        case kWideFace:
            return Dial(kWideDial).face(scale, entry, title, unit, out);
        case kArcFace:
            return Dial(kSmallArc).face(scale, entry, title, unit, out);
        case kWideArcFace:
            return Dial(kWideArc).face(scale, entry, title, unit, out);
        case kWideTrack:
            return kWideBar.track(scale, entry, title, unit, out);
        case kTallTrack:
            return kTallBar.track(scale, entry, title, unit, out);
        default:
            return false;
    }
}

bool writePng(const Picture& p, const char* path) {
    if (!p.rgba || p.w <= 0 || p.h <= 0) {
        return false;
    }
    if (!g_crc[1]) {
        crcInit();
    }
    FILE* f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    static const unsigned char kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    unsigned char ihdr[13];
    be32(ihdr, static_cast<uint32_t>(p.w));
    be32(ihdr + 4, static_cast<uint32_t>(p.h));
    ihdr[8] = 8, ihdr[9] = 6, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0;
    // the image data: zlib with stored blocks (each at most 65535 bytes), rows filter 0
    const size_t row = static_cast<size_t>(p.w) * 4 + 1, raw = row * p.h;
    const size_t blocks = (raw + 65534) / 65535;
    const size_t total = 2 + raw + blocks * 5 + 4;
    unsigned char* z = static_cast<unsigned char*>(malloc(total));
    bool ok = z != nullptr;
    if (ok) {
        size_t at = 0, done = 0;
        uint32_t s1 = 1, s2 = 0;
        z[at++] = 0x78, z[at++] = 0x01;
        while (done < raw) {
            const size_t n = raw - done < 65535 ? raw - done : 65535;
            z[at++] = done + n == raw ? 1 : 0;
            z[at++] = static_cast<unsigned char>(n), z[at++] = static_cast<unsigned char>(n >> 8);
            z[at++] = static_cast<unsigned char>(~n), z[at++] = static_cast<unsigned char>(~n >> 8);
            for (size_t i = 0; i < n; ++i, ++done) {
                const size_t y = done / row, x = done % row;
                const unsigned char b = x == 0 ? 0 : p.rgba[y * (row - 1) + x - 1];
                z[at++] = b;
                s1 = (s1 + b) % 65521;
                s2 = (s2 + s1) % 65521;
            }
        }
        be32(z + at, (s2 << 16) | s1);
        at += 4;
        ok = fwrite(kSig, 1, 8, f) == 8 && chunk(f, "IHDR", ihdr, 13) && chunk(f, "IDAT", z, at) &&
             chunk(f, "IEND", nullptr, 0);
    }
    free(z);
    return fclose(f) == 0 && ok;
}

}  // namespace draw
}  // namespace gauges
}  // namespace evo
