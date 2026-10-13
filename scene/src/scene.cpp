#include "squatch/scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace squatch {

namespace {

constexpr int S = Scene::kSize;
constexpr float kPi = 3.14159265f;

// ---- layout
constexpr float kHorizon = 210;
constexpr float kLeftTreeX = 48, kLeftTreeBase = 344;
constexpr float kRightTreeX = 432, kRightTreeBase = 334;
constexpr float kStumpX = 388, kStumpY = 362;
constexpr float kFireX = 96, kFireY = 392;
constexpr float kHideX = 74, kHideY = 290;   // peeking out from behind the left tree
constexpr float kEggX = 240, kEggY = 288;
constexpr float kFriendlyFoot = 345, kWanderFoot = 302;
constexpr float kMessSpots[3][2] = {{168, 350}, {312, 352}, {132, 326}};

// ---- feel
constexpr float kStrokeShare = 0.6f;      // stroke across this much of the pet's width to pet it
constexpr float kMaxStrokeSpeed = 1500;   // px/s; a flick is not a stroke
constexpr uint32_t kToastMs = 2600;

// ---- colours
constexpr Rgb kNight = 0x0A1030;
constexpr Rgb kGrass = 0x5E9E4A, kClearing = 0x86B85A, kBackTrees = 0x2F5D50;
constexpr Rgb kTrunk = 0x6B4A2F, kLeaves1 = 0x2E6B3F, kLeaves2 = 0x3A7F4A;
constexpr Rgb kFur = 0x8A5A3B, kFace = 0xC79A72, kEye = 0x1A120C;
constexpr Rgb kUiDark = 0x1B2416, kUiLight = 0xF3EBD9, kGold = 0xF2C14E;
constexpr Rgb kTray = 0x2B2118, kButton = 0x4A3A2C;

constexpr Scene::Slot kMainSlots[] = {Scene::Slot::Feed, Scene::Slot::Clean, Scene::Slot::Medicine,
                                      Scene::Slot::Play, Scene::Slot::Light, Scene::Slot::Photos};
constexpr Scene::Slot kFoodSlots[] = {Scene::Slot::Back, Scene::Slot::Berries, Scene::Slot::Fish,
                                      Scene::Slot::Roots};

Scene::Item itemFor(Scene::Slot s) {
    switch (s) {
        case Scene::Slot::Clean: return Scene::Item::Brush;
        case Scene::Slot::Medicine: return Scene::Item::Poultice;
        case Scene::Slot::Play: return Scene::Item::Pinecone;
        case Scene::Slot::Berries: return Scene::Item::Berries;
        case Scene::Slot::Fish: return Scene::Item::Fish;
        case Scene::Slot::Roots: return Scene::Item::Roots;
        default: return Scene::Item::None;
    }
}

bool isFood(Scene::Item it) {
    return it == Scene::Item::Berries || it == Scene::Item::Fish || it == Scene::Item::Roots;
}

float smooth(float t) { return t * t * (3 - 2 * t); }

float segmentDistance(float px, float py, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? std::max(0.0f, std::min(1.0f, ((px - x0) * dx + (py - y0) * dy) / len2)) : 0;
    return std::hypot(px - (x0 + t * dx), py - (y0 + t * dy));
}

float headRatio(Stage s) {
    switch (s) {
        case Stage::Baby: return 1.3f;
        case Stage::Child: return 1.15f;
        case Stage::Teen: return 1.05f;
        default: return 1.0f;
    }
}

void drawHeart(Canvas& c, float x, float y, float r, Rgb col, float a) {
    c.fillCircle(x - r * 0.5f, y - r * 0.2f, r * 0.58f, col, a);
    c.fillCircle(x + r * 0.5f, y - r * 0.2f, r * 0.58f, col, a);
    c.fillTriangle(x - r * 1.05f, y + r * 0.05f, x + r * 1.05f, y + r * 0.05f, x, y + r * 1.15f, col, a);
}

void drawFootprint(Canvas& c, float x, float y, float k, Rgb col, float a = 1) {
    c.fillEllipse(x, y + 3 * k, 6 * k, 8.5f * k, col, a);
    const float toes[5][3] = {{-6, -7.5f, 2.2f}, {-2.5f, -9.5f, 2.2f}, {1.5f, -9.8f, 2.1f}, {5, -8.6f, 1.9f}, {7.4f, -6, 1.6f}};
    for (const auto& t : toes) c.fillCircle(x + t[0] * k, y + t[1] * k, t[2] * k, col, a);
}

const char* trustName(TrustLevel t) {
    switch (t) {
        case TrustLevel::Hiding: return "HIDING";
        case TrustLevel::Wandering: return "WANDERING";
        case TrustLevel::Friendly: return "FRIENDLY";
    }
    return "?";
}

}  // namespace

// ================================================================ setup

Scene::Scene(Sim& sim) : sim_(sim) {}

bool Scene::takeSaveRequest() {
    bool r = saveRequested_;
    saveRequested_ = false;
    return r;
}

// ================================================================ input

void Scene::touch(const TouchEvent& e) {
    gestures_.touch(e);
    Gesture g;
    while (gestures_.poll(g)) handle(g);
}

void Scene::handle(const Gesture& g) {
    if (overlay_ != Overlay::None) {
        if (g.type == GestureType::Tap) overlay_ = Overlay::None;  // a tap anywhere closes it
        return;
    }
    switch (g.type) {
        case GestureType::Tap: onTap(g.x, g.y); break;
        case GestureType::LongPress:
            if (g.y < kStripTouchH && !footprintArea().contains(g.x, g.y)) overlay_ = Overlay::Stats;
            break;
        case GestureType::TwoFingerTap:
            if (g.y >= kStripTouchH && !(trayOpen() && g.y >= trayTop() - 44)) doScold();
            break;
        case GestureType::DragStart: onDragStart(g); break;
        case GestureType::DragMove: onDragMove(g); break;
        case GestureType::DragEnd: onDragEnd(g); break;
    }
}

void Scene::onTap(int x, int y) {
    if (footprintArea().contains(x, y)) {
        if (sim_.state().calling) doAnswerCall();
        else overlay_ = Overlay::Stats;
        return;
    }
    if (y < kStripTouchH) {
        overlay_ = Overlay::Stats;
        return;
    }
    if (handleArea().contains(x, y)) {
        trayTarget_ = trayOpen() ? 0.0f : 1.0f;
        if (!trayOpen()) {
            held_ = Item::None;
            foodPage_ = false;
        }
        return;
    }
    if (trayOpen() && y >= trayTop()) {
        for (int i = 0; i < slotCount(); i++)
            if (slotArea(i).contains(x, y)) pressSlot(slotAt(i));
        return;
    }
    if (held_ != Item::None) {
        useItem(held_, x, y);  // tapping empty ground does nothing; the item stays in hand
        return;
    }
    for (int i = 0; i < visibleMesses(); i++) {
        if (messArea(i).contains(x, y)) {
            doClean(kMessSpots[i][0], kMessSpots[i][1]);
            return;
        }
    }
    if (squatchArea().contains(x, y)) tapPet();
}

void Scene::pressSlot(Slot s) {
    switch (s) {
        case Slot::Feed:
            foodPage_ = true;
            held_ = Item::None;
            break;
        case Slot::Back:
            foodPage_ = false;
            held_ = Item::None;
            break;
        case Slot::Light:
            sim_.lights(!sim_.state().lightsOn);
            say(sim_.state().lightsOn ? "LIGHTS ON" : "LIGHTS OFF", true);
            saveRequested_ = true;
            break;
        case Slot::Photos: overlay_ = Overlay::Gallery; break;
        default: {
            Item it = itemFor(s);
            held_ = (held_ == it) ? Item::None : it;  // tap again to put it back
            break;
        }
    }
}

