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

Rgb Canvas::pixel(int x, int y) const { return from565(px_[y * w_ + x]); }

void Canvas::blend(int x, int y, Rgb c, float a) {
    uint16_t& p = px_[y * w_ + x];
    p = to565(a >= 0.999f ? c : mix(from565(p), c, a));
}

void Canvas::clear(Rgb c) { std::fill(px_, px_ + w_ * h_, to565(c)); }

void Canvas::verticalGradient(int y0, int y1, Rgb top, Rgb bottom) {
    y0 = std::max(y0, 0);
    y1 = std::min(y1, h_);
    for (int y = y0; y < y1; y++) {
        uint16_t c = to565(mix(top, bottom, static_cast<float>(y - y0) / std::max(1, y1 - y0 - 1)));
        std::fill(px_ + y * w_, px_ + (y + 1) * w_, c);
    }
}

void Canvas::tint(Rgb c, float alpha) {
    if (alpha <= 0) return;
    for (int i = 0; i < w_ * h_; i++) px_[i] = to565(mix(from565(px_[i]), c, alpha));
}

// Fills every pixel in the box by coverage: dist is the signed distance in pixels from a
// pixel centre to the shape's edge (negative inside), and the edge pixel gets partial ink.
template <typename Dist>
void Canvas::fillShape(float x0, float y0, float x1, float y1, Rgb c, float a, Dist dist) {
    if (a <= 0) return;
    int ix0 = std::max(0, static_cast<int>(std::floor(x0 - 1)));
    int iy0 = std::max(0, static_cast<int>(std::floor(y0 - 1)));
    int ix1 = std::min(w_, static_cast<int>(std::ceil(x1 + 1)));
    int iy1 = std::min(h_, static_cast<int>(std::ceil(y1 + 1)));
    for (int y = iy0; y < iy1; y++) {
        float py = y + 0.5f;
        for (int x = ix0; x < ix1; x++) {
            float cov = clamp01(0.5f - dist(x + 0.5f, py));
            if (cov > 0) blend(x, y, c, a * cov);
        }
    }
}

void Canvas::fillRect(float x, float y, float w, float h, Rgb c, float a) { fillRoundRect(x, y, w, h, 0, c, a); }

void Canvas::fillRoundRect(float x, float y, float w, float h, float r, Rgb c, float a) {
    float hx = w / 2, hy = h / 2, cx = x + hx, cy = y + hy;
    r = std::min(r, std::min(hx, hy));
    fillShape(x, y, x + w, y + h, c, a, [=](float px, float py) {
        float qx = std::fabs(px - cx) - (hx - r), qy = std::fabs(py - cy) - (hy - r);
        float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
        return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
    });
}

void Canvas::fillCircle(float cx, float cy, float r, Rgb c, float a) {
    fillShape(cx - r, cy - r, cx + r, cy + r, c, a,
              [=](float px, float py) { return std::hypot(px - cx, py - cy) - r; });
}

void Canvas::fillEllipse(float cx, float cy, float rx, float ry, Rgb c, float a) {
    if (rx <= 0 || ry <= 0) return;
    float inner = -std::min(rx, ry);
    fillShape(cx - rx, cy - ry, cx + rx, cy + ry, c, a, [=](float px, float py) {
        // Implicit function over its gradient: a good pixel distance near the edge.
        float dx = px - cx, dy = py - cy;
        float f = dx * dx / (rx * rx) + dy * dy / (ry * ry) - 1;
        float gx = 2 * dx / (rx * rx), gy = 2 * dy / (ry * ry);
        float g = std::sqrt(gx * gx + gy * gy);
        return g > 1e-6f ? std::max(f / g, inner) : inner;
    });
}

void Canvas::ring(float cx, float cy, float r, float thickness, Rgb c, float a) {
    float o = r + thickness / 2;
    fillShape(cx - o, cy - o, cx + o, cy + o, c, a,
              [=](float px, float py) { return std::fabs(std::hypot(px - cx, py - cy) - r) - thickness / 2; });
}

void Canvas::glow(float cx, float cy, float r, Rgb c, float a) {
    if (a <= 0 || r <= 0) return;
    int x0 = std::max(0, static_cast<int>(cx - r)), x1 = std::min(w_, static_cast<int>(cx + r) + 1);
    int y0 = std::max(0, static_cast<int>(cy - r)), y1 = std::min(h_, static_cast<int>(cy + r) + 1);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float t = 1 - std::hypot(x + 0.5f - cx, y + 0.5f - cy) / r;
            if (t > 0) blend(x, y, c, a * t * t);
        }
    }
}

void Canvas::fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Rgb c, float a) {
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
        float ex = xs[j] - xs[i], ey = ys[j] - ys[i], len = std::hypot(ex, ey);
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
    float hw = width / 2;
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    fillShape(std::min(x0, x1) - hw, std::min(y0, y1) - hw, std::max(x0, x1) + hw, std::max(y0, y1) + hw, c, a,
              [=](float px, float py) {
                  float t = len2 > 0 ? clamp01(((px - x0) * dx + (py - y0) * dy) / len2) : 0;
                  return std::hypot(px - (x0 + t * dx), py - (y0 + t * dy)) - hw;
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
    x = std::round(x);
    y = std::round(y);
    for (; s && *s; s++, x += 6 * scale) {
        const char* rows = glyphRows(*s);
        if (!rows) continue;
        for (int r = 0; r < 7; r++)
            for (int col = 0; col < 5; col++)
                if (rows[r * 5 + col] == '#')
                    fillRect(x + col * scale, y + r * scale, static_cast<float>(scale), static_cast<float>(scale), c, a);
    }
    return x;
}

}  // namespace squatch
