// Scene and gesture tests: drive the clearing with touch events, no window needed.
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <set>
#include <vector>

#include "squatch/canvas.hpp"
#include "squatch/gesture.hpp"
#include "squatch/scene.hpp"
#include "squatch/sim.hpp"
#include "squatch/wrist.hpp"

using namespace squatch;

static int gFailures = 0;
static int gChecks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        gChecks++;                                                         \
        if (!(cond)) {                                                     \
            gFailures++;                                                   \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                  \
    } while (0)

static constexpr int64_t kTenAm = 1798761600 + 10 * 3600;  // 2027-01-01 10:00 UTC

// ---------------------------------------------------------------- gestures

struct Reader {
    GestureReader r;
    std::vector<Gesture> out;
    void send(TouchPhase ph, uint8_t id, int x, int y, uint32_t ms) {
        r.touch(TouchEvent{ph, id, static_cast<int16_t>(x), static_cast<int16_t>(y), ms});
        drain();
    }
    void update(uint32_t ms) {
        r.update(ms);
        drain();
    }
    void drain() {
        Gesture g;
        while (r.poll(g)) out.push_back(g);
    }
    int count(GestureType t) const {
        int n = 0;
        for (const Gesture& g : out) n += g.type == t;
        return n;
    }
};

static void testTap() {
    std::printf("a short press is a tap, even a slow one under the long-press time\n");
    Reader rd;
    rd.send(TouchPhase::Down, 0, 100, 100, 0);
    rd.update(500);
    rd.send(TouchPhase::Move, 0, 105, 103, 520);  // a little wobble is fine
    rd.send(TouchPhase::Up, 0, 105, 103, 600);
    CHECK(rd.out.size() == 1);
    CHECK(rd.count(GestureType::Tap) == 1);
    CHECK(rd.out[0].x == 100 && rd.out[0].y == 100);
}

static void testLongPressThenDrag() {
    std::printf("a still finger long-presses, and can still drag afterwards\n");
    Reader rd;
    rd.send(TouchPhase::Down, 0, 50, 20, 0);
    rd.update(GestureReader::kLongPressMs + 10);
    CHECK(rd.count(GestureType::LongPress) == 1);
    rd.send(TouchPhase::Move, 0, 90, 20, 900);
    rd.send(TouchPhase::Move, 0, 130, 20, 950);
    rd.send(TouchPhase::Up, 0, 130, 20, 1000);
    CHECK(rd.count(GestureType::DragStart) == 1);
    CHECK(rd.count(GestureType::DragMove) == 1);
    CHECK(rd.count(GestureType::DragEnd) == 1);
    CHECK(rd.count(GestureType::Tap) == 0);
}

static void testDrag() {
    std::printf("moving past the slop starts a drag with its start point\n");
    Reader rd;
    rd.send(TouchPhase::Down, 0, 200, 300, 0);
    rd.send(TouchPhase::Move, 0, 200, 290, 20);  // within slop
    rd.send(TouchPhase::Move, 0, 200, 270, 40);
    rd.send(TouchPhase::Move, 0, 200, 240, 60);
    rd.send(TouchPhase::Up, 0, 200, 240, 80);
    CHECK(rd.out.size() == 3);
    CHECK(rd.out[0].type == GestureType::DragStart);
    CHECK(rd.out[0].startX == 200 && rd.out[0].startY == 300);
    CHECK(rd.out[1].type == GestureType::DragMove && rd.out[1].prevY == 270);
    CHECK(rd.out[1].speed > 0);
    CHECK(rd.out[2].type == GestureType::DragEnd);
}

static void testTwoFingerTap() {
    std::printf("two fingers down and up together make a two-finger tap\n");
    Reader rd;
    rd.send(TouchPhase::Down, 0, 200, 200, 0);
    rd.send(TouchPhase::Down, 1, 260, 200, 30);
    rd.send(TouchPhase::Up, 0, 200, 200, 150);
    rd.send(TouchPhase::Up, 1, 260, 200, 170);
    CHECK(rd.out.size() == 1);
    CHECK(rd.count(GestureType::TwoFingerTap) == 1);
    CHECK(rd.out[0].x == 230);

    std::printf("two fingers sliding apart are not a tap\n");
    Reader slide;
    slide.send(TouchPhase::Down, 0, 200, 200, 0);
    slide.send(TouchPhase::Down, 1, 260, 200, 30);
    slide.send(TouchPhase::Move, 1, 330, 200, 60);
    slide.send(TouchPhase::Up, 0, 200, 200, 150);
    slide.send(TouchPhase::Up, 1, 330, 200, 170);
    CHECK(slide.out.empty());
}

// ---------------------------------------------------------------- canvas

static void testCanvas() {
    std::printf("canvas shapes are anti-aliased and text draws\n");
    std::vector<uint16_t> fb(64 * 64);
    Canvas c(fb.data(), 64, 64);
    c.clear(0x000000);
    c.fillCircle(32, 32, 10.3f, 0xFFFFFF);
    CHECK(c.pixel(32, 32) == 0xFFFFFF);
    CHECK(c.pixel(2, 2) == 0x000000);
    Rgb edge = c.pixel(42, 32);  // on the rim: partly covered
    CHECK(edge != 0x000000 && edge != 0xFFFFFF);

    c.clear(0x000000);
    c.text(0, 0, "A", 1, 0xFFFFFF);
    CHECK(c.pixel(2, 0) == 0xFFFFFF);  // top of the A
    CHECK(c.pixel(0, 0) == 0x000000);
    CHECK(Canvas::textWidth("AB", 2) == 22);
}

// ---------------------------------------------------------------- scene

struct Driver {
    Sim sim;
    Scene scene{sim};
    uint32_t ms = 0;