void Scene::onDragStart(const Gesture& g) {
    float dx = static_cast<float>(g.x - g.startX), dy = static_cast<float>(g.y - g.startY);
    bool vertical = std::fabs(dy) > std::fabs(dx);
    dragX_ = g.x;
    dragY_ = g.y;
    strokeLen_ = 0;
    strokeDone_ = swipeCleaned_ = false;
    drag_ = DragMode::None;

    // Swipe up from the bottom edge opens the tray; swipe down on it closes it.
    if (!trayOpen() && g.startY >= S - 64 && dy < 0 && vertical) {
        trayTarget_ = 1;
        return;
    }
    if (trayOpen() && g.startY >= trayTop() - 44 && dy > 0 && vertical) {
        trayTarget_ = 0;
        held_ = Item::None;
        foodPage_ = false;
        return;
    }
    if (trayOpen() && g.startY >= trayTop()) {
        // Dragging straight out of a slot picks the item up on the way.
        for (int i = 0; i < slotCount(); i++) {
            Item it = itemFor(slotAt(i));
            if (it != Item::None && slotArea(i).contains(g.startX, g.startY)) {
                held_ = it;
                drag_ = DragMode::Item;
            }
        }
        return;
    }
    if (g.startY < kStripTouchH) return;
    if (held_ != Item::None) {
        drag_ = DragMode::Item;
        return;
    }
    drag_ = DragMode::Stroke;
    onDragMove(g);
}

void Scene::onDragMove(const Gesture& g) {
    dragX_ = g.x;
    dragY_ = g.y;
    if (drag_ != DragMode::Stroke) return;

    float mx = (g.x + g.prevX) * 0.5f, my = (g.y + g.prevY) * 0.5f;
    float seg = std::hypot(static_cast<float>(g.x - g.prevX), static_cast<float>(g.y - g.prevY));

    // A stroke across the pet is petting.
    if (!strokeDone_ && squatchArea().contains(static_cast<int>(mx), static_cast<int>(my)) &&
        g.speed < kMaxStrokeSpeed) {
        strokeLen_ += seg;
        if (strokeLen_ >= kStrokeShare * static_cast<float>(squatchArea().w)) {
            strokeDone_ = true;
            doPet();
        }
    }
    // A swipe across a mess cleans it.
    if (!swipeCleaned_) {
        for (int i = 0; i < visibleMesses(); i++) {
            if (segmentDistance(kMessSpots[i][0], kMessSpots[i][1], g.prevX, g.prevY, g.x, g.y) < 32) {
                swipeCleaned_ = true;
                doClean(kMessSpots[i][0], kMessSpots[i][1]);
                break;
            }
        }
    }
}

void Scene::onDragEnd(const Gesture& g) {
    dragX_ = g.x;
    dragY_ = g.y;
    if (drag_ == DragMode::Item) useItem(held_, g.x, g.y);  // a miss just goes back to the tray
    drag_ = DragMode::None;
}

bool Scene::useItem(Item it, int x, int y) {
    if (sim_.state().stage == Stage::Away) {
        if (isFood(it) && stumpArea().contains(x, y)) {
            doLeaveFood();
            return true;
        }
        return false;
    }
    if (it == Item::Brush) {
        for (int i = 0; i < visibleMesses(); i++) {
            if (messArea(i).contains(x, y)) {
                doClean(kMessSpots[i][0], kMessSpots[i][1]);
                return true;
            }
        }
    }
    if (!squatchArea().contains(x, y)) return false;
    switch (it) {
        case Item::Berries: doFeed(Food::Berries); break;
        case Item::Fish: doFeed(Food::Fish); break;
        case Item::Roots: doFeed(Food::Roots); break;
        case Item::Brush: doBrush(); break;
        case Item::Poultice: doMedicine(); break;
        case Item::Pinecone: doPlay(); break;
        case Item::None: return false;
    }
    return true;
}

// ================================================================ actions

void Scene::saved(Result r) {
    if (r == Result::Ok) saveRequested_ = true;
}

void Scene::sayNotNow() {
    const State& s = sim_.state();
    if (s.stage == Stage::Egg) say("IT HASN'T HATCHED YET", true);
    else if (s.stage == Stage::Away) say("IT'S NOT HERE", true);
    else if (s.asleep) say("SHH, IT'S ASLEEP", true);
}

void Scene::doFeed(Food f) {
    bool misbehaving = sim_.state().misbehaving;
    Result r = sim_.feed(f);
    saved(r);
    if (r == Result::Ok) {
        react(React::Eat, 1400);
        for (int i = 0; i < 5; i++)
            spawn(ParticleKind::Crumb, squatchX() + (rand01() - 0.5f) * 20, headY() + 30 * squatchScale(),
                  (rand01() - 0.5f) * 80, -40 - rand01() * 60, 900);
        if (f == Food::Berries) spawn(ParticleKind::Heart, squatchX() + 30, headY() - 20, 8, -40, 1200);
    } else if (r == Result::Refused) {
        react(React::Shake, 900);
        say(misbehaving ? "IT THREW IT BACK" : "IT'S FULL", true);
    } else {
        sayNotNow();
    }
}

void Scene::doClean(float x, float y) {
    Result r = sim_.clean();
    saved(r);
    if (r == Result::Ok) {
        for (int i = 0; i < 8; i++)
            spawn(ParticleKind::Sparkle, x + (rand01() - 0.5f) * 50, y - rand01() * 30, 0, -30, 700);
        if (hatched() && !hiding()) react(React::Happy, 700);
    } else {
        sayNotNow();
    }
}

void Scene::doBrush() {
    Result r = sim_.brush();
    saved(r);
    if (r == Result::Ok) {
        react(React::Happy, 1000);
        for (int i = 0; i < 6; i++)
            spawn(ParticleKind::Sparkle, squatchX() + (rand01() - 0.5f) * 90 * squatchScale(),
                  squatchFoot() - rand01() * 150 * squatchScale(), 0, -30, 800);
    } else {
        sayNotNow();
    }
}

void Scene::doMedicine() {
    Result r = sim_.medicine();
    saved(r);  // the Recovered event does the celebrating
    if (r == Result::NotNow) {
        if (hatched() && !sim_.state().sick) say("IT ISN'T SICK", true);
        else sayNotNow();
    }
}

void Scene::doPlay() {
    // Placeholder until the mini-games (build step 5): a game of catch that always counts as won.
    Result r = sim_.finishedGame(true, false);
    saved(r);
    if (r == Result::Ok) {
        react(React::Happy, 1500);
        spawn(ParticleKind::Heart, squatchX() - 24, headY() - 30, -6, -40, 1200);
        spawn(ParticleKind::Heart, squatchX() + 24, headY() - 40, 6, -44, 1200);
        say("YOU PLAYED CATCH", true);
    } else {
        sayNotNow();
    }
}

void Scene::doPet() {
    Result r = sim_.pet();
    saved(r);
    if (r == Result::Ok) {
        react(React::Happy, 1200);
        for (int i = 0; i < 3; i++)
            spawn(ParticleKind::Heart, squatchX() + (rand01() - 0.5f) * 60, headY() - 20, (rand01() - 0.5f) * 30,
                  -50, 1300);
    } else if (r == Result::Refused) {
        react(sim_.trustLevel() == TrustLevel::Hiding ? React::Flinch : React::Peek, 1000);  // not ready yet
    } else if (sim_.state().stage == Stage::Egg) {
        react(React::Wobble, 700);
    } else {
        sayNotNow();
    }
}

void Scene::doScold() {
    if (!hatched()) return;
    bool wasMisbehaving = sim_.state().misbehaving;
    Result r = sim_.scold();
    saved(r);
    if (r == Result::NotNow) {
        sayNotNow();
    } else if (wasMisbehaving) {
        say("IT STOPPED", true);
        react(React::Sulk, 1500);
    } else {
        say("IT DIDN'T DO ANYTHING", true);
        react(React::Flinch, 1200);
    }
}

void Scene::doAnswerCall() {
    uint32_t before = sim_.state().footprints;
    Result r = sim_.answerCall();
    saved(r);
    if (r != Result::Ok) return;
    say("YOU ANSWERED ITS CALL", true);
    if (sim_.state().footprints > before) spawn(ParticleKind::PlusOne, 430, 46, 0, -30, 1200);
    if (!hiding()) react(React::Wave, 1500);
}

