// The forest clearing: draws the pet and turns gestures into care actions.
//
// Hardware-free like the sim: touch events and a millisecond clock come in, pixels go
// out to a Canvas. The scene never advances the sim; the front end calls Sim::tick()
// with real time, and the scene only reads the state and calls actions.
//
// Screen layout (480x480): status strip on top, the clearing, and a care tray that
// slides up from the bottom. Every touch target is at least 64 px.
#pragma once

#include <cstddef>
#include <cstdint>

#include "squatch/canvas.hpp"
#include "squatch/gesture.hpp"
#include "squatch/sim.hpp"

namespace squatch {

class Scene {
public:
    static constexpr int kSize = 480;
    static constexpr int kStripTouchH = 64;  // the strip is drawn 40 px tall but touched at 64
    static constexpr int kTrayH = 116;

    struct Rect {
        int x, y, w, h;
        bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
        int cx() const { return x + w / 2; }
        int cy() const { return y + h / 2; }
    };

    enum class Overlay : uint8_t { None, Stats, Gallery, Trails, Settings };

    // Things you can pick up from the tray and give to Squatch.
    enum class Item : uint8_t { None, Berries, Fish, Roots, Brush, Poultice, Pinecone };

    // Tray buttons. The main page holds six; Feed opens the food page and More opens
    // Photos, Trails and Settings.
    enum class Slot : uint8_t {
        Feed, Clean, Medicine, Play, Light, More, Back, Berries, Fish, Roots, Photos, Trails, Settings
    };

    // The device's settings. They belong to the toy, not the pet, so they aren't in the
    // pet's save: the front end keeps them (flash on the board, a file on the PC).
    struct Settings {
        uint8_t brightness = 3;  // a step of brightnessPercent(): 20, 40, 60, 80, 100%
        uint8_t timeout = 2;     // a step of screenTimeoutMs(): 15 s, 30 s, 1, 2, 5, 10 min, never
    };
    static constexpr int kBrightnessSteps = 5;
    static constexpr int kTimeoutSteps = 7;

    explicit Scene(Sim& sim);

    void touch(const TouchEvent& e);
    void update(uint32_t nowMs);  // animation, long presses, and the sim's events
    void draw(Canvas& c);

    // True once after anything that changed the pet, so the front end saves promptly.
    bool takeSaveRequest();
    // True once when the trail map wants a fresh Wi-Fi scan. The front end scans (or
    // pretends to, on the PC) and passes the results to Sim::scoutTrails().
    bool takeScanRequest();

    // The device's battery, from the front end (the board's power chip). percent < 0
    // means there's no battery to show (the PC, or none connected).
    void setBattery(int percent, bool charging);
    int batteryPercent() const { return batteryPct_; }

    // ---- settings and the screen timeout
    void setSettings(const Settings& s);  // e.g. as loaded at start-up; out-of-range steps are clamped
    const Settings& settings() const { return settings_; }
    bool takeSettingsChanged();           // true once after the player changes one
    int brightnessPercent() const;
    uint32_t screenTimeoutMs() const;     // 0: never
    // After the timeout with no touch the screen should go dark; the front end turns the
    // panel off and stops drawing. The touch that wakes it does nothing else, so a tap on
    // a dark screen can't feed or scold by accident. News (hatching, a knock at the door,
    // getting sick, low battery...) wakes it too.
    bool screenOff() const { return screenOff_; }
    void wake();
    void screenOffNow();  // e.g. the board's power key
    // The settings page: row 0 brightness, row 1 screen timeout, each with - and +.
    Rect settingButtonArea(int row, bool plus) const;
    Rect settingsDoneArea() const;

    // ---- layout queries, for the tests and the front end
    Rect squatchArea() const;  // where tapping or dropping reaches the pet (or egg)
    Rect stumpArea() const;    // where food is left out while it's away
    Rect messArea(int i) const;
    Rect handleArea() const;   // the tray's pull tab
    Rect footprintArea() const;
    // The trail map: one row per trail, and the button that scans again.
    Rect trailRowArea(int i) const;
    Rect rescanArea() const;
    bool scanning() const { return scanning_; }
    const char* trailNote() const { return trailNote_; }  // the map's status line
    int slotCount() const;
    Rect slotArea(int i) const;
    Slot slotAt(int i) const;
    bool trayOpen() const { return trayTarget_ > 0.5f; }
    Item held() const { return held_; }
    Overlay overlay() const { return overlay_; }
    const char* toast() const;  // the message showing now, or nullptr

private:
    enum class React : uint8_t { None, Flinch, Peek, Wave, Happy, Eat, Shake, Sneeze, Wobble, Sulk };
    enum class DragMode : uint8_t { None, Item, Stroke };
    enum class ParticleKind : uint8_t { Heart, Sparkle, Crumb, Zzz, Pinecone, Drop, Shell, PlusOne, Ring };

    struct Particle {
        ParticleKind kind;
        float x, y, vx, vy;
        uint32_t born, life;
    };

    struct Toast {
        char text[40];
    };

    struct Look;  // a pose for drawSquatch

