// Software renderer into an RGB565 framebuffer (the panel's native format).
//
// Every shape is anti-aliased by pixel coverage, so even the placeholder art is smooth.
// The PC scene shows this buffer in an SDL window; on the board the same buffer goes
// to the panel, so both draw exactly the same pixels.
#pragma once

#include <cstdint>

namespace squatch {

// Colours are 0xRRGGBB.
using Rgb = uint32_t;

Rgb mix(Rgb a, Rgb b, float t);  // t = 0 gives a, 1 gives b

enum class Align : uint8_t { Left, Center, Right };

class Canvas {
public:
    Canvas(uint16_t* pixels, int width, int height) : px_(pixels), w_(width), h_(height) {}

    int width() const { return w_; }
    int height() const { return h_; }
    uint16_t* pixels() { return px_; }
    Rgb pixel(int x, int y) const;  // for tests

    void clear(Rgb c);
    void verticalGradient(int y0, int y1, Rgb top, Rgb bottom);
    void tint(Rgb c, float alpha);  // the whole screen, for night and lights-off

    // a is opacity, 0..1
    void fillRect(float x, float y, float w, float h, Rgb c, float a = 1);
    void fillRoundRect(float x, float y, float w, float h, float r, Rgb c, float a = 1);
    void fillCircle(float cx, float cy, float r, Rgb c, float a = 1);
    void fillEllipse(float cx, float cy, float rx, float ry, Rgb c, float a = 1);
    void ring(float cx, float cy, float r, float thickness, Rgb c, float a = 1);
    void fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Rgb c, float a = 1);
    // Soft light: a disc that fades smoothly from a at the centre to nothing at radius r.
    void glow(float cx, float cy, float r, Rgb c, float a);
    // A thick line with round ends.
    void line(float x0, float y0, float x1, float y1, float width, Rgb c, float a = 1);

    // Built-in 5x7 font (upper case, digits, a little punctuation), scaled by whole pixels.
    // Returns the x just after the text.
    float text(float x, float y, const char* s, int scale, Rgb c, Align align = Align::Left, float a = 1);
    static int textWidth(const char* s, int scale);
    static int textHeight(int scale) { return 7 * scale; }

private:
    template <typename Dist>
    void fillShape(float x0, float y0, float x1, float y1, Rgb c, float a, Dist dist);
    void blend(int x, int y, Rgb c, float a);

    uint16_t* px_;
    int w_, h_;
};

}  // namespace squatch