void Scene::doLeaveFood() {
    Result r = sim_.leaveFoodOut();
    saved(r);
    if (r == Result::NotNow) {
        say("ALREADY LEFT FOOD TODAY", true);
    } else if (sim_.state().stage == Stage::Away) {  // otherwise the Returned event speaks
        char buf[40];
        std::snprintf(buf, sizeof buf, "FOOD LEFT OUT (%d OF %d DAYS)", sim_.state().coaxDays,
                      sim_.tuning().coaxDaysNeeded);
        say(buf, true);
    }
}

void Scene::tapPet() {
    const State& s = sim_.state();
    if (s.stage == Stage::Egg) {
        react(React::Wobble, 700);
        return;
    }
    if (s.calling) {
        doAnswerCall();
        return;
    }
    if (s.asleep) {
        say("SHH, IT'S ASLEEP", true);
        return;
    }
    switch (sim_.trustLevel()) {
        case TrustLevel::Hiding: react(React::Flinch, 1500); break;
        case TrustLevel::Wandering: react(React::Peek, 1000); break;
        case TrustLevel::Friendly: react(React::Wave, 1600); break;
    }
}

// ================================================================ animation

void Scene::update(uint32_t nowMs) {
    uint32_t dtMs = started_ ? nowMs - now_ : 0;
    started_ = true;
    now_ = nowMs;
    float dt = static_cast<float>(std::min<uint32_t>(dtMs, 100)) / 1000.0f;
    t_ += dt;

    gestures_.update(nowMs);
    Gesture g;
    while (gestures_.poll(g)) handle(g);

    Event ev[16];
    for (size_t n; (n = sim_.drainEvents(ev, 16)) > 0;)
        for (size_t i = 0; i < n; i++) handleEvent(ev[i]);

    animate(dt);
}

void Scene::handleEvent(const Event& e) {
    char buf[40];
    switch (e.type) {
        case EventType::Hatched:
            say("IT HATCHED!");
            for (int i = 0; i < 10; i++)
                spawn(ParticleKind::Shell, kEggX, kEggY, (rand01() - 0.5f) * 260, -150 - rand01() * 150, 1200);
            react(React::Happy, 1200);
            break;
        case EventType::StageChanged: {
            Stage st = static_cast<Stage>(e.a);
            if (st == Stage::Child || st == Stage::Teen || st == Stage::Elder) {
                std::snprintf(buf, sizeof buf, "NOW A %s", toString(st));
                say(buf);
            }
            break;
        }
        case EventType::BecameAdult:
            std::snprintf(buf, sizeof buf, "GREW UP: %s", toString(static_cast<AdultForm>(e.a)));
            say(buf);
            break;
        case EventType::CallStarted: nextKnock_ = now_; break;
        case EventType::GotSick: say("IT'S SICK: GIVE IT A POULTICE"); break;
        case EventType::Recovered:
            say("FEELING BETTER");
            react(React::Happy, 1200);
            break;
        case EventType::Misbehaving:
            say("IT'S MISBEHAVING!");
            nextThrow_ = now_;
            break;
        case EventType::RanAway: say("IT RAN OFF INTO THE WOODS"); break;
        case EventType::Returned:
            say("IT CAME BACK!");
            react(React::Wave, 2000);
            break;
        case EventType::PhotoEarned:
            flashUntil_ = now_ + 400;
            say("NEW SIGHTING PHOTO!");
            break;
        case EventType::LaidEgg: say("A NEW EGG!"); break;
        default: break;
    }
}

void Scene::react(React r, uint32_t ms) {
    react_ = r;
    reactUntil_ = now_ + ms;
}

void Scene::say(const char* text, bool now) {
    if (now) toastCount_ = 0;  // the answer to what you just did shouldn't wait in line
    if (toastCount_ == kMaxToasts) {  // drop the oldest
        std::memmove(&toasts_[0], &toasts_[1], sizeof(Toast) * (kMaxToasts - 1));
        toastCount_--;
        toastUntil_ = now_ + kToastMs;
    }
    std::snprintf(toasts_[toastCount_].text, sizeof(Toast::text), "%s", text);
    if (toastCount_++ == 0) toastUntil_ = now_ + kToastMs;
}

const char* Scene::toast() const { return toastCount_ ? toasts_[0].text : nullptr; }

void Scene::spawn(ParticleKind k, float x, float y, float vx, float vy, uint32_t life) {
    if (particleCount_ == kMaxParticles) return;
    particles_[particleCount_++] = Particle{k, x, y, vx, vy, now_, life};
}

float Scene::rand01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ >> 8) / static_cast<float>(1u << 24);
}

void Scene::animate(float dt) {
    const State& s = sim_.state();

    trayAnim_ += (trayTarget_ > trayAnim_ ? 1 : -1) * std::min(std::fabs(trayTarget_ - trayAnim_), dt * 6);

    if (toastCount_ && now_ >= toastUntil_) {
        std::memmove(&toasts_[0], &toasts_[1], sizeof(Toast) * (kMaxToasts - 1));
        if (--toastCount_) toastUntil_ = now_ + kToastMs;
    }

    if (react_ != React::None && now_ >= reactUntil_) react_ = React::None;

    if (now_ >= nextBlink_) {
        blinkUntil_ = now_ + 140;
        nextBlink_ = now_ + 2500 + static_cast<uint32_t>(rand01() * 3000);
    }

    // Spontaneous life: glances, the odd wave at high Trust, an egg that rocks.
    if (now_ >= nextIdle_) {
        nextIdle_ = now_ + 3000 + static_cast<uint32_t>(rand01() * 5000);
        lookTarget_ = rand01() * 2 - 1;
        if (react_ == React::None) {
            if (s.stage == Stage::Egg) react(React::Wobble, 700);
            else if (hatched() && !s.asleep && sim_.trustLevel() == TrustLevel::Friendly && rand01() < 0.3f)
                react(React::Wave, 1600);
        }
    }
    lookX_ += (lookTarget_ - lookX_) * std::min(1.0f, dt * 4);

    bool wandering = hatched() && !s.asleep && sim_.trustLevel() == TrustLevel::Wandering;
    if (wandering) {
        if (now_ >= nextWander_) {
            wanderTarget_ = 150 + rand01() * 180;
            nextWander_ = now_ + 4000 + static_cast<uint32_t>(rand01() * 4000);
        }
        float step = std::min(std::fabs(wanderTarget_ - wanderX_), 36 * dt);
        wanderX_ += wanderTarget_ > wanderX_ ? step : -step;
    }

    if (hatched()) {
        float k = squatchScale();
        if (s.asleep && now_ >= nextZ_) {
            float x = hiding() ? kHideX + 10 : squatchX() + 24 * k;
            float y = hiding() ? kHideY - 24 : headY() - 30 * k;
            spawn(ParticleKind::Zzz, x, y, 12, -20, 2400);
            nextZ_ = now_ + 1500;
        }
        if (s.misbehaving && !s.asleep && now_ >= nextThrow_) {
            float dir = rand01() < 0.5f ? -1.0f : 1.0f;
            float x = hiding() ? kHideX : squatchX() + dir * 50 * k;
            float y = hiding() ? kHideY : squatchFoot() - 110 * k;
            spawn(ParticleKind::Pinecone, x, y, dir * (80 + rand01() * 80), -260, 1400);
            nextThrow_ = now_ + 1800;
        }
        if (s.sick && !s.asleep && now_ >= nextSneeze_) {
            if (!hiding()) {
                react(React::Sneeze, 500);
                for (int i = 0; i < 4; i++)
                    spawn(ParticleKind::Drop, squatchX(), headY() + 14 * k, (rand01() - 0.5f) * 120,
                          -20 - rand01() * 60, 700);
            }
            nextSneeze_ = now_ + 4000 + static_cast<uint32_t>(rand01() * 3000);
        }
    }
    if (s.calling && now_ >= nextKnock_) {  // wood knocks from the trees
        spawn(ParticleKind::Ring, kRightTreeX, kRightTreeBase - 70, 0, 0, 900);
        nextKnock_ = now_ + 1400;
    }

    size_t keep = 0;
    for (size_t i = 0; i < particleCount_; i++) {
        Particle p = particles_[i];
        if (now_ - p.born >= p.life) continue;
        bool falls = p.kind == ParticleKind::Pinecone || p.kind == ParticleKind::Crumb ||
                     p.kind == ParticleKind::Shell || p.kind == ParticleKind::Drop;
        if (falls) p.vy += 600 * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        particles_[keep++] = p;
    }
    particleCount_ = keep;
}