    // ---- input
    void handle(const Gesture& g);
    void onTap(int x, int y);
    void onDragStart(const Gesture& g);
    void onDragMove(const Gesture& g);
    void onDragEnd(const Gesture& g);
    void pressSlot(Slot s);
    bool useItem(Item it, int x, int y);  // false when nothing there takes it

    // ---- actions, with feedback
    void doFeed(Food f);
    void doClean(float x, float y);
    void doBrush();
    void doMedicine();
    void doPlay();
    void doPet();
    void doScold();
    void doAnswerCall();
    void doLeaveFood();
    void doForage(int trail);
    void openTrails();
    void changeSetting(int row, int by);
    void requestScan();
    void note(const char* text);
    void tapPet();
    void sayNotNow();
    void saved(Result r);

    // ---- animation
    void handleEvent(const Event& e);
    void react(React r, uint32_t ms);
    void say(const char* text, bool now = false);  // now: replace whatever is showing
    void spawn(ParticleKind k, float x, float y, float vx, float vy, uint32_t life);
    void animate(float dt);
    float rand01();

    // ---- where things are
    bool hatched() const;
    bool hiding() const;
    float squatchScale() const;
    float squatchX() const;
    float squatchFoot() const;
    float headY() const;
    float trayTop() const;
    float hourOfDay() const;
    float darkness() const;  // 0 day .. 1 night
    int visibleMesses() const;
    bool foodLeftToday() const;
    void furColours(Rgb& fur, Rgb& face, Rgb& mark) const;

    // ---- drawing
    Rgb sh(Rgb c) const;  // shade a scenery colour for the time of day
    void drawSky(Canvas& c);
    void drawGround(Canvas& c);
    void drawTree(Canvas& c, float x, float base, float size);
    void drawStump(Canvas& c);
    void drawCampfire(Canvas& c, bool flames);
    void drawLantern(Canvas& c);
    void drawEgg(Canvas& c);
    void drawPet(Canvas& c);
    void drawSquatch(Canvas& c, const Look& look);
    void drawHidingHead(Canvas& c);
    void drawHidingEyes(Canvas& c);
    void drawAwayTrail(Canvas& c);
    void drawParticles(Canvas& c);
    void drawStrip(Canvas& c);
    void drawBattery(Canvas& c, float x, float y);
    void drawToast(Canvas& c);
    void drawTray(Canvas& c);
    void drawItem(Canvas& c, Item it, float x, float y, float size);
    void drawSlotIcon(Canvas& c, Slot s, float x, float y);
    void drawStats(Canvas& c);
    void drawGallery(Canvas& c);
    void drawTrails(Canvas& c);
    void drawSettings(Canvas& c);
    void drawForagingBanner(Canvas& c);
    void drawLandmarkIcon(Canvas& c, LandmarkKind k, float x, float y, float size);

    Sim& sim_;
    GestureReader gestures_;

    uint32_t now_ = 0;
    bool started_ = false;
    double t_ = 0;  // animation seconds
    float dark_ = 0;  // darkness() for the frame being drawn
    uint32_t rng_ = 0x2545F491u;
    bool saveRequested_ = false;

    // tray
    float trayAnim_ = 0, trayTarget_ = 0;
    enum class Page : uint8_t { Main, Food, More };
    Page page_ = Page::Main;
    Item held_ = Item::None;
    DragMode drag_ = DragMode::None;
    float dragX_ = 0, dragY_ = 0;
    float strokeLen_ = 0;
    bool strokeDone_ = false, swipeCleaned_ = false;

    Overlay overlay_ = Overlay::None;

    // the trail map
    bool scanRequested_ = false, scanning_ = false;
    uint32_t scanSince_ = 0, lastScanMs_ = 0;
    bool everScanned_ = false;
    char trailNote_[40] = {};

    Settings settings_;
    bool settingsChanged_ = false;
    bool screenOff_ = false, waking_ = false;
    uint32_t lastInputMs_ = 0;
    uint8_t fingers_ = 0;  // a bit for each finger down

    int batteryPct_ = -1;
    bool charging_ = false;
    bool warnedLowBattery_ = false;
    uint32_t nextGlint_ = 0;  // a Golden coat's sparkle

    // pet animation
    React react_ = React::None;
    uint32_t reactUntil_ = 0;
    uint32_t nextBlink_ = 1500, blinkUntil_ = 0;
    uint32_t nextIdle_ = 4000;  // next spontaneous wave, wobble or glance
    float lookX_ = 0, lookTarget_ = 0;
    float wanderX_ = 240, wanderTarget_ = 240;
    uint32_t nextWander_ = 0;
    uint32_t nextZ_ = 0, nextThrow_ = 0, nextSneeze_ = 0, nextKnock_ = 0;
    uint32_t flashUntil_ = 0;

    static constexpr size_t kMaxParticles = 48;
    Particle particles_[kMaxParticles]{};
    size_t particleCount_ = 0;

    static constexpr size_t kMaxToasts = 3;
    Toast toasts_[kMaxToasts]{};
    size_t toastCount_ = 0;
    uint32_t toastUntil_ = 0;
};

const char* toString(Scene::Slot s);

}  // namespace squatch