    // A freshly hatched baby at 10 in the morning, friendly, hungry, with nothing going on.
    Driver() {
        sim.newGame(kTenAm, 0, 99);
        sim.tick(86400 + 60);
        State& s = sim.mutableState();
        s.trust = 80;
        s.hunger = 1;
        s.happiness = 1;
        s.calling = s.misbehaving = s.sick = false;
        s.messes = 0;
        sim.setName("TESTY");  // so the naming page doesn't open over every test
        settle();
    }
    void step(uint32_t d = 16) {
        ms += d;
        scene.update(ms);
    }
    void settle() {
        for (int i = 0; i < 40; i++) step();
    }
    void send(TouchPhase ph, uint8_t id, int x, int y) {
        scene.touch(TouchEvent{ph, id, static_cast<int16_t>(x), static_cast<int16_t>(y), ms});
    }
    void tap(int x, int y) {
        send(TouchPhase::Down, 0, x, y);
        step(60);
        send(TouchPhase::Up, 0, x, y);
        step();
    }
    void tap(const Scene::Rect& r) { tap(r.cx(), r.cy()); }
    void drag(int x0, int y0, int x1, int y1, int steps = 20, uint32_t perStep = 30) {
        send(TouchPhase::Down, 0, x0, y0);
        for (int i = 1; i <= steps; i++) {
            step(perStep);
            send(TouchPhase::Move, 0, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
        }
        send(TouchPhase::Up, 0, x1, y1);
        step();
    }
    void openTray() {
        if (!scene.trayOpen()) tap(scene.handleArea());
        settle();
    }
    int slotOf(Scene::Slot want) const {
        for (int i = 0; i < scene.slotCount(); i++)
            if (scene.slotAt(i) == want) return i;
        return -1;
    }
    void pressSlot(Scene::Slot want) {
        int i = slotOf(want);
        if (i >= 0) tap(scene.slotArea(i));
    }
};

static void testFeedByTapping() {
    std::printf("tray: tap Feed, tap Fish, tap Squatch feeds it\n");
    Driver d;
    CHECK(d.sim.state().stage == Stage::Baby);
    d.openTray();
    CHECK(d.scene.trayOpen());
    d.pressSlot(Scene::Slot::Feed);
    CHECK(d.slotOf(Scene::Slot::Fish) >= 0);
    d.pressSlot(Scene::Slot::Fish);
    CHECK(d.scene.held() == Scene::Item::Fish);
    d.tap(d.scene.squatchArea());
    CHECK(d.sim.state().hunger > 2.9f);
    CHECK(d.sim.state().messes == 1);
    CHECK(d.scene.takeSaveRequest());
}

static void testFeedByDragging() {
    std::printf("tray: drag berries straight out of the tray onto Squatch\n");
    Driver d;
    d.openTray();
    d.pressSlot(Scene::Slot::Feed);
    Scene::Rect from = d.scene.slotArea(d.slotOf(Scene::Slot::Berries));
    Scene::Rect to = d.scene.squatchArea();
    d.drag(from.cx(), from.cy(), to.cx(), to.cy());
    CHECK(d.sim.state().hunger > 1.9f);
    CHECK(d.sim.state().snacks == 1);

    std::printf("a drop that misses keeps the item in hand\n");
    d.drag(from.cx(), from.cy(), 240, 120);
    CHECK(d.sim.state().snacks == 1);
    CHECK(d.scene.held() == Scene::Item::Berries);
}

static void testTraySwipes() {
    std::printf("swipe up from the bottom edge opens the tray, swipe down closes it\n");
    Driver d;
    CHECK(!d.scene.trayOpen());
    d.drag(100, 470, 100, 380);
    d.settle();
    CHECK(d.scene.trayOpen());
    d.drag(100, 400, 100, 470);
    d.settle();
    CHECK(!d.scene.trayOpen());
}

static void testSwipeCleansMess() {
    std::printf("a swipe across a mess cleans it\n");
    Driver d;
    d.sim.mutableState().messes = 1;
    Scene::Rect m = d.scene.messArea(0);
    d.drag(m.x - 40, m.cy(), m.x + m.w + 40, m.cy());
    CHECK(d.sim.state().messes == 0);
}

static void testStrokePets() {
    std::printf("a slow stroke across a friendly Squatch pets it\n");
    Driver d;
    Scene::Rect a = d.scene.squatchArea();
    d.drag(a.x - 30, a.cy(), a.x + a.w + 30, a.cy(), 30, 30);
    CHECK(d.sim.state().happiness > 1.4f);

    std::printf("a timid Squatch won't be petted\n");
    Driver t;
    t.sim.mutableState().trust = 40;
    Scene::Rect b = t.scene.squatchArea();
    t.drag(b.x - 30, b.cy(), b.x + b.w + 30, b.cy(), 30, 30);
    CHECK(t.sim.state().happiness < 1.1f);
}

static void testScold() {
    std::printf("a two-finger tap scolds a misbehaving Squatch\n");
    Driver d;
    d.sim.mutableState().misbehaving = true;
    float before = d.sim.state().discipline;
    d.send(TouchPhase::Down, 0, 210, 260);
    d.send(TouchPhase::Down, 1, 270, 260);
    d.step(80);
    d.send(TouchPhase::Up, 0, 210, 260);
    d.send(TouchPhase::Up, 1, 270, 260);
    d.step();
    CHECK(!d.sim.state().misbehaving);
    CHECK(d.sim.state().discipline > before);
    CHECK(d.scene.toast() && std::strcmp(d.scene.toast(), "TESTY STOPPED") == 0);
}

static void testAnswerCall() {
    std::printf("tapping the pulsing footprints answers a call\n");
    Driver d;
    State& s = d.sim.mutableState();
    s.calling = s.callGenuine = true;
    s.callSince = s.now;
    uint32_t prints = s.footprints;
    d.tap(d.scene.footprintArea());
    CHECK(!d.sim.state().calling);
    CHECK(d.sim.state().footprints == prints + 1);
}

static void testOverlays() {
    std::printf("long-press the status strip for stats; More > Photos opens the gallery; a tap closes them\n");
    Driver d;
    d.send(TouchPhase::Down, 0, 120, 20);
    for (int i = 0; i < 50; i++) d.step();
    d.send(TouchPhase::Up, 0, 120, 20);
    d.step();
    CHECK(d.scene.overlay() == Scene::Overlay::Stats);
    d.tap(240, 240);
    CHECK(d.scene.overlay() == Scene::Overlay::None);

    d.openTray();
    d.pressSlot(Scene::Slot::More);
    d.pressSlot(Scene::Slot::Photos);
    CHECK(d.scene.overlay() == Scene::Overlay::Gallery);
    d.tap(240, 240);
    CHECK(d.scene.overlay() == Scene::Overlay::None);
}

static void testLeaveFoodWhileAway() {
    std::printf("while it's away, food dropped on the stump counts towards coaxing it back\n");
    Driver d;
    State& s = d.sim.mutableState();
    s.stageBeforeAway = s.stage;
    s.stage = Stage::Away;
    d.openTray();
    d.pressSlot(Scene::Slot::Feed);
    d.pressSlot(Scene::Slot::Fish);
    d.tap(d.scene.stumpArea());
    CHECK(d.sim.state().coaxDays == 1);
    d.tap(d.scene.stumpArea());  // once a day
    CHECK(d.sim.state().coaxDays == 1);
}

static void testAsleep() {
    std::printf("a sleeping Squatch says so instead of eating\n");
    Driver d;
    d.sim.tick(12 * 3600);  // 22:00
    d.settle();
    CHECK(d.sim.state().asleep);
    d.sim.mutableState().hunger = 1;
    d.openTray();
    d.pressSlot(Scene::Slot::Feed);
    d.pressSlot(Scene::Slot::Fish);
    d.tap(d.scene.squatchArea());
    CHECK(d.sim.state().hunger < 1.1f);
    CHECK(d.scene.toast() && std::strstr(d.scene.toast(), "ASLEEP"));
    std::printf("the Light button turns the light off\n");
    d.pressSlot(Scene::Slot::Back);
    d.pressSlot(Scene::Slot::Light);
    CHECK(!d.sim.state().lightsOn);
}

static void testHorseTouchVoices() {
    std::printf("the horse makes one sound per touch, a different one for nose, tummy and head\n");
    Driver d;
    d.scene.setSpecies(Scene::Species::Horse);
    Cue c;
    while (d.scene.takeCue(c)) {}
    auto voices = [&](Cue& only) {
        int n = 0;
        while (d.scene.takeCue(c)) {
            if (c == Cue::Nicker || c == Cue::Neigh || c == Cue::Snort || c == Cue::Blow || c == Cue::Chuckle ||
                c == Cue::Sigh) {
                n++;
                only = c;
            }
        }
        return n;
    };
    // Tap all over it (a pause between, so the pokes never add up to a swat).
    const Scene::Rect a = d.scene.squatchArea();
    bool sawBlow = false, sawChuckle = false, sawSigh = false, oneEach = true;
    for (int y = a.y + 4; y < a.y + a.h; y += 8) {
        for (int x = a.x + 4; x < a.x + a.w; x += 8) {
            d.tap(x, y);
            Cue got = Cue::Count;
            if (voices(got) > 1) oneEach = false;
            sawBlow |= got == Cue::Blow;
            sawChuckle |= got == Cue::Chuckle;
            sawSigh |= got == Cue::Sigh;
            d.step(7000);
            d.step();
        }
    }
    CHECK(oneEach);
    CHECK(sawBlow && sawChuckle && sawSigh);

    std::printf("bringing it home from the stable is one whinny, not two sounds at once\n");
    d.openTray();
    d.pressSlot(Scene::Slot::More);
    d.pressSlot(Scene::Slot::Stable);
    CHECK(d.sim.state().boarding);
    d.settle();
    while (d.scene.takeCue(c)) {}
    d.tap(d.scene.homeArea());
    Cue home = Cue::Count;
    CHECK(voices(home) == 1);
    CHECK(home == Cue::Neigh);
}

static void testHorseChatter() {
    std::printf("the horse has lots to say, in the bubble's width, without repeating itself\n");
    Driver d;
    d.scene.setSpecies(Scene::Species::Horse);
    Scene::Settings never = d.scene.settings();
    never.timeout = 6;  // the screen stays on
    d.scene.setSettings(never);
    std::vector<std::string> said;
    std::string last;
    for (int i = 0; i < 400000 && said.size() < 60; i++) {  // a few hours of being left alone
        d.step(64);
        if (i % 2000 == 0) {  // keep it fed, happy and awake, so it chatters rather than asks
            State& st = d.sim.mutableState();
            st.hunger = st.happiness = 4;
            st.messes = 0;
            st.asleep = false;
        }
        const char* b = d.scene.bubble();
        if (b && last != b) {
            last = b;
            said.push_back(b);
        }
    }
    CHECK(said.size() >= 20);
    int distinct = 0;
    for (size_t i = 0; i < said.size(); i++) {
        CHECK(said[i].size() <= 27);
        bool seen = false;
        for (size_t j = 0; j < i; j++) seen |= said[j] == said[i];
        distinct += !seen;
        if (i >= 1) CHECK(said[i] != said[i - 1]);
    }
    CHECK(distinct >= 15);
}

static void testVisitors() {
    std::printf("animals visit now and then; each is a big target, and a tap sends it on its way\n");
    Driver d;
    d.scene.setSpecies(Scene::Species::Horse);
    Scene::Settings never = d.scene.settings();
    never.timeout = 6;
    d.scene.setSettings(never);
    bool came = false;
    for (int i = 0; i < 20000 && !came; i++) {  // within about 20 minutes
        d.step(64);
        came = d.scene.visitor() != Scene::Visitor::None;
    }
    CHECK(came);
    using V = Scene::Visitor;
    for (V v : {V::Bunny, V::Bird, V::Deer, V::Duck, V::Butterfly, V::Owl}) {
        d.scene.previewVisitor(v);
        for (int i = 0; i < 160; i++) d.step();  // let it arrive
        CHECK(d.scene.visitor() == v);
        const Scene::Rect r = d.scene.visitorArea();
        CHECK(r.w >= 64 && r.h >= 64);
        d.tap(r);
        bool left = false;
        for (int i = 0; i < 2000 && !left; i++) {
            d.step();
            left = d.scene.visitor() == V::None;
        }
        CHECK(left);
    }
}

static void testGlasses() {
    std::printf("the horse's info card switches its glasses: none, four frame colours, none again\n");
    Driver d;
    d.scene.setSpecies(Scene::Species::Horse);
    d.tap(240, 20);  // the top bar opens the info card
    CHECK(d.scene.overlay() == Scene::Overlay::Stats);
    const Scene::Rect g = d.scene.glassesArea();
    CHECK(g.w >= 64 && g.h >= 64);
    d.scene.takeSettingsChanged();
    for (int i = 1; i <= Scene::kGlassesStyles; i++) {
        d.tap(g);
        CHECK(d.scene.overlay() == Scene::Overlay::Stats);  // stays open
        CHECK(d.scene.settings().glasses == i % Scene::kGlassesStyles);
    }
    CHECK(d.scene.takeSettingsChanged());
    d.tap(g);
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    d.scene.draw(c);
    d.tap(240, 250);  // anywhere else closes it
    CHECK(d.scene.overlay() == Scene::Overlay::None);
    d.scene.draw(c);  // wearing them
    Driver b;  // the Bigfoot has no glasses button
    CHECK(b.scene.glassesArea().w == 0);
}

static void testRudolphNose() {
    std::printf("at Christmas a boop on the horse's nose switches its red reindeer nose, and it's kept\n");
    for (int month : {12, 6}) {
        Driver d;
        d.scene.setSpecies(Scene::Species::Horse);
        d.scene.previewDate(month, 12);
        d.scene.takeSettingsChanged();
        const Scene::Rect a = d.scene.squatchArea();
        bool flipped = false;
        for (int y = a.y + 4; y < a.y + a.h && !flipped; y += 6) {
            for (int x = a.x + 4; x < a.x + a.w && !flipped; x += 6) {
                d.tap(x, y);
                flipped = d.scene.settings().rudolph == 1;
                d.step(7000);
                d.step();
            }
        }
        CHECK(flipped == (month == 12));
        CHECK(d.scene.takeSettingsChanged() == (month == 12));
    }
}

static void testBoardingStable() {
    std::printf("More > Stable sends it to the stable; BRING HOME brings it back\n");
    Driver d;
    d.scene.setSpecies(Scene::Species::Horse);
    d.openTray();
    d.pressSlot(Scene::Slot::More);
    CHECK(d.slotOf(Scene::Slot::Stable) >= 0);
    d.pressSlot(Scene::Slot::Stable);
    CHECK(d.sim.state().boarding);
    CHECK(d.sim.state().lightsOn);  // one tap, one button (the Light button sat under it)
    CHECK(d.scene.takeSaveRequest());
    CHECK(d.scene.squatchArea().w == 0);  // not in the paddock
    d.settle();
    d.tap(d.scene.homeArea());
    CHECK(!d.sim.state().boarding);
    CHECK(d.scene.squatchArea().w > 0);
    CHECK(d.sim.state().hunger == 4);
}

static void testTouchTargets() {
    std::printf("every touch target is at least 64 px\n");
    Driver d;
    auto big = [](const Scene::Rect& r) { return r.w >= 64 && r.h >= 64; };
    CHECK(big(d.scene.squatchArea()));  // a small baby, still easy to hit
    CHECK(big(d.scene.handleArea()));
    CHECK(big(d.scene.footprintArea()));
    CHECK(big(d.scene.stumpArea()));
    for (int i = 0; i < 3; i++) CHECK(big(d.scene.messArea(i)));
    d.openTray();
    for (int i = 0; i < d.scene.slotCount(); i++) CHECK(big(d.scene.slotArea(i)));
    d.pressSlot(Scene::Slot::Feed);
    for (int i = 0; i < d.scene.slotCount(); i++) CHECK(big(d.scene.slotArea(i)));
    d.pressSlot(Scene::Slot::Back);
    d.pressSlot(Scene::Slot::More);
    for (int i = 0; i < d.scene.slotCount(); i++) CHECK(big(d.scene.slotArea(i)));
    for (int i = 0; i < State::kMaxTrails; i++) CHECK(big(d.scene.trailRowArea(i)));
    CHECK(big(d.scene.rescanArea()));
    CHECK(big(d.scene.homeArea()));
    for (int row = 0; row < Scene::kSettingRows; row++) {
        CHECK(big(d.scene.settingButtonArea(row, false)));
        CHECK(big(d.scene.settingButtonArea(row, true)));
    }
    CHECK(big(d.scene.settingsDoneArea()));
    CHECK(big(d.scene.startOverArea()) && big(d.scene.startKeepArea()) && big(d.scene.startWipeArea()) &&
          big(d.scene.startCancelArea()));
    CHECK(big(d.scene.treeArea(0)) && big(d.scene.treeArea(1)) && big(d.scene.fireArea()));
    d.sim.mutableState().trust = 10;  // hiding behind the tree
    CHECK(big(d.scene.squatchArea()));
}

static void testDrawsEveryState() {
    std::printf("the scene draws every stage, day and night\n");
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);