// ================================================================ where things are

bool Scene::hatched() const {
    Stage st = sim_.state().stage;
    return st != Stage::Egg && st != Stage::Away;
}

bool Scene::hiding() const { return hatched() && sim_.trustLevel() == TrustLevel::Hiding; }

float Scene::squatchScale() const {
    float k = 1;
    switch (sim_.state().stage) {
        case Stage::Baby: k = 0.55f; break;
        case Stage::Child: k = 0.7f; break;
        case Stage::Teen: k = 0.85f; break;
        case Stage::Elder: k = 0.95f; break;
        default: break;
    }
    return sim_.trustLevel() == TrustLevel::Wandering ? k * 0.82f : k;  // further back
}

float Scene::squatchX() const {
    switch (sim_.trustLevel()) {
        case TrustLevel::Hiding: return kHideX;
        case TrustLevel::Wandering: return wanderX_;
        case TrustLevel::Friendly: break;
    }
    return 240 + static_cast<float>(std::sin(t_ * 0.7)) * 6;
}

float Scene::squatchFoot() const {
    return sim_.trustLevel() == TrustLevel::Wandering ? kWanderFoot : kFriendlyFoot;
}

float Scene::headY() const {
    float k = squatchScale(), hr = headRatio(sim_.state().stage);
    return squatchFoot() - 128 * k - (hr - 1) * 30 * k;
}

float Scene::trayTop() const { return S - kTrayH * smooth(trayAnim_); }

float Scene::hourOfDay() const {
    int64_t local = sim_.state().now + sim_.state().tzOffset;
    int64_t sec = ((local % 86400) + 86400) % 86400;
    return static_cast<float>(sec) / 3600.0f;
}

float Scene::darkness() const {
    const float keys[][2] = {{0, 1}, {5, 1}, {7, 0}, {18, 0}, {20.5f, 1}, {24, 1}};
    float h = hourOfDay();
    for (int i = 0; i < 5; i++)
        if (h <= keys[i + 1][0])
            return keys[i][1] + (keys[i + 1][1] - keys[i][1]) * (h - keys[i][0]) / (keys[i + 1][0] - keys[i][0]);
    return 1;
}

int Scene::visibleMesses() const { return hatched() ? std::min<int>(sim_.state().messes, 3) : 0; }

bool Scene::foodLeftToday() const {
    const State& s = sim_.state();
    return s.stage == Stage::Away && s.lastCoaxDay == (s.now + s.tzOffset) / 86400;
}

Scene::Rect Scene::squatchArea() const {
    Stage st = sim_.state().stage;
    if (st == Stage::Away) return {0, 0, 0, 0};
    if (st == Stage::Egg) return {184, 222, 112, 128};
    if (hiding()) return {static_cast<int>(kHideX) - 40, static_cast<int>(kHideY) - 40, 80, 80};
    float k = squatchScale(), hr = headRatio(st);
    int h = std::max(72, static_cast<int>((190 + (hr - 1) * 40) * k));
    int w = std::max(72, static_cast<int>(132 * k));
    return {static_cast<int>(squatchX()) - w / 2, static_cast<int>(squatchFoot()) - h, w, h};
}

Scene::Rect Scene::stumpArea() const { return {static_cast<int>(kStumpX) - 36, static_cast<int>(kStumpY) - 44, 72, 72}; }

Scene::Rect Scene::messArea(int i) const {
    return {static_cast<int>(kMessSpots[i][0]) - 34, static_cast<int>(kMessSpots[i][1]) - 36, 68, 64};
}

Scene::Rect Scene::handleArea() const {
    return {160, std::min(static_cast<int>(trayTop()) - 44, S - 64), 160, 64};
}

Scene::Rect Scene::footprintArea() const { return {S - 96, 0, 96, kStripTouchH}; }

int Scene::slotCount() const { return foodPage_ ? 4 : 6; }

Scene::Rect Scene::slotArea(int i) const {
    int w = S / slotCount();
    return {i * w, static_cast<int>(trayTop()) + 20, w, kTrayH - 20};
}

Scene::Slot Scene::slotAt(int i) const { return foodPage_ ? kFoodSlots[i] : kMainSlots[i]; }

// ================================================================ drawing

Rgb Scene::sh(Rgb c) const { return mix(c, kNight, dark_ * 0.6f); }

void Scene::draw(Canvas& c) {
    const State& s = sim_.state();
    dark_ = darkness();

    drawSky(c);
    drawGround(c);
    drawCampfire(c, false);
    drawStump(c);
    for (int i = 0; i < visibleMesses(); i++) {
        float x = kMessSpots[i][0], y = kMessSpots[i][1];
        c.fillEllipse(x, y, 17, 8, sh(0x4E3219));
        c.fillCircle(x - 3, y - 7, 8, sh(0x5C3B1E));
        c.fillCircle(x + 2, y - 14, 5.5f, sh(0x6A4524));
        if (s.now - s.oldestMessAt >= sim_.tuning().messOldAfter) {  // getting old: stink lines
            for (int k = -1; k <= 1; k++) {
                float wob = static_cast<float>(std::sin(t_ * 3 + k)) * 3;
                c.line(x + k * 9 + wob, y - 22, x + k * 9 - wob, y - 36, 2.5f, sh(0x8BAA5C), 0.8f);
            }
        }
    }
    if (s.stage == Stage::Away) drawAwayTrail(c);
    if (hiding()) drawHidingHead(c);
    drawTree(c, kLeftTreeX, kLeftTreeBase, 1.0f);
    drawTree(c, kRightTreeX, kRightTreeBase, 0.95f);
    drawLantern(c);
    if (s.stage == Stage::Egg) drawEgg(c);
    else if (hiding()) drawHidingEyes(c);
    else if (hatched()) drawPet(c);
    float h = hourOfDay();
    drawCampfire(c, h >= 17 && h < 23);
    drawParticles(c);

    if (!s.lightsOn) c.tint(0x02040C, 0.5f);
    if (now_ < flashUntil_) c.tint(0xFFFFFF, static_cast<float>(flashUntil_ - now_) / 400.0f * 0.8f);

    drawStrip(c);
    drawToast(c);
    drawTray(c);
    if (drag_ == DragMode::Item && held_ != Item::None) {
        c.fillEllipse(dragX_, dragY_ - 8, 20, 6, 0x000000, 0.2f);
        drawItem(c, held_, dragX_, dragY_ - 40, 1.3f);  // above the finger, not under it
    }
    if (overlay_ == Overlay::Stats) drawStats(c);
    if (overlay_ == Overlay::Gallery) drawGallery(c);
}

