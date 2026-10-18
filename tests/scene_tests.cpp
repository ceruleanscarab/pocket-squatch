// Scene and gesture tests: drive the clearing with touch events, no window needed.
#include <cstdio>
#include <cstring>
#include <vector>

#include "squatch/canvas.hpp"
#include "squatch/gesture.hpp"
#include "squatch/scene.hpp"
#include "squatch/sim.hpp"

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
    CHECK(d.scene.toast() && std::strcmp(d.scene.toast(), "IT STOPPED") == 0);
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
    for (int row = 0; row < 2; row++) {
        CHECK(big(d.scene.settingButtonArea(row, false)));
        CHECK(big(d.scene.settingButtonArea(row, true)));
    }
    CHECK(big(d.scene.settingsDoneArea()));
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

int main() {
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

    std::printf("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
