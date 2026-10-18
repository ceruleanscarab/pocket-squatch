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
constexpr float kPumpkinX = 448, kPumpkinY = 392;
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
constexpr Rgb kEye = 0x1A120C;  // fur and face colours come from the DNA (furColours)
constexpr Rgb kUiDark = 0x1B2416, kUiLight = 0xF3EBD9, kGold = 0xF2C14E;
constexpr Rgb kTray = 0x2B2118, kButton = 0x4A3A2C;

constexpr Scene::Slot kMainSlots[] = {Scene::Slot::Feed, Scene::Slot::Clean, Scene::Slot::Medicine,
                                      Scene::Slot::Play, Scene::Slot::Light, Scene::Slot::More};
constexpr Scene::Slot kFoodSlots[] = {Scene::Slot::Back, Scene::Slot::Berries, Scene::Slot::Fish,
                                      Scene::Slot::Roots};
constexpr Scene::Slot kMoreSlots[] = {Scene::Slot::Back, Scene::Slot::Photos, Scene::Slot::Trails,
                                      Scene::Slot::Settings};

// ---- the naming page
constexpr int kKeyCols = 7, kKeyW = 64, kKeyH = 64;
constexpr int kKeyX = (480 - kKeyCols * kKeyW) / 2, kKeyY = 118;
constexpr const char* kSuggestedNames[] = {"MOSS",   "BRAMBLE", "HUCKLE", "FERN",   "BIRCH",  "TIMBER",
                                           "CEDAR",  "JUNIPER", "HAZEL",  "BISCUIT", "SCOUT", "PEBBLE",
                                           "THISTLE", "MAPLE",  "BOULDER", "WILLOW", "SHAGGY", "ROWAN"};

// ---- the settings page
constexpr int kSetX = 24, kSetY = 28, kSetW = 432, kSetH = 424;
constexpr int kSetRowY[Scene::kSettingRows] = {96, 200, 304};  // the - and + buttons of each row
constexpr int kBrightness[Scene::kBrightnessSteps] = {20, 40, 60, 80, 100};
constexpr uint32_t kTimeoutSecs[Scene::kTimeoutSteps] = {15, 30, 60, 120, 300, 600, 0};
constexpr const char* kTimeoutNames[Scene::kTimeoutSteps] = {"15 SEC", "30 SEC", "1 MIN", "2 MIN",
                                                             "5 MIN",  "10 MIN", "NEVER"};
constexpr const char* kVolumeNames[Scene::kVolumeSteps] = {"OFF", "QUIET", "MEDIUM", "LOUD"};

// ---- the trail map overlay
constexpr int kMapX = 24, kMapY = 24, kMapW = 432, kMapH = 448;
constexpr int kRowY = 76, kRowH = 64;
constexpr uint32_t kScanTimeoutMs = 12000;
constexpr uint32_t kRescanAfterMs = 60000;  // opening the map scans again only after a minute

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

// Hue in degrees, saturation and value 0..1.
Rgb hsv(float h, float sat, float val) {
    h = std::fmod(h, 360.0f);
    if (h < 0) h += 360;
    float c = val * sat, x = c * (1 - std::fabs(std::fmod(h / 60, 2.0f) - 1)), m = val - c;
    float r = 0, g = 0, b = 0;
    if (h < 60) r = c, g = x;
    else if (h < 120) r = x, g = c;
    else if (h < 180) g = c, b = x;
    else if (h < 240) g = x, b = c;
    else if (h < 300) r = x, b = c;
    else r = c, b = x;
    auto ch = [](float v) { return static_cast<Rgb>(std::max(0.0f, std::min(255.0f, v * 255 + 0.5f))); };
    return ch(r + m) << 16 | ch(g + m) << 8 | ch(b + m);
}

