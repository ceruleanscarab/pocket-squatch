#include "squatch/wrist.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace squatch {

namespace {

constexpr Rgb kInk = 0xF3EBD9, kDim = 0x1B2416, kGold = 0xF2C14E;
constexpr int kButtonW = 92, kButtonH = 76, kButtonY = 348, kButtonGap = 100;

bool needsYou(const State& s) {
    return s.calling || s.sick || s.hunger <= 1 || s.happiness <= 1 || s.messes >= 2;
}

bool present(const State& s) {
    return s.stage != Stage::Egg && s.stage != Stage::Away && !s.foraging;
}

int choose(const State& s, Wrist::Action out[Wrist::kMaxButtons]) {
    if (s.stage == Stage::Egg || s.foraging) return 0;
    if (s.stage == Stage::Away) {
        out[0] = Wrist::Action::LeaveFood;
        return 1;
    }
    if (s.asleep) return 0;
    int n = 0;
    auto add = [&](Wrist::Action a) {
        if (n < Wrist::kMaxButtons) out[n++] = a;
    };
    if (s.calling) add(Wrist::Action::Answer);
    if (s.sick) add(Wrist::Action::Heal);
    if (s.messes > 0) add(Wrist::Action::Clean);
    if (s.hunger < 3.75f) add(Wrist::Action::Feed);
    add(Wrist::Action::Pet);
    if (n < Wrist::kMaxButtons && s.hunger >= 3.75f) add(Wrist::Action::Feed);  // so FEED is always somewhere
    return n;
}

void drawIcon(Canvas& c, Wrist::Action a, float x, float y) {
    switch (a) {
        case Wrist::Action::Feed:
        case Wrist::Action::LeaveFood:
            c.fillCircle(x - 7, y + 4, 8, 0xB0336A);
            c.fillCircle(x + 7, y + 4, 8, 0xB0336A);
            c.fillCircle(x, y - 7, 8, 0xC2417A);
            c.fillEllipse(x + 6, y - 16, 6, 3, 0x5C8F3E);
            break;
        case Wrist::Action::Pet:
            c.fillCircle(x - 7, y - 4, 8.5f, 0xE76F8A);
            c.fillCircle(x + 7, y - 4, 8.5f, 0xE76F8A);
            c.fillTriangle(x - 15, y - 1, x + 15, y - 1, x, y + 15, 0xE76F8A);
            break;
        case Wrist::Action::Heal:
            c.fillEllipse(x, y, 10, 17, 0x6FA34A);
            c.fillRect(x - 9, y - 2.5f, 18, 5, 0xFFFFFF);
            c.fillRect(x - 2.5f, y - 9, 5, 18, 0xFFFFFF);
            break;
        case Wrist::Action::Clean:
            c.fillCircle(x, y + 5, 10, 0x6EC6E8);
            c.fillTriangle(x - 9, y + 2, x + 9, y + 2, x, y - 17, 0x6EC6E8);
            break;
        case Wrist::Action::Answer:
            c.fillEllipse(x, y + 4, 8, 12, 0xD9B48A);
            for (int t = -1; t <= 1; t++) c.fillCircle(x + t * 7.0f, y - 13, 3.5f, 0xD9B48A);
            break;
        case Wrist::Action::None: break;
    }
}

}  // namespace

const char* toString(Wrist::Action a) {
    switch (a) {
        case Wrist::Action::Feed: return "FEED";
        case Wrist::Action::Pet: return "PET";
        case Wrist::Action::Heal: return "HEAL";
        case Wrist::Action::Clean: return "CLEAN";
        case Wrist::Action::Answer: return "ANSWER";
        case Wrist::Action::LeaveFood: return "FOOD";
        case Wrist::Action::None: break;
    }
    return "";
}

Wrist::Wrist(Sim& sim) : sim_(sim), scene_(sim) { scene_.setCompact(true); }

void Wrist::update(uint32_t nowMs) {
    now_ = nowMs;
    scene_.update(nowMs);
    Cue c;
    while (scene_.takeCue(c)) {}  // the watch is quiet; the buzz is the notification's job
}

int Wrist::buttonCount() const {
    Action a[kMaxButtons];
    return choose(sim_.state(), a);
}

Wrist::Action Wrist::buttonAction(int i) const {
    Action a[kMaxButtons];
    int n = choose(sim_.state(), a);
    return i >= 0 && i < n ? a[i] : Action::None;
}

Scene::Rect Wrist::buttonArea(int i) const {
    const int n = buttonCount();
    const int cx = kSize / 2 + (2 * i - (n - 1)) * kButtonGap / 2;
    return {cx - kButtonW / 2, kButtonY, kButtonW, kButtonH};
}

const char* Wrist::status() const {
    const State& s = sim_.state();
    if (s.stage == Stage::Egg) return "AN EGG";
    if (s.stage == Stage::Away) return "IT RAN OFF";
    if (s.foraging) return "OUT ON A TRAIL";
    if (s.asleep) return "ASLEEP";
    if (s.calling) return "CALLING YOU!";
    if (s.sick) return "SICK";
    if (s.misbehaving) return "MISBEHAVING";
    if (s.hunger <= 1) return "HUNGRY";
    if (s.happiness <= 1) return "LONELY";
    if (s.messes >= 2) return "MESSY";
    return "ALL GOOD";
}

const char* Wrist::message() const { return now_ < msgUntil_ && msg_[0] ? msg_ : nullptr; }

