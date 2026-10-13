#include "squatch/gesture.hpp"

#include <cmath>

namespace squatch {

namespace {
float dist(int x0, int y0, int x1, int y1) { return std::hypot(static_cast<float>(x1 - x0), static_cast<float>(y1 - y0)); }
}  // namespace

void GestureReader::touch(const TouchEvent& e) {
    const uint8_t id = e.id & 1;

    switch (e.phase) {
        case TouchPhase::Down:
            down_[id] = true;
            x_[id] = downX_[id] = e.x;
            y_[id] = downY_[id] = e.y;
            if (mode_ == Mode::Idle) {
                mode_ = Mode::Pressed;
                primary_ = id;
                startX_ = prevX_ = e.x;
                startY_ = prevY_ = e.y;
                downMs_ = lastMs_ = e.ms;
                speed_ = 0;
            } else if (mode_ == Mode::Pressed || mode_ == Mode::Held) {
                mode_ = Mode::TwoFinger;
            }
            break;

        case TouchPhase::Move: {
            if (!down_[id]) break;
            x_[id] = e.x;
            y_[id] = e.y;
            if (mode_ == Mode::TwoFinger) {
                // two fingers sliding around is not a tap
                if (dist(e.x, e.y, downX_[id], downY_[id]) > 2 * kSlopPx) mode_ = Mode::Cancelled;
                break;
            }
            if (id != primary_) break;
            if (mode_ == Mode::Pressed || mode_ == Mode::Held) {
                if (dist(e.x, e.y, startX_, startY_) > kSlopPx) {
                    mode_ = Mode::Dragging;
                    push(GestureType::DragStart, e.x, e.y);
                    prevX_ = e.x;
                    prevY_ = e.y;
                    lastMs_ = e.ms;
                }
            } else if (mode_ == Mode::Dragging) {
                uint32_t dt = e.ms - lastMs_;
                if (dt > 0) {
                    float inst = dist(prevX_, prevY_, e.x, e.y) * 1000.0f / static_cast<float>(dt);
                    speed_ = speed_ == 0 ? inst : 0.6f * speed_ + 0.4f * inst;
                }
                push(GestureType::DragMove, e.x, e.y);
                prevX_ = e.x;
                prevY_ = e.y;
                lastMs_ = e.ms;
            }
            break;
        }

        case TouchPhase::Up:
            if (!down_[id]) break;
            down_[id] = false;
            if (mode_ == Mode::Pressed) {
                push(GestureType::Tap, startX_, startY_);
            } else if (mode_ == Mode::Dragging && id == primary_) {
                push(GestureType::DragEnd, x_[id], y_[id]);
                mode_ = Mode::Cancelled;  // ignore a second finger still resting
            } else if (mode_ == Mode::TwoFinger && fingersDown() == 0) {
                push(GestureType::TwoFingerTap, (x_[0] + x_[1]) / 2, (y_[0] + y_[1]) / 2);
            }
            if (fingersDown() == 0) mode_ = Mode::Idle;
            break;
    }
}

void GestureReader::update(uint32_t nowMs) {
    if (mode_ == Mode::Pressed && nowMs - downMs_ >= kLongPressMs) {
        push(GestureType::LongPress, startX_, startY_);
        mode_ = Mode::Held;  // lifting now does nothing, but moving still starts a drag
    }
}

bool GestureReader::poll(Gesture& out) {
    if (qCount_ == 0) return false;
    out = queue_[(qHead_ + kQueueCap - qCount_) % kQueueCap];
    qCount_--;
    return true;
}

void GestureReader::push(GestureType t, int x, int y) {
    Gesture g{};
    g.type = t;
    g.x = static_cast<int16_t>(x);
    g.y = static_cast<int16_t>(y);
    g.startX = startX_;
    g.startY = startY_;
    g.prevX = t == GestureType::DragStart ? startX_ : prevX_;
    g.prevY = t == GestureType::DragStart ? startY_ : prevY_;
    g.speed = speed_;
    queue_[qHead_] = g;
    qHead_ = (qHead_ + 1) % kQueueCap;
    if (qCount_ < kQueueCap) qCount_++;
}

}  // namespace squatch