    Sim egg;
    egg.newGame(kTenAm, 0, 1);
    Scene eggScene(egg);
    eggScene.update(16);
    eggScene.draw(c);
    Rgb daySky = c.pixel(240, 4);

    Driver d;
    const Stage stages[] = {Stage::Baby, Stage::Child, Stage::Teen, Stage::Adult, Stage::Elder, Stage::Away};
    for (float trust : {10.0f, 40.0f, 80.0f}) {
        for (Stage st : stages) {
            State& s = d.sim.mutableState();
            s.stage = st;
            s.adult = st >= Stage::Adult && st != Stage::Away ? AdultForm::CouchSquatch : AdultForm::None;
            s.trust = trust;
            s.messes = 3;
            s.sick = s.calling = s.misbehaving = true;
            for (int i = 0; i < 20; i++) d.step(50);
            d.scene.draw(c);
        }
    }
    d.sim.tick(12 * 3600);
    d.step();
    d.scene.draw(c);
    CHECK(c.pixel(240, 4) != daySky);  // the night sky is not the day sky
}

// Raise the Driver's baby, looked after, until it's a Child at 11:00.
static void growToChild(Driver& d) {
    for (int h = 0; h < 49; h++) {
        State& s = d.sim.mutableState();
        s.hunger = s.happiness = 4;
        s.sick = s.misbehaving = s.calling = false;
        s.messes = 0;
        d.sim.tick(3600);
    }
    d.sim.mutableState().trust = 50;
    d.settle();
}

static void testTrailMap() {
    std::printf("More > Trails opens the trail map and asks for a Wi-Fi scan\n");
    Driver d;
    d.openTray();
    d.pressSlot(Scene::Slot::More);
    d.pressSlot(Scene::Slot::Trails);
    CHECK(d.scene.overlay() == Scene::Overlay::Trails);
    CHECK(d.scene.takeScanRequest());
    CHECK(!d.scene.takeScanRequest());  // once
    CHECK(d.scene.scanning());

    std::printf("the scan's results arrive as trails, with a note about new ones\n");
    Sighting seen[] = {{trailHash("Ranger Station"), -48}, {trailHash("Pinecone Cafe"), -72}};
    d.sim.scoutTrails(seen, 2);
    d.step();
    CHECK(!d.scene.scanning());
    CHECK(std::strstr(d.scene.trailNote(), "2 NEW TRAILS") != nullptr);

    std::printf("a baby is too little to go\n");
    d.tap(d.scene.trailRowArea(0));
    CHECK(!d.sim.state().foraging);
    CHECK(std::strstr(d.scene.trailNote(), "TOO LITTLE") != nullptr);
    CHECK(d.scene.overlay() == Scene::Overlay::Trails);

    std::printf("LOOK AGAIN scans again; tapping outside closes the map\n");
    d.tap(d.scene.rescanArea());
    CHECK(d.scene.takeScanRequest());
    d.tap(10, 476);
    CHECK(d.scene.overlay() == Scene::Overlay::None);

    std::printf("a Child that trusts you sets off, is gone from the clearing, and comes back\n");
    growToChild(d);
    CHECK(d.sim.state().stage == Stage::Child);
    d.openTray();
    if (d.slotOf(Scene::Slot::Trails) < 0) d.pressSlot(Scene::Slot::More);
    d.pressSlot(Scene::Slot::Trails);
    CHECK(d.scene.overlay() == Scene::Overlay::Trails);
    d.tap(d.scene.trailRowArea(0));
    CHECK(d.sim.state().foraging);
    CHECK(d.scene.overlay() == Scene::Overlay::None);
    CHECK(d.scene.toast() && std::strstr(d.scene.toast(), "OFF TO") != nullptr);
    CHECK(d.scene.squatchArea().w == 0);  // nobody to tap
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    d.scene.draw(c);  // with the "back in" banner
    d.sim.tick(31 * 60);
    d.settle();
    CHECK(!d.sim.state().foraging);
    bool heardBack = false;  // after any toasts already in line
    for (int i = 0; i < 600 && !heardBack; i++) {
        d.step(16);
        heardBack = d.scene.toast() && std::strstr(d.scene.toast(), "BACK FROM") != nullptr;
    }
    CHECK(heardBack);
    CHECK(d.scene.squatchArea().w > 0);
}

static void testDrawsEveryCoat() {
    std::printf("every coat draws, and rare coats really look different\n");
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    Driver d;
    growToChild(d);
    d.sim.mutableState().trust = 80;  // out front
    const uint8_t rarityGene[] = {0, 200, 230, 230, 248, 248, 255};
    const uint8_t variantGene[] = {0, 0, 0, 1, 0, 1, 0};
    Rgb body[7];
    for (int v = 0; v < 7; v++) {
        for (uint8_t extra = 0; extra < 4; extra++) {
            Genome& g = d.sim.mutableState().genome;
            g.g[kGeneRarity] = rarityGene[v];
            g.g[kGeneVariant] = variantGene[v];
            g.g[kGeneShag] = static_cast<uint8_t>(extra << 6);
            g.g[kGeneMarkings] = static_cast<uint8_t>(extra << 6);
            g.g[kGeneShade] = 160;
            d.step();
            d.scene.draw(c);
        }
        Scene::Rect r = d.scene.squatchArea();
        body[v] = c.pixel(r.cx() - r.w / 3, r.y + r.h * 2 / 3);  // a shoulder, clear of the face
    }
    CHECK(body[2] != body[6]);  // Swamp red vs Golden
    CHECK(body[3] != body[0]);  // Snowcoat vs an ordinary brown
    CHECK(body[4] != body[3]);  // Albino vs Snowcoat
}

static void testBattery() {
    std::printf("the battery shows only when the front end reports one, and warns once when low\n");
    Driver d;
    CHECK(d.scene.batteryPercent() < 0);  // the PC has none
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    d.scene.draw(c);
    Rgb before = c.pixel(345, 22);
    d.scene.setBattery(64, true);
    d.step();
    d.scene.draw(c);
    CHECK(c.pixel(345, 22) != before);  // the icon is drawn
    d.scene.setBattery(12, false);
    d.step();
    CHECK(d.scene.toast() && std::strstr(d.scene.toast(), "BATTERY LOW") != nullptr);
    for (int i = 0; i < 400; i++) d.step();  // let the warning go
    d.scene.setBattery(11, false);
    d.step();
    CHECK(!d.scene.toast() || std::strstr(d.scene.toast(), "BATTERY LOW") == nullptr);  // only once
}

static void testSettings() {
    std::printf("settings change brightness and the screen timeout; a tap on a dark screen only wakes it\n");
    Driver d;
    CHECK(d.scene.brightnessPercent() == 80);
    CHECK(d.scene.screenTimeoutMs() == 60000);
    d.openTray();
    d.pressSlot(Scene::Slot::More);
    d.pressSlot(Scene::Slot::Settings);
    CHECK(d.scene.overlay() == Scene::Overlay::Settings);
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    d.scene.draw(c);

    d.tap(d.scene.settingButtonArea(0, true));
    CHECK(d.scene.brightnessPercent() == 100);
    CHECK(d.scene.takeSettingsChanged());
    d.tap(d.scene.settingButtonArea(0, true));  // already at the top
    CHECK(d.scene.brightnessPercent() == 100);
    CHECK(!d.scene.takeSettingsChanged());
    for (int i = 0; i < 9; i++) d.tap(d.scene.settingButtonArea(0, false));
    CHECK(d.scene.brightnessPercent() == 20);  // never all the way dark
    d.tap(d.scene.settingButtonArea(1, false));
    CHECK(d.scene.screenTimeoutMs() == 30000);
    CHECK(d.scene.overlay() == Scene::Overlay::Settings);  // still open after all that
    d.tap(d.scene.settingsDoneArea());
    CHECK(d.scene.overlay() == Scene::Overlay::None);

    // 30 seconds without a touch: dark.
    for (int i = 0; i < 29; i++) d.step(1000);
    CHECK(!d.scene.screenOff());
    d.step(1000);
    d.step(1000);
    CHECK(d.scene.screenOff());
    d.tap(10, 10);  // the strip: would open the stats page if the screen were on
    CHECK(!d.scene.screenOff());
    CHECK(d.scene.overlay() == Scene::Overlay::None);
    d.tap(10, 10);
    CHECK(d.scene.overlay() == Scene::Overlay::Stats);  // the next tap works as usual
    d.tap(10, 10);

    // A finger held still doesn't let it go dark.
    d.send(TouchPhase::Down, 0, 240, 300);
    for (int i = 0; i < 40; i++) d.step(1000);
    CHECK(!d.scene.screenOff());
    d.send(TouchPhase::Up, 0, 240, 300);
    for (int i = 0; i < 31; i++) d.step(1000);
    CHECK(d.scene.screenOff());

    // The board's power key: off at once, and the next tap only wakes it.
    d.scene.wake();
    d.scene.screenOffNow();
    CHECK(d.scene.screenOff());
    d.tap(10, 10);
    CHECK(!d.scene.screenOff());
    CHECK(d.scene.overlay() == Scene::Overlay::None);
    d.scene.screenOffNow();

    // Low battery wakes it.
    d.scene.setBattery(10, false);
    CHECK(!d.scene.screenOff());

    // Never: stays on; settings load clamped.
    Scene::Settings st;
    st.brightness = 200;
    st.timeout = Scene::kTimeoutSteps - 1;
    d.scene.setSettings(st);
    CHECK(d.scene.brightnessPercent() == 100);
    CHECK(d.scene.screenTimeoutMs() == 0);
    for (int i = 0; i < 4000; i++) d.step(1000);
    CHECK(!d.scene.screenOff());
}

