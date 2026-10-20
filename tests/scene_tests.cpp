// Scene and gesture tests: drive the clearing with touch events, no window needed.
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
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
    for (int row = 0; row < Scene::kSettingRows; row++) {
        CHECK(big(d.scene.settingButtonArea(row, false)));
        CHECK(big(d.scene.settingButtonArea(row, true)));
    }
    CHECK(big(d.scene.settingsDoneArea()));
    CHECK(big(d.scene.startOverArea()) && big(d.scene.startKeepArea()) && big(d.scene.startWipeArea()) &&
          big(d.scene.startCancelArea()));
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
    CHECK(d.scene.volume() == 2);
    while (d.scene.takeCue(c)) {}
    d.tap(d.scene.settingButtonArea(2, true));
    CHECK(d.scene.volume() == 3);
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
        CHECK(Synth::lengthMs(cue) > 0 && Synth::lengthMs(cue) < 2000);
        syn.play(cue, 3);
        int peak = 0, blocks = 0;
        while (syn.render(buf.data(), static_cast<int>(buf.size())) && blocks < 300) {
            for (int16_t v : buf) peak = std::max(peak, std::abs(static_cast<int>(v)));
            blocks++;
        }
        CHECK(blocks > 0 && blocks < 300);  // it finishes
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
    testSounds();
    testNaming();
    testSceneryCache();
    testWeather();
    testHalloween();
    testGames();
    testSharperScale();
    testWrist();
    testStartOver();

    std::printf("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
