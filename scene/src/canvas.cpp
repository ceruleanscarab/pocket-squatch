#include "squatch/canvas.hpp"

#include <algorithm>
#include <cmath>

namespace squatch {

namespace {

inline uint16_t to565(Rgb c) {
    return static_cast<uint16_t>(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

inline Rgb from565(uint16_t p) {
    uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

inline int alpha256(float a) { return static_cast<int>(clamp01(a) * 256 + 0.5f); }

// Mixes src into dst by a/256, channel by channel, all in integers (the board has no fast
// floating point to spare per pixel).
inline void blend565(uint16_t& dst, uint16_t src, int a) {
    if (a >= 256) {
        dst = src;
        return;
    }
    int dr = dst >> 11, dg = (dst >> 5) & 63, db = dst & 31;
    int sr = src >> 11, sg = (src >> 5) & 63, sb = src & 31;
    dr += ((sr - dr) * a + 128) >> 8;  // + 128: round to nearest
    dg += ((sg - dg) * a + 128) >> 8;
    db += ((sb - db) * a + 128) >> 8;
    dst = static_cast<uint16_t>(dr << 11 | dg << 5 | db);
}

inline float length(float x, float y) { return std::sqrt(x * x + y * y); }

// Blending a run of pixels with one colour at one opacity: each pixel spread out to
// 0000 0GGG GGG0 0000 RRRR R000 00BB BBB so all three channels mix in one multiply.
// 5-bit opacity, which is plenty for a run.
inline uint32_t spread565(uint16_t p) { return (static_cast<uint32_t>(p) | (static_cast<uint32_t>(p) << 16)) & 0x07E0F81Fu; }

void blendRun(uint16_t* p, int n, uint16_t c565, int a256) {
    if (a256 >= 256) {
        std::fill(p, p + n, c565);
        return;
    }
    const uint32_t a = static_cast<uint32_t>((a256 + 4) >> 3);  // 0..32
    if (a == 0) return;
    const uint32_t src = spread565(c565) * a, keep = 32 - a;
    for (int i = 0; i < n; i++) {
        uint32_t v = ((spread565(p[i]) * keep + src) >> 5) & 0x07E0F81Fu;
        p[i] = static_cast<uint16_t>(v | (v >> 16));
    }
}

// The glow's falloff, (1 - distance/r)^2, looked up by (distance/r)^2 so no square root.
struct GlowTable {
    uint8_t v[257];
    GlowTable() {
        for (int i = 0; i <= 256; i++) {
            float t = 1 - std::sqrt(i / 256.0f);
            v[i] = static_cast<uint8_t>(t * t * 255 + 0.5f);
        }
    }
};
const GlowTable kGlow;

// 5x7 glyphs, one string of 7 rows x 5 columns each.
struct Glyph {
    char c;
    const char* rows;
};

// clang-format off
const Glyph kFont[] = {
    {'A', ".###.#...##...#######...##...##...#"},
    {'B', "####.#...##...#####.#...##...#####."},
    {'C', ".###.#...##....#....#....#...#.###."},
    {'D', "####.#...##...##...##...##...#####."},
    {'E', "######....#....####.#....#....#####"},
    {'F', "######....#....####.#....#....#...."},
    {'G', ".###.#...##....#.####...##...#.####"},
    {'H', "#...##...##...#######...##...##...#"},
    {'I', ".###...#....#....#....#....#...###."},
    {'J', "..###...#....#....#....#.#..#..##.."},
    {'K', "#...##..#.#.#..##...#.#..#..#.#...#"},
    {'L', "#....#....#....#....#....#....#####"},
    {'M', "#...###.###.#.##.#.##...##...##...#"},
    {'N', "#...##...###..##.#.##..###...##...#"},
    {'O', ".###.#...##...##...##...##...#.###."},
    {'P', "####.#...##...#####.#....#....#...."},
    {'Q', ".###.#...##...##...##.#.##..#..##.#"},
    {'R', "####.#...##...#####.#.#..#..#.#...#"},
    {'S', ".#####....#.....###.....#....#####."},
    {'T', "#####..#....#....#....#....#....#.."},
    {'U', "#...##...##...##...##...##...#.###."},
    {'V', "#...##...##...##...##...#.#.#...#.."},
    {'W', "#...##...##...##.#.##.#.##.#.#.#.#."},
    {'X', "#...##...#.#.#...#...#.#.#...##...#"},
    {'Y', "#...##...#.#.#...#....#....#....#.."},
    {'Z', "#####....#...#...#...#...#....#####"},
    {'0', ".###.#...##..###.#.###..##...#.###."},
    {'1', "..#...##....#....#....#....#...###."},
    {'2', ".###.#...#....#...#...#...#...#####"},
    {'3', "#####...#...#.....#.....##...#.###."},
    {'4', "...#...##..#.#.#..#.#####...#....#."},
    {'5', "######....####.....#....##...#.###."},
    {'6', "..##..#...#....####.#...##...#.###."},
    {'7', "#####....#...#...#...#....#....#..."},
    {'8', ".###.#...##...#.###.#...##...#.###."},
    {'9', ".###.#...##...#.####....#...#..##.."},
    {'!', "..#....#....#....#....#.........#.."},
    {'\'', "..#....#...#......................."},
    {'(', "...#...#...#....#....#.....#.....#."},
    {')', ".#.....#.....#....#....#...#...#..."},
    {'+', ".......#....#..#####..#....#......."},
    {',', "....................##....#...#...."},
    {'-', "...............#####..............."},
    {'.', ".........................##....##.."},
    {'/', ".........#...#...#...#...#........."},
    {':', "......##...##.......##...##........"},
    {'?', ".###.#...#....#...#...#.........#.."},
    {'%', "##...##..#...#...#...#...#..##...##"},
};
// clang-format on

const char* glyphRows(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (const Glyph& g : kFont)
        if (g.c == c) return g.rows;
    return nullptr;  // space and anything unknown draw as blank
}

}  // namespace

Rgb mix(Rgb a, Rgb b, float t) {
    t = clamp01(t);
    auto ch = [&](int shift) {
        float x = static_cast<float>((a >> shift) & 0xFF), y = static_cast<float>((b >> shift) & 0xFF);
        return static_cast<Rgb>(x + (y - x) * t + 0.5f) << shift;
    };
    return ch(16) | ch(8) | ch(0);
}

uint16_t Canvas::ink(Rgb c) const { return to565(shadeA_ > 0 ? mix(c, shadeC_, shadeA_) : c); }

Rgb Canvas::pixel(int x, int y) const { return from565(px_[y * k_ * w_ + x * k_]); }

void Canvas::clear(Rgb c) { std::fill(px_, px_ + w_ * h_, ink(c)); }

void Canvas::verticalGradient(int y0, int y1, Rgb top, Rgb bottom) {
    y0 *= k_;
    y1 *= k_;
    y0 = std::max(y0, 0);
    y1 = std::min(y1, h_);
    for (int y = y0; y < y1; y++) {
        uint16_t c = ink(mix(top, bottom, static_cast<float>(y - y0) / std::max(1, y1 - y0 - 1)));
        std::fill(px_ + y * w_, px_ + (y + 1) * w_, c);
    }
}

void Canvas::tint(Rgb c, float alpha) {
    int a = alpha256(alpha);
    if (a <= 0) return;
    uint16_t c565 = to565(c);
    blendRun(px_, w_ * h_, c565, a);
}

// Fills a shape by pixel coverage: dist is the signed distance in pixels from a pixel centre
// to the shape's edge (negative inside), and edge pixels get partial ink.
//
// For convex shapes each row is one run: march in from both ends of the row (a distance
// says how far it is safe to skip), anti-alias the rim, and fill the inside as a solid run
// without measuring it. Only the rim costs a distance per pixel.
template <typename Dist>
void Canvas::fillShape(float x0, float y0, float x1, float y1, Rgb c, float a, Dist dist, float march) {
    const int a256 = alpha256(a);
    if (a256 <= 0) return;
    const uint16_t c565 = ink(c);
    const int ix0 = std::max(0, static_cast<int>(std::floor(x0 - 1)));
    const int iy0 = std::max(0, static_cast<int>(std::floor(y0 - 1)));
    const int ix1 = std::min(w_, static_cast<int>(std::ceil(x1 + 1)));
    const int iy1 = std::min(h_, static_cast<int>(std::ceil(y1 + 1)));

    for (int y = iy0; y < iy1; y++) {
        const float py = y + 0.5f;
        uint16_t* row = px_ + y * w_;
        auto rim = [&](int x, float d) {
            int cov = static_cast<int>(clamp01(0.5f - d) * static_cast<float>(a256) + 0.5f);
            if (cov > 0) blend565(row[x], c565, cov);
        };

        if (march <= 0) {
            for (int x = ix0; x < ix1; x++) rim(x, dist(x + 0.5f, py));
            continue;
        }

        int l = ix0;
        while (l < ix1) {
            float d = dist(l + 0.5f, py);
            if (d < 0.5f) break;
            l += std::max(1, static_cast<int>(d * march));
        }
        bool solid = false;
        for (; l < ix1; l++) {
            float d = dist(l + 0.5f, py);
            if (d <= -0.5f) {
                solid = true;
                break;
            }
            if (d >= 0.5f) break;  // out the other side: this row only grazes the shape
            rim(l, d);
        }
        if (!solid) continue;

        int r = ix1 - 1;
        while (r > l) {
            float d = dist(r + 0.5f, py);
            if (d < 0.5f) break;
            r -= std::max(1, static_cast<int>(d * march));
        }
        r = std::max(r, l);
        for (; r > l; r--) {
            float d = dist(r + 0.5f, py);
            if (d <= -0.5f) break;
            rim(r, d);
        }

        blendRun(row + l, r - l + 1, c565, a256);
    }
}

void Canvas::fillRect(float x, float y, float w, float h, Rgb c, float a) { fillRoundRect(x, y, w, h, 0, c, a); }

void Canvas::fillRoundRect(float x, float y, float w, float h, float r, Rgb c, float a) {
    x *= k_, y *= k_, w *= k_, h *= k_, r *= k_;
    float hx = w / 2, hy = h / 2, cx = x + hx, cy = y + hy;
    r = std::min(r, std::min(hx, hy));
    fillShape(x, y, x + w, y + h, c, a, [=](float px, float py) {
        float qx = std::fabs(px - cx) - (hx - r), qy = std::fabs(py - cy) - (hy - r);
        float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
        return length(ox, oy) + std::min(std::max(qx, qy), 0.0f) - r;
    });
}

void Canvas::fillCircle(float cx, float cy, float r, Rgb c, float a) {
    cx *= k_, cy *= k_, r *= k_;
    fillShape(cx - r, cy - r, cx + r, cy + r, c, a,
              [=](float px, float py) { return length(px - cx, py - cy) - r; });
}

void Canvas::fillEllipse(float cx, float cy, float rx, float ry, Rgb c, float a) {
    cx *= k_, cy *= k_, rx *= k_, ry *= k_;
    if (rx <= 0 || ry <= 0) return;
    float inner = -std::min(rx, ry);
    fillShape(cx - rx, cy - ry, cx + rx, cy + ry, c, a, [=](float px, float py) {
        // Implicit function over its gradient: a good pixel distance near the edge.
        float dx = px - cx, dy = py - cy;
        float f = dx * dx / (rx * rx) + dy * dy / (ry * ry) - 1;
        float gx = 2 * dx / (rx * rx), gy = 2 * dy / (ry * ry);
        float g = std::sqrt(gx * gx + gy * gy);
        return g > 1e-6f ? std::max(f / g, inner) : inner;
    }, 0.5f);  // an estimate, so skip empty space more cautiously
}

void Canvas::ring(float cx, float cy, float r, float thickness, Rgb c, float a) {
    cx *= k_, cy *= k_, r *= k_, thickness *= k_;
    float o = r + thickness / 2;
    fillShape(cx - o, cy - o, cx + o, cy + o, c, a,
              [=](float px, float py) { return std::fabs(length(px - cx, py - cy) - r) - thickness / 2; },
              0);  // a ring isn't convex
}

void Canvas::glow(float cx, float cy, float r, Rgb c, float a) {
    cx *= k_, cy *= k_, r *= k_;
    const int a256 = alpha256(a);
    if (a256 <= 0 || r <= 0) return;
    const uint16_t c565 = ink(c);
    int x0 = std::max(0, static_cast<int>(cx - r)), x1 = std::min(w_, static_cast<int>(cx + r) + 1);
    int y0 = std::max(0, static_cast<int>(cy - r)), y1 = std::min(h_, static_cast<int>(cy + r) + 1);
    const float inv = 256.0f / (r * r);
    for (int y = y0; y < y1; y++) {
        const float dy = y + 0.5f - cy, dy2 = dy * dy * inv;
        if (dy2 >= 256) continue;
        uint16_t* row = px_ + y * w_;
        for (int x = x0; x < x1; x++) {
            const float dx = x + 0.5f - cx;
            const int q = static_cast<int>(dx * dx * inv + dy2);
            if (q >= 256) continue;
            // Full 8-bit opacity here: the glow's faint outer edge would round away in 5 bits.
            const int a = (static_cast<int>(kGlow.v[q]) * a256 + 128) >> 8;
            if (a > 0) blend565(row[x], c565, a);
        }
    }
}

void Canvas::fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Rgb c, float a) {
    x0 *= k_, y0 *= k_, x1 *= k_, y1 *= k_, x2 *= k_, y2 *= k_;
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::fabs(area) < 1e-3f) return;
    if (area < 0) {  // make the winding consistent so "outside" is always positive
        std::swap(x1, x2);
        std::swap(y1, y2);
    }
    const float xs[3] = {x0, x1, x2}, ys[3] = {y0, y1, y2};
    float nx[3], ny[3], off[3];
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        float ex = xs[j] - xs[i], ey = ys[j] - ys[i], len = length(ex, ey);
        nx[i] = ey / len;
        ny[i] = -ex / len;
        off[i] = nx[i] * xs[i] + ny[i] * ys[i];
    }
    float bx0 = std::min({x0, x1, x2}), by0 = std::min({y0, y1, y2});
    float bx1 = std::max({x0, x1, x2}), by1 = std::max({y0, y1, y2});
    fillShape(bx0, by0, bx1, by1, c, a, [&](float px, float py) {
        float d = -1e9f;
        for (int i = 0; i < 3; i++) d = std::max(d, nx[i] * px + ny[i] * py - off[i]);
        return d;
    });
}

void Canvas::line(float x0, float y0, float x1, float y1, float width, Rgb c, float a) {
    x0 *= k_, y0 *= k_, x1 *= k_, y1 *= k_, width *= k_;
    float hw = width / 2;
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    fillShape(std::min(x0, x1) - hw, std::min(y0, y1) - hw, std::max(x0, x1) + hw, std::max(y0, y1) + hw, c, a,
              [=](float px, float py) {
                  float t = len2 > 0 ? clamp01(((px - x0) * dx + (py - y0) * dy) / len2) : 0;
                  return length(px - (x0 + t * dx), py - (y0 + t * dy)) - hw;
              });
}

int Canvas::textWidth(const char* s, int scale) {
    int n = 0;
    for (; s && *s; s++) n++;
    return n ? (n * 6 - 1) * scale : 0;
}

float Canvas::text(float x, float y, const char* s, int scale, Rgb c, Align align, float a) {
    float w = static_cast<float>(textWidth(s, scale));
    if (align == Align::Center) x -= w / 2;
    if (align == Align::Right) x -= w;
    x = std::round(x * k_);
    y = std::round(y * k_);
    const int dot = scale * k_;
    // Whole-pixel squares need no anti-aliasing: fill each dot's rows directly.
    const uint16_t c565 = ink(c);
    const int a256 = alpha256(a);
    for (; s && *s; s++, x += 6 * dot) {
        const char* rows = glyphRows(*s);
        if (!rows || a256 <= 0) continue;
        for (int r = 0; r < 7; r++) {
            for (int col = 0; col < 5; col++) {
                if (rows[r * 5 + col] != '#') continue;
                int px0 = static_cast<int>(x) + col * dot, py0 = static_cast<int>(y) + r * dot;
                int px1 = std::min(w_, px0 + dot), py1 = std::min(h_, py0 + dot);
                px0 = std::max(0, px0);
                for (int py = std::max(0, py0); py < py1; py++)
                    if (px1 > px0) blendRun(px_ + py * w_ + px0, px1 - px0, c565, a256);
            }
        }
    }
    return x / k_;
}

}  // namespace squatch