static void testSounds() {
    std::printf("care makes sounds at the chosen volume; none with the volume off or the screen dark\n");
    Driver d;
    Cue c;
    while (d.scene.takeCue(c)) {}
    d.openTray();
    d.pressSlot(Scene::Slot::Feed);
    d.pressSlot(Scene::Slot::Fish);
    d.tap(d.scene.squatchArea());
    std::vector<Cue> heard;
    while (d.scene.takeCue(c)) heard.push_back(c);
    bool ate = false, picked = false;
    for (Cue x : heard) {
        ate |= x == Cue::Eat;
        picked |= x == Cue::Pickup;
    }
    CHECK(ate && picked);

    // The volume row: down to off, and then nothing is queued.
    d.pressSlot(Scene::Slot::Back);
    d.pressSlot(Scene::Slot::More);
    d.pressSlot(Scene::Slot::Settings);
    CHECK(d.scene.volume() == 1);
    while (d.scene.takeCue(c)) {}
    d.tap(d.scene.settingButtonArea(2, true));
    CHECK(d.scene.volume() == 2);
    CHECK(d.scene.takeCue(c) && c == Cue::Happy);  // a chirp to hear the new level
    for (int i = 0; i < 5; i++) d.tap(d.scene.settingButtonArea(2, false));
    CHECK(d.scene.volume() == 0);
    while (d.scene.takeCue(c)) {}
    d.tap(d.scene.settingsDoneArea());
    d.pressSlot(Scene::Slot::Back);
    d.pressSlot(Scene::Slot::Light);
    CHECK(!d.scene.takeCue(c));

    // Volume back on; with the screen off only a knock gets through.
    Scene::Settings st = d.scene.settings();
    st.volume = 2;
    d.scene.setSettings(st);
    d.scene.screenOffNow();
    d.scene.setBattery(50, false);
    d.sim.mutableState().sick = true;
    for (int i = 0; i < 600; i++) d.step();  // sneezes, unheard
    CHECK(!d.scene.takeCue(c));

    // The synth: every cue plays, ends, and stays within range; volume 0 is silence.
    Synth syn;
    std::vector<int16_t> buf(Synth::kRate / 100);
    for (int i = 0; i < static_cast<int>(Cue::Count); i++) {
        Cue cue = static_cast<Cue>(i);
        // Short cues, except Santa's "ho ho ho, merry Christmas", which is a few seconds.
        const int longest = cue == Cue::HoHoHo ? 4000 : 2000;
        CHECK(Synth::lengthMs(cue) > 0 && Synth::lengthMs(cue) < longest);
        syn.play(cue, 3);
        int peak = 0, blocks = 0;
        while (syn.render(buf.data(), static_cast<int>(buf.size())) && blocks < 500) {
            for (int16_t v : buf) peak = std::max(peak, std::abs(static_cast<int>(v)));
            blocks++;
        }
        CHECK(blocks > 0 && blocks < longest / 10);  // it finishes
        CHECK(peak > 1000);                 // and it's audible
    }
    syn.play(Cue::Happy, 0);
    CHECK(!syn.active());
    syn.play(Cue::Happy, 1);
    int quiet = 0;
    while (syn.render(buf.data(), static_cast<int>(buf.size())))
        for (int16_t v : buf) quiet = std::max(quiet, std::abs(static_cast<int>(v)));
    CHECK(quiet > 0 && quiet < 6000);  // quiet is quiet
}

static void testNaming() {
    std::printf("an unnamed pet is asked for a name after hatching; the name shows in messages\n");
    Sim sim;
    sim.newGame(kTenAm, 0, 99);
    Scene scene(sim);
    uint32_t ms = 0;
    auto step = [&](uint32_t d) { ms += d; scene.update(ms); };
    auto tap = [&](const Scene::Rect& r) {
        scene.touch(TouchEvent{TouchPhase::Down, 0, static_cast<int16_t>(r.cx()), static_cast<int16_t>(r.cy()), ms});
        step(60);
        scene.touch(TouchEvent{TouchPhase::Up, 0, static_cast<int16_t>(r.cx()), static_cast<int16_t>(r.cy()), ms});
        step(16);
    };
    for (int i = 0; i < 100; i++) step(16);
    CHECK(scene.overlay() == Scene::Overlay::None);  // an egg isn't asked
    sim.tick(sim.tuning().eggEnd + 1);
    step(16);
    CHECK(scene.overlay() == Scene::Overlay::None);  // it hatches first...
    for (int i = 0; i < 200; i++) step(16);
    CHECK(scene.overlay() == Scene::Overlay::Name);  // ...then is asked

    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    scene.draw(c);

    tap(scene.nameDoneArea());  // nothing typed: Done waits
    CHECK(scene.overlay() == Scene::Overlay::Name);
    for (char ch : std::string("MOSZ")) tap(scene.nameKeyArea(ch - 'A'));
    tap(scene.nameKeyArea(27));  // delete the Z
    tap(scene.nameKeyArea(18));  // S
    tap(scene.nameKeyArea(26));  // a space
    tap(scene.nameKeyArea(26));  // a second one is ignored
    CHECK(std::strcmp(scene.nameDraft(), "MOSS ") == 0);
    for (int i = 0; i < 12; i++) tap(scene.nameKeyArea(0));
    CHECK(std::strlen(scene.nameDraft()) == Scene::kMaxNameLen);  // capped
    for (int i = 0; i < 5; i++) tap(scene.nameKeyArea(27));
    tap(scene.nameDoneArea());  // trailing space trimmed
    CHECK(std::strcmp(sim.state().name, "MOSS") == 0);
    CHECK(scene.overlay() == Scene::Overlay::None);
    CHECK(scene.toast() && std::strcmp(scene.toast(), "HELLO, MOSS!") == 0);

    // Messages use the name.
    for (int i = 0; i < 400; i++) step(16);
    sim.mutableState().hunger = 4;
    sim.mutableState().asleep = false;
    tap(scene.squatchArea());  // a tap, not a feed; then feed from the tray
    CHECK(sim.state().name[0]);

    // Renaming from the stats page; Suggest fills in a name.
    tap(Scene::Rect{10, 10, 64, 40});  // the strip opens the stats page
    CHECK(scene.overlay() == Scene::Overlay::Stats);
    tap(scene.nameEditArea());
    CHECK(scene.overlay() == Scene::Overlay::Name);
    CHECK(std::strcmp(scene.nameDraft(), "MOSS") == 0);
    tap(scene.nameSuggestArea());
    CHECK(scene.nameDraft()[0] && std::strcmp(scene.nameDraft(), "MOSS") != 0);
    tap(scene.nameDoneArea());
    CHECK(std::strcmp(sim.state().name, scene.nameDraft()) == 0);

    // Every key is big enough.
    for (int i = 0; i < Scene::kNameKeys; i++) CHECK(scene.nameKeyArea(i).w >= 64 && scene.nameKeyArea(i).h >= 64);
    CHECK(scene.nameSuggestArea().h >= 64 && scene.nameDoneArea().h >= 64 && scene.nameEditArea().h >= 64);
    CHECK(scene.nameKeyArea(Scene::kNameKeys - 1).y + 64 <= scene.nameDoneArea().y);  // no overlap
}

static void testSceneryCache() {
    std::printf("the cached scenery draws the same picture as drawing it all every frame\n");
    std::vector<uint16_t> a(Scene::kSize * Scene::kSize), b(a.size());
    Canvas ca(a.data(), Scene::kSize, Scene::kSize), cb(b.data(), Scene::kSize, Scene::kSize);
    struct Case { int64_t hours; bool lights; int messes; int trust; };
    const Case cases[] = {{0, true, 0, 80}, {12, true, 2, 80}, {12, false, 3, 10}, {9, true, 1, 10}, {20, false, 0, 80}};
    for (const Case& k : cases) {
        Driver d;
        d.sim.tick(k.hours * 3600);
        State& st = d.sim.mutableState();
        st.lightsOn = k.lights;
        st.messes = k.messes;
        st.trust = k.trust;
        st.calling = st.sick = st.misbehaving = false;
        d.settle();
        std::fill(a.begin(), a.end(), 0x1234);
        std::fill(b.begin(), b.end(), 0x4321);  // leftovers would show
        d.scene.setSceneryCache(true);
        d.scene.draw(ca);
        d.scene.draw(ca);  // the second frame comes from the cache
        d.scene.setSceneryCache(false);
        d.scene.draw(cb);
        int diff = 0;
        for (size_t i = 0; i < a.size(); i++) diff += a[i] != b[i];
        CHECK(diff == 0);
        if (diff) std::printf("    %d pixels differ at +%lld h\n", diff, static_cast<long long>(k.hours));
    }
}

