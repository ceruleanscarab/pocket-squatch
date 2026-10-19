// The watch's view of the pet: a round screen with the clearing, its name, how it's doing,
// and up to three big buttons for whatever it needs right now.
//
// Hardware-free like the scene. The Wear OS app (android/wear) loads the phone's save,
// ticks it to the present, draws this into a bitmap, and passes taps back. It also asks
// nextNeed() when to buzz your wrist next.
#pragma once

#include <cstdint>

#include "squatch/canvas.hpp"
#include "squatch/scene.hpp"
#include "squatch/sim.hpp"

namespace squatch {

class Wrist {
public:
    static constexpr int kSize = Scene::kSize;  // drawn at 480x480, shown on a round screen
    static constexpr int kMaxButtons = 3;

    enum class Action : uint8_t { None, Feed, Pet, Heal, Clean, Answer, LeaveFood };

    explicit Wrist(Sim& sim);

    void update(uint32_t nowMs);
    void draw(Canvas& c);
    // A tap on the round screen, in 480x480 terms. Returns what was done (None if nothing),
    // so the app knows to save and sync.
    Action tap(int x, int y);

    int buttonCount() const;
    Action buttonAction(int i) const;
    Scene::Rect buttonArea(int i) const;
    const char* status() const;   // "HUNGRY", "CALLING YOU", "ALL GOOD", ...
    const char* message() const;  // the reply to the last tap, for a moment, or nullptr

    // How long until it next needs you (hungry, sad, sick, calling, or a mess going stale),
    // looking ahead up to horizonSecs; -1 if not within that. Also what it will want.
    // Works on a copy, so the real pet is untouched.
    static int64_t nextNeed(const State& now, int64_t horizonSecs, const char** why);
    // What it needs this moment, or nullptr: the notification's words.
    static const char* needNow(const State& s);

private:
    void say(const char* text);

    Sim& sim_;
    Scene scene_;
    uint32_t now_ = 0;
    char msg_[32] = {};
    uint32_t msgUntil_ = 0;
};

const char* toString(Wrist::Action a);

}  // namespace squatch