void Scene::drawSky(Canvas& c) {
    struct Key {
        float h;
        Rgb top, bottom;
    };
    const Key keys[] = {
        {0, 0x0B1026, 0x1C2850},  {5, 0x0B1026, 0x1C2850},  {6.5f, 0x3D4F86, 0xF2A47A}, {8, 0x6FB3E8, 0xD6ECF7},
        {17, 0x6FB3E8, 0xD6ECF7}, {19, 0x4B3B73, 0xF08A5D}, {20.5f, 0x0B1026, 0x1C2850}, {24, 0x0B1026, 0x1C2850},
    };
    float h = hourOfDay();
    int i = 0;
    while (i < 6 && h > keys[i + 1].h) i++;
    float t = (h - keys[i].h) / (keys[i + 1].h - keys[i].h);
    c.verticalGradient(0, static_cast<int>(kHorizon) + 20, mix(keys[i].top, keys[i + 1].top, t),
                       mix(keys[i].bottom, keys[i + 1].bottom, t));

    if (dark_ > 0.3f) {
        float a = (dark_ - 0.3f) / 0.7f;
        for (int k = 0; k < 40; k++) {
            float x = static_cast<float>((k * 97 + 13) % S), y = static_cast<float>((k * 53 + 7) % 190);
            float tw = 0.5f + 0.5f * static_cast<float>(std::sin(t_ * 1.7 + k * 1.3));
            c.fillCircle(x, y, k % 5 == 0 ? 1.6f : 1.1f, 0xFFF8E0, a * (0.4f + 0.6f * tw));
        }
        c.fillCircle(330, 70, 18, 0xF4F1DE, a);
        c.fillCircle(338, 64, 15, mix(keys[i].top, keys[i + 1].top, t), a);  // crescent
    } else {
        c.fillCircle(150, 76, 22, 0xFFE9A8, 1 - dark_ / 0.3f);
    }
}

void Scene::drawGround(Canvas& c) {
    c.fillEllipse(110, kHorizon + 14, 210, 62, sh(0x5B7F8F));
    c.fillEllipse(390, kHorizon + 18, 230, 72, sh(0x4F7584));
    for (int i = 0; i < 16; i++) {  // distant tree line
        float x = static_cast<float>(i * 32 + (i % 3) * 7 - 10);
        float hgt = 46 + static_cast<float>((i * 37) % 28);
        c.fillTriangle(x - 18, kHorizon + 12, x + 18, kHorizon + 12, x, kHorizon + 12 - hgt, sh(kBackTrees));
    }
    c.fillRect(0, kHorizon, S, S - kHorizon, sh(kGrass));
    c.fillEllipse(240, 372, 252, 122, sh(kClearing));
    for (int i = 0; i < 18; i++) {  // grass tufts
        float x = static_cast<float>((i * 131 + 40) % S), y = kHorizon + 30 + static_cast<float>((i * 71) % 240);
        c.line(x - 4, y, x - 6, y - 9, 2, sh(0x4C8A3A));
        c.line(x, y, x, y - 11, 2, sh(0x4C8A3A));
        c.line(x + 4, y, x + 6, y - 9, 2, sh(0x4C8A3A));
    }
}

void Scene::drawTree(Canvas& c, float x, float base, float size) {
    c.fillRoundRect(x - 14 * size, base - 150 * size, 28 * size, 150 * size, 6, sh(kTrunk));
    c.fillEllipse(x, base, 30 * size, 7 * size, sh(0x3F5E2E));  // roots in shadow
    for (int i = 0; i < 4; i++) {
        float y = base - 90 * size - i * 45 * size, hw = (78 - i * 14) * size;
        c.fillTriangle(x - hw, y, x + hw, y, x, y - 85 * size, sh(i % 2 ? kLeaves2 : kLeaves1));
    }
}

void Scene::drawStump(Canvas& c) {
    float x = kStumpX, y = kStumpY;
    c.fillRoundRect(x - 30, y - 26, 60, 30, 8, sh(kTrunk));
    c.fillEllipse(x, y - 26, 30, 9, sh(0xC49A6C));
    c.ring(x, y - 26, 14, 2, sh(0xA67C52));
    if (foodLeftToday()) {
        c.fillEllipse(x, y - 30, 20, 8, sh(0x9C6B3A));
        c.fillCircle(x - 7, y - 36, 5, sh(0x8E2A4F));
        c.fillCircle(x + 3, y - 37, 5, sh(0x8E2A4F));
        c.fillCircle(x + 9, y - 33, 4, sh(0x8E2A4F));
    }
}

void Scene::drawCampfire(Canvas& c, bool flames) {
    float x = kFireX, y = kFireY;
    if (!flames) {
        for (int i = 0; i < 7; i++) {
            float a = i * 2 * kPi / 7;
            c.fillEllipse(x + std::cos(a) * 26, y + std::sin(a) * 9, 8, 5, sh(0x8C8C86));
        }
        c.line(x - 16, y + 2, x + 14, y - 4, 7, sh(0x5A3A22));
        c.line(x - 14, y - 4, x + 16, y + 2, 7, sh(0x6B4A2F));
        return;
    }
    float fl = static_cast<float>(std::sin(t_ * 13) * 0.5 + std::sin(t_ * 7.3) * 0.5);
    c.glow(x, y - 20, 90, 0xFFB347, 0.15f + 0.35f * dark_);
    c.fillTriangle(x - 14, y, x + 14, y, x + fl * 3, y - 38 - fl * 6, 0xF07B2B);
    c.fillTriangle(x - 8, y, x + 9, y, x - fl * 2, y - 26 + fl * 4, 0xFFD25E);
}

void Scene::drawLantern(Canvas& c) {
    float x = 402, y = 238;
    bool lit = sim_.state().lightsOn;
    c.line(kRightTreeX - 14, y - 16, x, y - 16, 4, sh(kTrunk));  // a branch stub
    c.line(x, y - 16, x, y - 6, 2, sh(0x2A2A2A));
    if (lit) c.glow(x, y + 8, 50, 0xFFC857, 0.5f * dark_);
    c.fillRoundRect(x - 9, y - 6, 18, 26, 4, sh(0x3B2A1E));
    c.fillRoundRect(x - 6, y - 2, 12, 18, 3, lit ? 0xFFD36B : sh(0x6B6B5A));
}

void Scene::drawEgg(Canvas& c) {
    float progress = static_cast<float>(sim_.ageSecs()) / static_cast<float>(sim_.tuning().eggEnd);
    float wob = 0;
    if (react_ == React::Wobble && now_ < reactUntil_)
        wob = static_cast<float>(std::sin(t_ * 24)) * 5 * static_cast<float>(reactUntil_ - now_) / 700.0f;
    float x = kEggX + wob, y = kEggY;
    c.fillEllipse(kEggX, y + 52, 46, 8, 0x000000, 0.18f);
    c.fillEllipse(x, y, 40, 52, sh(0xE8E0C8));
    c.fillEllipse(x + 6, y + 14, 30, 34, sh(0xDDD3B8));
    c.fillEllipse(x - 13, y - 22, 9, 14, 0xFFFFFF, 0.35f);
    c.fillEllipse(x - 16, y + 22, 12, 7, sh(0x6E9A4E));
    c.fillEllipse(x + 15, y - 6, 9, 6, sh(0x7FAE5A));
    c.fillEllipse(x + 4, y + 38, 11, 5, sh(0x6E9A4E));
    c.fillCircle(x - 8, y - 4, 2.5f, sh(0x9C8F70));
    c.fillCircle(x + 18, y + 16, 2, sh(0x9C8F70));
    c.fillCircle(x + 2, y - 34, 2, sh(0x9C8F70));
    if (progress > 0.75f) {  // nearly time
        const float crack[][2] = {{-22, -14}, {-11, -24}, {-3, -13}, {7, -27}, {17, -16}, {24, -22}};
        for (int i = 0; i < 5; i++)
            c.line(x + crack[i][0], y + crack[i][1], x + crack[i + 1][0], y + crack[i + 1][1], 2.2f, sh(0x5A4A32));
    }
}

struct Scene::Look {
    float x, foot, ground, s, head;
    Rgb fur, face;
    float blink, lookX, armL, armR;
    float mouth;  // -1 frown, 0 flat, 1 smile, 2 chewing
    float breath, wide;
    bool angry;
};