// Upper case copy, for building toasts from names like "Snowcoat".
void upper(char* s) {
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z') *s = static_cast<char>(*s - 'a' + 'A');
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

void Scene::setBattery(int percent, bool charging) {
    batteryPct_ = percent < 0 ? -1 : std::min(percent, 100);
    charging_ = charging && batteryPct_ >= 0;
    bool low = batteryPct_ >= 0 && batteryPct_ <= 15 && !charging_;
    if (low && !warnedLowBattery_) {
        say("BATTERY LOW: PLUG ME IN", true);
        wake();
        cue(Cue::Warning);
    }
    warnedLowBattery_ = low || (warnedLowBattery_ && batteryPct_ >= 0 && batteryPct_ < 25 && !charging_);
}

void Scene::setSettings(const Settings& s) {
    settings_.brightness = std::min<uint8_t>(s.brightness, kBrightnessSteps - 1);
    settings_.timeout = std::min<uint8_t>(s.timeout, kTimeoutSteps - 1);
    settings_.volume = std::min<uint8_t>(s.volume, kVolumeSteps - 1);
}

void Scene::cue(Cue c) {
    if (settings_.volume == 0 || (screenOff_ && c != Cue::Knock)) return;
    if (cueCount_ == static_cast<int>(sizeof cues_ / sizeof cues_[0])) return;  // nobody's listening
    cues_[cueCount_++] = c;
}

bool Scene::takeCue(Cue& out) {
    if (cueCount_ == 0) return false;
    out = cues_[0];
    for (int i = 1; i < cueCount_; i++) cues_[i - 1] = cues_[i];
    cueCount_--;
    return true;
}

bool Scene::takeSettingsChanged() {
    bool r = settingsChanged_;
    settingsChanged_ = false;
    return r;
}

int Scene::brightnessPercent() const { return kBrightness[settings_.brightness]; }

uint32_t Scene::screenTimeoutMs() const { return kTimeoutSecs[settings_.timeout] * 1000; }

void Scene::wake() {
    screenOff_ = false;
    lastInputMs_ = now_;
}

void Scene::screenOffNow() {
    screenOff_ = true;
    waking_ = fingers_ != 0;  // a finger still down when it goes dark doesn't count as a tap later
}

Scene::Rect Scene::nameKeyArea(int i) const {
    return {kKeyX + (i % kKeyCols) * kKeyW, kKeyY + (i / kKeyCols) * kKeyH, kKeyW, kKeyH};
}
Scene::Rect Scene::nameSuggestArea() const { return {kKeyX, kKeyY + 4 * kKeyH + 12, 216, 68}; }
Scene::Rect Scene::nameDoneArea() const { return {kKeyX + kKeyCols * kKeyW - 216, kKeyY + 4 * kKeyH + 12, 216, 68}; }
Scene::Rect Scene::nameEditArea() const { return {40, 76, 400, 64}; }

void Scene::openNaming() {
    std::snprintf(nameDraft_, sizeof nameDraft_, "%.10s", sim_.state().name);
    overlay_ = Overlay::Name;
    askedName_ = true;
}

void Scene::suggestName() {
    constexpr int n = static_cast<int>(sizeof kSuggestedNames / sizeof kSuggestedNames[0]);
    if (suggestion_ == 0) suggestion_ = static_cast<uint8_t>(1 + (sim_.state().genome.g[0] % n));  // its DNA picks the first
    std::snprintf(nameDraft_, sizeof nameDraft_, "%.10s", kSuggestedNames[(suggestion_ - 1) % n]);
    suggestion_ = static_cast<uint8_t>(suggestion_ % n + 1);
}

void Scene::changeSetting(int row, int by) {
    uint8_t& v = row == 0 ? settings_.brightness : row == 1 ? settings_.timeout : settings_.volume;
    const int top = (row == 0 ? kBrightnessSteps : row == 1 ? kTimeoutSteps : kVolumeSteps) - 1;
    int next = std::max(0, std::min(top, v + by));
    if (next == v) return;
    v = static_cast<uint8_t>(next);
    settingsChanged_ = true;
    cue(row == 2 ? Cue::Happy : Cue::Tick);  // a chirp at the new volume, to hear it
}

Scene::Rect Scene::settingButtonArea(int row, bool plus) const {
    return {plus ? kSetX + kSetW - 24 - 96 : kSetX + 24, kSetRowY[row], 96, 72};
}

Scene::Rect Scene::settingsDoneArea() const { return {S / 2 - 120, kSetY + kSetH - 84, 240, 72}; }

void Scene::setWeather(const Weather& w) {
    const Sky before = weather_.sky;
    weather_ = w;
    if (w.sky == before || w.sky == Sky::Unknown) return;
    const bool first = before == Sky::Unknown;
    const char* msg = nullptr;
    switch (w.sky) {
        case Sky::Clear: msg = first ? nullptr : "THE SKY CLEARED"; break;
        case Sky::Cloudy: msg = first ? nullptr : "CLOUDS ROLLED IN"; break;
        case Sky::Fog: msg = "FOG IN THE WOODS"; break;
        case Sky::Drizzle: msg = "A LITTLE DRIZZLE"; break;
        case Sky::Rain: msg = "RAIN IN THE FOREST"; break;
        case Sky::Snow: msg = "SNOW IS FALLING!"; break;
        case Sky::Storm: msg = "A STORM IS COMING"; break;
        case Sky::Unknown: break;
    }
    if (msg) say(msg);
    if (hatched() && !hiding() && !sim_.state().asleep) {
        if (w.sky == Sky::Snow) react(React::Happy, 1200);
        if (w.sky == Sky::Storm) react(React::Flinch, 1200);
    }
    nextLightning_ = now_ + 4000;
}

void Scene::localDate(int& month, int& day) const {
    if (previewMonth_) {
        month = previewMonth_;
        day = previewDay_;
        return;
    }
    const State& s = sim_.state();
    int64_t z = (s.now + s.tzOffset) / 86400 + 719468;  // days, shifted to 0000-03-01 (H. Hinnant's civil_from_days)
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
}

bool Scene::autumn() const {
    int m, d;
    localDate(m, d);
    return (m == 9 && d >= 22) || m == 10 || m == 11;
}

bool Scene::halloween() const {
    int m, d;
    localDate(m, d);
    return m == 10 && d >= 15;
}

bool Scene::hatWeek() const {
    int m, d;
    localDate(m, d);
    return m == 10 && d >= 25;
}

void Scene::previewDate(int month, int day) {
    previewMonth_ = month;
    previewDay_ = day;
    sceneryKey_ = -1;
}

Scene::Rect Scene::pumpkinArea() const {
    return {static_cast<int>(kPumpkinX) - 32, static_cast<int>(kPumpkinY) - 38, 64, 64};
}

void Scene::tapPumpkin() {
    say("BOO!", true);
    cue(Cue::Boo);
    for (int i = 0; i < 6; i++)
        spawn(ParticleKind::Sparkle, kPumpkinX + (rand01() - 0.5f) * 40, kPumpkinY - 20 - rand01() * 20, 0, -40, 700);
    if (hatched() && !hiding() && !sim_.state().asleep) {
        react(React::Flinch, 700);
        giggleAt_ = now_ + 800;  // then it giggles
    }
}

float Scene::overcast() const {
    switch (weather_.sky) {
        case Sky::Cloudy: return 0.35f;
        case Sky::Fog: return 0.45f;
        case Sky::Drizzle: return 0.45f;
        case Sky::Snow: return 0.5f;
        case Sky::Rain: return 0.6f;
        case Sky::Storm: return 0.8f;
        default: return 0;
    }
}

bool Scene::takeScanRequest() {
    bool r = scanRequested_;
    scanRequested_ = false;
    return r;
}

// ================================================================ input

void Scene::touch(const TouchEvent& e) {
    lastInputMs_ = e.ms;
    const uint8_t bit = static_cast<uint8_t>(1u << (e.id & 7));
    if (e.phase == TouchPhase::Down) fingers_ |= bit;
    else if (e.phase == TouchPhase::Up) fingers_ &= static_cast<uint8_t>(~bit);
    if (screenOff_ || waking_) {  // this touch only wakes the screen
        screenOff_ = false;
        waking_ = fingers_ != 0;
        return;
    }
    gestures_.touch(e);
    Gesture g;
    while (gestures_.poll(g)) handle(g);
}

void Scene::handle(const Gesture& g) {
    if (overlay_ == Overlay::Trails) {
        if (g.type != GestureType::Tap) return;
        for (int i = 0; i < sim_.state().trailCount; i++) {
            if (trailRowArea(i).contains(g.x, g.y)) {
                doForage(i);
                return;
            }
        }
        if (rescanArea().contains(g.x, g.y)) {
            if (!scanning_) requestScan();
            return;
        }
        Rect panel{kMapX, kMapY, kMapW, kMapH};
        if (!panel.contains(g.x, g.y) || g.y < kRowY) overlay_ = Overlay::None;  // outside, or the title
        return;
    }
    if (overlay_ == Overlay::Name) {
        if (g.type != GestureType::Tap) return;
        size_t len = std::strlen(nameDraft_);
        for (int i = 0; i < kNameKeys; i++) {
            if (!nameKeyArea(i).contains(g.x, g.y)) continue;
            if (i == 27) {
                if (len) nameDraft_[len - 1] = '\0';
            } else if (len < static_cast<size_t>(kMaxNameLen) && !(i == 26 && (len == 0 || nameDraft_[len - 1] == ' '))) {
                nameDraft_[len] = i == 26 ? ' ' : static_cast<char>('A' + i);
                nameDraft_[len + 1] = '\0';
            }
            cue(Cue::Tick);
            return;
        }
        if (nameSuggestArea().contains(g.x, g.y)) {
            suggestName();
            cue(Cue::Tick);
            return;
        }
        if (nameDoneArea().contains(g.x, g.y)) {
            while (len && nameDraft_[len - 1] == ' ') nameDraft_[--len] = '\0';
            if (!len) return;  // a name first
            sim_.setName(nameDraft_);
            saveRequested_ = true;
            overlay_ = Overlay::None;
            char buf[40];
            std::snprintf(buf, sizeof buf, "HELLO, %s!", nameDraft_);
            say(buf, true);
            cue(Cue::Happy);
            if (hatched() && !hiding()) react(React::Wave, 1600);
        }
        return;
    }
    if (overlay_ == Overlay::Stats && g.type == GestureType::Tap && nameEditArea().contains(g.x, g.y) &&
        sim_.state().stage != Stage::Egg) {
        openNaming();
        return;
    }
    if (overlay_ == Overlay::Settings) {
        if (g.type != GestureType::Tap) return;
        for (int row = 0; row < kSettingRows; row++) {
            for (int plus = 0; plus < 2; plus++) {
                if (settingButtonArea(row, plus).contains(g.x, g.y)) {
                    changeSetting(row, plus ? 1 : -1);
                    return;
                }
            }
        }
        Rect panel{kSetX, kSetY, kSetW, kSetH};
        if (settingsDoneArea().contains(g.x, g.y) || !panel.contains(g.x, g.y)) {
            overlay_ = Overlay::None;
            cue(Cue::Tick);
        }
        return;
    }
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
            page_ = Page::Main;
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
    if (halloween() && pumpkinArea().contains(x, y)) {
        tapPumpkin();
        return;
    }
    if (squatchArea().contains(x, y)) tapPet();
}

void Scene::pressSlot(Slot s) {
    if (itemFor(s) == Item::None) cue(Cue::Tick);
    else cue(held_ == itemFor(s) ? Cue::Tick : Cue::Pickup);
    switch (s) {
        case Slot::Feed:
            page_ = Page::Food;
            held_ = Item::None;
            break;
        case Slot::More:
            page_ = Page::More;
            held_ = Item::None;
            break;
        case Slot::Back:
            page_ = Page::Main;
            held_ = Item::None;
            break;
        case Slot::Trails: openTrails(); break;
        case Slot::Light:
            sim_.lights(!sim_.state().lightsOn);
            cue(sim_.state().lightsOn ? Cue::LightOn : Cue::LightOff);
            say(sim_.state().lightsOn ? "LIGHTS ON" : "LIGHTS OFF", true);
            saveRequested_ = true;
            break;
        case Slot::Photos: overlay_ = Overlay::Gallery; break;
        case Slot::Settings: overlay_ = Overlay::Settings; break;
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
        page_ = Page::Main;
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
    if (s.foraging) say("IT'S OUT ON A TRAIL", true);
    else if (s.stage == Stage::Egg) say("IT HASN'T HATCHED YET", true);
    else if (s.stage == Stage::Away) say("IT'S NOT HERE", true);
    else if (s.asleep) say("SHH, IT'S ASLEEP", true);
}

void Scene::doFeed(Food f) {
    bool misbehaving = sim_.state().misbehaving;
    Result r = sim_.feed(f);
    saved(r);
    if (r == Result::Ok) {
        cue(Cue::Eat);
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
        cue(Cue::Clean);
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
        cue(Cue::Clean);
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
        cue(Cue::Play);
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
        cue(Cue::Happy);
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
        cue(Cue::Sulk);
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
    cue(Cue::Happy);
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

void Scene::openTrails() {
    overlay_ = Overlay::Trails;
    trailNote_[0] = '\0';
    if (!everScanned_ || now_ - lastScanMs_ >= kRescanAfterMs) requestScan();
}

void Scene::requestScan() {
    scanRequested_ = scanning_ = true;
    everScanned_ = true;
    scanSince_ = lastScanMs_ = now_;
    note("LISTENING FOR TRAILS...");
}

void Scene::note(const char* text) { std::snprintf(trailNote_, sizeof trailNote_, "%s", text); }

void Scene::doForage(int trail) {
    const State& s = sim_.state();
    Result r = sim_.sendForaging(trail);
    saved(r);
    char name[24], buf[40];
    if (r == Result::Ok) {
        landmarkName(s.forageHash, name, sizeof name);
        std::snprintf(buf, sizeof buf, "OFF TO %s!", name);
        overlay_ = Overlay::None;
        trayTarget_ = 0;
        say(buf, true);
        return;
    }
    if (s.foraging) {
        landmarkName(s.forageHash, name, sizeof name);
        std::snprintf(buf, sizeof buf, "IT'S OUT AT %s", name);
        note(buf);
    } else if (r == Result::NotNow) {
        if (s.stage == Stage::Egg) note("IT HASN'T HATCHED YET");
        else if (s.stage == Stage::Away) note("IT'S NOT HERE");
        else if (s.asleep) note("IT'S ASLEEP: TRY IN THE MORNING");
        else note("NOT RIGHT NOW");
    } else if (s.stage == Stage::Baby) {
        note("TOO LITTLE TO GO ALONE");
    } else if (sim_.trustLevel() == TrustLevel::Hiding) {
        note("TOO SHY TO GO WHERE YOU POINT");
    } else if (s.sick) {
        note("IT'S NOT WELL ENOUGH");
    } else if (s.misbehaving) {
        note("NOT WHILE IT'S MISBEHAVING");
    } else {
        note("FEED IT BEFORE A LONG WALK");
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
    if (!started_) lastInputMs_ = nowMs;
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

    // Halloween: a greeting once on the day, and the giggle after a fright.
    {
        int m, d;
        localDate(m, d);
        const int64_t day = (sim_.state().now + sim_.state().tzOffset) / 86400;
        if (m == 10 && d == 31 && greetedDay_ != day && !screenOff_ && overlay_ == Overlay::None) {
            greetedDay_ = day;
            say("HAPPY HALLOWEEN!");
            cue(Cue::Boo);
        }
    }
    if (giggleAt_ && now_ >= giggleAt_) {
        giggleAt_ = 0;
        react(React::Happy, 900);
        cue(Cue::Happy);
    }

    // A pet with no name gets asked for one, a moment after hatching (or at start-up).
    if (!askedName_ && !sim_.state().name[0] && hatched() && overlay_ == Overlay::None && !screenOff_ &&
        now_ >= askNameAt_ && drag_ == DragMode::None) {
        openNaming();
        suggestion_ = 0;
    }

    const uint32_t timeout = screenTimeoutMs();
    if (!screenOff_ && timeout && fingers_ == 0 && nowMs - lastInputMs_ >= timeout) screenOff_ = true;
}

void Scene::handleEvent(const Event& e) {
    switch (e.type) {  // news worth lighting the screen for
        case EventType::Hatched:
        case EventType::StageChanged:
        case EventType::CallStarted:
        case EventType::GotSick:
        case EventType::RanAway:
        case EventType::Returned:
        case EventType::LaidEgg:
        case EventType::BackFromForaging: wake(); break;
        default: break;
    }
    char buf[40];
    switch (e.type) {
        case EventType::Hatched: {
            for (int i = 0; i < 10; i++)
                spawn(ParticleKind::Shell, kEggX, kEggY, (rand01() - 0.5f) * 260, -150 - rand01() * 150, 1200);
            react(React::Happy, 1200);
            cue(Cue::Hatch);
            askNameAt_ = now_ + 2500;  // see it hatch first
            Traits t = sim_.traits();
            if (t.variant != Variant::None) {
                char kind[16];
                std::snprintf(kind, sizeof kind, "%s", toString(t.variant));
                upper(kind);
                std::snprintf(buf, sizeof buf, "IT HATCHED: A%s %s %s!", t.rarity == Rarity::Epic ? "N" : "",
                              t.rarity == Rarity::Mythic ? "MYTHIC" : (t.rarity == Rarity::Epic ? "EPIC" : "RARE"),
                              kind);
                say(buf);
            } else if (t.rarity == Rarity::Uncommon) {
                say("IT HATCHED, WITH AN UNUSUAL COAT!");
            } else {
                say("IT HATCHED!");
            }
            break;
        }
        case EventType::StageChanged: {
            Stage st = static_cast<Stage>(e.a);
            if (st == Stage::Child || st == Stage::Teen || st == Stage::Elder) {
                std::snprintf(buf, sizeof buf, "NOW A %s", toString(st));
                say(buf);
                cue(Cue::GrowUp);
            }
            break;
        }
        case EventType::BecameAdult:
            std::snprintf(buf, sizeof buf, "GREW UP: %s", toString(static_cast<AdultForm>(e.a)));
            say(buf);
            cue(Cue::GrowUp);
            break;
        case EventType::CallStarted:
            nextKnock_ = nextKnockSound_ = now_;
            break;
        case EventType::GotSick:
            say("IT'S SICK: GIVE IT A POULTICE");
            cue(Cue::Sick);
            break;
        case EventType::Recovered:
            say("FEELING BETTER");
            cue(Cue::Heal);
            react(React::Happy, 1200);
            break;
        case EventType::Misbehaving:
            say("IT'S MISBEHAVING!");
            nextThrow_ = now_;
            break;
        case EventType::RanAway:
            say("IT RAN OFF INTO THE WOODS");
            cue(Cue::RanAway);
            break;
        case EventType::Returned:
            say("IT CAME BACK!");
            cue(Cue::Returned);
            react(React::Wave, 2000);
            break;
        case EventType::PhotoEarned:
            flashUntil_ = now_ + 400;
            say("NEW SIGHTING PHOTO!");
            cue(Cue::Photo);
            break;
        case EventType::LaidEgg:
            say("A NEW EGG!");
            cue(Cue::NewEgg);
            break;
        case EventType::TrailsScouted:
            scanning_ = false;
            if (sim_.state().trailCount == 0) note("NO TRAILS HERE: TRY SOMEWHERE ELSE");
            else if (e.a == 0) note("NO NEW TRAILS, BUT OLD FRIENDS");
            else {
                std::snprintf(buf, sizeof buf, "%u NEW TRAIL%s NEARBY!", static_cast<unsigned>(e.a), e.a == 1 ? "" : "S");
                note(buf);
            }
            saveRequested_ = true;
            break;
        case EventType::BackFromForaging: {
            char name[24];
            landmarkName(sim_.state().forageHash, name, sizeof name);
            std::snprintf(buf, sizeof buf, "BACK FROM %s", name);
            say(buf);
            spawn(ParticleKind::PlusOne, 430, 46, 0, -30, 1200);
            react(React::Happy, 1600);
            cue(Cue::ForageBack);
            saveRequested_ = true;
            break;
        }
        default: break;
    }
}

void Scene::react(React r, uint32_t ms) {
    react_ = r;
    reactUntil_ = now_ + ms;
}

void Scene::say(const char* text, bool now) {
    // "IT'S FULL" reads as "MOSS'S FULL" once it has a name, if that still fits.
    char named[sizeof(Toast::text)];
    const char* name = sim_.state().name;
    if (name[0] && (std::strncmp(text, "IT ", 3) == 0 || std::strncmp(text, "IT'S ", 5) == 0)) {
        int n = std::snprintf(named, sizeof named, "%s%s", name, text + 2);
        if (n > 0 && n < static_cast<int>(sizeof named) && Canvas::textWidth(named, 2) <= 440) text = named;
    }
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

    if (scanning_ && now_ - scanSince_ >= kScanTimeoutMs) {
        scanning_ = false;
        note("COULDN'T LISTEN FOR TRAILS");
    }

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
        if (sim_.traits().variant == Variant::Golden && !hiding() && now_ >= nextGlint_) {
            spawn(ParticleKind::Sparkle, squatchX() + (rand01() - 0.5f) * 100 * k,
                  squatchFoot() - rand01() * 170 * k, 0, -20, 700);
            nextGlint_ = now_ + 500 + static_cast<uint32_t>(rand01() * 900);
        }
        if (s.sick && !s.asleep && now_ >= nextSneeze_) {
            if (!hiding()) {
                react(React::Sneeze, 500);
                cue(Cue::Sneeze);
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
        if (now_ >= nextKnockSound_) {  // heard now and then, not every knock
            cue(Cue::Knock);
            nextKnockSound_ = now_ + 15000;
        }
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

bool Scene::hatched() const {  // and here in the clearing
    Stage st = sim_.state().stage;
    return st != Stage::Egg && st != Stage::Away && !sim_.state().foraging;
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
    if (st == Stage::Away || sim_.state().foraging) return {0, 0, 0, 0};
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

int Scene::slotCount() const {
    switch (page_) {
        case Page::Food: return 4;
        case Page::More: return 4;
        case Page::Main: break;
    }
    return 6;
}

Scene::Rect Scene::slotArea(int i) const {
    int w = S / slotCount();
    return {i * w, static_cast<int>(trayTop()) + 20, w, kTrayH - 20};
}

Scene::Slot Scene::slotAt(int i) const {
    switch (page_) {
        case Page::Food: return kFoodSlots[i];
        case Page::More: return kMoreSlots[i];
        case Page::Main: break;
    }
    return kMainSlots[i];
}

Scene::Rect Scene::trailRowArea(int i) const { return {kMapX + 12, kRowY + i * kRowH, kMapW - 24, kRowH}; }

Scene::Rect Scene::rescanArea() const { return {S / 2 - 120, kRowY + State::kMaxTrails * kRowH + 4, 240, 64}; }

void Scene::furColours(Rgb& fur, Rgb& face, Rgb& mark) const {
    const State& s = sim_.state();
    const Traits t = sim_.traits();
    switch (t.variant) {
        case Variant::Swamp: fur = hsv(t.hue, 0.68f, 0.42f + t.shade * 0.15f); break;
        case Variant::Snowcoat: fur = 0xDCE4EA; break;
        case Variant::Albino: fur = 0xF1ECE4; break;
        case Variant::Golden: fur = 0xD9A93A; break;
        case Variant::None: fur = hsv(t.hue, 0.62f - t.shade * 0.25f, 0.2f + t.shade * 0.52f); break;
    }
    face = t.variant == Variant::Albino ? 0xF2C9C2 : mix(fur, 0xEED3B0, t.variant == Variant::None ? 0.55f : 0.45f);

    // How it was raised still shows, a little, on top of the coat it was born with.
    Rgb form = 0;
    float amount = 0.3f;
    switch (s.adult) {
        case AdultForm::Guardian: form = 0x7B4F33; break;
        case AdultForm::RidgeRunner: form = 0x9B5E34; break;
        case AdultForm::CouchSquatch: form = 0xA0724E; break;
        case AdultForm::Hermit: form = 0x5E4A3A; break;
        case AdultForm::Legend:
            form = 0xD8CFC4;
            amount = 0.5f;
            break;
        case AdultForm::None: amount = 0; break;
    }
    if (t.variant == Variant::None && amount > 0) fur = mix(fur, form, amount);
    if (s.stage == Stage::Elder) fur = mix(fur, 0xA89F96, 0.5f);
    if (s.sick) {
        fur = mix(fur, 0x9DB58A, 0.35f);
        face = mix(face, 0xC8D6B0, 0.45f);
    }
    switch (t.markings) {
        case 1: mark = mix(face, 0xFFFFFF, 0.25f + 0.35f * t.markShade); break;  // pale chest blaze
        case 2: mark = mix(fur, 0x1E140C, 0.45f * t.markShade); break;           // dark eye mask
        default: mark = mix(fur, t.shade > 0.6f ? 0x3A2818 : 0xE8D2B4, 0.35f * t.markShade); break;
    }
}

// ================================================================ drawing

Rgb Scene::sh(Rgb c) const { return mix(c, kNight, dark_ * 0.6f); }

void Scene::draw(Canvas& c) {
    const State& s = sim_.state();
    dark_ = darkness();

    // With the lights off the whole clearing is shaded, as each thing is drawn.
    const float shade = s.lightsOn ? 0.0f : 0.5f;
    c.setShade(0x02040C, shade);

    // The scenery: from the cache when it's still the same minute, else drawn afresh.
    const int key = ((static_cast<int>(hourOfDay() * 60) * 2 + (s.lightsOn ? 1 : 0)) * 8 + static_cast<int>(weather_.sky)) * 2 +
                    (foodLeftToday() ? 1 : 0);
    if (cacheScenery_ && c.width() == S && c.height() == S) {
        if (scenery_.size() != static_cast<size_t>(S * S)) scenery_.assign(static_cast<size_t>(S * S), 0);
        if (key != sceneryKey_) {
            Canvas sc(scenery_.data(), S, S);
            sc.setShade(0x02040C, shade);
            drawScenery(sc);
            sceneryKey_ = key;
        }
        std::memcpy(c.pixels(), scenery_.data(), scenery_.size() * sizeof(uint16_t));
    } else {
        drawScenery(c);
    }
    drawStars(c);
    if (halloween()) drawBats(c);
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
    if (s.stage == Stage::Away || s.foraging) drawAwayTrail(c);
    if (hiding()) {
        drawHidingHead(c);
        drawTree(c, kLeftTreeX, kLeftTreeBase, 1.0f);  // it peeks out from behind the tree
    }
    if (s.stage == Stage::Egg) drawEgg(c);
    else if (hiding()) drawHidingEyes(c);
    else if (hatched()) drawPet(c);
    float h = hourOfDay();
    drawCampfire(c, h >= 17 && h < 23);
    drawParticles(c);
    drawPrecipitation(c);
    const Sky sky = weather_.sky;
    if (autumn() && sky != Sky::Rain && sky != Sky::Drizzle && sky != Sky::Storm && sky != Sky::Snow) drawFallingLeaves(c);

    c.setShade(0, 0);  // the screen's own furniture isn't shaded
    if (weather_.sky == Sky::Storm && now_ >= nextLightning_) {
        lightningUntil_ = now_ + 220;
        nextLightning_ = now_ + 9000 + static_cast<uint32_t>(rand01() * 15000);
        cue(Cue::Thunder);
    }
    if (now_ < lightningUntil_) c.tint(0xF4F6FF, static_cast<float>(lightningUntil_ - now_) / 220.0f * 0.55f);
    if (now_ < flashUntil_) c.tint(0xFFFFFF, static_cast<float>(flashUntil_ - now_) / 400.0f * 0.8f);

    drawStrip(c);
    if (s.foraging) drawForagingBanner(c);
    drawToast(c);
    drawTray(c);
    if (drag_ == DragMode::Item && held_ != Item::None) {
        c.fillEllipse(dragX_, dragY_ - 8, 20, 6, 0x000000, 0.2f);
        drawItem(c, held_, dragX_, dragY_ - 40, 1.3f);  // above the finger, not under it
    }
    if (overlay_ == Overlay::Stats) drawStats(c);
    if (overlay_ == Overlay::Gallery) drawGallery(c);
    if (overlay_ == Overlay::Trails) drawTrails(c);
    if (overlay_ == Overlay::Settings) drawSettings(c);
    if (overlay_ == Overlay::Name) drawNaming(c);
}

void Scene::drawScenery(Canvas& c) {
    drawSky(c);
    drawClouds(c);
    drawGround(c);
    if (weather_.sky == Sky::Snow) drawSnowCover(c);
    else if (autumn()) drawFallenLeaves(c);
    drawCampfire(c, false);
    const float h = hourOfDay();
    if (h >= 17 && h < 23) c.glow(kFireX, kFireY - 20, 90, 0xFFB347, 0.15f + 0.35f * dark_);  // the fire's light
    drawStump(c);
    drawTree(c, kLeftTreeX, kLeftTreeBase, 1.0f);
    drawTree(c, kRightTreeX, kRightTreeBase, 0.95f);
    drawLantern(c);
    if (halloween()) drawPumpkin(c, dark_ > 0.25f);
    if (weather_.sky == Sky::Fog) drawFog(c);
}

void Scene::drawPumpkin(Canvas& c, bool lit) {
    const float x = kPumpkinX, y = kPumpkinY;
    if (lit) c.glow(x, y - 4, 46, 0xFF9A2E, 0.55f * dark_);
    c.fillEllipse(x, y + 16, 22, 5, 0x000000, 0.2f);
    const Rgb skin = sh(0xE37B1C), rib = sh(0xC4620F);
    c.fillEllipse(x - 9, y, 11, 16, skin);
    c.fillEllipse(x + 9, y, 11, 16, skin);
    c.fillEllipse(x, y, 13, 17, rib);
    c.fillEllipse(x, y, 10, 16, skin);
    c.line(x, y - 15, x + 3, y - 23, 4, sh(0x4E6B2A));  // stalk
    const Rgb face = lit ? 0xFFD36B : sh(0x5A2A08);
    c.fillTriangle(x - 10, y - 2, x - 3, y - 2, x - 6.5f, y - 9, face);
    c.fillTriangle(x + 3, y - 2, x + 10, y - 2, x + 6.5f, y - 9, face);
    c.fillTriangle(x - 3, y + 1, x + 3, y + 1, x, y + 5, face);
    c.fillRoundRect(x - 11, y + 6, 22, 5, 2, face);
    c.fillRect(x - 4, y + 6, 3, 2, skin);
    c.fillRect(x + 2, y + 9, 3, 2, skin);
}

// Leaves on the ground, scattered over the clearing (part of the scenery).
void Scene::drawFallenLeaves(Canvas& c) {
    const Rgb cols[] = {0xC8502A, 0xD98B2B, 0xB8862E, 0x9C3B22};
    for (int i = 0; i < 34; i++) {
        const uint32_t h = static_cast<uint32_t>(i + 7) * 2654435761u;
        const float x = static_cast<float>(h % 470) + 5, y = kHorizon + 40 + static_cast<float>((h >> 9) % 250);
        c.fillEllipse(x, y, 4.5f, 2.6f, sh(cols[i % 4]), 0.9f);
    }
}

void Scene::drawFallingLeaves(Canvas& c) {
    const Rgb cols[] = {0xD9622B, 0xE8A23A, 0xC8B03A, 0xB0402A};
    const float t = static_cast<float>(t_);
    const float wind = std::min(weather_.windKmh, 40.0f) * 1.5f;
    for (int i = 0; i < 12; i++) {
        const uint32_t h = static_cast<uint32_t>(i + 3) * 2654435761u;
        const float vy = 22.0f + static_cast<float>(i % 4) * 7;
        const float y = std::fmod(static_cast<float>(h % 1000) * 0.5f + t * vy, 520.0f) - 20;
        const float sway = std::sin(t * 1.1f + static_cast<float>(i) * 1.7f) * 18;
        const float x = std::fmod(static_cast<float>((h >> 10) % 480) + t * wind + sway + 4800.0f, 480.0f);
        const float turn = std::fabs(std::sin(t * 2.3f + static_cast<float>(i)));  // tumbling
        c.fillEllipse(x, y, 2 + 3.5f * turn, 2.6f, cols[i % 4], 0.95f);
    }
}

// Bats over the trees at dusk and after dark, in Halloween time.
void Scene::drawBats(Canvas& c) {
    if (dark_ < 0.2f) return;
    const float t = static_cast<float>(t_);
    const Rgb col = mix(0x2A1E2E, 0x0A0810, dark_);
    for (int i = 0; i < 4; i++) {
        const float speed = 38.0f + static_cast<float>(i) * 9;
        const float x = std::fmod(t * speed + static_cast<float>(i) * 151, 600.0f) - 60;
        const float y = 70.0f + static_cast<float>(i) * 22 + std::sin(t * 1.3f + static_cast<float>(i)) * 14;
        const float flap = std::sin(t * 14 + static_cast<float>(i)) * 6;
        c.fillTriangle(x - 2, y, x - 14, y - 4 + flap, x - 9, y + 4, col);
        c.fillTriangle(x + 2, y, x + 14, y - 4 + flap, x + 9, y + 4, col);
        c.fillEllipse(x, y + 1, 3.2f, 4, col);
    }
}

void Scene::drawClouds(Canvas& c) {
    const float o = overcast();
    if (o <= 0) return;
    const Rgb light = mix(0xF2F4F7, 0x3A4252, dark_), heavy = mix(0x7D8590, 0x1C212B, dark_);
    const Rgb col = mix(light, heavy, (o - 0.35f) / 0.45f);
    const int n = o < 0.4f ? 4 : 7;
    for (int i = 0; i < n; i++) {
        const float x = static_cast<float>((i * 151 + 40) % 520) - 20, y = 28.0f + static_cast<float>((i * 37) % 70);
        const float w = 46.0f + static_cast<float>((i * 23) % 30);
        c.fillEllipse(x, y + 10, w, 16, col, 0.92f);
        c.fillCircle(x - w * 0.35f, y + 2, w * 0.38f, col, 0.92f);
        c.fillCircle(x + w * 0.2f, y - 6, w * 0.48f, col, 0.92f);
    }
}

void Scene::drawSnowCover(Canvas& c) {
    const Rgb snow = sh(0xEEF3F8);
    c.fillRect(0, kHorizon + 6, S, S - kHorizon - 6, snow, 0.75f);
    c.fillEllipse(240, 372, 252, 122, snow, 0.6f);
}

void Scene::drawFog(Canvas& c) {
    // Thickest along the tree line, thinning out above and below in soft bands.
    const Rgb fog = mix(0xD9DEE3, 0x3A4048, dark_);
    for (int i = 0; i < 24; i++) {
        const float y = 80.0f + i * 12.0f, d = std::fabs(y + 6 - 230.0f) / 150.0f;
        if (d < 1) c.fillRect(0, y, S, 12, fog, 0.5f * (1 - d * d));
    }
}

// Rain and snow, from the time alone: each drop's place is worked out, nothing is stored.
void Scene::drawPrecipitation(Canvas& c) {
    const Sky sky = weather_.sky;
    const bool rain = sky == Sky::Rain || sky == Sky::Drizzle || sky == Sky::Storm;
    if (!rain && sky != Sky::Snow) return;
    const float wind = std::min(weather_.windKmh, 40.0f) * (rain ? 6.0f : 2.0f);  // sideways px/s
    const float t = static_cast<float>(t_);
    if (rain) {
        const int n = sky == Sky::Drizzle ? 30 : sky == Sky::Storm ? 100 : 65;
        const float vy = sky == Sky::Drizzle ? 330.0f : 560.0f;
        const Rgb col = mix(0xB8D0E8, 0x6A7F99, dark_);
        for (int i = 0; i < n; i++) {
            const uint32_t h = static_cast<uint32_t>(i) * 2654435761u;
            const float y = std::fmod(static_cast<float>(h % 1000) * 0.52f + t * vy, 520.0f) - 20;
            const float x = std::fmod(static_cast<float>((h >> 10) % 480) + t * wind + 4800.0f, 480.0f);
            const float len = sky == Sky::Drizzle ? 7.0f : 13.0f;
            c.line(x, y, x - wind / vy * len, y - len, sky == Sky::Drizzle ? 1.2f : 1.6f, col, 0.6f);
        }
    } else {
        const Rgb col = mix(0xFFFFFF, 0xB8C2CE, dark_);
        for (int i = 0; i < 55; i++) {
            const uint32_t h = static_cast<uint32_t>(i) * 2654435761u;
            const float vy = 30.0f + static_cast<float>(i % 5) * 9;
            const float y = std::fmod(static_cast<float>(h % 1000) * 0.52f + t * vy, 520.0f) - 20;
            const float sway = std::sin(t * 1.3f + static_cast<float>(i)) * 10;
            const float x = std::fmod(static_cast<float>((h >> 10) % 480) + t * wind + sway + 4800.0f, 480.0f);
            c.fillCircle(x, y, i % 3 == 0 ? 2.6f : 1.8f, col, 0.9f);
        }
    }
}

// Twinkling, so drawn every frame: only in the open sky between the trees, and not over the moon.
void Scene::drawStars(Canvas& c) {
    if (dark_ <= 0.3f || overcast() > 0) return;  // no stars through cloud
    const float a = (dark_ - 0.3f) / 0.7f;
    for (int k = 0; k < 40; k++) {
        float x = static_cast<float>((k * 97 + 13) % S), y = static_cast<float>((k * 53 + 7) % 190);
        if (x < 132 || x > 348 || ((x - 330) * (x - 330) + (y - 70) * (y - 70) < 24 * 24)) continue;
        float tw = 0.5f + 0.5f * static_cast<float>(std::sin(t_ * 1.7 + k * 1.3));
        c.fillCircle(x, y, k % 5 == 0 ? 1.6f : 1.1f, 0xFFF8E0, a * (0.4f + 0.6f * tw));
    }
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
    // A grey sky under cloud, darker for rain and storms.
    const Rgb grey = mix(0x9AA3AD, 0x161A22, dark_);
    const float o = overcast();
    c.verticalGradient(0, static_cast<int>(kHorizon) + 20, mix(mix(keys[i].top, keys[i + 1].top, t), grey, o),
                       mix(mix(keys[i].bottom, keys[i + 1].bottom, t), grey, o * 0.8f));

    if (dark_ > 0.3f && o < 0.45f) {
        float a = (dark_ - 0.3f) / 0.7f * (1 - o);
        c.fillCircle(330, 70, 18, halloween() ? 0xF5B95E : 0xF4F1DE, a);  // a harvest moon at Halloween
        c.fillCircle(338, 64, 15, mix(keys[i].top, keys[i + 1].top, t), a);  // crescent
    } else if (overcast() < 0.45f) {
        c.fillCircle(150, 76, 22, 0xFFE9A8, (1 - dark_ / 0.3f) * (1 - overcast()));
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
        if (weather_.sky == Sky::Snow)  // a cap of snow on each tier
            c.fillTriangle(x - hw * 0.42f, y - 50 * size, x + hw * 0.42f, y - 50 * size, x, y - 85 * size, sh(0xEEF3F8));
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
    float fl = static_cast<float>(std::sin(t_ * 13) * 0.5 + std::sin(t_ * 7.3) * 0.5);  // its light is in the scenery
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
    const Traits t = sim_.traits();
    if (t.rarity >= Rarity::Rare) {  // something special inside
        Rgb tint = 0xFFD34D;
        if (t.variant == Variant::Swamp) tint = 0xE08A55;
        if (t.variant == Variant::Snowcoat) tint = 0xBFE3FF;
        if (t.variant == Variant::Albino) tint = 0xFFE6F0;
        float strength = t.rarity == Rarity::Rare ? 0.3f : (t.rarity == Rarity::Epic ? 0.42f : 0.55f);
        float pulse = 0.75f + 0.25f * static_cast<float>(std::sin(t_ * 2));
        c.glow(kEggX, y, 90, tint, strength * pulse);
    }
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
    Rgb fur, face, mark;
    uint8_t markings, shag;
    float blink, lookX, armL, armR;
    float mouth;  // -1 frown, 0 flat, 1 smile, 2 chewing
    float breath, wide;
    bool angry;
    bool hat;  // a witch's hat, Halloween week
};

void Scene::drawPet(Canvas& c) {
    const State& s = sim_.state();
    Look L{};
    L.x = squatchX();
    L.foot = L.ground = squatchFoot();
    L.s = squatchScale();
    L.head = headRatio(s.stage);
    L.wide = 1;

    const Traits dna = sim_.traits();
    L.wide = dna.build * (s.adult == AdultForm::CouchSquatch ? 1.15f : 1.0f);
    L.markings = dna.markings;
    L.shag = dna.shag;
    Rgb fur, face, mark;
    furColours(fur, face, mark);
    // Shaded for the night, but less than the scenery, so it stays easy to see.
    L.fur = mix(fur, kNight, dark_ * 0.35f);
    L.face = mix(face, kNight, dark_ * 0.35f);
    L.mark = mix(mark, kNight, dark_ * 0.35f);

    float t = static_cast<float>(t_);
    L.blink = (s.asleep || now_ < blinkUntil_) ? 1.0f : 0.0f;
    L.lookX = lookX_;
    L.breath = 1 + 0.018f * std::sin(t * (s.asleep ? 1.6f : 2.6f));
    L.mouth = s.happiness >= 2 ? 1.0f : (s.happiness < 1 ? -1.0f : 0.0f);
    L.angry = s.misbehaving;
    L.hat = hatWeek();

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

    const float bodyY = f - 66 * s * b, bodyRx = 56 * s * w, bodyRy = 62 * s * b;
    c.fillEllipse(x, bodyY, bodyRx, bodyRy, L.fur);
    // Shaggy coats stick out in tufts down both sides.
    if (L.shag > 0) {
        const int n = 2 + L.shag * 2;
        const float len = (5 + 3.5f * L.shag) * s, half = 7 * s;
        for (float side : {-1.0f, 1.0f}) {
            for (int j = 0; j < n; j++) {
                float a = (0.2f + 0.65f * j / (n - 1)) * kPi;
                float px = x + side * bodyRx * std::sin(a), py = bodyY - bodyRy * std::cos(a);
                float nx = side * std::sin(a) / bodyRx, ny = -std::cos(a) / bodyRy;
                float nl = std::hypot(nx, ny);
                nx /= nl;
                ny /= nl;
                c.fillTriangle(px - ny * half - nx * 2, py + nx * half - ny * 2, px + ny * half - nx * 2,
                               py - nx * half - ny * 2, px + nx * len, py + ny * len, L.fur);
            }
        }
    }
    c.fillEllipse(x, f - 56 * s, 34 * s * w, 40 * s * b, L.face);
    if (L.markings == 1) c.fillEllipse(x, f - 64 * s, 16 * s * w, 24 * s * b, L.mark);  // chest blaze
    if (L.markings == 3) {  // speckles on the shoulders
        const float dots[][2] = {{-38, -92}, {-30, -76}, {-44, -70}, {36, -94}, {28, -80}, {42, -72}, {-20, -104}};
        for (const auto& d : dots) c.fillCircle(x + d[0] * s * w, f + d[1] * s * b, 3.4f * s, L.mark);
    }

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
    if (L.shag > 1) {  // a wild coat gets a mop on top
        for (int j = -L.shag; j <= L.shag; j++) {
            float a = j * 0.32f;
            float px = x + std::sin(a) * hr, py = hy - std::cos(a) * hr;
            c.fillTriangle(px - std::cos(a) * hr * 0.2f, py - std::sin(a) * hr * 0.2f, px + std::cos(a) * hr * 0.2f,
                           py + std::sin(a) * hr * 0.2f, px + std::sin(a) * hr * 0.3f, py - std::cos(a) * hr * 0.3f,
                           L.fur);
        }
    }
    c.fillEllipse(fx, hy + hr * 0.2f, hr * 0.7f, hr * 0.62f, L.face);
    if (L.markings == 2) c.fillEllipse(fx, hy + hr * 0.07f, hr * 0.56f, hr * 0.22f, L.mark);  // eye mask

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

    if (L.hat) {  // a little witch's hat, tipped back a bit
        const float by = hy - hr * 0.78f, tilt = hr * 0.35f;
        const Rgb hat = 0x3B2452, band = 0xE37B1C;
        c.fillTriangle(x - hr * 0.55f, by, x + hr * 0.55f, by, x + tilt, by - hr * 1.25f, hat);
        c.fillRoundRect(x - hr * 0.52f, by - hr * 0.2f, hr * 1.04f, hr * 0.16f, hr * 0.05f, band);
        c.fillEllipse(x, by + hr * 0.02f, hr * 0.95f, hr * 0.17f, hat);
    }
}

void Scene::drawHidingHead(Canvas& c) {
    if (react_ == React::Flinch) return;  // ducked out of sight
    Rgb fur, face, mark;
    furColours(fur, face, mark);
    Rgb shadow = sh(mix(fur, 0x000000, 0.45f));  // in the tree's shade
    c.fillCircle(kHideX, kHideY, 24, shadow);
    c.fillEllipse(kHideX, kHideY - 22, 10, 7, shadow);
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
    drawBattery(c, 330, 14);
    char buf[12];
    std::snprintf(buf, sizeof buf, "%u", static_cast<unsigned>(s.footprints % 10000));
    c.text(426, 15, buf, 2, kUiLight);
}

// A small battery: the fill shows the charge, a bolt while it charges, red and pulsing when low.
void Scene::drawBattery(Canvas& c, float x, float y) {
    if (batteryPct_ < 0) return;
    bool low = batteryPct_ <= 15 && !charging_;
    float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(t_ * 6));
    Rgb fill = low ? 0xE5484D : (batteryPct_ <= 30 ? kGold : 0x7BC86C);
    c.fillRoundRect(x, y, 32, 17, 4, kUiLight, low ? 0.6f + 0.4f * pulse : 1.0f);
    c.fillRoundRect(x + 32, y + 5, 3, 7, 1, kUiLight);
    c.fillRect(x + 2.5f, y + 2.5f, 27, 12, kUiDark);
    c.fillRect(x + 2.5f, y + 2.5f, 27 * std::max(0.08f, batteryPct_ / 100.0f), 12, fill);
    if (charging_) {
        c.fillTriangle(x + 18, y + 1, x + 10, y + 10, x + 16, y + 10, kUiLight);
        c.fillTriangle(x + 16, y + 7, x + 14, y + 16, x + 22, y + 7, kUiLight);
    }
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

void Scene::drawForagingBanner(Canvas& c) {
    const State& s = sim_.state();
    char name[24], buf[64];
    landmarkName(s.forageHash, name, sizeof name);
    int mins = static_cast<int>((sim_.forageSecsLeft() + 59) / 60);
    std::snprintf(buf, sizeof buf, "%s - %d MIN", name, std::max(1, mins));
    int scale = Canvas::textWidth(buf, 2) <= 440 ? 2 : 1;
    float w = static_cast<float>(Canvas::textWidth(buf, scale)) + 24;
    c.fillRoundRect(240 - w / 2, 88, w, 26, 13, kUiDark, 0.7f);
    c.text(240, 101 - Canvas::textHeight(scale) / 2.0f, buf, scale, kUiLight, Align::Center);
}

void Scene::drawLandmarkIcon(Canvas& c, LandmarkKind k, float x, float y, float z) {
    c.fillCircle(x, y, 24 * z, 0x35603F);
    switch (k) {
        case LandmarkKind::Falls:
            c.fillRect(x - 14 * z, y - 18 * z, 8 * z, 30 * z, 0x6B6B5A);
            c.fillRect(x - 6 * z, y - 18 * z, 10 * z, 34 * z, 0xA8E0F0);
            c.fillEllipse(x, y + 16 * z, 16 * z, 5 * z, 0xD6F2FA);
            break;
        case LandmarkKind::Cabin:
            c.fillRect(x - 14 * z, y - 4 * z, 28 * z, 18 * z, 0x8A5A2B);
            c.fillTriangle(x - 18 * z, y - 4 * z, x + 18 * z, y - 4 * z, x, y - 20 * z, 0x5E3B1A);
            c.fillRect(x - 4 * z, y + 4 * z, 8 * z, 10 * z, 0x2A1A10);
            break;
        case LandmarkKind::Cave:
            c.fillEllipse(x, y + 4 * z, 20 * z, 16 * z, 0x6B6B5A);
            c.fillEllipse(x, y + 10 * z, 10 * z, 10 * z, 0x1A120C);
            break;
        case LandmarkKind::BerryPatch:
            c.fillCircle(x, y + 4 * z, 15 * z, 0x2E6B3F);
            drawItem(c, Item::Berries, x, y + 2 * z, 0.9f * z);
            break;
        case LandmarkKind::Lookout:
            c.line(x - 10 * z, y + 18 * z, x - 4 * z, y - 8 * z, 3 * z, 0x8A5A2B);
            c.line(x + 10 * z, y + 18 * z, x + 4 * z, y - 8 * z, 3 * z, 0x8A5A2B);
            c.fillRect(x - 10 * z, y - 18 * z, 20 * z, 10 * z, 0x6B4A2F);
            c.fillTriangle(x - 13 * z, y - 18 * z, x + 13 * z, y - 18 * z, x, y - 25 * z, 0x5E3B1A);
            break;
        case LandmarkKind::HotSpring:
            c.fillEllipse(x, y + 10 * z, 18 * z, 7 * z, 0x7FC4D6);
            for (int i = -1; i <= 1; i++) c.line(x + i * 7 * z, y + 2 * z, x + i * 7 * z + 3 * z, y - 14 * z, 2.5f * z, 0xF3EBD9, 0.8f);
            break;
        case LandmarkKind::Pond:
            c.fillEllipse(x, y + 6 * z, 19 * z, 10 * z, 0x5F87A3);
            c.line(x + 8 * z, y + 2 * z, x + 10 * z, y - 16 * z, 2 * z, 0x5C8F3E);
            c.fillEllipse(x + 10 * z, y - 16 * z, 3 * z, 6 * z, 0x8A5A2B);
            break;
        case LandmarkKind::Ridge:
            c.fillTriangle(x - 20 * z, y + 14 * z, x - 2 * z, y - 16 * z, x + 10 * z, y + 14 * z, 0x6B6B5A);
            c.fillTriangle(x - 4 * z, y + 14 * z, x + 10 * z, y - 6 * z, x + 20 * z, y + 14 * z, 0x55554A);
            c.fillTriangle(x - 6 * z, y - 10 * z, x - 2 * z, y - 16 * z, x + 2 * z, y - 10 * z, 0xF3EBD9);
            break;
    }
}

void Scene::drawTrails(Canvas& c) {
    const State& s = sim_.state();
    const Rgb ink = 0x3B2A1E, faint = 0x8A7A66;
    c.tint(0x000000, 0.55f);
    c.fillRoundRect(kMapX, kMapY, kMapW, kMapH, 20, kUiLight);
    c.text(240, kMapY + 10, "TRAIL MAP", 2, ink, Align::Center);

    for (int i = 0; i < s.trailCount; i++) {
        const Trail& tr = s.trails[i];
        Rect r = trailRowArea(i);
        bool out = s.foraging && s.forageHash == tr.hash;
        c.fillRoundRect(r.x, r.y + 3, r.w, r.h - 6, 12, out ? 0xF2D98A : 0xE6DCC6);
        drawLandmarkIcon(c, landmarkKind(tr.hash), r.x + 34.0f, r.cy(), 0.95f);
        char name[24];
        landmarkName(tr.hash, name, sizeof name);
        c.text(r.x + 68, r.y + 14, name, 2, ink);
        char sub[32];
        std::snprintf(sub, sizeof sub, "%s%s", toString(trailDistance(tr.rssi)), out ? "  - IT'S HERE NOW" : "");
        c.text(r.x + 68, r.y + 36, sub, 2, faint);
        if (!sim_.trailVisited(tr.hash)) {  // a place it's never been
            c.fillRoundRect(r.x + r.w - 64, r.y + 20, 52, 24, 12, kGold);
            c.text(r.x + r.w - 38.0f, r.y + 25, "NEW", 2, ink, Align::Center);
        }
    }
    if (s.trailCount == 0) {
        c.text(240, kRowY + 60, scanning_ ? "LISTENING..." : "NO TRAILS YET", 2, faint, Align::Center);
        c.text(240, kRowY + 90, "NEARBY WI-FI BECOMES PLACES TO EXPLORE", 1, faint, Align::Center);
    }

    if (trailNote_[0]) {  // under the title, above the rows
        int scale = Canvas::textWidth(trailNote_, 2) <= kMapW - 24 ? 2 : 1;
        c.text(240, kMapY + 30.0f, trailNote_, scale, 0x5E8A3C, Align::Center);
    }
    Rect b = rescanArea();
    c.fillRoundRect(b.x, b.y + 4, b.w, b.h - 8, 14, scanning_ ? 0xB8AE9A : kButton);
    c.text(b.cx(), b.cy() - 7.0f, scanning_ ? "LISTENING..." : "LOOK AGAIN", 2, kUiLight, Align::Center);
}

void Scene::drawNaming(Canvas& c) {
    const Rgb ink = 0x3B2A1E, faint = 0x8A7A66, key = 0xE6DCC6;
    c.tint(0x000000, 0.55f);
    c.fillRoundRect(8, 10, 464, 460, 20, kUiLight);
    c.text(240, 22, "WHAT'S ITS NAME?", 2, ink, Align::Center);

    // the name so far, with a blinking cursor
    c.fillRoundRect(40, 46, 400, 60, 12, 0xFFFFFF, 0.6f);
    const size_t len = std::strlen(nameDraft_);
    char shown[kMaxNameLen + 2];
    const bool blink = static_cast<int>(t_ * 2) % 2 == 0;
    std::snprintf(shown, sizeof shown, "%s%s", nameDraft_, len < static_cast<size_t>(kMaxNameLen) && blink ? "_" : "");
    c.text(240, 62, len || blink ? shown : " ", 4, ink, Align::Center);

    for (int i = 0; i < kNameKeys; i++) {
        Rect r = nameKeyArea(i);
        c.fillRoundRect(r.x + 3, r.y + 3, r.w - 6, r.h - 6, 10, i < 26 ? key : 0xD9CFBC);
        const float cx = static_cast<float>(r.cx()), cy = static_cast<float>(r.cy());
        if (i < 26) {
            char ch[2] = {static_cast<char>('A' + i), 0};
            c.text(cx, cy - 10, ch, 3, ink, Align::Center);
        } else if (i == 26) {  // space: a little bracket
            c.line(cx - 14, cy + 2, cx - 14, cy + 8, 3, ink);
            c.line(cx - 14, cy + 8, cx + 14, cy + 8, 3, ink);
            c.line(cx + 14, cy + 8, cx + 14, cy + 2, 3, ink);
        } else {  // delete: a left arrow with a cross
            c.fillTriangle(cx - 20, cy, cx - 8, cy - 12, cx - 8, cy + 12, ink);
            c.fillRect(cx - 8, cy - 12, 26, 24, ink);
            c.line(cx - 2, cy - 6, cx + 10, cy + 6, 3, key);
            c.line(cx - 2, cy + 6, cx + 10, cy - 6, 3, key);
        }
    }
    Rect s = nameSuggestArea(), d = nameDoneArea();
    c.fillRoundRect(s.x + 3, s.y + 4, s.w - 6, s.h - 8, 14, 0x6E8F5A);
    c.text(s.cx(), s.cy() - 7.0f, "SUGGEST", 2, kUiLight, Align::Center);
    bool ready = false;
    for (size_t i = 0; i < len; i++) ready |= nameDraft_[i] != ' ';
    c.fillRoundRect(d.x + 3, d.y + 4, d.w - 6, d.h - 8, 14, ready ? kButton : 0xB8AE9A);
    c.text(d.cx(), d.cy() - 7.0f, "DONE", 2, kUiLight, Align::Center);
    (void)faint;
}

void Scene::drawSettings(Canvas& c) {
    const Rgb ink = 0x3B2A1E, faint = 0x8A7A66, off = 0xD9CFBC;
    c.tint(0x000000, 0.55f);
    c.fillRoundRect(kSetX, kSetY, kSetW, kSetH, 20, kUiLight);
    c.text(240, kSetY + 12, "SETTINGS", 2, ink, Align::Center);

    const char* labels[kSettingRows] = {"BRIGHTNESS", "SCREEN OFF AFTER", "VOLUME"};
    for (int row = 0; row < kSettingRows; row++) {
        const int y = kSetRowY[row];
        c.text(240, y - 24.0f, labels[row], 2, faint, Align::Center);
        const int value = row == 0 ? settings_.brightness : row == 1 ? settings_.timeout : settings_.volume;
        const int top = (row == 0 ? kBrightnessSteps : row == 1 ? kTimeoutSteps : kVolumeSteps) - 1;
        for (int plus = 0; plus < 2; plus++) {
            Rect b = settingButtonArea(row, plus);
            bool can = plus ? value < top : value > 0;
            c.fillRoundRect(b.x + 4, b.y + 4, b.w - 8, b.h - 8, 14, can ? kButton : off);
            float cx = b.cx(), cy = b.cy();
            c.line(cx - 14, cy, cx + 14, cy, 5, kUiLight);
            if (plus) c.line(cx, cy - 14, cx, cy + 14, 5, kUiLight);
        }
        char buf[16];
        if (row == 0) std::snprintf(buf, sizeof buf, "%d%%", brightnessPercent());
        else std::snprintf(buf, sizeof buf, "%s", row == 1 ? kTimeoutNames[value] : kVolumeNames[value]);
        c.text(240, y + 14.0f, buf, 3, ink, Align::Center);
        // one pip per step, filled up to the setting (volume: none lit when it's off)
        const bool fromZero = row == 2;
        const int steps = fromZero ? top : top + 1, lit = fromZero ? value : value + 1;
        const float pip = 14, gap = 6, w = steps * pip + (steps - 1) * gap;
        for (int i = 0; i < steps; i++)
            c.fillRoundRect(240 - w / 2 + i * (pip + gap), y + 48.0f, pip, 8, 4, i < lit ? kGold : off);
    }

    Rect d = settingsDoneArea();
    c.fillRoundRect(d.x, d.y + 4, d.w, d.h - 8, 14, kButton);
    c.text(d.cx(), d.cy() - 7.0f, "DONE", 2, kUiLight, Align::Center);
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
        case Slot::More:
            for (int i = -1; i <= 1; i++) c.fillCircle(x + i * 14.0f, y, 5, kUiLight);
            break;
        case Slot::Trails:  // a signpost
            c.line(x, y + 22, x, y - 18, 5, 0xA0703F);
            c.fillTriangle(x - 2, y - 20, x + 24, y - 13, x - 2, y - 6, 0xCFC7B5);
            c.fillTriangle(x + 2, y - 4, x - 24, y + 3, x + 2, y + 10, 0xCFC7B5);
            break;
        case Slot::Settings:  // a gear
            for (int i = 0; i < 8; i++) {
                float a = i * 0.785398f;
                c.line(x, y, x + std::cos(a) * 21, y + std::sin(a) * 21, 8, 0xCFC7B5);
            }
            c.fillCircle(x, y, 15, 0xCFC7B5);
            c.fillCircle(x, y, 6, kButton);
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
    c.text(240, 92, name, Canvas::textWidth(name, 3) <= 380 ? 3 : 2, ink, Align::Center);
    if (s.stage != Stage::Egg) c.text(240, 118, "TAP THE NAME TO CHANGE IT", 1, 0x8A7A66, Align::Center);

    char lines[10][40];
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
    const Traits t = sim_.traits();
    if (t.variant != Variant::None)
        std::snprintf(lines[n++], 40, "COAT: %s %s", toString(t.rarity), toString(t.variant));
    else
        std::snprintf(lines[n++], 40, "COAT: %s", toString(t.rarity));
    char code[12];
    dnaCode(s.genome, code, sizeof code);
    std::snprintf(lines[n++], 40, "DNA: %s  TRAILS: %u", code, static_cast<unsigned>(s.trailsVisited));
    if (batteryPct_ >= 0)
        std::snprintf(lines[n++], 40, "BATTERY: %d%%%s", batteryPct_, charging_ ? " (CHARGING)" : "");
    if (weather_.sky != Sky::Unknown && n < 10) {
        char sky[12];
        std::snprintf(sky, sizeof sky, "%s", toString(weather_.sky));
        for (char* p = sky; *p; p++) *p = static_cast<char>(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
        const float temp = fahrenheit_ ? weather_.tempC * 9 / 5 + 32 : weather_.tempC;
        std::snprintf(lines[n++], 40, "WEATHER: %s, %d%s", sky, static_cast<int>(std::lround(temp)), fahrenheit_ ? "F" : "C");
    }
    for (int i = 0; i < n; i++) c.text(64, 136 + i * 23.0f, lines[i], 2, ink);
    c.text(240, 366, "TAP TO CLOSE", 2, 0x8A7A66, Align::Center);
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
        case Scene::Slot::More: return "MORE";
        case Scene::Slot::Trails: return "TRAILS";
        case Scene::Slot::Settings: return "SETTINGS";
        case Scene::Slot::Back: return "BACK";
        case Scene::Slot::Berries: return "BERRIES";
        case Scene::Slot::Fish: return "FISH";
        case Scene::Slot::Roots: return "ROOTS";
    }
    return "?";
}

}  // namespace squatch
