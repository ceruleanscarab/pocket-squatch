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
#include <vector>

#include "squatch/canvas.hpp"
#include "squatch/gesture.hpp"
#include "squatch/sim.hpp"
#include "squatch/sounds.hpp"
#include "squatch/weather.hpp"

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

    enum class Overlay : uint8_t { None, Stats, Gallery, Trails, Settings, Name, Game, StartOver };

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
        uint8_t volume = 2;      // off, quiet, medium, loud (Synth's steps)
    };
    static constexpr int kBrightnessSteps = 5;
    static constexpr int kTimeoutSteps = 7;
    static constexpr int kVolumeSteps = Synth::kVolumeSteps;
    static constexpr int kSettingRows = 3;  // brightness, screen timeout, volume

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

    // The weather outside, from the front end (Open-Meteo on the board). A change of sky
    // gets a message, and the clearing shows it: clouds, fog, rain, snow, storms.
    void setWeather(const Weather& w);
    const Weather& weather() const { return weather_; }
    void setFahrenheit(bool on) { fahrenheit_ = on; }

    // ---- the games, from the tray's Play button: a picker, then one of three games, or a
    // quick game of catch with no timing at all. Each ends in Sim::finishedGame().
    enum class Game : uint8_t { None, Pick, Seek, Berry, Photo };
    static constexpr int kGameChoices = 4;  // Hide and Seek, Berry Catch, Blurry Photo, Quick Catch
    static constexpr int kSeekSpots = 5;
    static constexpr int kBerriesToWin = 10;
    Game game() const { return game_; }
    bool gameOver() const { return gameOver_; }
    bool gameWon() const { return gameWon_; }
    int gameScore() const { return score_; }
    Rect gameChoiceArea(int i) const;
    Rect gameQuitArea() const;
    Rect seekSpotArea(int i) const;
    int seekAnswer() const { return seekSpot_; }
    Rect snapArea() const;
    bool petInFrame() const;  // Blurry Photo: inside the viewfinder right now
    float basketX() const { return basketX_; }

    // ---- the seasons, from the local date: autumn leaves (late September to November), and
    // for Halloween (October 15-31) a jack-o'-lantern to tap, bats at dusk, a harvest moon,
    // and a witch's hat for the last week.
    bool autumn() const;
    bool halloween() const;
    bool hatWeek() const;
    Rect pumpkinArea() const;
    void previewDate(int month, int day);  // for trying the seasons out; 0 goes back to the real date

    // ---- settings and the screen timeout
    void setSettings(const Settings& s);  // e.g. as loaded at start-up; out-of-range steps are clamped
    const Settings& settings() const { return settings_; }
    bool takeSettingsChanged();           // true once after the player changes one
    int brightnessPercent() const;
    uint32_t screenTimeoutMs() const;     // 0: never
    int volume() const { return settings_.volume; }  // 0 (off) .. kVolumeSteps - 1
    // After the timeout with no touch the screen should go dark; the front end turns the
    // panel off and stops drawing. The touch that wakes it does nothing else, so a tap on
    // a dark screen can't feed or scold by accident. News (hatching, a knock at the door,
    // getting sick, low battery...) wakes it too.
    bool screenOff() const { return screenOff_; }
    void wake();
    void screenOffNow();  // e.g. the board's power key
    // Sounds to play, oldest first; the front end hands them to a Synth at volume(). Nothing
    // is queued with the volume off, or while the screen is off (except a knock: it wants you).
    bool takeCue(Cue& out);

    // Naming: a big-letter keyboard that opens after hatching (or for any pet without a name),
    // and from the stats page by tapping the name. Keys 0..25 are A..Z, 26 a space, 27 delete.
    static constexpr int kNameKeys = 28;
    static constexpr int kMaxNameLen = 10;
    Rect nameKeyArea(int i) const;
    Rect nameSuggestArea() const;
    Rect nameDoneArea() const;
    Rect nameEditArea() const;  // on the stats page
    const char* nameDraft() const { return nameDraft_; }

    // The settings page: row 0 brightness, 1 screen timeout, 2 volume, each with - and +.
    Rect settingButtonArea(int row, bool plus) const;
    Rect settingsDoneArea() const;
    // Start over: a button on the settings page, then a page to confirm: a new egg keeping the
    // photos, footprints and trail map, a new egg with nothing kept, or cancel.
    Rect startOverArea() const;
    Rect startKeepArea() const;
    Rect startWipeArea() const;
    Rect startCancelArea() const;

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

    // The scenery (sky, ground, trees, stump, lantern) changes only with the time of day, so
    // it's drawn once a minute into a buffer of its own and copied in each frame. Off draws
    // it all every frame (for tests that compare the two).
    void setSceneryCache(bool on) { cacheScenery_ = on; sceneryKey_ = -1; }

    // Extra detail for screens with pixels and speed to spare (the phone): fur texture and
    // shading on the pet, layered pines, grass, stones and flowers. The boards leave it off.
    void setDetail(bool on) {
        detail_ = on;
        sceneryKey_ = -1;
    }
    bool detail() const { return detail_; }

    // Things to do in the clearing, and what it says. All just for fun: none of them is
    // needed to raise it, and none needs quick fingers.
    Rect treeArea(int i) const;  // 0 left, 1 right: tap to shake it (a pinecone may fall)
    Rect fireArea() const;       // tap to light the campfire for a while
    Rect coneArea() const;       // a pinecone on the ground: tap to toss it, or flick it
    bool coneOnGround() const { return fetch_ == Fetch::Lying; }
    bool fetching() const { return fetch_ == Fetch::Chase || fetch_ == Fetch::Carry; }
    bool fireLit() const;
    const char* bubble() const;  // what it's saying, or nullptr

    // Just the clearing and the pet: no status strip, tray or messages, and no naming page.
    // For the watch, which draws its own controls on top.
    void setCompact(bool on) {
        compact_ = on;
        if (on) askedName_ = true;
    }