void Scene::drawPet(Canvas& c) {
    const State& s = sim_.state();
    Look L{};
    L.x = squatchX();
    L.foot = L.ground = squatchFoot();
    L.s = squatchScale();
    L.head = headRatio(s.stage);
    L.wide = 1;

    Rgb fur = kFur;
    switch (s.adult) {
        case AdultForm::Guardian: fur = 0x7B4F33; break;
        case AdultForm::RidgeRunner: fur = 0x9B5E34; break;
        case AdultForm::CouchSquatch:
            fur = 0xA0724E;
            L.wide = 1.15f;
            break;
        case AdultForm::Hermit: fur = 0x5E4A3A; break;
        case AdultForm::Legend: fur = 0xD8CFC4; break;
        case AdultForm::None: break;
    }
    if (s.stage == Stage::Elder) fur = mix(fur, 0xA89F96, 0.6f);
    Rgb face = kFace;
    if (s.sick) {
        fur = mix(fur, 0x9DB58A, 0.35f);
        face = mix(face, 0xC8D6B0, 0.45f);
    }
    // Shaded for the night, but less than the scenery, so it stays easy to see.
    L.fur = mix(fur, kNight, dark_ * 0.35f);
    L.face = mix(face, kNight, dark_ * 0.35f);

    float t = static_cast<float>(t_);
    L.blink = (s.asleep || now_ < blinkUntil_) ? 1.0f : 0.0f;
    L.lookX = lookX_;
    L.breath = 1 + 0.018f * std::sin(t * (s.asleep ? 1.6f : 2.6f));
    L.mouth = s.happiness >= 2 ? 1.0f : (s.happiness < 1 ? -1.0f : 0.0f);
    L.angry = s.misbehaving;

    if (sim_.trustLevel() == TrustLevel::Wandering && !s.asleep && std::fabs(wanderTarget_ - wanderX_) > 1) {
        L.foot -= std::fabs(std::sin(t * 8)) * 4 * L.s;  // walking bob
        L.lookX = wanderTarget_ > wanderX_ ? 0.8f : -0.8f;
    }
    switch (react_) {
        case React::Wave:
            L.armR = 0.8f + 0.2f * std::sin(t * 12);
            L.mouth = 1;
            break;
        case React::Happy:
            L.foot -= std::fabs(std::sin(t * 9)) * 10 * L.s;
            L.armL = L.armR = 0.5f;
            L.mouth = 1;
            break;
        case React::Eat: L.mouth = 2; break;
        case React::Shake:
            L.lookX = std::sin(t * 20);
            L.mouth = -1;
            break;
        case React::Sneeze:
            L.blink = 1;
            L.mouth = 2;
            break;
        case React::Flinch:
            L.s *= 0.94f;
            L.blink = 0.6f;
            L.armL = L.armR = 0.35f;
            break;
        case React::Peek:
            L.foot -= 6;
            L.x += L.x < 240 ? -10.0f : 10.0f;
            break;
        case React::Sulk:
            L.mouth = -1;
            L.lookX = -0.9f;
            L.angry = false;
            break;
        default: break;
    }
    drawSquatch(c, L);
}

void Scene::drawSquatch(Canvas& c, const Look& L) {
    const float s = L.s, x = L.x, f = L.foot, w = L.wide, b = L.breath;
    const Rgb dark = mix(L.fur, 0x1E140C, 0.45f);

    c.fillEllipse(x, L.ground, 60 * s * w, 10 * s, 0x000000, 0.18f);
    c.fillEllipse(x - 26 * s * w, f - 9 * s, 24 * s, 11 * s, dark);
    c.fillEllipse(x + 26 * s * w, f - 9 * s, 24 * s, 11 * s, dark);

    c.fillEllipse(x, f - 66 * s * b, 56 * s * w, 62 * s * b, L.fur);
    c.fillEllipse(x, f - 56 * s, 34 * s * w, 40 * s * b, L.face);

    auto arm = [&](float side, float raise) {
        float sx = x + side * 44 * s * w, sy = f - 102 * s * b;
        float hx = x + side * (60 * s * w + raise * 16 * s);
        float hy = (f - 46 * s) + raise * (-122 * s);
        c.line(sx, sy, hx, hy, 22 * s, L.fur);
        c.fillCircle(hx, hy, 12 * s, dark);
    };
    arm(-1, L.armL);
    arm(1, L.armR);

    const float hr = 44 * s * L.head;
    const float hy = f - 128 * s * b - (L.head - 1) * 30 * s;
    const float fx = x + L.lookX * hr * 0.12f;
    c.fillEllipse(x, hy - hr * 0.88f, hr * 0.42f, hr * 0.3f, L.fur);  // crest
    for (float side : {-1.0f, 1.0f}) {
        c.fillCircle(x + side * hr * 0.9f, hy + hr * 0.05f, hr * 0.24f, L.fur);
        c.fillCircle(x + side * hr * 0.92f, hy + hr * 0.07f, hr * 0.12f, L.face);
    }
    c.fillCircle(x, hy, hr, L.fur);
    c.fillEllipse(fx, hy + hr * 0.2f, hr * 0.7f, hr * 0.62f, L.face);

    if (L.angry) {
        c.line(fx - hr * 0.52f, hy - hr * 0.24f, fx - hr * 0.1f, hy - hr * 0.06f, hr * 0.13f, dark);
        c.line(fx + hr * 0.52f, hy - hr * 0.24f, fx + hr * 0.1f, hy - hr * 0.06f, hr * 0.13f, dark);
    } else {
        c.line(fx - hr * 0.46f, hy - hr * 0.14f, fx + hr * 0.46f, hy - hr * 0.14f, hr * 0.14f, dark);
    }

    const float er = hr * 0.13f, ey = hy + hr * 0.07f;
    for (float side : {-1.0f, 1.0f}) {
        float ex = fx + side * hr * 0.28f + L.lookX * hr * 0.05f;
        if (L.blink > 0.8f) {
            c.line(ex - er, ey, ex + er, ey, std::max(1.5f, er * 0.45f), kEye);
        } else {
            c.fillEllipse(ex, ey, er, er * (1 - L.blink * 0.8f), kEye);
            c.fillCircle(ex + er * 0.35f, ey - er * 0.35f, er * 0.35f, 0xFFFFFF, 0.9f);
        }
    }

    c.fillEllipse(fx, hy + hr * 0.33f, hr * 0.15f, hr * 0.1f, dark);
    const float my = hy + hr * 0.53f, mw = hr * 0.2f;
    if (L.mouth > 1.5f) {
        float open = 0.5f + 0.5f * static_cast<float>(std::sin(t_ * 14));
        c.fillEllipse(fx, my, mw * 0.6f, hr * (0.05f + 0.07f * open), 0x3A1E14);
    } else {
        float curve = L.mouth * hr * 0.06f;
        c.line(fx - mw, my - curve, fx, my + curve, hr * 0.06f, 0x3A1E14);
        c.line(fx, my + curve, fx + mw, my - curve, hr * 0.06f, 0x3A1E14);
    }
}

void Scene::drawHidingHead(Canvas& c) {
    if (react_ == React::Flinch) return;  // ducked out of sight
    c.fillCircle(kHideX, kHideY, 24, sh(mix(kFur, 0x000000, 0.45f)));
    c.fillEllipse(kHideX, kHideY - 22, 10, 7, sh(mix(kFur, 0x000000, 0.45f)));
}

void Scene::drawHidingEyes(Canvas& c) {
    if (react_ == React::Flinch) return;
    const State& s = sim_.state();
    bool closed = s.asleep || now_ < blinkUntil_;
    for (float dx : {-4.0f, 10.0f}) {
        float ex = kHideX + dx, ey = kHideY - 4;
        if (closed) {
            c.line(ex - 4, ey, ex + 4, ey, 2, 0x1A120C);
        } else {
            c.fillEllipse(ex, ey, 4.8f, 4.8f, 0xF3EED2);  // eyes catch the light even at night
            c.fillCircle(ex + lookX_ * 1.6f, ey, 2.4f, kEye);
        }
    }
}

void Scene::drawAwayTrail(Canvas& c) {
    for (int i = 0; i < 6; i++) {
        float t = i / 5.0f;
        float x = 240 + (406 - 240) * t, y = 352 + (318 - 352) * t + (i % 2 ? -7.0f : 7.0f);
        drawFootprint(c, x, y, 0.9f - 0.25f * t, sh(0x4A3A2A), 0.75f);
    }
}