static void testWeather() {
    std::printf("weather: Open-Meteo replies are read, changes get a message, and every sky draws\n");
    const char* reply =
        "{\"latitude\":38.8,\"current_units\":{\"time\":\"iso8601\",\"temperature_2m\":\"\u00b0C\","
        "\"weather_code\":\"wmo code\",\"wind_speed_10m\":\"km/h\"},\"current\":{\"time\":\"2026-10-18T16:00\","
        "\"interval\":900,\"temperature_2m\":-2.5,\"weather_code\":73,\"wind_speed_10m\":14.2}}";
    Weather w;
    CHECK(parseOpenMeteo(reply, w));
    CHECK(w.sky == Sky::Snow);
    CHECK(w.tempC < -2.4f && w.tempC > -2.6f);
    CHECK(w.windKmh > 14 && w.windKmh < 14.5f);
    CHECK(!parseOpenMeteo("{\"error\":true,\"reason\":\"nope\"}", w));
    CHECK(!parseOpenMeteo("", w));
    CHECK(skyFromWmo(0) == Sky::Clear && skyFromWmo(3) == Sky::Cloudy && skyFromWmo(45) == Sky::Fog);
    CHECK(skyFromWmo(53) == Sky::Drizzle && skyFromWmo(81) == Sky::Rain && skyFromWmo(86) == Sky::Snow);
    CHECK(skyFromWmo(95) == Sky::Storm);

    Driver d;
    for (int i = 0; i < 5000 && d.scene.toast(); i++) d.step();  // let earlier messages go
    Weather clear;
    clear.sky = Sky::Clear;
    d.scene.setWeather(clear);
    d.step();
    CHECK(!d.scene.toast());  // a plain first report isn't news
    w.sky = Sky::Rain;
    d.scene.setWeather(w);
    d.step();
    CHECK(d.scene.toast() && std::strcmp(d.scene.toast(), "RAIN IN THE FOREST") == 0);

    std::vector<uint16_t> a(Scene::kSize * Scene::kSize), b(a.size());
    Canvas ca(a.data(), Scene::kSize, Scene::kSize), cb(b.data(), Scene::kSize, Scene::kSize);
    Rgb sky[8];
    for (int k = 1; k <= static_cast<int>(Sky::Storm); k++) {
        Weather x;
        x.sky = static_cast<Sky>(k);
        x.windKmh = 20;
        d.scene.setWeather(x);
        d.scene.setSceneryCache(true);
        d.scene.draw(ca);
        d.scene.setSceneryCache(false);
        d.scene.draw(cb);
        int diff = 0;
        for (size_t i = 0; i < a.size(); i++) diff += a[i] != b[i];
        CHECK(diff == 0);
        sky[k] = ca.pixel(240, 140);
    }
    CHECK(sky[1] != sky[7]);  // a storm sky isn't a clear one
    d.scene.setFahrenheit(true);
    d.scene.setSceneryCache(true);
}

static void testHalloween() {
    std::printf("seasons: autumn leaves, Halloween's pumpkin says BOO, and the hat comes out the last week\n");
    Driver d;
    for (int i = 0; i < 5000 && d.scene.toast(); i++) d.step();
    d.scene.previewDate(7, 4);
    CHECK(!d.scene.autumn() && !d.scene.halloween());
    d.scene.previewDate(9, 25);
    CHECK(d.scene.autumn() && !d.scene.halloween());
    d.scene.previewDate(10, 20);
    CHECK(d.scene.halloween() && !d.scene.hatWeek());
    d.scene.previewDate(10, 28);
    CHECK(d.scene.halloween() && d.scene.hatWeek());
    d.scene.previewDate(11, 1);
    CHECK(d.scene.autumn() && !d.scene.halloween());

    CHECK(d.scene.pumpkinArea().w >= 64 && d.scene.pumpkinArea().h >= 64);
    Cue c;
    while (d.scene.takeCue(c)) {}
    d.scene.previewDate(7, 4);
    d.tap(d.scene.pumpkinArea());
    CHECK(!d.scene.toast() || std::strcmp(d.scene.toast(), "BOO!") != 0);  // no pumpkin in July
    d.scene.previewDate(10, 20);
    d.tap(d.scene.pumpkinArea());
    CHECK(d.scene.toast() && std::strcmp(d.scene.toast(), "BOO!") == 0);
    bool boo = false;
    while (d.scene.takeCue(c)) boo |= c == Cue::Boo;
    CHECK(boo);

    // The real date: Halloween 2027 starts at 1824940800 (UTC midnight, Oct 31).
    Sim sim;
    sim.newGame(1824940800 + 20 * 3600, 0, 3);
    Scene scene(sim);
    CHECK(scene.halloween() && scene.hatWeek());

    std::vector<uint16_t> a(Scene::kSize * Scene::kSize), b(a.size());
    Canvas ca(a.data(), Scene::kSize, Scene::kSize), cb(b.data(), Scene::kSize, Scene::kSize);
    d.scene.previewDate(10, 31);
    d.scene.setSceneryCache(true);
    d.scene.draw(ca);
    d.scene.setSceneryCache(false);
    d.scene.draw(cb);
    int diff = 0;
    for (size_t i = 0; i < a.size(); i++) diff += a[i] != b[i];
    CHECK(diff == 0);
    d.scene.setSceneryCache(true);
}

static void testGames() {
    std::printf("games: the picker, Hide and Seek, Berry Catch, Blurry Photo, Quick Catch, and quitting\n");
    auto big = [](const Scene::Rect& r) { return r.w >= 64 && r.h >= 64; };
    auto openPicker = [](Driver& d) {
        d.openTray();
        d.pressSlot(Scene::Slot::Play);
    };
    {
        Driver d;
        openPicker(d);
        CHECK(d.scene.overlay() == Scene::Overlay::Game && d.scene.game() == Scene::Game::Pick);
        CHECK(!d.scene.trayOpen());
        for (int i = 0; i < Scene::kGameChoices; i++) CHECK(big(d.scene.gameChoiceArea(i)));
        CHECK(big(d.scene.gameQuitArea()) && big(d.scene.snapArea()));
        for (int i = 0; i < Scene::kSeekSpots; i++) CHECK(big(d.scene.seekSpotArea(i)));
        std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
        Canvas c(fb.data(), Scene::kSize, Scene::kSize);
        d.scene.draw(c);

        // Hide and Seek: one wrong guess, then the right one.
        d.tap(d.scene.gameChoiceArea(0));
        CHECK(d.scene.game() == Scene::Game::Seek);
        d.scene.draw(c);
        const uint32_t prints = d.sim.state().footprints;
        const int answer = d.scene.seekAnswer();
        d.tap(d.scene.seekSpotArea((answer + 1) % Scene::kSeekSpots));
        CHECK(!d.scene.gameOver());
        d.tap(d.scene.seekSpotArea(answer));
        CHECK(d.scene.gameOver() && d.scene.gameWon());
        d.scene.draw(c);
        for (int i = 0; i < 200; i++) d.step();
        CHECK(d.scene.overlay() == Scene::Overlay::None);
        CHECK(d.sim.state().footprints == prints + 5);
    }
    {
        Driver d;  // Hide and Seek, three wrong guesses
        openPicker(d);
        d.tap(d.scene.gameChoiceArea(0));
        const int answer = d.scene.seekAnswer();
        const uint32_t prints = d.sim.state().footprints;
        for (int k = 1; k <= 3; k++) d.tap(d.scene.seekSpotArea((answer + k) % Scene::kSeekSpots));
        CHECK(d.scene.gameOver() && !d.scene.gameWon());
        d.tap(240, 240);  // a tap moves on
        CHECK(d.scene.overlay() == Scene::Overlay::None);
        CHECK(d.sim.state().footprints == prints + 2);  // playing still counts for something
    }
    {
        Driver d;  // Berry Catch: the basket follows the finger; nobody catching loses
        openPicker(d);
        d.tap(d.scene.gameChoiceArea(1));
        CHECK(d.scene.game() == Scene::Game::Berry);
        d.send(TouchPhase::Down, 0, 330, 300);
        CHECK(d.scene.basketX() == 330);
        d.send(TouchPhase::Move, 0, 20, 300);
        CHECK(d.scene.basketX() == 60);  // kept on screen
        d.send(TouchPhase::Up, 0, 20, 300);
        std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
        Canvas c(fb.data(), Scene::kSize, Scene::kSize);
        for (int i = 0; i < 26000 / 16; i++) {
            d.step();
            if (i == 300) d.scene.draw(c);
        }
        CHECK(d.scene.gameOver());
        CHECK(d.scene.gameScore() < Scene::kBerriesToWin);
        CHECK(!d.scene.gameWon());
    }
    {
        Driver d;  // Blurry Photo: snap while it's in the frame
        openPicker(d);
        d.tap(d.scene.gameChoiceArea(2));
        CHECK(d.scene.game() == Scene::Game::Photo);
        const int photos = d.sim.photoCount();
        int i = 0;
        while (!d.scene.petInFrame() && i++ < 1000) d.step();
        CHECK(d.scene.petInFrame());
        std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
        Canvas c(fb.data(), Scene::kSize, Scene::kSize);
        d.scene.draw(c);
        d.send(TouchPhase::Down, 0, d.scene.snapArea().cx(), d.scene.snapArea().cy());
        d.step(40);
        d.send(TouchPhase::Up, 0, d.scene.snapArea().cx(), d.scene.snapArea().cy());
        d.step();
        CHECK(d.scene.gameOver() && d.scene.gameWon());
        for (int k = 0; k < 200; k++) d.step();
        CHECK(d.sim.photoCount() == photos + 1 || photos == Sim::kPhotoCount);
    }
    {
        Driver d;  // Quick Catch is the old game; Quit gives nothing
        openPicker(d);
        d.tap(d.scene.gameChoiceArea(3));
        CHECK(d.scene.overlay() == Scene::Overlay::None);
        CHECK(d.scene.toast() && std::strstr(d.scene.toast(), "CATCH") != nullptr);
        openPicker(d);
        d.tap(d.scene.gameChoiceArea(0));
        const uint32_t prints = d.sim.state().footprints;
        d.tap(d.scene.gameQuitArea());
        CHECK(d.scene.overlay() == Scene::Overlay::None && d.sim.state().footprints == prints);
    }
    {
        Driver d;  // a shy pet won't play yet
        d.sim.mutableState().trust = 10;
        d.settle();
        openPicker(d);
        CHECK(d.scene.overlay() == Scene::Overlay::None);
        CHECK(d.scene.toast() && std::strstr(d.scene.toast(), "TOO SHY") != nullptr);
    }
}

static void testSharperScale() {
    std::printf("drawing at 2x and 3x gives the same picture, just sharper (the phone app)\n");
    Driver d;
    std::vector<uint16_t> one(Scene::kSize * Scene::kSize);
    Canvas c1(one.data(), Scene::kSize, Scene::kSize);
    d.scene.draw(c1);
    for (int k = 2; k <= 3; k++) {
        const int px = Scene::kSize * k;
        std::vector<uint16_t> big(static_cast<size_t>(px) * px), plain(big.size());
        Canvas ck(big.data(), px, px, k), cp(plain.data(), px, px, k);
        CHECK(ck.width() == Scene::kSize && ck.height() == Scene::kSize && ck.scale() == k);
        d.scene.setSceneryCache(true);
        d.scene.draw(ck);
        d.scene.draw(ck);  // from the cache, at this size
        d.scene.setSceneryCache(false);
        d.scene.draw(cp);
        d.scene.setSceneryCache(true);
        int diff = 0;
        for (size_t i = 0; i < big.size(); i++) diff += big[i] != plain[i];
        CHECK(diff == 0);
        // Flat areas match the 1x picture: sky, grass, the status strip.
        const int pts[][2] = {{240, 120}, {60, 420}, {240, 20}, {300, 400}};
        for (auto& p : pts) {
            Rgb a = c1.pixel(p[0], p[1]), b = ck.pixel(p[0], p[1]);
            int dr = static_cast<int>((a >> 16) & 255) - static_cast<int>((b >> 16) & 255);
            int dg = static_cast<int>((a >> 8) & 255) - static_cast<int>((b >> 8) & 255);
            CHECK(dr * dr + dg * dg < 400);
        }
    }
}