private:
    enum class React : uint8_t { None, Flinch, Peek, Wave, Happy, Eat, Shake, Sneeze, Wobble, Sulk, Boop, Giggle, Pat, Swat };
    enum class DragMode : uint8_t { None, Item, Stroke, Toss };
    enum class ParticleKind : uint8_t { Heart, Sparkle, Crumb, Zzz, Pinecone, Drop, Shell, PlusOne, Ring, Leaf, Ember };
    // A pinecone to play fetch with: lying in the grass, in the air, chased, or carried back.
    enum class Fetch : uint8_t { None, Lying, Flying, Chase, Carry };

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
    void openNaming();
    void suggestName();
    void cue(Cue c);
    void requestScan();
    void note(const char* text);
    void tapPet();
    void touchPet(int x, int y);  // a tap on a friendly pet: nose, belly or head
    void shakeTree(int i);
    void tossCone(float vx, float vy, float vz);
    void lightFire();
    void updateFetch(float dt);
    void endFetch();
    bool canPlay() const;
    void drawCone(Canvas& c);
    float treeSway(float treeX) const;
    // ---- talking
    void speak(const char* text, uint32_t ms = 2400);
    void greet();
    void chatter();
    void drawBubble(Canvas& c);
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
    void drawScenery(Canvas& c);  // everything that only changes with the time of day
    void drawSky(Canvas& c);
    void drawStars(Canvas& c);
    void drawClouds(Canvas& c);
    void drawPumpkin(Canvas& c, bool lit);
    void drawBats(Canvas& c);
    void drawFallenLeaves(Canvas& c);
    void drawFallingLeaves(Canvas& c);
    void localDate(int& month, int& day) const;
    void tapPumpkin();
    void openGames();
    void startGame(Game g);
    void gameTap(int x, int y);
    void updateGame();
    void finishGame(bool won, const char* msg);
    void endGame(bool reward);
    void drawGame(Canvas& c);
    void drawGamePicker(Canvas& c);
    void drawBush(Canvas& c, float x, float y);
    void drawPeek(Canvas& c, float x, float y);
    void drawFog(Canvas& c);
    void drawSnowCover(Canvas& c);
    void drawPrecipitation(Canvas& c);
    float overcast() const;  // 0 clear .. 1 a dark storm sky
    void drawGround(Canvas& c);
    void drawTree(Canvas& c, float x, float base, float size);
    void drawStump(Canvas& c);
    void drawGroundDetail(Canvas& c);
    void drawTreeDetail(Canvas& c, float x, float base, float size);
    void drawFur(Canvas& c, float cx, float cy, float rx, float ry, Rgb fur, float s, int n, uint32_t seed, bool radial);
    void drawFringe(Canvas& c, float cx, float cy, float rx, float ry, Rgb lit, Rgb shade, float s, int n, float from, float to);
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
    void drawNaming(Canvas& c);
    void drawStartOver(Canvas& c);
    void startOver(bool keepCollection);
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

    char nameDraft_[kMaxNameLen + 1] = {};
    uint32_t askNameAt_ = 0;   // when to open the naming page for a pet with no name
    bool askedName_ = false;
    uint8_t suggestion_ = 0;
    std::vector<uint16_t> scenery_;
    int sceneryKey_ = -1;
    bool cacheScenery_ = true;
    bool compact_ = false;
    bool detail_ = false;
    Settings settings_;
    Cue cues_[8];
    int cueCount_ = 0;
    uint32_t nextKnockSound_ = 0;
    Weather weather_;
    int previewMonth_ = 0, previewDay_ = 0;
    struct FallingBerry {
        float x, vy;
        uint32_t born;
        bool live;
    };
    Game game_ = Game::None;
    bool gameOver_ = false, gameWon_ = false, petOverride_ = false, running_ = false, snapped_ = false, runRight_ = true;
    int score_ = 0, tries_ = 0, seekSpot_ = 0, runsLeft_ = 0;
    uint8_t seekWrong_ = 0;  // a bit per spot already looked behind
    uint32_t gameUntil_ = 0, resultUntil_ = 0, nextEvent_ = 0, runStart_ = 0;
    float basketX_ = 240, gameX_ = 0;
    FallingBerry berries_[10]{};
    char gameMsg_[32] = {};
    int64_t greetedDay_ = -1;
    uint32_t giggleAt_ = 0;
    bool fahrenheit_ = false;
    uint32_t nextLightning_ = 0, lightningUntil_ = 0;
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
    float lookY_ = 0, lookTargetY_ = 0;
    float fingerX_ = 240, fingerY_ = 240;
    uint32_t fingerUntil_ = 0;  // its eyes follow a finger until then
    uint32_t pokeSince_ = 0;
    int pokes_ = 0, tickles_ = 0;

    // the clearing
    int shakingTree_ = -1, shakes_ = 0;
    uint32_t shakeUntil_ = 0;
    Fetch fetch_ = Fetch::None;
    float coneX_ = 0, coneY_ = 0, coneZ_ = 0, coneVx_ = 0, coneVy_ = 0, coneVz_ = 0;
    float fetchX_ = 240;  // where the pet is while it fetches
    uint32_t coneSince_ = 0;
    uint32_t fireUntil_ = 0, nextEmber_ = 0;

    // talking
    char bubble_[28] = {};
    uint32_t bubbleUntil_ = 0, nextChatter_ = 30000, greetAt_ = 1200, offAt_ = 0;
    bool greeted_ = false, wasOff_ = false;
    uint8_t lastChatter_ = 255;

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
