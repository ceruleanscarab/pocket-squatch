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
    // width and height are the buffer's real size in pixels. With scale 2 or 3 everything is
    // drawn that many times larger and sharper (a phone's screen), while callers keep drawing
    // in the same coordinates: width() and height() are the buffer's size divided by scale.
    Canvas(uint16_t* pixels, int width, int height, int scale = 1)
        : px_(pixels), w_(width), h_(height), k_(scale < 1 ? 1 : scale) {}

    int width() const { return w_ / k_; }
    int height() const { return h_ / k_; }
    int scale() const { return k_; }
    int pixelCount() const { return w_ * h_; }
    uint16_t* pixels() { return px_; }
    Rgb pixel(int x, int y) const;  // for tests, in drawing coordinates

    void clear(Rgb c);
    void verticalGradient(int y0, int y1, Rgb top, Rgb bottom);
    void tint(Rgb c, float alpha);  // the whole screen, for overlays and the photo flash
    // Shading for everything drawn from now on: each colour is mixed toward c by alpha
    // before it's drawn. The same picture as drawing and then tint(), without a pass over
    // the whole screen. 0 turns it off.
    void setShade(Rgb c, float alpha) {
        shadeC_ = c;
        shadeA_ = alpha;
    }

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
    // march: how far a distance may be trusted when skipping empty space (1 for exact
    // distances, less for estimates); 0 marks a shape that isn't convex, tested pixel by pixel.
    template <typename Dist>
    void fillShape(float x0, float y0, float x1, float y1, Rgb c, float a, Dist dist, float march = 1);

    uint16_t ink(Rgb c) const;  // c, shaded, as RGB565

    uint16_t* px_;
    int w_, h_;  // real pixels
    int k_;      // drawing coordinates to real pixels
    Rgb shadeC_ = 0;
    float shadeA_ = 0;
};

}  // namespace squatch