static void testWrist() {
    std::printf("the watch: buttons for what it needs, actions that work, and a forecast of the next nudge\n");
    Driver d;
    State& s = d.sim.mutableState();
    s.hunger = 0.8f;
    s.calling = true;
    s.messes = 1;
    Wrist w(d.sim);
    w.update(16);
    CHECK(std::strcmp(w.status(), "CALLING YOU!") == 0);
    CHECK(w.buttonCount() == 3);
    CHECK(w.buttonAction(0) == Wrist::Action::Answer);
    for (int i = 0; i < w.buttonCount(); i++) {
        Scene::Rect r = w.buttonArea(i);
        CHECK(r.w >= 64 && r.h >= 64);
        const float dx = r.cx() - 240.0f, dy = r.y + r.h - 240.0f;
        CHECK(dx * dx + dy * dy < 236.0f * 236.0f);  // inside the round screen
    }
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    w.draw(c);
    CHECK(Wrist::needNow(d.sim.state()) != nullptr);
    CHECK(w.tap(w.buttonArea(0).cx(), w.buttonArea(0).cy()) == Wrist::Action::Answer);
    CHECK(!d.sim.state().calling);
    CHECK(w.message() != nullptr);
    // Feed until full: the button keeps working, then says so.
    int fed = 0;
    for (int k = 0; k < 8; k++) {
        for (int i = 0; i < w.buttonCount(); i++) {
            if (w.buttonAction(i) != Wrist::Action::Feed) continue;
            fed += w.tap(w.buttonArea(i).cx(), w.buttonArea(i).cy()) == Wrist::Action::Feed;
            break;
        }
    }
    CHECK(fed >= 2);
    CHECK(d.sim.state().hunger >= 3);
    CHECK(w.tap(5, 5) == Wrist::Action::None);  // not a button

    // Asleep: no buttons. Away: leave food out.
    s.asleep = true;
    CHECK(w.buttonCount() == 0);
    s.asleep = false;
    s.stage = Stage::Away;
    CHECK(w.buttonCount() == 1 && w.buttonAction(0) == Wrist::Action::LeaveFood);
    s.stage = Stage::Baby;

    // The forecast: well cared for now, so a while before it needs you; on a copy only.
    s.hunger = s.happiness = 4;
    s.messes = 0;
    s.calling = s.sick = false;
    const State before = d.sim.state();
    const char* why = nullptr;
    const int64_t secs = Wrist::nextNeed(d.sim.state(), 2 * 86400, &why);
    CHECK(secs > 0 && why != nullptr);
    CHECK(d.sim.state().now == before.now && d.sim.state().hunger == before.hunger);
    // And it's right: tick the real pet that long and it does need you (unless it fell asleep).
    d.sim.tick(secs);
    CHECK(Wrist::needNow(d.sim.state()) != nullptr || d.sim.state().asleep);
}

static void testStartOver() {
    std::printf("start over: two taps from the settings, keeping the collection or not; cancel keeps the pet\n");
    auto toSettings = [](Driver& d) {
        d.openTray();
        d.pressSlot(Scene::Slot::More);
        d.pressSlot(Scene::Slot::Settings);
    };
    Driver d;
    State& s = d.sim.mutableState();
    s.photos = 0b1011;
    s.footprints = 42;
    s.generation = 3;
    const uint32_t genes = d.sim.state().genome.g[0] | d.sim.state().genome.g[1] << 8;
    toSettings(d);
    d.tap(d.scene.startOverArea());
    CHECK(d.scene.overlay() == Scene::Overlay::StartOver);
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    d.scene.draw(c);
    d.tap(d.scene.startCancelArea());
    CHECK(d.scene.overlay() == Scene::Overlay::Settings);
    CHECK(d.sim.state().stage == Stage::Baby && std::strcmp(d.sim.state().name, "TESTY") == 0);

    d.tap(d.scene.startOverArea());
    d.tap(d.scene.startKeepArea());
    CHECK(d.scene.overlay() == Scene::Overlay::None);
    CHECK(d.sim.state().stage == Stage::Egg && d.sim.state().name[0] == '\0');
    CHECK(d.sim.state().generation == 1);
    CHECK(d.sim.state().photos == 0b1011 && d.sim.state().footprints == 42);
    CHECK(static_cast<uint32_t>(d.sim.state().genome.g[0] | d.sim.state().genome.g[1] << 8) != genes);  // fresh DNA
    CHECK(d.scene.takeSaveRequest());
    d.step();
    CHECK(d.scene.toast() && std::strstr(d.scene.toast(), "NEW EGG") != nullptr);

    // Wipe everything: the collection goes too. And the new egg hatches and asks for a name.
    d.sim.mutableState().footprints = 9;
    toSettings(d);
    d.tap(d.scene.startOverArea());
    d.tap(d.scene.startWipeArea());
    CHECK(d.sim.state().photos == 0 && d.sim.state().footprints == 0);
    d.sim.tick(d.sim.tuning().eggEnd + 1);
    for (int i = 0; i < 250; i++) d.step();
    CHECK(d.scene.overlay() == Scene::Overlay::Name);
}

static void testDetail() {
    std::printf("the phone's detailed drawing: richer, cached the same, and off again gives the plain picture\n");
    std::vector<uint16_t> plain(Scene::kSize * Scene::kSize), rich(plain.size()), uncached(plain.size());
    Canvas cp(plain.data(), Scene::kSize, Scene::kSize), cr(rich.data(), Scene::kSize, Scene::kSize),
        cu(uncached.data(), Scene::kSize, Scene::kSize);
    for (int64_t hours : {12, 20, 30}) {
        Driver d;
        d.sim.tick(hours * 3600);
        d.settle();
        CHECK(!d.scene.detail());
        d.scene.draw(cp);
        d.scene.setDetail(true);
        CHECK(d.scene.detail());
        d.scene.draw(cr);
        d.scene.draw(cr);  // from the cache, which was redrawn when detail came on
        d.scene.setSceneryCache(false);
        d.scene.draw(cu);
        int changed = 0, cacheDiff = 0;
        for (size_t i = 0; i < plain.size(); i++) {
            changed += plain[i] != rich[i];
            cacheDiff += rich[i] != uncached[i];
        }
        CHECK(changed > 20000);  // grass, boughs, fur: a good part of the screen
        CHECK(cacheDiff == 0);
        // The tray and the status strip aren't touched.
        CHECK(cp.pixel(240, 20) == cr.pixel(240, 20));
        CHECK(cp.pixel(240, 470) == cr.pixel(240, 470));
        // Turning it off again gives the plain picture back.
        d.scene.setDetail(false);
        d.scene.setSceneryCache(true);
        d.scene.draw(cu);
        int back = 0;
        for (size_t i = 0; i < plain.size(); i++) back += plain[i] != uncached[i];
        CHECK(back == 0);
    }
}

static bool says(Driver& d, const char* what) {
    const char* b = d.scene.bubble();
    return b && std::strcmp(b, what) == 0;
}

static void testPlayAndTalk() {
    std::printf("play: tickle, pat and poke it, shake a tree, fetch a pinecone, light the fire; it talks back\n");
    {
        Driver d;
        for (int i = 0; i < 80; i++) d.step();
        CHECK(d.scene.bubble() != nullptr);  // a hello (it's hungry, so it says so)
        CHECK(says(d, "SO HUNGRY..."));
        for (int i = 0; i < 200; i++) d.step();  // let that one go
        const Scene::Rect r = d.scene.squatchArea();
        d.tap(r.cx(), r.y + r.h * 3 / 4);  // the belly
        CHECK(says(d, "HEE HEE!"));
        d.tap(r.cx(), r.y + r.h * 3 / 4);
        d.tap(r.cx(), r.y + r.h * 3 / 4);
        CHECK(says(d, "HAHAHA! STOP!"));
        d.tap(r.cx(), r.y + 6);  // the top of its head
        CHECK(says(d, "MMM..."));
        for (int i = 0; i < 3; i++) d.tap(r.cx(), r.y + 6);
        CHECK(says(d, "HEY! GENTLY!"));  // seven pokes in a few seconds
        const float happy = d.sim.state().happiness;

        // Shake a tree until a pinecone falls; it runs after it and brings it back.
        for (int i = 0; i < 3 && !d.scene.coneOnGround() && !d.scene.fetching(); i++) {
            d.tap(d.scene.treeArea(1));
            for (int k = 0; k < 120; k++) d.step();
        }
        bool fetched = false, back = false;
        for (int k = 0; k < 1200 && !back; k++) {
            d.step();
            fetched = fetched || d.scene.fetching();
            back = fetched && d.scene.coneOnGround();
        }
        CHECK(fetched && back);
        CHECK(says(d, "AGAIN! AGAIN!") || says(d, "THROW IT!"));
        CHECK(d.sim.state().happiness > happy);  // it counts as a cuddle

        // Tap it to toss it (no flick needed): off it goes again.
        for (int k = 0; k < 60; k++) d.step();
        d.tap(d.scene.coneArea());
        CHECK(!d.scene.coneOnGround());
        fetched = false;
        for (int k = 0; k < 300 && !fetched; k++) {
            d.step();
            fetched = d.scene.fetching();
        }
        CHECK(fetched);
    }
    {
        Driver d;  // the campfire, any time of day, but not in the rain
        CHECK(!d.scene.fireLit());
        d.tap(d.scene.fireArea());
        CHECK(d.scene.fireLit());
    }
    {
        Driver d;
        Weather w;
        w.sky = Sky::Rain;
        d.scene.setWeather(w);
        d.settle();
        d.tap(d.scene.fireArea());
        CHECK(!d.scene.fireLit());
    }
    {
        Driver d;  // it chats now and then, on its own
        State& s = d.sim.mutableState();
        s.hunger = s.happiness = 4;
        int heard = 0;
        const char* last = nullptr;
        for (int k = 0; k < 120 * 60; k++) {  // two minutes
            d.step();
            const char* b = d.scene.bubble();
            if (b && b != last) heard++;
            last = b;
        }
        CHECK(heard >= 2);  // the hello and at least one more
    }
    {
        Driver d;  // asleep: no hello, and its pinecone waits
        d.sim.mutableState().asleep = true;
        for (int i = 0; i < 100; i++) d.step();
        CHECK(d.scene.bubble() == nullptr);
    }
}