void Scene::drawParticles(Canvas& c) {
    for (size_t i = 0; i < particleCount_; i++) {
        const Particle& p = particles_[i];
        float age = static_cast<float>(now_ - p.born) / static_cast<float>(p.life);
        float a = 1 - age;
        switch (p.kind) {
            case ParticleKind::Heart: drawHeart(c, p.x, p.y, 8, 0xE76F8A, a); break;
            case ParticleKind::Sparkle:
                c.line(p.x - 6, p.y, p.x + 6, p.y, 2.5f, 0xFFF3B0, a);
                c.line(p.x, p.y - 6, p.x, p.y + 6, 2.5f, 0xFFF3B0, a);
                break;
            case ParticleKind::Crumb: c.fillCircle(p.x, p.y, 2.5f, sh(0x8A5A2B), a); break;
            case ParticleKind::Zzz: c.text(p.x, p.y, "Z", 2, 0xE8F0FF, Align::Center, a); break;
            case ParticleKind::Pinecone: drawItem(c, Item::Pinecone, p.x, p.y, 0.6f); break;
            case ParticleKind::Drop: c.fillCircle(p.x, p.y, 2.5f, 0xA8E0F0, a); break;
            case ParticleKind::Shell: c.fillEllipse(p.x, p.y, 7, 4, sh(0xE8E0C8), std::min(1.0f, a * 2)); break;
            case ParticleKind::PlusOne: c.text(p.x, p.y, "+1", 2, kGold, Align::Center, a); break;
            case ParticleKind::Ring: c.ring(p.x, p.y, 10 + age * 40, 4, 0xF3EBD9, 0.7f * a); break;
        }
    }
}

void Scene::drawStrip(Canvas& c) {
    const State& s = sim_.state();
    const bool live = hatched();
    const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(t_ * 6));
    c.fillRoundRect(6, 4, S - 12, 36, 12, kUiDark, 0.55f);

    // icon size grows a little while it needs attention
    auto k = [&](bool needs) { return live && needs ? 1 + 0.18f * pulse : 1.0f; };
    auto pips = [&](float x, float value) {
        for (int i = 0; i < 4; i++) {
            float fill = std::max(0.0f, std::min(1.0f, value - i));
            c.fillCircle(x + 20 + i * 9.5f, 22, 3.6f, 0x000000, 0.35f);
            if (fill > 0) c.fillCircle(x + 20 + i * 9.5f, 22, 3.6f * (0.4f + 0.6f * fill), kUiLight);
        }
    };
    const float a = live ? 1.0f : 0.35f;

    // hunger: berries
    float kk = k(s.hunger <= 1);
    c.fillCircle(24 - 5 * kk, 25, 6 * kk, 0xB0336A, a);
    c.fillCircle(24 + 5 * kk, 25, 6 * kk, 0xB0336A, a);
    c.fillCircle(24, 17, 6 * kk, 0xC2417A, a);
    pips(24, live ? s.hunger : 0);

    // happiness: heart
    kk = k(s.happiness <= 1);
    drawHeart(c, 100, 20, 9 * kk, 0xE76F8A, a);
    pips(100, live ? s.happiness : 0);

    // cleanliness: water drop
    bool oldMess = s.messes > 0 && s.now - s.oldestMessAt >= sim_.tuning().messOldAfter;
    kk = k(oldMess);
    c.fillCircle(176, 25, 7 * kk, 0x6EC6E8, a);
    c.fillTriangle(176 - 6.3f * kk, 23, 176 + 6.3f * kk, 23, 176, 23 - 14 * kk, 0x6EC6E8, a);
    pips(176, live ? std::max(0, 4 - static_cast<int>(s.messes)) : 0);

    // energy: sun while awake, moon while asleep (pulses if the light is still on)
    kk = k(s.asleep && s.lightsOn);
    if (s.asleep) {
        c.fillCircle(252, 22, 9 * kk, 0xF4F1DE, a);
        c.fillCircle(257, 18, 8 * kk, kUiDark, a);
    } else {
        for (int i = 0; i < 8; i++) {
            float ang = i * kPi / 4;
            c.line(252 + std::cos(ang) * 9, 22 + std::sin(ang) * 9, 252 + std::cos(ang) * 12,
                   22 + std::sin(ang) * 12, 2, 0xFFD25E, a);
        }
        c.fillCircle(252, 22, 6.5f, 0xFFD25E, a);
    }

    if (live && s.sick) {  // red cross while sick
        float sk = k(true);
        c.fillCircle(318, 22, 11 * sk, 0xFFFFFF);
        c.fillRect(318 - 7 * sk, 22 - 2.5f * sk, 14 * sk, 5 * sk, 0xD64545);
        c.fillRect(318 - 2.5f * sk, 22 - 7 * sk, 5 * sk, 14 * sk, 0xD64545);
    }

    // footprints, the currency; the icon pulses while it's calling for you
    float fk = s.calling ? 1 + 0.2f * pulse : 1.0f;
    if (s.calling) c.ring(410, 22, 15 + pulse * 5, 3, kGold, 1 - pulse * 0.6f);
    drawFootprint(c, 410, 23, fk, s.calling ? kGold : kUiLight);
    char buf[12];
    std::snprintf(buf, sizeof buf, "%u", static_cast<unsigned>(s.footprints % 10000));
    c.text(426, 15, buf, 2, kUiLight);
}

void Scene::drawToast(Canvas& c) {
    const char* t = toast();
    if (!t) return;
    float w = static_cast<float>(Canvas::textWidth(t, 2)) + 28;
    c.fillRoundRect(240 - w / 2, 50, w, 32, 16, kUiDark, 0.8f);
    c.text(240, 59, t, 2, kUiLight, Align::Center);
}

void Scene::drawTray(Canvas& c) {
    const float top = trayTop();
    const bool open = trayAnim_ > 0.5f;

    // The pull tab rides on the tray's top edge.
    c.fillRoundRect(240 - 48, top - 24, 96, 34, 12, kTray, 0.92f);
    float cy = top - 9, d = open ? 4.0f : -4.0f;
    c.line(228, cy - d, 240, cy + d, 4, kUiLight);
    c.line(240, cy + d, 252, cy - d, 4, kUiLight);
    if (top >= S) return;

    c.fillRoundRect(0, top, S, kTrayH + 20, 18, kTray, 0.95f);
    const float bob = static_cast<float>(std::sin(t_ * 5)) * 3;
    for (int i = 0; i < slotCount(); i++) {
        Rect r = slotArea(i);
        Slot slot = slotAt(i);
        float cx = static_cast<float>(r.cx());
        bool selected = itemFor(slot) != Item::None && itemFor(slot) == held_;
        if (selected) c.fillRoundRect(cx - 40, top + 18, 80, 72, 16, kGold);
        c.fillRoundRect(cx - 36, top + 22, 72, 64, 14, kButton);
        if (!(selected && drag_ == DragMode::Item)) drawSlotIcon(c, slot, cx, top + 54 + (selected ? bob : 0));
        c.text(cx, top + 94, toString(slot), 2, kUiLight, Align::Center);
    }
}

void Scene::drawSlotIcon(Canvas& c, Slot s, float x, float y) {
    Item it = itemFor(s);
    if (it != Item::None) {
        drawItem(c, it, x, y, 1);
        return;
    }
    switch (s) {
        case Slot::Feed:  // a bowl of food
            drawItem(c, Item::Fish, x + 4, y - 6, 0.7f);
            drawItem(c, Item::Berries, x - 10, y - 4, 0.6f);
            c.fillEllipse(x, y + 8, 22, 9, 0x9C6B3A);
            c.fillRect(x - 22, y, 44, 8, 0x9C6B3A);
            break;
        case Slot::Light: {
            bool lit = sim_.state().lightsOn;
            if (lit) c.glow(x, y + 2, 26, 0xFFC857, 0.5f);
            c.ring(x, y - 14, 5, 2.5f, 0xCFC7B5);
            c.fillRoundRect(x - 11, y - 10, 22, 30, 5, 0x2A1E14);
            c.fillRoundRect(x - 7, y - 6, 14, 22, 3, lit ? 0xFFD36B : 0x6B6B5A);
            break;
        }
        case Slot::Photos:
            c.fillRoundRect(x - 20, y - 12, 40, 28, 6, 0xCFC7B5);
            c.fillRect(x - 8, y - 17, 16, 6, 0xCFC7B5);
            c.fillCircle(x, y + 2, 9, 0x2A1E14);
            c.fillCircle(x, y + 2, 5, 0x6E8FA8);
            break;
        case Slot::Back:
            c.line(x - 14, y, x + 14, y, 5, kUiLight);
            c.line(x - 14, y, x - 4, y - 10, 5, kUiLight);
            c.line(x - 14, y, x - 4, y + 10, 5, kUiLight);
            break;
        default: break;
    }
}

