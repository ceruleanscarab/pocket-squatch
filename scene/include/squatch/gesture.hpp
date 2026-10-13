// Raw touch points in, gestures out. The one place that decides what a tap, a drag,
// a long press or a two-finger tap is, shared by the scene and (later) the mini-games.
//
// Accessibility: nothing here depends on speed. A tap can be slow, a long press only
// needs a still finger, and there are no double taps. Holding still before dragging
// still drags.
#pragma once

#include <cstddef>
#include <cstdint>

namespace squatch {

enum class TouchPhase : uint8_t { Down, Move, Up };

struct TouchEvent {
    TouchPhase phase;
    uint8_t id;  // finger, 0 or 1
    int16_t x, y;
    uint32_t ms;
};

enum class GestureType : uint8_t {
    Tap,
    LongPress,     // fires while the finger is still down
    DragStart,     // x,y is where it is now; startX,startY where the finger went down
    DragMove,
    DragEnd,
    TwoFingerTap,  // at the midpoint of the two fingers
};

struct Gesture {
    GestureType type;
    int16_t x, y;
    int16_t startX, startY;
    int16_t prevX, prevY;  // for drags: the previous point, so a segment can be hit-tested
    float speed;           // for drags: recent speed in px/s
};

class GestureReader {
public:
    static constexpr int kSlopPx = 14;  // movement that still counts as a tap
    static constexpr uint32_t kLongPressMs = 650;
    static constexpr size_t kQueueCap = 16;

    void touch(const TouchEvent& e);
    void update(uint32_t nowMs);  // long presses fire from here
    bool poll(Gesture& out);

private:
    enum class Mode : uint8_t { Idle, Pressed, Held, Dragging, TwoFinger, Cancelled };

    void push(GestureType t, int x, int y);
    int fingersDown() const { return down_[0] + down_[1]; }

    Mode mode_ = Mode::Idle;
    bool down_[2] = {false, false};
    int16_t x_[2] = {}, y_[2] = {};
    int16_t downX_[2] = {}, downY_[2] = {};
    int16_t startX_ = 0, startY_ = 0, prevX_ = 0, prevY_ = 0;
    uint8_t primary_ = 0;
    uint32_t downMs_ = 0, lastMs_ = 0;
    float speed_ = 0;

    Gesture queue_[kQueueCap]{};
    size_t qHead_ = 0, qCount_ = 0;
};

}  // namespace squatch