static void testVoicePitch() {
    std::printf("a young pet's voice is higher; buttons and weather aren't\n");
    Driver d;
    d.sim.mutableState().stage = Stage::Baby;
    const float baby = d.scene.voicePitch(Cue::Neigh);
    d.sim.mutableState().stage = Stage::Adult;
    const float adult = d.scene.voicePitch(Cue::Neigh);
    d.sim.mutableState().stage = Stage::Elder;
    const float elder = d.scene.voicePitch(Cue::Neigh);
    CHECK(baby > adult && adult > elder);
    CHECK(d.scene.voicePitch(Cue::Tick) == 1 && d.scene.voicePitch(Cue::Thunder) == 1);
    // Played higher, a recording also finishes sooner.
    auto samplesOf = [](float pitch) {
        Synth s;
        s.play(Cue::Neigh, 3, pitch);
        int16_t buf[256];
        int n = 0;
        while (s.render(buf, 256)) n += 256;
        return n;
    };
    CHECK(samplesOf(1.8f) < samplesOf(1.0f) * 0.7f);
}

static void testFoalBirth() {
    std::printf("horse edition: the foal is born beside its mother, then goes off to hide\n");
    Sim sim;
    sim.newGame(1798761600 + 10 * 3600, 0, 7);
    Scene scene(sim);
    scene.setSpecies(Scene::Species::Horse);
    uint32_t ms = 0;
    auto step = [&](int n) {
        for (int i = 0; i < n; i++) {
            ms += 16;
            scene.update(ms);
        }
    };
    step(100);
    CHECK(sim.state().stage == Stage::Egg);
    CHECK(scene.squatchArea().w >= 64 && scene.squatchArea().h >= 64);  // tapping the mother
    while (sim.state().stage == Stage::Egg) {
        sim.tick(5);
        step(1);
    }
    step(30);
    CHECK(scene.squatchArea().cx() > 150);  // at its mother's side first
    step(400);
    CHECK(scene.squatchArea().cx() < 120);  // then off behind the tree
}

static void testHorseGames() {
    std::printf("horse games: five big choices\n");
    auto openPicker = [](Driver& d) {
        d.openTray();
        d.pressSlot(Scene::Slot::Play);
    };
    Driver d;
    d.scene.setSpecies(Scene::Species::Horse);
    openPicker(d);
    CHECK(d.scene.game() == Scene::Game::Pick && d.scene.gameChoiceCount() == 5);
    for (int i = 0; i < 5; i++) {
        const Scene::Rect r = d.scene.gameChoiceArea(i);
        CHECK(r.w >= 64 && r.h >= 64 && r.y >= 100 && r.y + r.h <= 436);
    }
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    d.scene.draw(c);
    d.tap(240, 30);  // outside the panel closes it
    CHECK(d.scene.game() == Scene::Game::None);
    // Its own Hide and Seek reveal draws.
    for (int i = 0; i < 200; i++) d.step();
    openPicker(d);
    d.tap(d.scene.gameChoiceArea(0));
    CHECK(d.scene.game() == Scene::Game::Seek);
    for (int k = 0; k < 3 && !d.scene.gameOver(); k++) d.tap(d.scene.seekSpotArea((d.scene.seekAnswer() + 1 + k) % Scene::kSeekSpots));
    CHECK(d.scene.gameOver());
    d.scene.draw(c);
}

static void testGrooming() {
    std::printf("grooming: brush, hooves, mane, ribbon; taps alone are enough, and there's no clock\n");
    Driver d;
    d.scene.setSpecies(Scene::Species::Horse);
    d.openTray();
    d.pressSlot(Scene::Slot::Play);
    d.tap(d.scene.gameChoiceArea(2));
    CHECK(d.scene.game() == Scene::Game::Groom);
    CHECK(d.scene.groomStep() == Scene::GroomStep::Brush);
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    d.scene.draw(c);  // the spots go where the horse is drawn
    // Wait a long while first: nothing runs out.
    for (int i = 0; i < 2000; i++) d.step();
    CHECK(!d.scene.gameOver());
    // Brush: a stroke across the first spot cleans part of it; taps on each finish the rest.
    float x, y;
    CHECK(d.scene.groomSpot(0, x, y));
    d.drag(static_cast<int>(x) - 30, static_cast<int>(y), static_cast<int>(x) + 30, static_cast<int>(y), 10, 30);
    for (int round = 0; round < 6 && d.scene.groomStep() == Scene::GroomStep::Brush; round++) {
        for (int i = 0; i < Scene::kMudSpots; i++) {
            if (!d.scene.groomSpot(i, x, y)) continue;
            d.tap(static_cast<int>(x), static_cast<int>(y));
            d.step(700);
            d.scene.draw(c);
        }
    }
    CHECK(d.scene.groomStep() == Scene::GroomStep::Hooves);
    // Hooves: four of them, each with its stones; every stone is a big target.
    for (int hoof = 0; hoof < 4; hoof++) {
        for (int i = 0; i < Scene::kPebbles; i++) {
            const Scene::Rect r = d.scene.pebbleArea(i);
            CHECK(r.w >= 64 && r.h >= 64);
            d.tap(r);
            d.step(700);
        }
        d.scene.draw(c);
    }
    CHECK(d.scene.groomStep() == Scene::GroomStep::Mane);
    for (int round = 0; round < 6 && d.scene.groomStep() == Scene::GroomStep::Mane; round++) {
        d.scene.draw(c);
        for (int i = 0; i < Scene::kTangles; i++) {
            if (!d.scene.groomSpot(i, x, y)) continue;
            d.tap(static_cast<int>(x), static_cast<int>(y));
            d.step(700);
        }
    }
    CHECK(d.scene.groomStep() == Scene::GroomStep::Ribbon);
    d.scene.draw(c);
    for (int i = 0; i < Scene::kRibbons; i++) CHECK(d.scene.ribbonArea(i).w >= 64 && d.scene.ribbonArea(i).h >= 64);
    const float before = d.sim.state().happiness;
    d.tap(d.scene.ribbonArea(2));
    CHECK(d.scene.gameOver() && d.scene.gameWon());
    for (int i = 0; i < 300; i++) d.step();
    CHECK(d.scene.game() == Scene::Game::None);
    CHECK(d.sim.state().happiness > before);
    d.scene.draw(c);  // wearing its ribbon
}

static void testUpdateRow() {
    std::printf("Wi-Fi page: the version, and a big row that asks the board to check for an update\n");
    Driver d;
    d.scene.setWifiAvailable("POCKET-PONY");
    d.scene.setWifiStatus(Scene::WifiStatus::Joined, "HOME");
    d.openTray();
    d.pressSlot(Scene::Slot::More);
    d.pressSlot(Scene::Slot::Settings);
    d.tap(d.scene.wifiButtonArea());
    CHECK(d.scene.overlay() == Scene::Overlay::Wifi);
    d.tap(d.scene.updateArea());  // no updates on this front end: nothing happens
    CHECK(d.scene.takeWifiRequest() == 0);
    d.scene.setUpdateStatus(Scene::UpdateStatus::Idle, 412, 0);
    const Scene::Rect u = d.scene.updateArea();
    CHECK(u.w >= 64 && u.h >= 64);
    auto apart = [](const Scene::Rect& a, const Scene::Rect& b) {
        return a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
    };
    CHECK(apart(u, d.scene.wifiSetupArea()) && apart(u, d.scene.wifiDoneArea()));
    d.tap(u);
    CHECK(d.scene.takeWifiRequest() == 3);
    d.tap(u);  // already checking: once is enough
    CHECK(d.scene.takeWifiRequest() == 0);
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    d.scene.setUpdateStatus(Scene::UpdateStatus::Downloading, 412, 50);
    d.scene.draw(c);
}

static void testWifiPage() {
    std::printf("Wi-Fi page: only on boards; Set up asks for the hotspot; the screen stays on meanwhile\n");
    Driver d;
    CHECK(d.scene.wifiButtonArea().w == 0);  // the phone app has no Wi-Fi button
    d.scene.setWifiAvailable("POCKET-SQUATCH");
    d.scene.setWifiStatus(Scene::WifiStatus::NotSet, "");
    d.openTray();
    d.pressSlot(Scene::Slot::More);
    d.pressSlot(Scene::Slot::Settings);
    CHECK(d.scene.overlay() == Scene::Overlay::Settings);
    for (const Scene::Rect& r : {d.scene.wifiButtonArea(), d.scene.startOverArea(), d.scene.settingsDoneArea(),
                                 d.scene.wifiSetupArea(), d.scene.wifiDoneArea()})
        CHECK(r.w >= 64 && r.h >= 64);
    auto apart = [](const Scene::Rect& a, const Scene::Rect& b) {
        return a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
    };
    CHECK(apart(d.scene.wifiButtonArea(), d.scene.startOverArea()) &&
          apart(d.scene.wifiButtonArea(), d.scene.settingsDoneArea()));
    d.tap(d.scene.wifiButtonArea());
    CHECK(d.scene.overlay() == Scene::Overlay::Wifi);
    d.tap(d.scene.wifiSetupArea());
    CHECK(d.scene.takeWifiRequest() == 1 && d.scene.takeWifiRequest() == 0);
    d.scene.setWifiStatus(Scene::WifiStatus::SetupRunning, "");
    for (int i = 0; i < 20000 / 16; i++) d.step();  // longer than the screen timeout
    CHECK(!d.scene.screenOff());
    d.tap(d.scene.wifiSetupArea());  // now Cancel
    CHECK(d.scene.takeWifiRequest() == 2);
    d.scene.setWifiStatus(Scene::WifiStatus::Joined, "Endor");
    d.tap(d.scene.wifiDoneArea());
    CHECK(d.scene.overlay() == Scene::Overlay::Settings);
}