void Wrist::say(const char* text) {
    std::snprintf(msg_, sizeof msg_, "%s", text);
    msgUntil_ = now_ + 2200;
}

Wrist::Action Wrist::tap(int x, int y) {
    const int n = buttonCount();
    for (int i = 0; i < n; i++) {
        if (!buttonArea(i).contains(x, y)) continue;
        const Action a = buttonAction(i);
        const bool misbehaving = sim_.state().misbehaving;
        Result r = Result::NotNow;
        switch (a) {
            case Action::Feed:
                r = sim_.feed(Food::Fish);
                say(r == Result::Ok ? "YUM!" : r == Result::Refused ? (misbehaving ? "IT THREW IT BACK" : "IT'S FULL")
                                                                    : "NOT NOW");
                break;
            case Action::Pet:
                r = sim_.pet();
                say(r == Result::Ok ? "IT LIKED THAT" : r == Result::Refused ? "IT'S TOO SHY" : "NOT NOW");
                break;
            case Action::Heal:
                r = sim_.medicine();
                say(r == Result::Ok ? "FEELING BETTER" : "NOT NOW");
                break;
            case Action::Clean:
                r = sim_.clean();
                say(r == Result::Ok ? (sim_.state().messes ? "ONE CLEANED UP" : "ALL CLEAN") : "NOT NOW");
                break;
            case Action::Answer:
                r = sim_.answerCall();
                say(r == Result::Ok ? "YOU ANSWERED!" : "NOT NOW");
                break;
            case Action::LeaveFood:
                r = sim_.leaveFoodOut();
                say(r == Result::Ok ? "FOOD LEFT OUT" : "ALREADY LEFT TODAY");
                break;
            case Action::None: break;
        }
        return r == Result::Ok ? a : Action::None;
    }
    return Action::None;
}

void Wrist::draw(Canvas& c) {
    const State& s = sim_.state();
    scene_.draw(c);

    // Name and status at the top, in pills that stay inside the round screen.
    const char* name = s.name[0] ? s.name : "SQUATCH";
    const int ns = Canvas::textWidth(name, 3) <= 260 ? 3 : 2;
    float w = static_cast<float>(Canvas::textWidth(name, ns)) + 36;
    c.fillRoundRect(240 - w / 2, 42, w, 40, 20, kDim, 0.8f);
    c.text(240, 62 - Canvas::textHeight(ns) / 2.0f, name, ns, kInk, Align::Center);

    const char* st = status();
    const bool urgent = present(s) && !s.asleep && needsYou(s);
    w = static_cast<float>(Canvas::textWidth(st, 2)) + 28;
    c.fillRoundRect(240 - w / 2, 90, w, 28, 14, urgent ? 0x7A2E1E : kDim, 0.85f);
    c.text(240, 97, st, 2, urgent ? kGold : kInk, Align::Center);

    // Meters: food, happiness, cleanliness, four pips each.
    if (present(s)) {
        c.fillRoundRect(96, 126, 288, 30, 15, kDim, 0.75f);
        const float vals[3] = {s.hunger, s.happiness, static_cast<float>(std::max(0, 4 - static_cast<int>(s.messes)))};
        const Rgb cols[3] = {0xC2417A, 0xE76F8A, 0x6EC6E8};
        for (int m = 0; m < 3; m++) {
            const float x0 = 116 + m * 96.0f;
            c.fillCircle(x0, 141, 7, cols[m]);
            for (int p = 0; p < 4; p++) {
                const float fill = std::max(0.0f, std::min(1.0f, vals[m] - p));
                c.fillCircle(x0 + 18 + p * 12.0f, 141, 4, 0x000000, 0.4f);
                if (fill > 0) c.fillCircle(x0 + 18 + p * 12.0f, 141, 4 * (0.4f + 0.6f * fill), kInk);
            }
        }
    }

    // The reply to the last tap.
    if (const char* m = message()) {
        w = static_cast<float>(Canvas::textWidth(m, 2)) + 28;
        c.fillRoundRect(240 - w / 2, 166, w, 30, 15, kDim, 0.9f);
        c.text(240, 174, m, 2, kGold, Align::Center);
    }

    // The buttons along the bottom.
    for (int i = 0; i < buttonCount(); i++) {
        Scene::Rect r = buttonArea(i);
        c.fillRoundRect(r.x, r.y, r.w, r.h, 22, kDim, 0.88f);
        drawIcon(c, buttonAction(i), r.cx(), r.y + 28.0f);
        c.text(r.cx(), r.y + r.h - 20.0f, toString(buttonAction(i)), 2, kInk, Align::Center);
    }
}

const char* Wrist::needNow(const State& s) {
    if (!present(s) || s.asleep) return nullptr;
    if (s.calling) return "is calling you";
    if (s.sick) return "is sick";
    if (s.hunger <= 1) return "is hungry";
    if (s.happiness <= 1) return "is lonely";
    if (s.messes >= 2) return "needs a clean-up";
    return nullptr;
}

int64_t Wrist::nextNeed(const State& now, int64_t horizonSecs, const char** why) {
    Sim t;
    t.mutableState() = now;
    constexpr int64_t kStep = 300;
    Event ev[16];
    for (int64_t ahead = 0; ahead <= horizonSecs; ahead += kStep) {
        if (const char* w = needNow(t.state())) {
            if (why) *why = w;
            return ahead;
        }
        t.tick(kStep);
        while (t.drainEvents(ev, 16) > 0) {}
    }
    if (why) *why = nullptr;
    return -1;
}

}  // namespace squatch