void Scene::drawItem(Canvas& c, Item it, float x, float y, float k) {
    switch (it) {
        case Item::Berries:
            c.fillEllipse(x + 6 * k, y - 13 * k, 7 * k, 3.5f * k, 0x5C8F3E);
            c.fillCircle(x - 7 * k, y + 4 * k, 7 * k, 0x8E2A4F);
            c.fillCircle(x + 7 * k, y + 4 * k, 7 * k, 0x8E2A4F);
            c.fillCircle(x, y - 5 * k, 7 * k, 0xA3335C);
            c.fillCircle(x - 2 * k, y - 7 * k, 2 * k, 0xFFFFFF, 0.6f);
            break;
        case Item::Fish:
            c.fillTriangle(x + 10 * k, y, x + 22 * k, y - 10 * k, x + 22 * k, y + 10 * k, 0x5F87A3);
            c.fillEllipse(x - 3 * k, y, 17 * k, 9 * k, 0x7FA7C2);
            c.fillCircle(x - 12 * k, y - 2 * k, 2.2f * k, 0x1A1A1A);
            break;
        case Item::Roots:
            c.line(x, y - 8 * k, x - 7 * k, y - 20 * k, 3 * k, 0x5C8F3E);
            c.line(x, y - 8 * k, x, y - 22 * k, 3 * k, 0x5C8F3E);
            c.line(x, y - 8 * k, x + 7 * k, y - 20 * k, 3 * k, 0x5C8F3E);
            c.fillTriangle(x - 8 * k, y - 9 * k, x + 8 * k, y - 9 * k, x, y + 20 * k, 0xD9822B);
            break;
        case Item::Brush:
            c.line(x - 14 * k, y + 14 * k, x + 2 * k, y - 2 * k, 6 * k, 0xA0703F);
            c.fillRoundRect(x - 4 * k, y - 18 * k, 22 * k, 14 * k, 4 * k, 0x6B4A2F);
            for (int i = 0; i < 4; i++)
                c.line(x + (i * 5 - 1) * k, y - 18 * k, x + (i * 5 - 1) * k, y - 23 * k, 2 * k, 0xE8D8B0);
            break;
        case Item::Poultice:
            c.fillEllipse(x - 7 * k, y, 8 * k, 15 * k, 0x5C8F3E);
            c.fillEllipse(x + 7 * k, y, 8 * k, 15 * k, 0x4D7C34);
            c.fillEllipse(x, y - 2 * k, 7 * k, 16 * k, 0x6FA34A);
            c.line(x - 13 * k, y + 3 * k, x + 13 * k, y + 3 * k, 2.5f * k, 0xD8C8A0);
            break;
        case Item::Pinecone:
            c.fillEllipse(x, y, 10 * k, 15 * k, 0x8A5A2B);
            for (int i = -1; i <= 1; i++) c.line(x - 7 * k, y + i * 6 * k, x + 7 * k, y + i * 6 * k + 3 * k, 2 * k, 0x5E3B1A);
            break;
        case Item::None: break;
    }
}

void Scene::drawStats(Canvas& c) {
    const State& s = sim_.state();
    c.tint(0x000000, 0.55f);
    c.fillRoundRect(40, 76, 400, 312, 20, kUiLight);
    const Rgb ink = 0x3B2A1E;
    const char* name = s.name[0] ? s.name : "NO NAME YET";
    c.text(240, 98, name, Canvas::textWidth(name, 3) <= 380 ? 3 : 2, ink, Align::Center);

    char lines[7][40];
    int n = 0;
    Stage stage = s.stage == Stage::Away ? s.stageBeforeAway : s.stage;
    if (s.adult != AdultForm::None)
        std::snprintf(lines[n++], 40, "STAGE: %s (%s)", toString(stage), toString(s.adult));
    else
        std::snprintf(lines[n++], 40, "STAGE: %s", toString(stage));
    int64_t age = sim_.ageSecs();
    int days = static_cast<int>(age / 86400), hours = static_cast<int>(age % 86400 / 3600);
    std::snprintf(lines[n++], 40, "AGE: %d DAY%s %d HR%s", days, days == 1 ? "" : "S", hours, hours == 1 ? "" : "S");
    std::snprintf(lines[n++], 40, "GENERATION: %u", static_cast<unsigned>(s.generation));
    std::snprintf(lines[n++], 40, "TRUST: %s", s.stage == Stage::Away ? "AWAY" : trustName(sim_.trustLevel()));
    std::snprintf(lines[n++], 40, "PHOTOS: %d OF %d", sim_.photoCount(), Sim::kPhotoCount);
    std::snprintf(lines[n++], 40, "FOOTPRINTS: %u", static_cast<unsigned>(s.footprints));
    for (int i = 0; i < n; i++) c.text(64, 148 + i * 30.0f, lines[i], 2, ink);
    c.text(240, 360, "TAP TO CLOSE", 2, 0x8A7A66, Align::Center);
}

void Scene::drawGallery(Canvas& c) {
    const State& s = sim_.state();
    c.tint(0x000000, 0.55f);
    c.fillRoundRect(16, 56, 448, 380, 20, kUiLight);
    char title[32];
    std::snprintf(title, sizeof title, "SIGHTINGS %d / %d", sim_.photoCount(), Sim::kPhotoCount);
    c.text(240, 74, title, 2, 0x3B2A1E, Align::Center);
    for (int i = 0; i < Sim::kPhotoCount; i++) {
        float x = 28 + (i % 6) * 72.0f, y = 100 + (i / 6) * 72.0f;
        if (s.photos & (1u << i)) {
            // Placeholder "blurry photo": a dark shape among trees.
            c.fillRoundRect(x, y, 64, 64, 8, 0x35603F);
            c.fillTriangle(x + 4, y + 50, x + 22, y + 50, x + 13, y + 10, 0x264A30);
            c.fillTriangle(x + 40, y + 52, x + 60, y + 52, x + 50, y + 8, 0x264A30);
            float bx = x + 22 + static_cast<float>((i * 37) % 20);
            c.fillEllipse(bx, y + 42, 9, 13, 0x2A1A10, 0.8f);
            c.fillCircle(bx, y + 26, 7, 0x2A1A10, 0.8f);
        } else {
            c.fillRoundRect(x, y, 64, 64, 8, 0xD9CFBC);
            c.text(x + 32, y + 25, "?", 2, 0xA89C86, Align::Center);
        }
    }
    c.text(240, 404, "TAP TO CLOSE", 2, 0x8A7A66, Align::Center);
}

const char* toString(Scene::Slot s) {
    switch (s) {
        case Scene::Slot::Feed: return "FEED";
        case Scene::Slot::Clean: return "CLEAN";
        case Scene::Slot::Medicine: return "HEAL";
        case Scene::Slot::Play: return "PLAY";
        case Scene::Slot::Light: return "LIGHT";
        case Scene::Slot::Photos: return "PHOTOS";
        case Scene::Slot::Back: return "BACK";
        case Scene::Slot::Berries: return "BERRIES";
        case Scene::Slot::Fish: return "FISH";
        case Scene::Slot::Roots: return "ROOTS";
    }
    return "?";
}

}  // namespace squatch