static void testTownLookup() {
    std::printf("your town: the geocoding reply, time zone rules, and the weather's offset\n");
    const char* reply =
        "{\"results\":[{\"id\":5417598,\"name\":\"Colorado Springs\",\"latitude\":38.83388,\"longitude\":-104.82136,"
        "\"elevation\":1839.0,\"feature_code\":\"PPLA2\",\"country_code\":\"US\",\"admin1_id\":5417618,"
        "\"timezone\":\"America/Denver\",\"population\":456568,\"postcodes\":[\"80901\",\"80902\"],"
        "\"country_id\":6252001,\"country\":\"United States\",\"admin1\":\"Colorado\",\"admin2\":\"El Paso\"}],"
        "\"generationtime_ms\":0.6}";
    Place p;
    CHECK(parseGeocode(reply, p));
    CHECK(std::strcmp(p.name, "COLORADO SPRINGS, COLORADO") == 0);
    CHECK(std::fabs(p.lat - 38.83388f) < 0.001f && std::fabs(p.lon + 104.82136f) < 0.001f);
    CHECK(std::strcmp(p.zone, "America/Denver") == 0);
    CHECK(std::strcmp(posixZone(p.zone), "MST7MDT,M3.2.0,M11.1.0") == 0);
    CHECK(!parseGeocode("{\"generationtime_ms\":0.3}", p));  // nothing found
    CHECK(posixZone("Asia/Kathmandu") == nullptr);           // not in the list: the weather's offset instead
    char tz[16];
    fixedZone(20700, tz, sizeof tz);
    CHECK(std::strcmp(tz, "UTC-5:45") == 0);
    fixedZone(-25200, tz, sizeof tz);
    CHECK(std::strcmp(tz, "UTC7") == 0);
    Weather w;
    CHECK(parseOpenMeteo("{\"utc_offset_seconds\":-25200,\"current\":{\"temperature_2m\":5.1,\"weather_code\":3}}", w));
    CHECK(w.utcOffsetSecs == -25200);
    CHECK(parseOpenMeteo("{\"current\":{\"weather_code\":0}}", w) && w.utcOffsetSecs == Weather::kNoOffset);
}

static void testBigText() {
    std::printf("big text (the small AMOLED board): pages still work, and their buttons stay big and apart\n");
    auto apart = [](const Scene::Rect& a, const Scene::Rect& b) {
        return a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
    };
    auto big = [](const Scene::Rect& r) { return r.w >= 64 && r.h >= 64; };
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    for (int horse = 0; horse < 2; horse++) {
        Driver d;
        if (horse) d.scene.setSpecies(Scene::Species::Horse);
        d.scene.setBigText(true);
        CHECK(d.scene.bigText());
        CHECK(big(d.scene.nameEditArea()) && big(d.scene.homeArea()));
        CHECK(big(d.scene.startKeepArea()) && big(d.scene.startWipeArea()) && big(d.scene.startCancelArea()));
        CHECK(apart(d.scene.startKeepArea(), d.scene.startWipeArea()) && apart(d.scene.startWipeArea(), d.scene.startCancelArea()));
        if (horse) CHECK(big(d.scene.glassesArea()) && apart(d.scene.glassesArea(), d.scene.nameEditArea()));
        for (int i = 0; i < d.scene.gameChoiceCount(); i++) CHECK(big(d.scene.gameChoiceArea(i)));

        // The info card: the name opens naming, and (horse) the glasses button still works.
        d.tap(240, 20);
        CHECK(d.scene.overlay() == Scene::Overlay::Stats);
        d.scene.draw(c);
        if (horse) {
            d.tap(d.scene.glassesArea());
            CHECK(d.scene.settings().glasses == 1);
        }
        d.tap(d.scene.nameEditArea());
        CHECK(d.scene.overlay() == Scene::Overlay::Name);
        d.scene.draw(c);

        // Every page and every Wi-Fi state draws, and a long line is wrapped rather than cut.
        Driver w;
        if (horse) w.scene.setSpecies(Scene::Species::Horse);
        w.scene.setBigText(true);
        w.scene.setWifiAvailable("POCKET-PONY");
        w.scene.setBackupCode("K7QM-2XHT-9BFA-WP4D");
        w.scene.setUpdateStatus(Scene::UpdateStatus::Idle, 412, 0);
        w.openTray();
        w.pressSlot(Scene::Slot::More);
        w.pressSlot(Scene::Slot::Settings);
        w.scene.draw(c);
        w.tap(w.scene.wifiButtonArea());
        CHECK(w.scene.overlay() == Scene::Overlay::Wifi);
        const Scene::WifiStatus states[] = {Scene::WifiStatus::NotSet, Scene::WifiStatus::Saved, Scene::WifiStatus::Joined,
                                            Scene::WifiStatus::SetupRunning, Scene::WifiStatus::Checking, Scene::WifiStatus::Failed};
        for (Scene::WifiStatus st : states) {
            w.scene.setWifiStatus(st, "A VERY LONG NETWORK NAME INDEED");
            w.scene.setWifiTown("SOMEWHERE WITH A LONG NAME, A STATE");
            w.scene.draw(c);
        }
        w.scene.setWifiStatus(Scene::WifiStatus::Joined, "HOME");
        CHECK(big(w.scene.updateArea()) && apart(w.scene.updateArea(), w.scene.wifiSetupArea()));
        w.tap(w.scene.updateArea());
        CHECK(w.scene.takeWifiRequest() == 3);
    }
}

static void testWelcome() {
    std::printf("first start: a welcome page starts the setup hotspot and shows its QR code; LATER puts it off\n");
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    for (int big = 0; big < 2; big++) {
        Driver d;
        d.scene.setSpecies(Scene::Species::Horse);
        d.scene.setBigText(big);
        d.scene.openWelcome();  // nothing happens on a front end without Wi-Fi
        CHECK(!d.scene.welcoming() && d.scene.takeWifiRequest() == 0);
        d.scene.setWifiAvailable("POCKET-PONY");
        d.scene.setWifiStatus(Scene::WifiStatus::NotSet, "");
        d.scene.openWelcome();
        CHECK(d.scene.welcoming() && d.scene.overlay() == Scene::Overlay::Wifi);
        CHECK(d.scene.takeWifiRequest() == 1);  // the hotspot, for the QR code
        d.scene.draw(c);
        d.scene.setSetupPhoneJoined(true);  // then the page's address, in case it doesn't open by itself
        d.scene.draw(c);
        d.scene.setSetupPhoneJoined(false);
        d.step(200);
        CHECK(!d.scene.takeWelcomeDone());
        d.tap(d.scene.wifiDoneArea());  // LATER
        CHECK(d.scene.overlay() == Scene::Overlay::None && !d.scene.welcoming());
        CHECK(d.scene.takeWifiRequest() == 2);  // the hotspot comes down
        CHECK(d.scene.takeWelcomeDone());
        CHECK(!d.scene.takeWelcomeDone());  // once

        Driver j;  // set up from the phone: done, and the page says so
        j.scene.setWifiAvailable("POCKET-PONY");
        j.scene.setBigText(big);
        j.scene.openWelcome();
        j.scene.takeWifiRequest();
        j.scene.setWifiStatus(Scene::WifiStatus::Checking, "HOME");
        j.scene.setWifiStatus(Scene::WifiStatus::Joined, "HOME");
        CHECK(j.scene.takeWelcomeDone());
        j.scene.draw(c);
        j.tap(j.scene.wifiDoneArea());
        CHECK(j.scene.overlay() == Scene::Overlay::None);
    }
}

static void testLikeNew() {
    std::printf("like new (boards): a fourth start-over choice, confirmed by a second tap, asks the board to erase it all\n");
    auto apart = [](const Scene::Rect& a, const Scene::Rect& b) {
        return a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
    };
    std::vector<uint16_t> fb(Scene::kSize * Scene::kSize);
    Canvas c(fb.data(), Scene::kSize, Scene::kSize);
    {
        Driver d;
        CHECK(d.scene.startNewArea().w == 0);  // the PC and phone don't offer it
    }
    for (int big = 0; big < 2; big++) {
        Driver d;
        d.scene.setBigText(big);
        d.scene.setFactoryResetAvailable(true);
        const Scene::Rect k = d.scene.startKeepArea(), w = d.scene.startWipeArea(), n = d.scene.startNewArea(),
                          x = d.scene.startCancelArea();
        for (const Scene::Rect& r : {k, w, n, x}) CHECK(r.w >= 64 && r.h >= 64 && r.y + r.h <= Scene::kSize);
        CHECK(apart(k, w) && apart(w, n) && apart(n, x));
        d.openTray();
        d.pressSlot(Scene::Slot::More);
        d.pressSlot(Scene::Slot::Settings);
        d.tap(d.scene.startOverArea());
        CHECK(d.scene.overlay() == Scene::Overlay::StartOver);
        d.scene.draw(c);
        d.tap(n);  // asks first
        CHECK(!d.scene.takeFactoryReset() && d.scene.overlay() == Scene::Overlay::StartOver);
        d.scene.draw(c);
        d.tap(x);  // cancel: back to the four choices, nothing erased
        CHECK(!d.scene.takeFactoryReset() && d.scene.overlay() == Scene::Overlay::StartOver);
        d.tap(n);
        d.tap(n);  // yes
        CHECK(d.scene.takeFactoryReset());
        CHECK(!d.scene.takeFactoryReset());
    }
}

static void testMareAnswers() {
    std::printf("before the foal comes, its mother answers a touch with her own sounds\n");
    Driver d;
    d.scene.setSpecies(Scene::Species::Horse);
    d.sim.newGame(1798761600, 0, 3);
    CHECK(d.sim.state().stage == Stage::Egg);
    Cue c;
    while (d.scene.takeCue(c)) {}
    std::set<int> heard;
    for (int i = 0; i < 5; i++) {
        d.tap(d.scene.squatchArea());
        for (int k = 0; k < 70; k++) d.step();  // a second between pats
        bool voice = false;
        while (d.scene.takeCue(c)) {
            if (c == Cue::Nicker || c == Cue::Blow || c == Cue::Sigh || c == Cue::Chuckle) {
                voice = true;
                heard.insert(static_cast<int>(c));
            }
        }
        CHECK(voice);
    }
    CHECK(heard.size() >= 3);  // not the same sound every time
    CHECK(d.sim.state().stage == Stage::Egg);  // just for fun: nothing in the game changes
    CHECK(Tuning::quickToTrust().eggEnd == 7 * 60);
}

int main() {
    testBoardingStable();
    testHorseTouchVoices();
    testRudolphNose();
    testGlasses();
    testVisitors();
    testHorseChatter();
    testTap();
    testLongPressThenDrag();
    testDrag();
    testTwoFingerTap();
    testCanvas();
    testFeedByTapping();
    testFeedByDragging();
    testTraySwipes();
    testSwipeCleansMess();
    testStrokePets();
    testScold();
    testAnswerCall();
    testOverlays();
    testLeaveFoodWhileAway();
    testAsleep();
    testTouchTargets();
    testDrawsEveryState();
    testTrailMap();
    testDrawsEveryCoat();
    testBattery();
    testSettings();
    testSounds();
    testNaming();
    testSceneryCache();
    testWeather();
    testHalloween();
    testGames();
    testSharperScale();
    testWrist();
    testStartOver();
    testDetail();
    testPlayAndTalk();
    testVoicePitch();
    testFoalBirth();
    testHorseGames();
    testGrooming();
    testWifiPage();
    testUpdateRow();
    testTownLookup();
    testBigText();
    testWelcome();
    testLikeNew();
    testMareAnswers();

    std::printf("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
