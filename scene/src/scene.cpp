#include "squatch/scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "qrcodegen.h"

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
constexpr float kHideHorseX = 40;             // the hiding foal's middle, just behind the apple tree's trunk
constexpr float kEggX = 240, kEggY = 288;
constexpr float kFriendlyFoot = 345, kWanderFoot = 302;
constexpr float kGroomX = 214, kGroomFoot = 428, kGroomZoom = 1.8f;  // Grooming: it stands big in the middle
constexpr float kMessSpots[3][2] = {{168, 350}, {312, 352}, {132, 326}};

// ---- feel
constexpr float kStrokeShare = 0.6f;      // stroke across this much of the pet's width to pet it
constexpr float kMaxStrokeSpeed = 1500;   // px/s; a flick is not a stroke
constexpr uint32_t kToastMs = 2600;

// ---- colours
constexpr Rgb kNight = 0x0A1030;
constexpr Rgb kGrass = 0x5E9E4A, kClearing = 0x86B85A, kBackTrees = 0x2F5D50;
constexpr Rgb kTrunk = 0x6B4A2F, kLeaves1 = 0x2E6B3F, kLeaves2 = 0x3A7F4A;
constexpr Rgb kPaddock = 0xC4B47E;  // the horse's paddock: trampled earth and sand
constexpr Rgb kEye = 0x1A120C;  // fur and face colours come from the DNA (furColours)
constexpr Rgb kUiDark = 0x1B2416, kUiLight = 0xF3EBD9, kGold = 0xF2C14E;
constexpr Rgb kTray = 0x2B2118, kButton = 0x4A3A2C;

constexpr Scene::Slot kMainSlots[] = {Scene::Slot::Feed, Scene::Slot::Clean, Scene::Slot::Medicine,
                                      Scene::Slot::Play, Scene::Slot::Light, Scene::Slot::More};
constexpr Scene::Slot kFoodSlots[] = {Scene::Slot::Back, Scene::Slot::Berries, Scene::Slot::Fish,
                                      Scene::Slot::Roots};
constexpr Scene::Slot kMoreSlots[] = {Scene::Slot::Back, Scene::Slot::Photos, Scene::Slot::Trails,
                                      Scene::Slot::Stable, Scene::Slot::Settings};

// ---- the naming page
constexpr int kKeyCols = 7, kKeyW = 64, kKeyH = 64;
constexpr int kKeyX = (480 - kKeyCols * kKeyW) / 2, kKeyY = 118;
constexpr const char* kSuggestedNames[] = {"MOSS",   "BRAMBLE", "HUCKLE", "FERN",   "BIRCH",  "TIMBER",
                                           "CEDAR",  "JUNIPER", "HAZEL",  "BISCUIT", "SCOUT", "PEBBLE",
                                           "THISTLE", "MAPLE",  "BOULDER", "WILLOW", "SHAGGY", "ROWAN"};
// Horse names, for the horse edition (18, the same count, so the DNA's pick still works).
constexpr const char* kHorseNames[] = {"STAR",   "CLOVER", "MAPLE",  "DAISY",  "COMET",  "BUTTONS",
                                       "HONEY",  "SPIRIT", "PEPPER", "WILLOW", "SUNNY",  "BISCUIT",
                                       "STARDUST", "MAGIC", "TWINKLE", "ACORN", "BLAZE", "NUTMEG"};
static_assert(sizeof kHorseNames == sizeof kSuggestedNames, "keep the name lists the same length");

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
        case Scene::Slot::Berries: return Scene::Item::Berries;
        case Scene::Slot::Fish: return Scene::Item::Fish;
        case Scene::Slot::Roots: return Scene::Item::Roots;
        default: return Scene::Item::None;
    }
}

bool isFood(Scene::Item it) {
    return it == Scene::Item::Berries || it == Scene::Item::Fish || it == Scene::Item::Roots;
}

// A repeatable 0..1 from a number: where each blade of grass or tuft of fur goes.
float noise01(uint32_t n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    n ^= n >> 15;
    return static_cast<float>(n & 0xFFFFFFu) / 16777216.0f;
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
    settings_.rudolph = s.rudolph ? 1 : 0;
}

// The pet's own voice (and body), as against the world's sounds and the buttons.
static bool isVoice(Cue c) {
    switch (c) {
        case Cue::Happy: case Cue::Eat: case Cue::Sneeze: case Cue::Sulk: case Cue::Knock:
        case Cue::Giggle: case Cue::Babble: case Cue::Neigh: case Cue::Nicker: case Cue::Snort:
        case Cue::Munch: case Cue::Clop: case Cue::Blow: case Cue::Chuckle: case Cue::Sigh: case Cue::MunchHay:
        case Cue::Toot: return true;
        default: return false;
    }
}

void Scene::cue(Cue c) {
    if ((settings_.volume == 0 && !(findingMe() && !findMeQuiet_)) || (screenOff_ && c != Cue::Knock)) return;  // (a lost toy calls out even when muted)
    // Speaking adds a little babble, unless the pet already made a sound for this moment:
    // one voice at a time, or a nicker on top of a whinny sounds wrong.
    const bool babble = c == Cue::Babble;
    for (int i = 0; i < cueCount_; i++) {
        if (!isVoice(cues_[i])) continue;
        if (babble) return;
        if (cueBabble_[i]) {  // the babble gives way to the real sound
            for (int j = i + 1; j < cueCount_; j++) {
                cues_[j - 1] = cues_[j];
                cueBabble_[j - 1] = cueBabble_[j];
            }
            cueCount_--;
            break;
        }
    }
    if (species_ == Species::Horse) {  // the horse's own voice
        switch (c) {
            case Cue::Happy:
            case Cue::Giggle:
            case Cue::Babble: c = Cue::Nicker; break;
            case Cue::Play:
            case Cue::Knock:
            case Cue::Returned: c = Cue::Neigh; break;
            case Cue::Sneeze:
            case Cue::Sulk: c = Cue::Snort; break;
            case Cue::Hatch: c = Cue::Neigh; break;   // the mother calls as her foal arrives
            case Cue::NewEgg: c = Cue::Nicker; break;
            case Cue::Eat: c = Cue::Munch; break;
            default: break;
        }
    }
    if (cueCount_ == static_cast<int>(sizeof cues_ / sizeof cues_[0])) return;  // nobody's listening
    cueBabble_[cueCount_] = babble;
    cues_[cueCount_++] = c;
}

float Scene::voicePitch(Cue c) const {
    switch (c) {  // the pet's voice and body; not the world's sounds or the buttons
        case Cue::Happy: case Cue::Eat: case Cue::Sneeze: case Cue::Sulk: case Cue::Knock:
        case Cue::Giggle: case Cue::Babble: case Cue::Neigh: case Cue::Nicker: case Cue::Snort:
        case Cue::Munch: case Cue::Clop: case Cue::Blow: case Cue::Chuckle: case Cue::Sigh: case Cue::MunchHay: break;
        case Cue::StepNear:
        case Cue::StepFar: {  // hooves: a little lighter on a foal, and never two quite alike
            const float jitter = 0.93f + 0.12f * noise01(now_ / 7 + static_cast<uint32_t>(c));
            switch (sim_.state().stage) {
                case Stage::Baby: return 1.3f * jitter;
                case Stage::Child: return 1.18f * jitter;
                case Stage::Teen: return 1.08f * jitter;
                case Stage::Elder: return 0.95f * jitter;
                default: return jitter;
            }
        }
        default: return 1;
    }
    // Never quite the same twice: a little up or down each time (a few percent either way).
    const float vary = species_ == Species::Horse ? 0.94f + 0.12f * noise01(now_ / 3 + static_cast<uint32_t>(c) * 977) : 1.0f;
    switch (sim_.state().stage) {
        case Stage::Baby: return 1.8f * vary;    // a foal's squeaky little whinny: nearly an octave up
        case Stage::Child: return 1.5f * vary;
        case Stage::Teen: return 1.22f * vary;
        case Stage::Elder: return 0.84f * vary;  // lower and slower with age
        case Stage::Egg: return 0.9f * vary;     // the mother, before her foal comes
        default: return vary;
    }
}

bool Scene::takeCue(Cue& out) {
    if (cueCount_ == 0) return false;
    out = cues_[0];
    for (int i = 1; i < cueCount_; i++) {
        cues_[i - 1] = cues_[i];
        cueBabble_[i - 1] = cueBabble_[i];
    }
    cueCount_--;
    return true;
}

bool Scene::takeSettingsChanged() {
    bool r = settingsChanged_;
    settingsChanged_ = false;
    return r;
}

int Scene::brightnessPercent() const { return findingMe() ? 100 : kBrightness[settings_.brightness]; }

void Scene::startFindMe(uint32_t durationMs) {
    findMeUntil_ = std::max<uint32_t>(1, now_ + durationMs);
    findMeNextCall_ = now_;
    chargeCall_ = findMeQuiet_ = false;
    wake();
}

void Scene::startChargeCall() {
    if (findingMe()) return;  // already calling out (a ping): that does the job
    const float h = hourOfDay();
    findMeUntil_ = std::max<uint32_t>(1, now_ + 60 * 1000);
    findMeNextCall_ = now_;
    chargeCall_ = true;
    findMeQuiet_ = h >= 21.0f || h < 7.0f;  // nobody wants a neigh at 3 am: just the light
    nextChargeCall_ = now_ + 20 * 60 * 1000;
    wake();
}

// From update: start (or repeat) the call while the battery is nearly empty; stop it once plugged in.
void Scene::updateChargeCall() {
    const bool empty = batteryPct_ >= 0 && batteryPct_ <= kChargeCallPercent && !charging_;
    if (!empty) {
        nextChargeCall_ = 0;
        if (callingForCharge()) findMeUntil_ = 0;
        return;
    }
    if (nextChargeCall_ == 0 || now_ >= nextChargeCall_) startChargeCall();
}

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
Scene::Rect Scene::nameEditArea() const { return bigText_ ? Rect{40, 52, 400, 64} : Rect{40, 76, 400, 64}; }

void Scene::openNaming() {
    std::snprintf(nameDraft_, sizeof nameDraft_, "%.10s", sim_.state().name);
    overlay_ = Overlay::Name;
    askedName_ = true;
}

void Scene::suggestName() {
    constexpr int n = static_cast<int>(sizeof kSuggestedNames / sizeof kSuggestedNames[0]);
    if (suggestion_ == 0) suggestion_ = static_cast<uint8_t>(1 + (sim_.state().genome.g[0] % n));  // its DNA picks the first
    const char* const* names = species_ == Species::Horse ? kHorseNames : kSuggestedNames;
    std::snprintf(nameDraft_, sizeof nameDraft_, "%.10s", names[(suggestion_ - 1) % n]);
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

// With Wi-Fi on the board the bottom row holds three buttons: Start over, Wi-Fi, Done.
// Two buttons along the bottom, or three with WI-FI (the boards) or PRIVACY (the store phone app).
Scene::Rect Scene::settingsDoneArea() const {
    if (wifiAvailable_ || privacyAvailable_) return {S / 2 + 76, kSetY + kSetH - 84, 132, 72};
    return {S / 2 + 12, kSetY + kSetH - 84, 196, 72};
}
Scene::Rect Scene::startOverArea() const {
    if (wifiAvailable_ || privacyAvailable_) return {S / 2 - 208, kSetY + kSetH - 84, 132, 72};
    return {S / 2 - 208, kSetY + kSetH - 84, 196, 72};
}
Scene::Rect Scene::privacyArea() const {
    if (!privacyAvailable_ || wifiAvailable_) return {0, 0, 0, 0};
    return {S / 2 - 66, kSetY + kSetH - 84, 132, 72};
}
Scene::Rect Scene::wifiButtonArea() const {
    if (!wifiAvailable_) return {0, 0, 0, 0};
    return {S / 2 - 66, kSetY + kSetH - 84, 132, 72};
}
Scene::Rect Scene::wifiSetupArea() const { return {S / 2 - 208, kSetY + kSetH - 84, 196, 72}; }
Scene::Rect Scene::updateArea() const { return {kSetX + 16, kSetY + kSetH - 160, kSetW - 32, 68}; }

void Scene::setUpdateStatus(UpdateStatus st, int build, int percent) {
    updateStatus_ = st;
    updateBuild_ = build;
    updatePercent_ = percent;
}

void Scene::setBackupCode(const char* code) { std::snprintf(backupCode_, sizeof backupCode_, "%s", code ? code : ""); }

bool Scene::updateShown() const {
    return updateStatus_ != UpdateStatus::Unavailable && (wifiStatus_ == WifiStatus::Saved || wifiStatus_ == WifiStatus::Joined);
}
Scene::Rect Scene::wifiDoneArea() const { return {S / 2 + 12, kSetY + kSetH - 84, 196, 72}; }

void Scene::setWifiAvailable(const char* hotspotName) {
    wifiAvailable_ = true;
    std::snprintf(wifiHotspot_, sizeof wifiHotspot_, "%s", hotspotName ? hotspotName : "");
    // "Join this open network", in the form phone cameras understand.
    char text[64];
    std::snprintf(text, sizeof text, "WIFI:T:nopass;S:%s;;", wifiHotspot_);
    uint8_t temp[sizeof qr_];
    static_assert(sizeof qr_ >= qrcodegen_BUFFER_LEN_FOR_VERSION(4), "room for a version 4 code");
    qrSize_ = qrcodegen_encodeText(text, temp, qr_, qrcodegen_Ecc_LOW, 1, 4, qrcodegen_Mask_AUTO, true)
                  ? qrcodegen_getSize(qr_)
                  : 0;
    qrUrlSize_ = qrcodegen_encodeText("http://192.168.4.1/", temp, qrUrl_, qrcodegen_Ecc_LOW, 1, 4, qrcodegen_Mask_AUTO, true)
                     ? qrcodegen_getSize(qrUrl_)
                     : 0;
}

void Scene::openWelcome() {
    if (!wifiAvailable_) return;
    welcome_ = true;
    overlay_ = Overlay::Wifi;
    trayTarget_ = 0;
    if (wifiStatus_ != WifiStatus::SetupRunning && wifiStatus_ != WifiStatus::Checking) {
        wifiRequest_ = 1;
        wifiStatus_ = WifiStatus::SetupRunning;  // the board confirms (or not) next frame
    }
}

void Scene::drawQr(Canvas& c, float cx, float y, int m) const {
    const bool page = setupPhone_ && qrUrlSize_ > 0;  // once a phone has joined: the page's address
    const uint8_t* qr = page ? qrUrl_ : qr_;
    const int size = page ? qrUrlSize_ : qrSize_;
    if (size <= 0) return;
    const int quiet = 2, side = (size + 2 * quiet) * m;
    const float x0 = std::round(cx - side / 2.0f), y0 = std::round(y);
    c.fillRoundRect(x0, y0, side, side, m, 0xFFFFFF);
    for (int r = 0; r < size; r++)
        for (int q = 0; q < size; q++)
            if (qrcodegen_getModule(qr, q, r)) c.fillRect(x0 + (q + quiet) * m, y0 + (r + quiet) * m, m, m, 0x000000);
}

void Scene::setWifiStatus(WifiStatus st, const char* ssid) {
    wifiStatus_ = st;
    if (st != WifiStatus::SetupRunning) setupPhone_ = false;
    if (welcome_ && st == WifiStatus::Joined) welcomeDone_ = true;  // set up: no need to welcome again
    // The font is capitals, digits and a little punctuation: show the name in those.
    size_t n = 0;
    for (const char* p = ssid ? ssid : ""; *p && n < sizeof wifiSsid_ - 1; p++) {
        char ch = *p;
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        wifiSsid_[n++] = ch;
    }
    wifiSsid_[n] = '\0';
}

void Scene::setWifiTown(const char* name) { std::snprintf(wifiTown_, sizeof wifiTown_, "%s", name ? name : ""); }

int Scene::takeWifiRequest() {
    const int r = wifiRequest_;
    wifiRequest_ = 0;
    return r;
}
// With the "like new" choice there are four buttons, so everything sits higher.
Scene::Rect Scene::startKeepArea() const {
    if (factoryAvailable_) return {48, bigText_ ? 168 : 156, 384, 68};
    return bigText_ ? Rect{48, 176, 384, 76} : Rect{48, 196, 384, 68};
}
Scene::Rect Scene::startWipeArea() const {
    if (factoryAvailable_) return {48, bigText_ ? 240 : 228, 384, 68};
    return bigText_ ? Rect{48, 260, 384, 68} : Rect{48, 276, 384, 68};
}
Scene::Rect Scene::startNewArea() const {
    if (!factoryAvailable_) return {0, 0, 0, 0};
    return {48, bigText_ ? 312 : 300, 384, 68};
}
Scene::Rect Scene::startCancelArea() const {
    if (factoryAvailable_) return {48, bigText_ ? 384 : 372, 384, 68};
    return bigText_ ? Rect{48, 336, 384, 68} : Rect{48, 356, 384, 68};
}

void Scene::startOver(bool keepCollection) {
    sim_.startOver(keepCollection);
    overlay_ = Overlay::None;
    game_ = Game::None;
    petOverride_ = running_ = false;
    held_ = Item::None;
    trayTarget_ = trayAnim_ = 0;  // closed at once: the start-over page was covering it
    page_ = Page::Main;
    askedName_ = false;  // the new one gets asked its name when it hatches
    askNameAt_ = UINT32_MAX;
    particleCount_ = 0;
    toastCount_ = 0;
    saveRequested_ = true;
    sceneryKey_ = -1;
}

void Scene::setWeather(const Weather& w) {
    const Sky before = weather_.sky;
    weather_ = w;
    if (w.sky == before || w.sky == Sky::Unknown) return;
    const bool first = before == Sky::Unknown;
    const char* msg = nullptr;
    switch (w.sky) {
        case Sky::Clear: msg = first ? nullptr : "THE SKY CLEARED"; break;
        case Sky::Cloudy: msg = first ? nullptr : "CLOUDS ROLLED IN"; break;
        case Sky::Fog: msg = species_ == Species::Horse ? "FOG OVER THE FIELDS" : "FOG IN THE WOODS"; break;
        case Sky::Drizzle: msg = "A LITTLE DRIZZLE"; break;
        case Sky::Rain: msg = species_ == Species::Horse ? "RAIN ON THE PADDOCK" : "RAIN IN THE FOREST"; break;
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

int Scene::localYear() const {
    const State& s = sim_.state();
    const int64_t z = (s.now + s.tzOffset) / 86400 + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    return static_cast<int>(yoe + era * 400 + (mp >= 10 ? 1 : 0));
}

namespace {
int dayOfYear(int m, int d) {
    static const int kBefore[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    return kBefore[(m - 1) % 12] + d;
}
// Easter Sunday (the anonymous Gregorian computus).
void easter(int y, int& month, int& day) {
    const int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
    const int h = (19 * a + b - d - g + 15) % 30, i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
    const int m = (a + 11 * h + 22 * l) / 451;
    month = (h + l - 7 * m + 114) / 31;
    day = (h + l - 7 * m + 114) % 31 + 1;
}
}  // namespace

Scene::Holiday Scene::holiday() const {
    int m, d;
    localDate(m, d);
    if (m == 10 && d >= 15) return Holiday::Halloween;
    if (m == 11) return Holiday::Thanksgiving;
    if (m == 12 || (m == 1 && d == 1)) return Holiday::Christmas;
    if (m == 2 && d >= 7 && d <= 14) return Holiday::Valentine;
    if (m == 3 && d >= 14 && d <= 17) return Holiday::StPatrick;
    if (m == 7 && d <= 4) return Holiday::July4;
    int em, ed;
    easter(localYear(), em, ed);
    const int today = dayOfYear(m, d), sunday = dayOfYear(em, ed);
    if (today >= sunday - 7 && today <= sunday) return Holiday::Easter;
    return Holiday::None;
}

bool Scene::fireworksNight() const {
    int m, d;
    localDate(m, d);
    return dark_ > 0.35f && ((m == 12 && d == 31) || (m == 1 && d == 1) || (m == 7 && d == 4));
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

// Where a holiday's decoration stands (the pumpkin's spot): tap it.
Scene::Rect Scene::pumpkinArea() const {
    if (holiday() == Holiday::Christmas) return {static_cast<int>(kPumpkinX) - 36, static_cast<int>(kPumpkinY) - 96, 72, 104};
    if (holiday() == Holiday::Thanksgiving)  // wherever it has strutted to
        return {static_cast<int>(kPumpkinX) - 120, static_cast<int>(kPumpkinY) - 44, 152, 70};
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
    if (findingMe()) {  // found! any touch stops it, and does nothing else
        if (e.phase == TouchPhase::Down) {
            findMeUntil_ = 0;
            screenOff_ = waking_ = false;
            say(chargeCall_ ? "PLEASE PLUG ME IN!" : "YOU FOUND ME!", true);
            cue(Cue::Happy);
        }
        return;
    }
    const uint8_t bit = static_cast<uint8_t>(1u << (e.id & 7));
    if (e.phase == TouchPhase::Down) fingers_ |= bit;
    else if (e.phase == TouchPhase::Up) fingers_ &= static_cast<uint8_t>(~bit);
    if (screenOff_ || waking_) {  // this touch only wakes the screen
        screenOff_ = false;
        waking_ = fingers_ != 0;
        return;
    }
    if (e.phase != TouchPhase::Up && e.y >= kStripTouchH) {  // its eyes follow your finger
        fingerX_ = e.x;
        fingerY_ = e.y;
        fingerUntil_ = now_ + 1500;
    }
    // Berry Catch: the basket goes wherever the finger is.
    if (overlay_ == Overlay::Game && game_ == Game::Berry && !gameOver_ && e.phase != TouchPhase::Up &&
        e.y >= kStripTouchH)
        basketX_ = std::max(60.0f, std::min(420.0f, static_cast<float>(e.x)));
    // Grooming: brushing is wherever the finger goes, a little for every bit it moves.
    if (overlay_ == Overlay::Game && game_ == Game::Groom && !gameOver_ &&
        (groomStep_ == GroomStep::Brush || groomStep_ == GroomStep::Mane)) {
        const float x = e.x, y = e.y;
        if (e.phase == TouchPhase::Up) {
            rubX_ = rubY_ = -1;
        } else {
            if (rubX_ >= 0) groomRub(x, y, std::hypot(x - rubX_, y - rubY_) / 110.0f);
            rubX_ = x;
            rubY_ = y;
        }
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
    if (overlay_ == Overlay::Game) {
        if (g.type == GestureType::Tap) gameTap(g.x, g.y);
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
    if (overlay_ == Overlay::StartOver) {
        if (g.type != GestureType::Tap) return;
        if (confirmNew_) {  // "erase it all?": only the yes button does it
            confirmNew_ = false;
            cue(Cue::Tick);
            if (startNewArea().contains(g.x, g.y)) factoryReset_ = true;  // the board erases and restarts
            return;
        }
        if (startKeepArea().contains(g.x, g.y)) startOver(true);
        else if (startWipeArea().contains(g.x, g.y)) startOver(false);
        else if (startNewArea().contains(g.x, g.y)) {
            confirmNew_ = true;
            cue(Cue::Tick);
        } else {
            overlay_ = Overlay::Settings;  // cancel, or a tap anywhere else: back to the settings
            cue(Cue::Tick);
        }
        return;
    }
    if (overlay_ == Overlay::Wifi) {
        if (g.type != GestureType::Tap) return;
        const bool busy = wifiStatus_ == WifiStatus::SetupRunning || wifiStatus_ == WifiStatus::Checking;
        if (wifiSetupArea().contains(g.x, g.y)) {
            cue(Cue::Tick);
            if (wifiStatus_ == WifiStatus::SetupRunning) wifiRequest_ = 2;  // cancel
            else if (!busy) {
                wifiRequest_ = 1;
                wifiStatus_ = WifiStatus::SetupRunning;  // the board confirms (or not) next frame
            }
        } else if (wifiDoneArea().contains(g.x, g.y)) {
            cue(Cue::Tick);
            if (wifiStatus_ == WifiStatus::SetupRunning) wifiRequest_ = 2;
            overlay_ = Overlay::Settings;
            if (welcome_) {  // straight back to the pet
                welcome_ = false;
                welcomeDone_ = true;
                overlay_ = Overlay::None;
                if (wifiStatus_ != WifiStatus::Joined && wifiStatus_ != WifiStatus::Saved) say("WI-FI CAN WAIT. IT'S IN SETTINGS", true);
            }
        } else if (updateShown() && updateArea().contains(g.x, g.y) && updateStatus_ != UpdateStatus::Checking &&
                   updateStatus_ != UpdateStatus::Downloading && updateStatus_ != UpdateStatus::Restarting) {
            cue(Cue::Tick);
            wifiRequest_ = 3;
            updateStatus_ = UpdateStatus::Checking;  // the board confirms next frame
        }
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
        if (startOverArea().contains(g.x, g.y)) {
            overlay_ = Overlay::StartOver;
            confirmNew_ = false;
            cue(Cue::Tick);
            return;
        }
        if (wifiButtonArea().contains(g.x, g.y)) {
            overlay_ = Overlay::Wifi;
            cue(Cue::Tick);
            return;
        }
        if (privacyArea().contains(g.x, g.y)) {
            privacyRequest_ = true;
            cue(Cue::Tick);
            return;
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
        case GestureType::LongPress: break;  // lifting it taps, as a quick press would
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
    if (sim_.state().boarding && !trayOpen() && homeArea().contains(x, y)) {
        toggleBoarding();
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
        for (int i = 0; i < slotCount(); i++) {
            if (slotArea(i).contains(x, y)) {
                pressSlot(slotAt(i));  // once: a press can change the page, and with it the slots
                break;
            }
        }
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
    if (visitor_ != Visitor::None && !visLeaving_ && visitorArea().contains(x, y)) {
        tapVisitor();
        return;
    }
    if (holiday() != Holiday::None && pumpkinArea().contains(x, y)) {
        if (halloween()) tapPumpkin();
        else tapDecoration();
        return;
    }
    if (coneArea().contains(x, y)) {  // toss it somewhere else (the no-flick way)
        float tx = coneX_ < 240 ? 320 + rand01() * 90 : 70 + rand01() * 90;  // well away from it
        float ty = 312 + rand01() * 100;
        tossCone((tx - coneX_) / 0.8f, (ty - coneY_) / 0.8f, 480);
        return;
    }
    if (squatchArea().contains(x, y)) {
        const State& s = sim_.state();
        if (hatched() && sim_.trustLevel() == TrustLevel::Friendly && !s.asleep && !s.calling) touchPet(x, y);
        else tapPet();
        return;
    }
    for (int i = 0; i < 2; i++) {
        if (treeArea(i).contains(x, y)) {
            shakeTree(i);
            return;
        }
    }
    if (fireArea().contains(x, y)) lightFire();
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
        case Slot::Trails:
            if (trailsAvailable_) openTrails();
            break;
        case Slot::Light:
            sim_.lights(!sim_.state().lightsOn);
            cue(sim_.state().lightsOn ? Cue::LightOn : Cue::LightOff);
            say(sim_.state().lightsOn ? "LIGHTS ON" : "LIGHTS OFF", true);
            saveRequested_ = true;
            break;
        case Slot::Photos: overlay_ = Overlay::Gallery; break;
        case Slot::Settings: overlay_ = Overlay::Settings; break;
        case Slot::Stable: toggleBoarding(); break;
        case Slot::Play: openGames(); break;
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
    if (held_ == Item::None && fetch_ == Fetch::Lying && coneArea().contains(g.startX, g.startY)) {
        drag_ = DragMode::Toss;  // picked up; it goes where it's flicked
        onDragMove(g);
        return;
    }
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
    if (drag_ == DragMode::Toss) {
        coneX_ = std::max(30.0f, std::min(450.0f, static_cast<float>(g.x)));
        coneY_ = std::max(300.0f, std::min(430.0f, static_cast<float>(g.y) + 30));
        coneZ_ = coneY_ - static_cast<float>(g.y) + 10;  // held up, just above the finger
        return;
    }
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
    if (drag_ == DragMode::Toss) {
        const float dx = static_cast<float>(g.x - g.prevX), dy = static_cast<float>(g.y - g.prevY);
        const float len = std::hypot(dx, dy);
        if (len > 0.5f && g.speed > 150) {
            const float sp = std::min(g.speed, 1600.0f), ux = dx / len, uy = dy / len;
            tossCone(ux * sp * 0.45f, uy * sp * 0.12f, -uy * sp * 0.55f + 220);
        } else {
            tossCone(0, 0, 0);  // let go: it drops where it is
        }
    }
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
    else if (s.boarding) say(species_ == Species::Horse ? "IT'S AT THE STABLE" : "IT'S WITH THE SITTER", true);
    else if (s.stage == Stage::Egg) say(species_ == Species::Horse ? "THE FOAL ISN'T HERE YET" : "IT HASN'T HATCHED YET", true);
    else if (s.stage == Stage::Away) say("IT'S NOT HERE", true);
    else if (s.asleep) say("SHH, IT'S ASLEEP", true);
}

void Scene::doFeed(Food f) {
    bool misbehaving = sim_.state().misbehaving;
    Result r = sim_.feed(f);
    saved(r);
    if (r == Result::Ok) {
        cue(species_ == Species::Horse && f == Food::Roots ? Cue::MunchHay : Cue::Eat);
        react(React::Eat, 1400);
        // Apples one after another: by the third, now and then, a little... toot. (The horse only.)
        applesInRow_ = f == Food::Berries ? applesInRow_ + 1 : 0;
        if (species_ == Species::Horse && applesInRow_ >= 3 && rand01() < 0.5f) tootAt_ = now_ + 1800;
        if (species_ == Species::Horse) {  // apples are a treat, carrots a favourite, hay is just hay
            static const char* kApple[] = {"APPLES! MY FAVE!", "CRUNCH! SO SWEET", "YUM YUM APPLE!"};
            static const char* kCarrot[] = {"CRUNCHY CARROTS!", "CARROTS ARE THE BEST", "MUNCH MUNCH"};
            static const char* kHay[] = {"HAY... OKAY.", "GOOD FOR MY TUMMY", "CHEWY HAY"};
            sayFresh(f == Food::Berries ? kApple : f == Food::Fish ? kCarrot : kHay, 3);
        } else {
            speak(f == Food::Berries ? "BERRIES!" : (f == Food::Fish ? "FISHY!" : "CRUNCHY!"));
        }
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
        cue(species_ == Species::Horse ? Cue::Brush : Cue::Clean);
        if (species_ == Species::Horse) speak(rand01() < 0.5f ? "SO SHINY!" : "MMM, THAT'S NICE");
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
        speak(rand01() < 0.5f ? "AHH..." : "MORE PLEASE!");
        for (int i = 0; i < 3; i++)
            spawn(ParticleKind::Heart, squatchX() + (rand01() - 0.5f) * 60, headY() - 20, (rand01() - 0.5f) * 30,
                  -50, 1300);
    } else if (r == Result::Refused) {
        react(sim_.trustLevel() == TrustLevel::Hiding ? React::Flinch : React::Peek, 1000);  // not ready yet
    } else if (sim_.state().stage == Stage::Egg) {
        if (species_ == Species::Horse) petMare();
        else react(React::Wobble, 700);
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
        if (s.stage == Stage::Egg) note(species_ == Species::Horse ? "THE FOAL ISN'T HERE YET" : "IT HASN'T HATCHED YET");
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

// Before the foal comes, its mother is in the paddock: touch her and she answers.
void Scene::petMare() {
    static const Cue kSounds[] = {Cue::Nicker, Cue::Blow, Cue::Sigh, Cue::Nicker, Cue::Chuckle};
    static const char* kLines[] = {"MAMA NICKERS SOFTLY", "SHE NUZZLES YOUR HAND", "SHE LIKES THAT",
                                   "SHE LEANS INTO YOUR HAND", "THE FOAL IS COMING SOON"};
    if (mareTurn_ && now_ - lastMarePat_ < 900) return;  // one answer per pat, not one per frame of a stroke
    lastMarePat_ = now_;
    const int i = mareTurn_++ % static_cast<int>(sizeof kSounds / sizeof kSounds[0]);
    cue(kSounds[i]);
    say(kLines[i], true);
    const Rect r = squatchArea();
    for (int k = 0; k < 2; k++)
        spawn(ParticleKind::Heart, r.cx() + 40 + (rand01() - 0.5f) * 50, r.y + 10.0f, (rand01() - 0.5f) * 30, -45, 1200);
}

void Scene::tapPet() {
    const State& s = sim_.state();
    if (s.stage == Stage::Egg) {
        if (species_ == Species::Horse) petMare();
        else react(React::Wobble, 700);
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
        case TrustLevel::Hiding:
            react(React::Flinch, 1500);
            speak("EEP!");
            break;
        case TrustLevel::Wandering:
            react(React::Peek, 1000);
            speak("OH! HI...");
            break;
        case TrustLevel::Friendly: react(React::Wave, 1600); break;
    }
}

// ================================================================ play in the clearing

void Scene::touchPet(int x, int y) {
    // Pokes add up: lots in a short while and it gets grumpy (it's only for fun, though).
    if (now_ - pokeSince_ > 6000) {
        pokeSince_ = now_;
        pokes_ = tickles_ = 0;
    }
    if (++pokes_ >= 7) {
        pokes_ = tickles_ = 0;
        react(React::Swat, 900);
        cue(Cue::Sulk);
        speak("HEY! GENTLY!");
        return;
    }
    const float k = squatchScale(), hr = 44 * k * headRatio(sim_.state().stage), hy = headY();
    const float nx = squatchX() + lookX_ * hr * 0.12f, ny = hy + hr * 0.33f;
    const float bx = (x - squatchX()) / (34 * k), by = (y - (squatchFoot() - 56 * k)) / (40 * k);
    if (std::hypot(x - nx, y - ny) < std::max(hr * 0.32f, 20.0f)) {
        react(React::Boop, 900);
        if (species_ == Species::Horse) cue(Cue::Blow);  // a puff through the nose
        else cue(Cue::Boop);
        if (species_ == Species::Horse && holiday() == Holiday::Christmas) {  // a boop switches the reindeer nose
            settings_.rudolph = settings_.rudolph ? 0 : 1;
            settingsChanged_ = true;
            speak(settings_.rudolph ? "I'M A REINDEER!" : "JUST A PONY AGAIN");
            for (int i = 0; i < 4; i++)
                spawn(ParticleKind::Sparkle, nx + (rand01() - 0.5f) * 40, ny + (rand01() - 0.5f) * 30, (rand01() - 0.5f) * 40, -30, 900);
        } else {
            speak("BOOP!");
        }
    } else if (bx * bx + by * by < 1) {
        react(React::Giggle, 1100);
        const bool fit = ++tickles_ >= 3;
        if (species_ == Species::Horse) cue(fit ? Cue::Neigh : Cue::Chuckle);  // a whinny for the fit
        else cue(Cue::Giggle);
        if (fit) {  // a laughing fit
            tickles_ = 0;
            speak("HAHAHA! STOP!");
            react(React::Giggle, 1800);
            for (int i = 0; i < 3; i++)
                spawn(ParticleKind::Heart, squatchX() + (rand01() - 0.5f) * 80, hy - 10, (rand01() - 0.5f) * 40,
                      -50, 1200);
        } else {
            speak(tickles_ == 1 ? "HEE HEE!" : "THAT TICKLES!");
        }
    } else if (y < hy - hr * 0.3f) {
        react(React::Pat, 1200);
        cue(species_ == Species::Horse ? Cue::Sigh : Cue::Happy);
        speak("MMM...");
        spawn(ParticleKind::Heart, squatchX(), hy - hr - 10, 0, -40, 1100);
    } else {
        react(React::Wave, 1600);
        static const char* kHi[] = {"HI!", "HELLO!", "HUH?", "YES?"};
        speak(kHi[static_cast<int>(rand01() * 4) & 3]);
    }
}

bool Scene::canPlay() const {
    const State& s = sim_.state();
    return hatched() && sim_.trustLevel() == TrustLevel::Friendly && !s.asleep && !s.calling && !s.sick &&
           overlay_ == Overlay::None && !petOverride_;
}

void Scene::shakeTree(int i) {
    if (species_ == Species::Horse && i == 1) {  // the barn: knock, and someone answers
        cue(Cue::Clop);  // a knock on the barn door (Knock would be a whinny for the horse)
        barnPeekUntil_ = rand01() < 0.5f ? now_ + 2600 : 0;  // sometimes the barn cat looks out
        if (canPlay()) {
            const float h = hourOfDay();
            static const char* kBarn[] = {"MY BARN!", "HOME SWEET BARN", "IS IT HAY TIME?", "HI, BARN CAT!"};
            speak(h >= 20 || h < 6 ? "BEDTIME IN THE BARN?" : kBarn[barnPeekUntil_ ? 3 : static_cast<int>(rand01() * 3) % 3]);
            react(React::Happy, 1000);
            cue(Cue::Neigh);
        }
        return;
    }
    const float tx = i ? kRightTreeX : kLeftTreeX, base = i ? kRightTreeBase : kLeftTreeBase;
    shakingTree_ = i;
    shakeUntil_ = now_ + 900;
    cue(Cue::Rustle);
    for (int n = 0; n < 6; n++)
        spawn(ParticleKind::Leaf, tx + (rand01() - 0.5f) * 110, base - 120 - rand01() * 150, 0, 20, 2200);
    if (hiding() && i == 0) {  // it was hiding behind that one
        react(React::Flinch, 1200);
        speak("EEP!");
        return;
    }
    // Every other shake or so a pinecone drops, if there isn't one out already.
    if (fetch_ == Fetch::None && (++shakes_ >= 2 || rand01() < 0.4f)) {
        shakes_ = 0;
        fetch_ = Fetch::Flying;
        appleEaten_ = false;
        coneX_ = tx + (i ? -40.0f : 40.0f);
        coneY_ = base + 8;
        coneZ_ = 170;
        coneVx_ = (i ? -1.0f : 1.0f) * (30 + rand01() * 40);
        coneVy_ = 20;
        coneVz_ = 0;
        coneSince_ = now_;
        if (canPlay()) speak(species_ == Species::Horse ? "OOH! AN APPLE!" : "OOH! A PINECONE!");
    } else if (canPlay() && rand01() < 0.5f) {
        speak("RUSTLE RUSTLE");
    }
}

void Scene::tossCone(float vx, float vy, float vz) {
    if (fetch_ == Fetch::Chase || fetch_ == Fetch::Carry) return;
    fetch_ = Fetch::Flying;
    coneVx_ = vx;
    coneVy_ = vy;
    coneVz_ = vz;
    coneSince_ = now_;
    cue(Cue::Pickup);
}

void Scene::endFetch() {
    if (appleEaten_) {  // nothing left to drop
        appleEaten_ = false;
        fetch_ = Fetch::None;
        return;
    }
    if (fetch_ == Fetch::Carry) {  // dropped where it stands
        coneX_ = fetchX_ + 40;
        coneY_ = squatchFoot() + 10;
        coneZ_ = 0;
    }
    if (fetch_ == Fetch::Chase || fetch_ == Fetch::Carry) fetch_ = Fetch::Lying;
}

void Scene::updateFetch(float dt) {
    if (fetch_ == Fetch::None) return;
    if (!hatched() && sim_.state().stage != Stage::Egg) {  // it left the clearing: so does the pinecone
        fetch_ = Fetch::None;
        return;
    }
    if (fetch_ == Fetch::Flying) {
        coneX_ += coneVx_ * dt;
        coneY_ += coneVy_ * dt;
        coneVz_ -= 1200 * dt;
        coneZ_ += coneVz_ * dt;
        if (coneX_ < 50 || coneX_ > 430) {  // off a tree trunk
            coneX_ = std::max(50.0f, std::min(430.0f, coneX_));
            coneVx_ = -coneVx_ * 0.4f;
        }
        coneY_ = std::max(300.0f, std::min(430.0f, coneY_));
        if (coneZ_ <= 0) {
            coneZ_ = 0;
            if (coneVz_ < -500) cue(Cue::Thud);  // the first, real landing (not the little bounce)
            if (coneVz_ < -250) {  // a little bounce
                coneVz_ = -coneVz_ * 0.3f;
                coneVx_ *= 0.5f;
                coneVy_ *= 0.5f;
            } else {
                fetch_ = Fetch::Lying;
                coneSince_ = now_;
            }
        }
        return;
    }
    const float speed = 170 * std::max(0.6f, squatchScale());
    if (fetch_ == Fetch::Lying) {
        const int32_t lying = static_cast<int32_t>(now_ - coneSince_);  // below 0 just after it's dropped
        if (lying > 180000) {  // left alone for three minutes: it rolls off into the grass
            fetch_ = Fetch::None;
            return;
        }
        // Off it goes, if it's in the mood and the pinecone isn't right at its feet. (A horse
        // goes for the apple wherever it is: to eat it.)
        const bool horse = species_ == Species::Horse;
        if (canPlay() && drag_ != DragMode::Toss && lying > 400 && (horse || std::fabs(coneX_ - squatchX()) > 50)) {
            fetchX_ = squatchX();
            fetch_ = Fetch::Chase;
        }
        return;
    }
    if (!canPlay()) {  // called away (a game, sleep, a call): drop it
        endFetch();
        return;
    }
    if (appleEaten_ && now_ < eatUntil_) return;  // munching where it found it
    const float target = fetch_ == Fetch::Chase ? coneX_ : 240.0f;
    const float step = std::min(std::fabs(target - fetchX_), speed * dt);
    fetchX_ += target > fetchX_ ? step : -step;
    if (std::fabs(target - fetchX_) > 1) return;
    if (fetch_ == Fetch::Chase && species_ == Species::Horse) {  // a horse eats its apple, then wanders back
        static const char* kYum[] = {"CRUNCH! YUM!", "MMM, AN APPLE!", "THE BEST APPLE EVER", "CRUNCHY AND SWEET!"};
        fetch_ = Fetch::Carry;
        appleEaten_ = true;
        eatUntil_ = now_ + 1600;
        react(React::Eat, 1600);
        cue(Cue::Munch);
        speak(kYum[static_cast<int>(rand01() * 4) % 4]);
        spawn(ParticleKind::Heart, fetchX_, headY() - 30, 0, -45, 1200);
        return;
    }
    if (fetch_ == Fetch::Chase) {
        fetch_ = Fetch::Carry;
        speak("GOT IT!");
        return;
    }
    if (appleEaten_) {  // back from its snack
        appleEaten_ = false;
        fetch_ = Fetch::None;
        return;
    }
    // Back with it: dropped at your feet, and that counts as a cuddle.
    fetch_ = Fetch::Lying;
    coneX_ = 240 + 46;
    coneY_ = squatchFoot() + 30;
    coneZ_ = 0;
    coneSince_ = now_ + 600;  // a moment before it'll chase again
    react(React::Happy, 1200);
    cue(Cue::Play);
    speak(rand01() < 0.5f ? "AGAIN! AGAIN!" : "THROW IT!");
    if (sim_.pet() == Result::Ok) {
        saveRequested_ = true;
        spawn(ParticleKind::Heart, 240, headY() - 30, 0, -45, 1200);
    }
}

void Scene::lightFire() {
    if (species_ == Species::Horse) {  // the water trough: a splash, and a drink if it's about
        cue(Cue::Splash);
        for (int i = 0; i < 6; i++)
            spawn(ParticleKind::Drop, kFireX + (rand01() - 0.5f) * 50, kFireY - 20, (rand01() - 0.5f) * 60, -120 - rand01() * 60, 700);
        if (canPlay() && fetch_ == Fetch::None && !drinking()) {
            drinkAt_ = now_ ? now_ : 1;
            speak("A DRINK? YES!");
        }
        return;
    }
    const Sky sky = weather_.sky;
    if (sky == Sky::Rain || sky == Sky::Drizzle || sky == Sky::Storm) {
        say("TOO WET FOR A FIRE", true);
        return;
    }
    cue(Cue::Crackle);
    for (int i = 0; i < 6; i++)
        spawn(ParticleKind::Ember, kFireX + (rand01() - 0.5f) * 24, kFireY - 20, (rand01() - 0.5f) * 60,
              -80 - rand01() * 60, 1200);
    if (fireLit()) {  // already going: a poke sends up sparks
        if (canPlay() && rand01() < 0.5f) speak("OOH, SPARKS!");
        return;
    }
    fireUntil_ = now_ + 180000;  // three minutes
    if (canPlay()) {
        speak("COZY!");
        react(React::Happy, 1000);
    }
}

bool Scene::fireLit() const {
    if (species_ == Species::Horse) return false;  // a water trough there instead
    const float h = hourOfDay();
    return (h >= 17 && h < 23) || now_ < fireUntil_;
}

float Scene::treeSway(float treeX) const {
    if (now_ >= shakeUntil_) return 0;
    const float tx = shakingTree_ ? kRightTreeX : kLeftTreeX;
    if (std::fabs(treeX - tx) > 1) return 0;
    const float left = static_cast<float>(shakeUntil_ - now_) / 900.0f;
    return 9 * left * static_cast<float>(std::sin(t_ * 32));
}

Scene::Rect Scene::treeArea(int i) const {
    return i ? Rect{S - 96, 96, 96, 214} : Rect{0, 96, 96, 224};  // the trunks and the boughs above
}

Scene::Rect Scene::fireArea() const { return {static_cast<int>(kFireX) - 46, static_cast<int>(kFireY) - 36, 84, 68}; }

Scene::Rect Scene::coneArea() const {
    if (fetch_ != Fetch::Lying) return {0, 0, 0, 0};
    return {static_cast<int>(coneX_) - 32, static_cast<int>(coneY_) - 44, 64, 64};
}

void Scene::drawCone(Canvas& c) {
    if (appleEaten_) {  // being eaten: smaller with each bite, then gone
        const float left = now_ < eatUntil_ ? static_cast<float>(eatUntil_ - now_) / 1600.0f : 0.0f;
        if (left <= 0) return;
        const float k = squatchScale(), r = 9 * left;
        const float x = fetchX_ + faceDir_ * 84 * k, y = squatchFoot() - 30 * k;
        c.fillCircle(x, y, r, 0xC8323A);
        c.fillCircle(x, y, r * 0.55f, 0xF3E3C3);  // bitten into
        return;
    }
    float x = coneX_, y = coneY_ - coneZ_ - 8;
    if (fetch_ == Fetch::Carry) {  // in its paw, or a horse's mouth
        const float k = squatchScale();
        x = fetchX_ + 60 * k;
        y = squatchFoot() - 64 * k;
        if (species_ == Species::Horse) {
            x = fetchX_ + faceDir_ * 84 * k;
            y = squatchFoot() - 100 * k;
        }
    } else {
        const float lift = std::min(1.0f, coneZ_ / 200);
        c.fillEllipse(coneX_, coneY_, 11 * (1 - lift * 0.5f), 3.5f, 0x000000, 0.2f * (1 - lift * 0.6f));
    }
    if (species_ == Species::Horse) {  // an apple
        c.fillCircle(x, y, 9, 0xC8323A);
        c.fillCircle(x - 3, y - 3, 3, 0xFFFFFF, 0.45f);
        c.line(x, y - 8, x + 1, y - 13, 2, 0x5A3A22);
        c.fillEllipse(x + 4, y - 12, 4, 2, 0x5C8F3E);
        return;
    }
    drawItem(c, Item::Pinecone, x, y, 0.85f);
}

// ================================================================ visitors

namespace {
constexpr float kBunnyY = 440, kDuckY = 452, kDeerY = 300;
constexpr float kHenY = 448, kGoatY = 306, kDogY = 436;  // farm visitors: hens and pigs in front, goats and sheep by the fence
constexpr float kOwlX = kLeftTreeX + 22, kOwlY = 196;
}  // namespace

void Scene::previewVisitor(Visitor v) { startVisitor(v); }

void Scene::startVisitor(Visitor v) {
    visitor_ = v;
    visSince_ = now_;
    visLeaving_ = false;
    visNextCall_ = now_ + 1200;
    visDir_ = rand01() < 0.5f ? 1.0f : -1.0f;
    const float edge = visDir_ > 0 ? -40.0f : S + 40.0f;
    switch (v) {
        case Visitor::Bunny: visX_ = edge, visY_ = kBunnyY + (rand01() - 0.5f) * 16, visV_ = 60; break;
        case Visitor::Duck: visX_ = edge, visY_ = kDuckY, visV_ = 22; break;
        case Visitor::Deer: visX_ = edge, visY_ = kDeerY, visV_ = 20; break;
        case Visitor::Bird: visX_ = 120 + rand01() * 240, visY_ = 392 + rand01() * 20, visV_ = 0; break;  // where it lands
        case Visitor::Butterfly: visX_ = edge, visY_ = 270, visV_ = 16; break;
        case Visitor::Owl: visX_ = kOwlX, visY_ = kOwlY, visV_ = 0; break;
        case Visitor::Hen: visX_ = edge, visY_ = kHenY, visV_ = 26; break;
        case Visitor::Goat: visX_ = edge, visY_ = kGoatY, visV_ = 34; break;
        case Visitor::Sheep: visX_ = edge, visY_ = kGoatY + 6, visV_ = 18; break;
        case Visitor::Dog: visX_ = edge, visY_ = kDogY, visV_ = 80; break;
        case Visitor::Piglet: visX_ = edge, visY_ = kHenY - 4, visV_ = 36; break;
        case Visitor::Cat: visX_ = edge, visY_ = kDogY + 6, visV_ = 42; break;
        case Visitor::None: return;
    }
    // A farm visitor stops a while to one side of the pony, then goes on its way.
    // (Goats and sheep by the fence stop left of the pony, clear of it and the barn; the dog,
    // who is tall, stops right of it, clear of the trough.)
    if (v == Visitor::Goat || v == Visitor::Sheep) visStopX_ = 100 + rand01() * 60;
    else if (v == Visitor::Dog) visStopX_ = 330 + rand01() * 50;
    else visStopX_ = (rand01() < 0.5f ? 110.0f : 330.0f) + rand01() * 50;
    visStopUntil_ = 0;
    // Its friend notices.
    const State& s = sim_.state();
    if (hatched() && !s.asleep && sim_.trustLevel() == TrustLevel::Friendly && now_ >= bubbleUntil_ && rand01() < 0.7f) {
        static const char* kHello[] = {"", "LOOK, A BUNNY!", "HELLO, BIRDIE!", "A DEER! SHHH...", "QUACK QUACK! HEE HEE",
                                       "A BUTTERFLY!", "WHO'S THAT? HOO!", "HI, MRS HEN!", "A GOAT! HE'S SILLY",
                                       "HELLO, WOOLLY!", "HI, PUPPY!", "A PIGGY! OINK!", "HI, BARN CAT!"};
        speak(kHello[static_cast<int>(v)]);
    }
}

void Scene::updateVisitor(float dt) {
    if (visitor_ == Visitor::None) {
        if (!nextVisit_) nextVisit_ = now_ + 60000 + static_cast<uint32_t>(rand01() * 60000);  // the first soon after waking
        const State& s = sim_.state();
        if (now_ < nextVisit_ || screenOff_ || compact_ || overlay_ != Overlay::None || s.stage == Stage::Away) return;
        nextVisit_ = now_ + 240000 + static_cast<uint32_t>(rand01() * 360000);  // every 4 to 10 minutes
        // Who might come by, for the hour, the month and the weather.
        int m, d;
        localDate(m, d);
        const float h = hourOfDay();
        const bool night = h >= 21 || h < 5, twilight = (h >= 5 && h < 9) || (h >= 17 && h < 21);
        const bool wet = weather_.sky == Sky::Rain || weather_.sky == Sky::Drizzle || weather_.sky == Sky::Storm ||
                         weather_.sky == Sky::Snow;
        Visitor pool[14];
        int n = 0;
        if (night) {
            pool[n++] = Visitor::Owl;
            if (species_ == Species::Horse && rand01() < 0.4f) pool[n++] = Visitor::Cat;  // out mousing
        } else {
            pool[n++] = Visitor::Bunny;
            if (!wet) pool[n++] = Visitor::Bird;
            if (twilight) pool[n++] = Visitor::Deer;
            if (m >= 3 && m <= 9) pool[n++] = Visitor::Duck;
            if (m >= 4 && m <= 9 && !wet && !twilight) pool[n++] = Visitor::Butterfly;
            if (species_ == Species::Horse) {  // the farm's own animals
                pool[n++] = Visitor::Dog;
                pool[n++] = Visitor::Cat;
                if (!wet) {
                    pool[n++] = Visitor::Hen;
                    pool[n++] = Visitor::Goat;
                    pool[n++] = Visitor::Sheep;
                    pool[n++] = Visitor::Piglet;
                }
            }
        }
        startVisitor(pool[static_cast<int>(rand01() * n) % n]);
        return;
    }
    const float age = static_cast<float>(now_ - visSince_) / 1000.0f;
    auto across = [&](float speed) { visX_ += visDir_ * speed * dt; };
    auto gone = [&]() { return visX_ < -60 || visX_ > S + 60; };
    switch (visitor_) {
        case Visitor::Bunny:  // hop, hop, then a nibble
            if (visLeaving_ || std::fmod(age, 3.4f) < 1.2f) across(visLeaving_ ? 170 : visV_);
            if (gone()) visitor_ = Visitor::None;
            break;
        case Visitor::Duck:
            across(visLeaving_ ? 55 : visV_);
            if (now_ >= visNextCall_) {
                cue(Cue::Quack);
                visNextCall_ = now_ + 5000 + static_cast<uint32_t>(rand01() * 5000);
            }
            if (gone() && age > 3) visitor_ = Visitor::None;
            break;
        case Visitor::Deer: {  // walks in, stops to look at you a while, walks on
            const bool looking = !visLeaving_ && std::fabs(visX_ - S / 2.0f) < 70 && age < 30;
            if (!looking) across(visLeaving_ ? 140 : visV_);
            if (gone() && age > 3) visitor_ = Visitor::None;
            break;
        }
        case Visitor::Bird:  // flies down, hops and pecks a while, then away
            if (!visLeaving_ && age > 16) visLeaving_ = true, visSince_ = now_;
            if (!visLeaving_ && now_ >= visNextCall_ && age > 1.2f) {
                cue(Cue::Tweet);
                visNextCall_ = now_ + 3500 + static_cast<uint32_t>(rand01() * 4000);
            }
            if (visLeaving_ && age > 2) visitor_ = Visitor::None;
            break;
        case Visitor::Butterfly:
            across(visLeaving_ ? 70 : visV_);
            if (visLeaving_) visY_ -= 60 * dt;
            if ((gone() || visY_ < 60) && age > 3) visitor_ = Visitor::None;
            break;
        case Visitor::Hen:  // a few steps, then a peck or two; chicks follow
        case Visitor::Goat:
        case Visitor::Sheep:
        case Visitor::Dog:
        case Visitor::Piglet:
        case Visitor::Cat: {
            // Walk in, stop a while beside the pony, then walk on (faster once tapped).
            const bool stopped = !visLeaving_ && visStopUntil_ && now_ < visStopUntil_;
            if (!visLeaving_ && !visStopUntil_ && std::fabs(visX_ - visStopX_) < 6) {
                visStopUntil_ = now_ + 9000 + static_cast<uint32_t>(rand01() * 8000);
            }
            const bool pecking = visitor_ == Visitor::Hen && !visLeaving_ && std::fmod(age, 2.6f) > 1.6f;
            if (!stopped && !pecking) across(visLeaving_ ? visV_ * 2.6f + 40 : visV_);
            if (!visLeaving_ && now_ >= visNextCall_ && age > 1.5f) {
                static const Cue kCall[] = {Cue::Cluck, Cue::Bleat, Cue::Bleat, Cue::Woof, Cue::Oink, Cue::Meow};
                cue(kCall[static_cast<int>(visitor_) - static_cast<int>(Visitor::Hen)]);
                visNextCall_ = now_ + 5000 + static_cast<uint32_t>(rand01() * 6000);
            }
            if (gone() && age > 3) visitor_ = Visitor::None;
            break;
        }
        case Visitor::Owl:
            if (!visLeaving_ && age > 45) visLeaving_ = true, visSince_ = now_;
            if (!visLeaving_ && now_ >= visNextCall_ && age > 2) {
                cue(Cue::Hoot);
                visNextCall_ = now_ + 7000 + static_cast<uint32_t>(rand01() * 6000);
            }
            if (visLeaving_ && age > 2) visitor_ = Visitor::None;
            break;
        case Visitor::None: break;
    }
}

Scene::Rect Scene::visitorArea() const {
    if (visitor_ == Visitor::None) return {0, 0, 0, 0};
    float x = visX_, y = visY_;
    if (visitor_ == Visitor::Butterfly) y += 12 * std::sin(t_ * 2.3f);
    if (visitor_ == Visitor::Deer) y -= 26;  // its middle, not its hooves
    if (visitor_ == Visitor::Bunny || visitor_ == Visitor::Duck || visitor_ == Visitor::Bird) y -= 10;
    if (visitor_ == Visitor::Goat || visitor_ == Visitor::Sheep) y -= 28;
    if (visitor_ == Visitor::Hen || visitor_ == Visitor::Piglet || visitor_ == Visitor::Cat) y -= 14;
    if (visitor_ == Visitor::Dog) y -= 22;
    return {static_cast<int>(x) - 36, static_cast<int>(y) - 36, 72, 72};
}

void Scene::tapVisitor() {
    visLeaving_ = true;
    visSince_ = now_;
    switch (visitor_) {
        case Visitor::Bunny: cue(Cue::Pickup); break;
        case Visitor::Bird: cue(Cue::Tweet); break;
        case Visitor::Duck: cue(Cue::Quack); break;
        case Visitor::Owl: cue(Cue::Hoot); break;
        case Visitor::Hen: cue(Cue::Cluck); break;
        case Visitor::Goat:
        case Visitor::Sheep: cue(Cue::Bleat); break;
        case Visitor::Dog: cue(Cue::Woof); break;
        case Visitor::Piglet: cue(Cue::Oink); break;
        case Visitor::Cat: cue(Cue::Meow); break;
        default: cue(Cue::Tick); break;
    }
    if (visitor_ == Visitor::Deer || visitor_ == Visitor::Bunny || visitor_ == Visitor::Duck || visitor_ >= Visitor::Hen) {
        visDir_ = visX_ < S / 2.0f ? -1.0f : 1.0f;  // off by the nearer side
    }
    for (int i = 0; i < 3; i++)
        spawn(ParticleKind::Sparkle, visX_ + (rand01() - 0.5f) * 30, visY_ - 20, (rand01() - 0.5f) * 40, -30, 700);
    const State& s = sim_.state();
    if (hatched() && !s.asleep && sim_.trustLevel() == TrustLevel::Friendly) {
        static const char* kBye[] = {"", "BYE, BUNNY!", "BYE, BIRDIE!", "COME BACK, DEER!", "BYE, DUCKS!",
                                     "FLY AWAY, BUTTERFLY", "NIGHT NIGHT, OWL", "BYE, CHICKS!", "BYE, BILLY GOAT!",
                                     "SEE YOU, SHEEP!", "BYE, GOOD DOG!", "BYE, PIGGY!", "BYE, KITTY!"};
        speak(kBye[static_cast<int>(visitor_)]);
    }
}

void Scene::drawVisitor(Canvas& c, bool behind) {
    if (visitor_ == Visitor::None || overlay_ == Overlay::Game) return;  // games have the paddock to themselves
    const bool back = visitor_ == Visitor::Deer || visitor_ == Visitor::Owl || visitor_ == Visitor::Goat || visitor_ == Visitor::Sheep;
    if (back != behind) return;
    const float age = static_cast<float>(now_ - visSince_) / 1000.0f, dir = visDir_;
    float x = visX_, y = visY_;
    int m, d;
    localDate(m, d);
    switch (visitor_) {
        case Visitor::Bunny: {  // grey-brown, white in winter; hops with its ears back
            const bool hopping = visLeaving_ || std::fmod(age, 3.4f) < 1.2f;
            const float hop = hopping ? std::fabs(std::sin(age * (visLeaving_ ? 12 : 7.8f))) * 12 : 0;
            const Rgb fur = sh(m == 12 || m <= 2 ? 0xEDEBE6 : 0x9C8A78), belly = sh(0xE8E0D4);
            c.fillEllipse(x, y + 6, 14, 3.5f, 0x000000, 0.18f);
            y -= hop;
            c.fillEllipse(x, y - 8, 14, 10, fur);
            c.fillCircle(x - dir * 13, y - 9, 5, belly);  // the white tail
            c.fillCircle(x + dir * 11, y - 15, 7.5f, fur);
            const float earTilt = hopping ? -dir * 4 : 0;
            c.fillEllipse(x + dir * 8 + earTilt, y - 27, 3, 9, fur);
            c.fillEllipse(x + dir * 13 + earTilt, y - 26, 3, 9, fur);
            c.fillEllipse(x + dir * 13 + earTilt, y - 26, 1.3f, 6, sh(0xE8A0A8));
            c.fillCircle(x + dir * 14, y - 17, 1.6f, 0x1A1A1A);
            c.fillCircle(x + dir * 18, y - 14, 1.3f, sh(0xE88A9A));
            if (!hopping) c.line(x + dir * 18, y - 12, x + dir * 20, y - 10 + std::sin(age * 18) * 1.5f, 1.5f, sh(0x6A9A3A));  // nibbling
            break;
        }
        case Visitor::Duck: {  // a mallard, and in spring a line of ducklings
            const int chicks = (m >= 3 && m <= 6) ? 3 : 0;
            for (int i = chicks; i >= 0; i--) {
                const float px = x - dir * i * 26, waddle = std::sin(age * 9 + i) * 2;
                const bool mum = i == 0;
                const float k = mum ? 1.0f : 0.55f;
                c.fillEllipse(px, y + 4, 13 * k, 3 * k, 0x000000, 0.15f);
                c.line(px - 3 * k, y - 2, px - 4 * k, y + 3, 2, sh(0xE88A2A));
                c.line(px + 3 * k, y - 2, px + 4 * k, y + 3, 2, sh(0xE88A2A));
                const Rgb body = sh(mum ? 0x8A6A4A : 0xF2D25A);
                c.fillEllipse(px + waddle * 0.3f, y - 10 * k, 13 * k, 8 * k, body);
                c.fillTriangle(px - dir * 11 * k, y - 12 * k, px - dir * 18 * k, y - 16 * k, px - dir * 10 * k, y - 6 * k, body);
                const float hx = px + dir * 10 * k, hy = y - 19 * k + waddle * 0.4f;
                c.fillCircle(hx, hy, 6 * k, sh(mum ? 0x2E7A4A : 0xF2D25A));
                c.fillTriangle(hx + dir * 4 * k, hy - 1.5f * k, hx + dir * 4 * k, hy + 2.5f * k, hx + dir * 11 * k, hy + 1.5f * k, sh(0xF0B83A));
                c.fillCircle(hx + dir * 2 * k, hy - 1.5f * k, 1.3f, 0x1A1A1A);
            }
            break;
        }
        case Visitor::Deer: {  // a young doe, lightly built; stops in the middle and turns to look
            const bool looking = !visLeaving_ && std::fabs(visX_ - S / 2.0f) < 70 && age < 30;
            const float g = looking ? 0 : age * (visLeaving_ ? 14 : 6), bound = visLeaving_ ? std::fabs(std::sin(g)) * 10 : 0;
            const Rgb coat = sh(0xB07A4A), dark = sh(0x8A5A34), light = sh(0xF0E2CC);
            c.fillEllipse(x, y + 3, 26, 4, 0x000000, 0.15f);
            y -= bound;
            for (int i = 0; i < 4; i++) {
                const float lx = x + dir * (i < 2 ? 15 : -15) + (i % 2 ? 3 : -3), sw = std::sin(g + i * 1.6f) * 5;
                c.line(lx, y - 28, lx + dir * sw, y, 3, i % 2 ? coat : dark);
            }
            c.fillEllipse(x, y - 34, 24, 11, coat);
            c.fillEllipse(x - dir * 2, y - 29, 18, 5, light);  // pale belly
            c.fillCircle(x - dir * 23, y - 38, 4, light);       // white tail
            const float hx = x + dir * (looking ? 22 : 30), hy = y - 58;
            c.line(x + dir * 18, y - 40, hx, hy + 6, 8, coat);
            c.fillEllipse(hx + dir * (looking ? 0 : 5), hy, looking ? 7 : 9, 7, coat);
            c.fillEllipse(hx - dir * 3 - 5, hy - 8, 2.5f, 6, coat);
            c.fillEllipse(hx - dir * 3 + 5, hy - 8, 2.5f, 6, coat);
            if (looking) {  // facing you: two dark eyes and a black nose
                c.fillCircle(hx - 3, hy - 1, 1.6f, 0x1A1A1A);
                c.fillCircle(hx + 3, hy - 1, 1.6f, 0x1A1A1A);
                c.fillCircle(hx, hy + 4, 1.8f, 0x1A1A1A);
            } else {
                c.fillCircle(hx + dir * 6, hy - 2, 1.5f, 0x1A1A1A);
                c.fillCircle(hx + dir * 13, hy + 1, 1.6f, 0x1A1A1A);
            }
            break;
        }
        case Visitor::Bird: {  // a bluebird: flutters down, hops and pecks, flutters off
            float a = 1;
            bool flying = false;
            if (!visLeaving_ && age < 1.2f) {  // coming in to land, from above
                const float p = age / 1.2f;
                x -= dir * (1 - p) * 120;
                y -= (1 - p) * (1 - p) * 220;
                flying = true;
            } else if (visLeaving_) {
                x += dir * age * 140;
                y -= age * age * 120;
                flying = true;
                a = std::max(0.0f, 1 - age / 2);
            } else {  // little hops now and then, pecking between
                const float p = std::fmod(age, 2.6f);
                if (p < 0.3f) y -= std::sin(p / 0.3f * kPi) * 6;
                if (p > 1.4f && p < 1.8f) y += 3;  // head down to peck
            }
            if (!flying) c.fillEllipse(x, y + 2, 7, 2, 0x000000, 0.15f);
            const Rgb blue = sh(0x3A6FC8), breast = sh(0xE8843A);
            c.fillTriangle(x - dir * 6, y - 6, x - dir * 13, y - 9, x - dir * 12, y - 4, blue, a);  // tail
            c.fillEllipse(x, y - 6, 7, 5, blue, a);
            c.fillEllipse(x + dir * 2, y - 4, 4.5f, 3.5f, breast, a);
            c.fillCircle(x + dir * 6, y - 11, 4, blue, a);
            c.fillTriangle(x + dir * 9, y - 12, x + dir * 9, y - 10, x + dir * 13, y - 11, sh(0x3A3A3A), a);
            c.fillCircle(x + dir * 7, y - 12, 1, 0x1A1A1A, a);
            if (flying) {
                const float flap = std::sin(now_ * 0.04f) * 9;
                c.line(x, y - 7, x - dir * 4, y - 13 - flap, 3, blue, a);
            }
            break;
        }
        case Visitor::Butterfly: {  // an orange monarch, flapping, wandering up and down
            y += 12 * std::sin(t_ * 2.3f) + 6 * std::sin(t_ * 5.1f);
            const float open = 0.35f + 0.65f * std::fabs(std::sin(t_ * 14));
            const Rgb wing = sh(0xF08A24), edge = sh(0x2A1E14);
            c.fillEllipse(x - 6 * open, y - 3, 7 * open + 1, 6, edge);
            c.fillEllipse(x + 6 * open, y - 3, 7 * open + 1, 6, edge);
            c.fillEllipse(x - 6 * open, y - 3, 5.5f * open + 0.5f, 4.8f, wing);
            c.fillEllipse(x + 6 * open, y - 3, 5.5f * open + 0.5f, 4.8f, wing);
            c.fillEllipse(x - 4 * open, y + 4, 4 * open + 1, 4, wing);
            c.fillEllipse(x + 4 * open, y + 4, 4 * open + 1, 4, wing);
            c.line(x, y - 6, x, y + 6, 2, edge);
            break;
        }
        case Visitor::Owl: {  // a little owl on the apple tree's branch, blinking; fades in and out
            float a = std::min(1.0f, age / 1.5f);
            if (visLeaving_) {
                a = std::max(0.0f, 1 - age / 1.5f);
                x -= age * 40;
                y -= age * 30;
            }
            const Rgb feathers = mix(0x8A6A4A, kNight, dark_ * 0.3f), face = mix(0xD8C4A0, kNight, dark_ * 0.3f);
            c.fillEllipse(x, y, 12, 15, feathers, a);
            c.fillEllipse(x, y + 4, 8, 9, face, a);
            c.fillCircle(x, y - 14, 10, feathers, a);
            c.fillTriangle(x - 9, y - 18, x - 5, y - 21, x - 9, y - 28, feathers, a);  // ear tufts
            c.fillTriangle(x + 9, y - 18, x + 5, y - 21, x + 9, y - 28, feathers, a);
            c.fillCircle(x - 4.5f, y - 14, 4.2f, face, a);
            c.fillCircle(x + 4.5f, y - 14, 4.2f, face, a);
            const bool blink = std::fmod(age, 4.0f) > 3.8f;
            if (blink) {
                c.line(x - 7, y - 14, x - 2, y - 14, 1.5f, 0x2A1E14, a);
                c.line(x + 2, y - 14, x + 7, y - 14, 1.5f, 0x2A1E14, a);
            } else {  // big yellow eyes that catch the light
                c.fillCircle(x - 4.5f, y - 14, 3, 0xF2C230, a);
                c.fillCircle(x + 4.5f, y - 14, 3, 0xF2C230, a);
                c.fillCircle(x - 4.5f, y - 14, 1.4f, 0x1A1A1A, a);
                c.fillCircle(x + 4.5f, y - 14, 1.4f, 0x1A1A1A, a);
            }
            c.fillTriangle(x - 1.5f, y - 10, x + 1.5f, y - 10, x, y - 7, 0xE8A23A, a);
            c.line(x - 5, y + 15, x - 3, y + 17, 2, 0xE8A23A, a);
            c.line(x + 5, y + 15, x + 3, y + 17, 2, 0xE8A23A, a);
            break;
        }
        case Visitor::Hen: {  // a brown hen with a red comb, and three chicks behind her
            const bool stopped = !visLeaving_ && visStopUntil_ && now_ < visStopUntil_;
            const bool walking = !stopped && !(std::fmod(age, 2.6f) > 1.6f && !visLeaving_);
            for (int i = 3; i >= 0; i--) {
                const bool mum = i == 0;
                const float k = mum ? 1.0f : 0.5f, px = x - dir * i * 24 - (mum ? 0 : dir * 6);
                const float step = walking ? std::sin(age * 10 + i * 1.7f) : 0;
                const float peck = !walking && std::fmod(age * (mum ? 1.4f : 2.1f) + i, 1.0f) < 0.4f ? 1.0f : 0.0f;
                c.fillEllipse(px, y + 3, 12 * k, 3 * k, 0x000000, 0.16f);
                c.line(px - 3 * k, y - 6 * k, px - 3 * k - step * 2, y + 1, 2, sh(0xE0A030));
                c.line(px + 3 * k, y - 6 * k, px + 3 * k + step * 2, y + 1, 2, sh(0xE0A030));
                const Rgb body = sh(mum ? 0xA8582A : 0xF4D860);
                c.fillEllipse(px, y - 14 * k, 13 * k, 10 * k, body);
                if (mum) {  // tail feathers up at the back, a wing
                    c.fillTriangle(px - dir * 9, y - 18, px - dir * 19, y - 32, px - dir * 15, y - 12, sh(0x7A3A1A));
                    c.fillEllipse(px - dir * 2, y - 14, 8, 5, sh(0x8E4820));
                }
                const float hx = px + dir * (10 + peck * 4) * k, hy = y - (24 - peck * 14) * k;
                c.fillCircle(hx, hy, 6 * k, body);
                if (mum) {
                    c.fillCircle(hx - dir * 1, hy - 6, 2.4f, sh(0xD63A2E));  // comb
                    c.fillCircle(hx + dir * 2, hy - 6.5f, 2.2f, sh(0xD63A2E));
                    c.fillCircle(hx + dir * 4, hy + 4, 1.8f, sh(0xD63A2E));  // wattle
                }
                c.fillTriangle(hx + dir * 5 * k, hy - 1.5f * k, hx + dir * 10 * k, hy + 0.5f * k, hx + dir * 5 * k, hy + 2 * k,
                               sh(0xE8A030));
                c.fillCircle(hx + dir * 2 * k, hy - 1.5f * k, 1.2f, 0x1A1A1A);
            }
            break;
        }
        case Visitor::Goat:
        case Visitor::Sheep: {  // by the fence: a white-and-brown goat with horns, or a woolly sheep (a lamb in spring)
            const bool goat = visitor_ == Visitor::Goat;
            const bool stopped = !visLeaving_ && visStopUntil_ && now_ < visStopUntil_;
            const int lambs = !goat && m >= 3 && m <= 6 ? 1 : 0;
            for (int i = lambs; i >= 0; i--) {
                const float k = i ? 0.6f : 0.85f, px = x - dir * i * 46;
                const float step = stopped ? 0 : std::sin(age * 6 + i);
                // a goat's little hop now and then, while it stands
                const float hop = goat && stopped && std::fmod(age, 5.0f) < 0.45f ? std::sin(std::fmod(age, 5.0f) / 0.45f * 3.14159f) * 10 : 0;
                const float by = y - hop;
                c.fillEllipse(px, y + 2, 26 * k, 4 * k, 0x000000, 0.15f);
                const Rgb leg = sh(goat ? 0x6A5040 : 0x3A3030);
                for (int l = 0; l < 4; l++) {
                    const float lx = px + (l < 2 ? 14 : -14) * k + (l % 2 ? 4 : -4) * k;
                    const float swing = (l % 2 ? step : -step) * 3 * k;
                    c.line(lx, by - 22 * k, lx + swing, by, 3.2f * k, leg);
                }
                if (goat) {
                    c.fillEllipse(px, by - 30 * k, 26 * k, 13 * k, sh(0xEDE8DE));
                    c.fillEllipse(px - dir * 8 * k, by - 32 * k, 10 * k, 8 * k, sh(0x9A6A44));  // a brown patch
                    c.fillTriangle(px - dir * 24 * k, by - 36 * k, px - dir * 30 * k, by - 46 * k, px - dir * 20 * k, by - 38 * k, sh(0xEDE8DE));  // tail up
                } else {  // fluffy: a cloud of wool
                    const Rgb wool = sh(0xF4F0E6), wool2 = sh(0xE6E0D2);
                    for (int b = 0; b < 6; b++)
                        c.fillCircle(px + (b - 2.5f) * 7 * k, by - (30 + (b % 2) * 6) * k, 11 * k, b % 2 ? wool2 : wool);
                    c.fillEllipse(px, by - 30 * k, 24 * k, 12 * k, wool);
                }
                // the head, up and forward (lowered to graze while a sheep stands)
                const float graze = !goat && stopped && std::fmod(age + i, 6.0f) < 3.5f ? 1.0f : 0.0f;
                const float hx = px + dir * (26 + graze * 4) * k, hy = by - (44 - graze * 26) * k;
                c.line(px + dir * 18 * k, by - 34 * k, hx, hy + 4 * k, 9 * k, goat ? sh(0xEDE8DE) : sh(0xF4F0E6));
                const Rgb face = goat ? sh(0xEDE8DE) : sh(0x3E3434);
                c.fillEllipse(hx + dir * 5 * k, hy + 2 * k, 10 * k, 6.5f * k, face);
                c.fillEllipse(hx - dir * 4 * k, hy - 2 * k, 3 * k, 6 * k, face);  // an ear
                if (goat) {
                    c.line(hx - dir * 1 * k, hy - 6 * k, hx - dir * 8 * k, hy - 15 * k, 2.5f * k, sh(0x8A7A60));  // horns
                    c.line(hx + dir * 2 * k, hy - 6 * k, hx - dir * 4 * k, hy - 16 * k, 2.5f * k, sh(0x8A7A60));
                    c.fillTriangle(hx + dir * 9 * k, hy + 6 * k, hx + dir * 13 * k, hy + 6 * k, hx + dir * 11 * k, hy + 14 * k, sh(0xD8D0C2));  // beard
                }
                c.fillCircle(hx + dir * 4 * k, hy, 1.6f, goat ? 0x1A1A1A : 0xF4F0E6);
            }
            break;
        }
        case Visitor::Dog: {  // a black-and-white farm dog that sits a while and wags
            const bool stopped = !visLeaving_ && visStopUntil_ && now_ < visStopUntil_;
            const float step = stopped ? 0 : std::sin(age * 12);
            const Rgb black = sh(0x2A2622), white = sh(0xF2EEE8);
            c.fillEllipse(x, y + 3, 22, 4, 0x000000, 0.16f);
            const float wag = std::sin(age * (stopped ? 14 : 8)) * 0.5f;
            const float tx = x - dir * 20, ty = y - 26;
            c.line(tx, ty, tx - dir * 14 * std::cos(wag), ty - 14 + 8 * std::sin(wag), 4, black);
            if (stopped) {  // sitting: haunches down, front legs straight
                c.fillEllipse(x - dir * 6, y - 14, 16, 13, black);
                c.line(x + dir * 8, y - 18, x + dir * 9, y, 4, white);
                c.line(x + dir * 14, y - 18, x + dir * 15, y, 4, white);
                c.fillEllipse(x + dir * 9, y - 26, 10, 12, white);  // the white chest
            } else {
                for (int l = 0; l < 4; l++) {
                    const float lx = x + (l < 2 ? 12 : -12) + (l % 2 ? 3 : -3);
                    c.line(lx, y - 18, lx + (l % 2 ? step : -step) * 4, y, 3.5f, l < 2 ? white : black);
                }
                c.fillEllipse(x, y - 24, 22, 10, black);
                c.fillEllipse(x + dir * 12, y - 22, 9, 8, white);
            }
            const float hx = x + dir * (stopped ? 12 : 22), hy = y - (stopped ? 42 : 36);
            c.fillCircle(hx, hy, 10, black);
            c.fillEllipse(hx + dir * 3, hy + 2, 3, 9, white);  // the blaze
            c.fillEllipse(hx + dir * 9, hy + 4, 6, 4.5f, white);  // the muzzle
            c.fillCircle(hx + dir * 14, hy + 3, 2.2f, 0x111111);
            c.fillEllipse(hx - dir * 6, hy - 6, 4, 7, black);  // a floppy ear
            c.fillCircle(hx + dir * 5, hy - 3, 1.8f, 0xF2EEE8);
            c.fillCircle(hx + dir * 5, hy - 3, 1.1f, 0x111111);
            if (stopped) c.fillEllipse(hx + dir * 11, hy + 10, 2.5f, 3.5f, sh(0xE07080));  // a pink tongue, panting
            break;
        }
        case Visitor::Piglet: {  // pink, with a curly tail; roots about with its snout
            const bool stopped = !visLeaving_ && visStopUntil_ && now_ < visStopUntil_;
            const float step = stopped ? 0 : std::sin(age * 13);
            const float root = stopped && std::fmod(age, 2.2f) < 1.2f ? 1.0f : 0.0f;
            const Rgb pink = sh(0xF2A8A8), dark = sh(0xD88888);
            c.fillEllipse(x, y + 3, 18, 3.5f, 0x000000, 0.16f);
            for (int l = 0; l < 4; l++) {
                const float lx = x + (l < 2 ? 9 : -9) + (l % 2 ? 3 : -3);
                c.line(lx, y - 8, lx + (l % 2 ? step : -step) * 2, y, 4, dark);
            }
            c.fillEllipse(x, y - 15, 19, 12, pink);
            c.ring(x - dir * 20, y - 20, 3.5f, 1.8f, dark);  // the curly tail
            const float hx = x + dir * 15, hy = y - 16 + root * 6;
            c.fillCircle(hx, hy, 9, pink);
            c.fillEllipse(hx + dir * 8, hy + 2, 4, 4.5f, dark);  // the snout
            c.fillCircle(hx + dir * 8, hy + 1, 0.9f, 0x5A2A2A);
            c.fillCircle(hx + dir * 8, hy + 3.5f, 0.9f, 0x5A2A2A);
            c.fillTriangle(hx - dir * 2, hy - 7, hx - dir * 8, hy - 14, hx + dir * 3, hy - 9, dark);  // ear
            c.fillCircle(hx + dir * 2, hy - 2, 1.4f, 0x1A1A1A);
            if (root) c.fillCircle(hx + dir * 12, y - 2, 1.6f, sh(0x7A5A3A));  // a bit of earth flicked up
            break;
        }
        case Visitor::Cat: {  // a ginger barn cat: pads in, sits, washes a paw, tail swishing
            const bool stopped = !visLeaving_ && visStopUntil_ && now_ < visStopUntil_;
            const float step = stopped ? 0 : std::sin(age * 11);
            const Rgb fur = sh(0xE0913A), stripe = sh(0xB86A22), pale = sh(0xF6DFC0);
            c.fillEllipse(x, y + 3, 15, 3, 0x000000, 0.16f);
            const float swish = std::sin(age * 2.5f) * 0.6f;
            if (stopped) {
                c.line(x - dir * 8, y - 2, x - dir * 18, y - 4 - 10 * std::sin(swish), 3, fur);  // tail on the ground, curling
                c.fillEllipse(x, y - 12, 10, 13, fur);
                c.fillEllipse(x + dir * 3, y - 10, 5, 8, pale);
                const float wash = std::fmod(age, 4.0f) < 1.6f ? 1.0f : 0.0f;  // a paw up to its face now and then
                c.line(x + dir * 5, y - 2, x + dir * (6 + wash * 2), y - 4 - wash * 16, 3, fur);
            } else {
                c.line(x - dir * 13, y - 14, x - dir * 20, y - 28 + 4 * swish, 3, fur);  // tail up
                for (int l = 0; l < 4; l++) {
                    const float lx = x + (l < 2 ? 8 : -8) + (l % 2 ? 2 : -2);
                    c.line(lx, y - 8, lx + (l % 2 ? step : -step) * 3, y, 3, fur);
                }
                c.fillEllipse(x, y - 13, 14, 7, fur);
                for (int b = -1; b <= 1; b++) c.line(x + b * 5, y - 19, x + b * 5 - dir * 2, y - 14, 1.6f, stripe);
            }
            const float hx = x + dir * (stopped ? 3 : 13), hy = y - (stopped ? 28 : 21);
            c.fillCircle(hx, hy, 7, fur);
            c.fillTriangle(hx - 6, hy - 3, hx - 4, hy - 12, hx - 1, hy - 6, fur);  // ears
            c.fillTriangle(hx + 6, hy - 3, hx + 4, hy - 12, hx + 1, hy - 6, fur);
            c.line(hx - 2, hy - 6, hx - 1, hy - 3, 1.4f, stripe);
            c.fillCircle(hx - 2.5f + dir * 1.5f, hy - 1, 1.3f, 0x2A4A1A);
            c.fillCircle(hx + 2.5f + dir * 1.5f, hy - 1, 1.3f, 0x2A4A1A);
            c.fillCircle(hx + dir * 1.5f, hy + 2, 1, sh(0xE07080));
            break;
        }
        case Visitor::None: break;
    }
}

// ================================================================ talking

void Scene::speak(const char* text, uint32_t ms) {
    std::snprintf(bubble_, sizeof bubble_, "%s", text);
    bubbleUntil_ = now_ + ms;
    cue(Cue::Babble);
}

const char* Scene::bubble() const {
    if (!bubble_[0] || now_ >= bubbleUntil_) return nullptr;
    const State& s = sim_.state();
    if (s.stage == Stage::Egg || s.stage == Stage::Away || s.foraging || s.boarding) return nullptr;
    return bubble_;
}

void Scene::greet() {
    greeted_ = true;
    const State& s = sim_.state();
    if (!hatched() || s.asleep || overlay_ != Overlay::None || screenOff_ || compact_) return;
    nextChatter_ = now_ + 40000;
    if (hiding()) {
        speak("...");
        return;
    }
    if (sim_.trustLevel() == TrustLevel::Wandering) {
        speak("OH! IT'S YOU.");
        return;
    }
    if (s.calling) speak("THERE YOU ARE!");
    else if (s.hunger <= 1) speak("SO HUNGRY...");
    else if (s.happiness <= 1) speak("I MISSED YOU!");
    else if (species_ == Species::Horse) {
        const float h = hourOfDay();
        static const char* kMorning[] = {"GOOD MORNING!", "MORNING! BREAKFAST?", "YAY, YOU'RE UP!"};
        static const char* kDay[] = {"HI THERE!", "HAY THERE!", "YOU'RE BACK!", "I WAS WAITING FOR YOU"};
        static const char* kEvening[] = {"GOOD EVENING!", "HI! PRETTY SKY TONIGHT", "THERE YOU ARE!"};
        if (h >= 5 && h < 11) sayFresh(kMorning, 3);
        else if (h < 17) sayFresh(kDay, 4);
        else if (h < 21) sayFresh(kEvening, 3);
        else speak("UP LATE?");
    } else {
        const float h = hourOfDay();
        speak(h >= 5 && h < 11 ? "GOOD MORNING!" : h < 17 ? "HI THERE!" : h < 21 ? "GOOD EVENING!" : "UP LATE?");
    }
    react(React::Wave, 1600);
}

void Scene::chatter() {
    nextChatter_ = now_ + 45000 + static_cast<uint32_t>(rand01() * 45000);
    const State& s = sim_.state();
    if (!hatched() || s.asleep || overlay_ != Overlay::None || screenOff_ || compact_ || toastCount_ ||
        sim_.trustLevel() != TrustLevel::Friendly || now_ < bubbleUntil_ || fetching())
        return;
    // What's on its mind: a need first, then the weather and the season, then idle chatter.
    if (s.sick) return speak("I FEEL ICKY...");
    if (s.hunger <= 1.5f) return speak(species_ == Species::Horse ? "HUNGRY... CARROTS?" : "TUMMY RUMBLES...");
    if (s.happiness <= 1.5f) return speak("PLAY WITH ME?");
    if (s.messes >= 2) return speak(species_ == Species::Horse ? "MUCK OUT MY PADDOCK?" : "STINKY IN HERE...");
    const float h = hourOfDay();
    if (h >= 21 || h < 5) return speak("YAWN... SLEEPY");
    if (species_ == Species::Horse) return horseChatter();
    if (rand01() < 0.5f) {
        switch (weather_.sky) {
            case Sky::Rain:
            case Sky::Drizzle: return speak("DRIP DRIP DRIP");
            case Sky::Storm: return speak("THUNDER IS LOUD!");
            case Sky::Snow: return speak("SNOW! SNOW!");
            case Sky::Fog: return speak("SO FOGGY...");
            default: break;
        }
        if (halloween()) return speak(species_ == Species::Horse ? "NEIGH-BOO! HEE HEE" : "BOO! HEE HEE");
        switch (holiday()) {
            case Holiday::Thanksgiving: return speak(rand01() < 0.5f ? "GOBBLE GOBBLE!" : "I'M THANKFUL FOR YOU");
            case Holiday::Christmas: return speak(rand01() < 0.5f ? "MERRY CHRISTMAS!" : "IS SANTA COMING?");
            case Holiday::Valentine: return speak("BE MY VALENTINE?");
            case Holiday::StPatrick: return speak("I FEEL LUCKY!");
            case Holiday::Easter: return speak("EGG HUNT?");
            case Holiday::July4: return speak("FIREWORKS TONIGHT!");
            default: break;
        }
        if (autumn()) return speak("CRUNCHY LEAVES!");
    }
    static const char* kIdle[] = {"HMM HMM HMM",      "LA LA LA",       "WHAT'S THAT?", "SNIFF SNIFF",
                                  "NICE DAY!",        "I LIKE IT HERE", "BOOP MY NOSE?", "TOSS A PINECONE?",
                                  "SHAKE A TREE?",    "YOU'RE NICE"};
    static const char* kHorseIdle[] = {"NEIGH!",          "MORE CARROTS?",  "LET'S GALLOP!",  "I LOVE MY BARN",
                                       "SHAKE THE TREE?", "BRUSH MY MANE?", "CLIP CLOP...",   "WHAT'S THAT?",
                                       "SNIFF SNIFF",     "YOU'RE MY FAVE"};
    static_assert(sizeof kHorseIdle == sizeof kIdle, "keep the two lists the same length");
    constexpr int n = static_cast<int>(sizeof kIdle / sizeof kIdle[0]);
    int pick = static_cast<int>(rand01() * n) % n;
    if (pick == lastChatter_) pick = (pick + 1) % n;
    lastChatter_ = static_cast<uint8_t>(pick);
    speak(species_ == Species::Horse ? kHorseIdle[pick] : kIdle[pick]);
}

// Says one of these, but not one it said lately.
void Scene::sayFresh(const char* const* lines, int n) {
    if (n <= 0) return;
    int pick = static_cast<int>(rand01() * n) % n;
    for (int tries = 0; tries < n; tries++, pick = (pick + 1) % n) {
        bool recent = false;
        for (const char* r : recentLines_) recent |= r == lines[pick];
        if (!recent) break;
    }
    recentLines_[recentAt_++ % 8] = lines[pick];
    speak(lines[pick]);
}

// The horse's own chatter, once its needs are met: the weather, the holidays and seasons,
// what it's wearing or what it is, the time of day, and things for its age, with a few
// jokes. (Each line fits a bubble: 27 characters at most.)
void Scene::horseChatter() {
    const State& s = sim_.state();
    const float h = hourOfDay();
    int m, d;
    localDate(m, d);
    const float roll = rand01();
    if (roll < 0.35f) {  // what's going on around it
        switch (weather_.sky) {
            case Sky::Rain:
            case Sky::Drizzle: {
                static const char* k[] = {"DRIP DRIP DRIP", "PUDDLES! SPLASH!", "MY MANE IS ALL WET", "RAIN MAKES GRASS GROW"};
                return sayFresh(k, 4);
            }
            case Sky::Storm: {
                static const char* k[] = {"THUNDER IS LOUD!", "STAY CLOSE, OK?", "I'M BRAVE. I THINK."};
                return sayFresh(k, 3);
            }
            case Sky::Snow: {
                static const char* k[] = {"SNOW! SNOW!", "SNOWFLAKE ON MY NOSE!", "LET'S PLAY IN THE SNOW", "MY HOOVES ARE COLD"};
                return sayFresh(k, 4);
            }
            case Sky::Fog: {
                static const char* k[] = {"SO FOGGY...", "WHERE DID THE BARN GO?", "SPOOKY FOG!"};
                return sayFresh(k, 3);
            }
            case Sky::Clear:
                if (h >= 9 && h < 17) {
                    static const char* k[] = {"SUNNY DAY!", "THE SUN FEELS NICE", "PERFECT DAY TO GALLOP"};
                    return sayFresh(k, 3);
                }
                break;
            default: break;
        }
        if (weather_.sky != Sky::Unknown && weather_.tempC <= 0) {
            static const char* k[] = {"BRRR! IT'S CHILLY", "I NEED A WARM BLANKET", "MY BREATH IS SMOKY!"};
            return sayFresh(k, 3);
        }
        if (weather_.sky != Sky::Unknown && weather_.tempC >= 30) {
            static const char* k[] = {"SO HOT TODAY!", "TIME FOR A DRINK", "SHADE UNDER THE TREE?"};
            return sayFresh(k, 3);
        }
        if (weather_.sky != Sky::Unknown && weather_.windKmh >= 30) {
            static const char* k[] = {"WINDY! MY MANE FLIES", "WHOOSH! SO WINDY"};
            return sayFresh(k, 2);
        }
        if (halloween()) {
            static const char* k[] = {"NEIGH-BOO! HEE HEE", "TRICK OR TREAT?", "I'M A SPOOKY HORSE"};
            return sayFresh(k, 3);
        }
        switch (holiday()) {
            case Holiday::Thanksgiving: {
                static const char* k[] = {"GOBBLE GOBBLE!", "I'M THANKFUL FOR YOU", "PIE? NO, CARROT PIE!", "THAT TURKEY IS SILLY"};
                return sayFresh(k, 4);
            }
            case Holiday::Christmas: {
                static const char* k[] = {"MERRY CHRISTMAS!", "IS SANTA COMING?", "I'D PULL THE SLEIGH!",
                                          "CARROTS IN MY STOCKING?", "JINGLE BELLS!"};
                return sayFresh(k, 5);
            }
            case Holiday::Valentine: {
                static const char* k[] = {"BE MY VALENTINE?", "I LOVE YOU A HAY-LOT"};
                return sayFresh(k, 2);
            }
            case Holiday::StPatrick: {
                static const char* k[] = {"I FEEL LUCKY!", "A LUCKY HORSESHOE!"};
                return sayFresh(k, 2);
            }
            case Holiday::Easter: {
                static const char* k[] = {"EGG HUNT?", "I SAW THE BUNNY!"};
                return sayFresh(k, 2);
            }
            case Holiday::July4: {
                static const char* k[] = {"FIREWORKS TONIGHT!", "HAPPY 4TH OF JULY!"};
                return sayFresh(k, 2);
            }
            default: break;
        }
        if (autumn()) {
            static const char* k[] = {"CRUNCHY LEAVES!", "APPLES ARE EXTRA GOOD", "THE LEAVES ARE ORANGE"};
            return sayFresh(k, 3);
        }
        if (m >= 3 && m <= 5) {
            static const char* k[] = {"SPRING GRASS! YUM", "I SEE FLOWERS!", "BIRDIES ARE SINGING"};
            return sayFresh(k, 3);
        }
        if (m >= 6 && m <= 8) {
            static const char* k[] = {"SUMMER IS FUN!", "BUZZ OFF, FLIES!", "LONG SUNNY DAYS"};
            return sayFresh(k, 3);
        }
        if (m == 12 || m <= 2) {
            static const char* k[] = {"MY COAT IS FLUFFY NOW", "COZY IN MY BARN"};
            return sayFresh(k, 2);
        }
    }
    if (roll < 0.5f) {  // what it's wearing, or what it is
        bool unicorn, golden;
        Rgb coat, mane, muzzle;
        horseColours(coat, mane, muzzle, unicorn, golden);
        if (ribbon_ && now_ < ribbonUntil_) {
            static const char* k[] = {"DO YOU LIKE MY BOW?", "I FEEL SO FANCY", "BEST GROOMED HORSE!"};
            return sayFresh(k, 3);
        }
        if (settings_.rudolph && holiday() == Holiday::Christmas) {
            static const char* k[] = {"MY NOSE IS GLOWING!", "I CAN GUIDE THE SLEIGH"};
            return sayFresh(k, 2);
        }
        if (golden) {
            static const char* k[] = {"MY MANE IS GOLDEN!", "I'M ONE IN A MILLION"};
            return sayFresh(k, 2);
        }
        if (unicorn) {
            static const char* k[] = {"I'M A UNICORN, YOU KNOW", "SPARKLE SPARKLE!", "MY HORN IS MAGIC"};
            return sayFresh(k, 3);
        }
    }
    if (roll < 0.65f) {  // the time of day
        if (h >= 5 && h < 11) {
            static const char* k[] = {"GOOD MORNING SUNSHINE", "BREAKFAST TIME?", "I SLEPT SO WELL", "THE DEW IS COLD!"};
            return sayFresh(k, 4);
        }
        if (h < 17) {
            static const char* k[] = {"NAP IN THE SUN?", "AFTERNOON SNACK?", "WANT TO PLAY?"};
            return sayFresh(k, 3);
        }
        static const char* k[] = {"PRETTY SUNSET!", "ALMOST BEDTIME", "LOOK, THE FIRST STAR!"};
        return sayFresh(k, 3);
    }
    if (roll < 0.82f) {  // for its age
        switch (s.stage) {
            case Stage::Baby: {
                static const char* k[] = {"MY LEGS ARE WOBBLY", "AM I BIG YET?", "I CAN RUN NOW!", "WHEE!", "TEE HEE!",
                                          "MAMA SAYS HI!", "I'M LITTLE BUT FAST"};
                return sayFresh(k, 7);
            }
            case Stage::Child:
            case Stage::Teen: {
                static const char* k[] = {"LET'S RACE!", "I'M GETTING TALLER!", "WATCH ME TROT!", "I CAN JUMP SO HIGH",
                                          "LET'S EXPLORE!", "AM I GROWN UP YET?"};
                return sayFresh(k, 6);
            }
            case Stage::Adult: {
                static const char* k[] = {"I'M A BIG HORSE NOW", "WHAT A FINE DAY", "THANKS FOR CARING",
                                          "READY FOR AN ADVENTURE?", "YOU RAISED ME WELL"};
                return sayFresh(k, 5);
            }
            case Stage::Elder: {
                static const char* k[] = {"I REMEMBER BEING TINY", "SLOW AND STEADY", "YOU'VE BEEN SO KIND",
                                          "A NAP SOUNDS NICE"};
                return sayFresh(k, 4);
            }
            default: break;
        }
    }
    static const char* kAnyTime[] = {
        "NEIGH!",          "MORE CARROTS?",          "LET'S GALLOP!",          "I LOVE MY BARN",
        "SHAKE THE TREE?", "BRUSH MY MANE?",         "CLIP CLOP...",           "WHAT'S THAT?",
        "SNIFF SNIFF",     "YOU'RE MY FAVE",         "APPLES ARE MY FAVE",     "I HEAR A BIRDIE",
        "TELL ME A STORY?", "HAY THERE!",            "WHAT'S UP, BUTTERCUP?",  "I'M HOOF-RAY HAPPY!",
        "YOU'RE SO NEIGH-CE!", "I SPY A BUTTERFLY",  "LET'S PLAY A GAME!",     "HORSES SLEEP STANDING!",
        "THE BARN CAT IS FUNNY", "I LIKE YOUR FACE", "BOOP MY NOSE?",          "HUMMING A HORSE SONG",
    };
    sayFresh(kAnyTime, static_cast<int>(sizeof kAnyTime / sizeof kAnyTime[0]));
}

// ---- lettering

namespace {
// Splits text at spaces into lines no wider than maxW at this scale; a word too long for a
// line gets one to itself. Returns the count.
// Copies src into dst (n bytes), cut short if it must be.
void copyText(char* dst, size_t n, const char* src) {
    size_t k = std::strlen(src);
    if (k >= n) k = n - 1;
    std::memcpy(dst, src, k);
    dst[k] = '\0';
}

int wrapText(const char* text, int scale, float maxW, char out[][64], int maxLines) {
    int n = 0;
    char cur[64] = {};
    const char* p = text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char* e = p;
        while (*e && *e != ' ') e++;
        char word[64];
        const size_t wl = std::min<size_t>(static_cast<size_t>(e - p), sizeof word - 1);
        std::memcpy(word, p, wl);
        word[wl] = '\0';
        char tryLine[130];
        copyText(tryLine, sizeof tryLine, cur);
        if (cur[0]) std::strcat(tryLine, " ");
        std::strcat(tryLine, word);
        if (!cur[0] || Canvas::textWidth(tryLine, scale) <= maxW) {
            copyText(cur, sizeof cur, tryLine);
        } else {
            if (n < maxLines) copyText(out[n], 64, cur);
            n++;
            copyText(cur, sizeof cur, word);
        }
        p = e;
    }
    if (cur[0]) {
        if (n < maxLines) copyText(out[n], 64, cur);
        n++;
    }
    return std::min(n, maxLines);
}
}  // namespace

int Scene::lines(const char* text, int scale, float maxW) const {
    char buf[8][64];
    return wrapText(text, ts(scale), maxW, buf, 8);
}

float Scene::para(Canvas& c, float cx, float y, const char* text, int scale, Rgb col, float maxW, float a) const {
    char buf[8][64];
    const int s = ts(scale), n = wrapText(text, s, maxW, buf, 8);
    for (int i = 0; i < n; i++) {
        c.text(cx, y, buf[i], s, col, Align::Center, a);
        y += lineStep(s);
    }
    return y;
}

void Scene::label(Canvas& c, const Rect& r, const char* text, int scale, Rgb col, float a) const {
    const float maxW = r.w - 10.0f;
    for (int s = ts(scale); s >= 1; s--) {
        char buf[2][64];
        char all[8][64];
        const int n = wrapText(text, s, maxW, all, 8);
        if (n == 1) {
            c.text(r.cx(), r.cy() - Canvas::textHeight(s) / 2.0f, text, s, col, Align::Center, a);
            return;
        }
        const float h = Canvas::textHeight(s) * 2 + 2.0f * s + 2;
        if (n == 2 && h <= r.h - 12) {
            copyText(buf[0], 64, all[0]);
            copyText(buf[1], 64, all[1]);
            const float y = r.cy() - h / 2;
            c.text(r.cx(), y, buf[0], s, col, Align::Center, a);
            c.text(r.cx(), y + h - Canvas::textHeight(s), buf[1], s, col, Align::Center, a);
            return;
        }
        if (s == 1) c.text(r.cx(), r.cy() - Canvas::textHeight(s) / 2.0f, text, s, col, Align::Center, a);
    }
}


void Scene::drawBubble(Canvas& c) {
    const char* text = bubble();
    if (!text) return;
    const float k = squatchScale(), hr = 44 * k * headRatio(sim_.state().stage);
    float ax = squatchX(), ay = headY() - hr - (hatWeek() ? 34 * k : 8);  // the point of the tail
    if (hiding()) {
        ax = kHideX + 10;
        ay = kHideY - 30;
    }
    const int ls = ts(2);
    char wrapped[3][64];
    const int n = wrapText(text, ls, 420, wrapped, 3);
    float tw = 0;
    for (int i = 0; i < n; i++) tw = std::max(tw, static_cast<float>(Canvas::textWidth(wrapped[i], ls)));
    const float w = tw + 24, h = 16 + Canvas::textHeight(ls) + (n - 1) * lineStep(ls);
    float bx = std::max(8.0f, std::min(static_cast<float>(S) - 8 - w, ax - w / 2));
    float by = std::max(46.0f, ay - 12 - h);
    // Fades out at the end.
    const float left = static_cast<float>(bubbleUntil_ - now_);
    const float a = std::min(1.0f, left / 250.0f);
    c.fillTriangle(ax - 8, by + h - 2, ax + 8, by + h - 2, ax + (ax < 240 ? 6.0f : -6.0f), std::max(by + h + 2, ay), 0xFFF8EC, a);
    c.fillRoundRect(bx, by, w, h, 14, 0xFFF8EC, a);
    for (int i = 0; i < n; i++) c.text(bx + w / 2, by + 8 + i * lineStep(ls), wrapped[i], ls, 0x2B2118, Align::Center, a);
}

// ================================================================ animation

void Scene::update(uint32_t nowMs) {
    uint32_t dtMs = started_ ? nowMs - now_ : 0;
    if (!started_) lastInputMs_ = nowMs;
    started_ = true;
    now_ = nowMs;
    float dt = static_cast<float>(std::min<uint32_t>(dtMs, 100)) / 1000.0f;
    t_ += dt;

    updateChargeCall();
    if (findingMe()) {  // calling out till found: screen on, a call every few seconds
        if (now_ >= findMeUntil_) {
            findMeUntil_ = 0;
        } else {
            screenOff_ = false;
            lastInputMs_ = now_;
            if (now_ >= findMeNextCall_) {
                if (!findMeQuiet_) cue(species_ == Species::Horse ? Cue::Neigh : Cue::Happy);
                findMeNextCall_ = now_ + (chargeCall_ ? 8000 : 3500);
            }
        }
    }

    gestures_.update(nowMs);
    Gesture g;
    while (gestures_.poll(g)) handle(g);

    Event ev[16];
    for (size_t n; (n = sim_.drainEvents(ev, 16)) > 0;)
        for (size_t i = 0; i < n; i++) handleEvent(ev[i]);

    animate(dt);

    if (overlay_ == Overlay::Game) updateGame();

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

    // It says hello when the screen comes back on after a while, and chats now and then.
    if (screenOff_ && !wasOff_) offAt_ = now_;
    if (!screenOff_ && wasOff_ && now_ - offAt_ >= 60000) {
        greeted_ = false;
        greetAt_ = now_ + 700;
    }
    wasOff_ = screenOff_;
    if (!greeted_ && now_ >= greetAt_) greet();
    if (now_ >= nextChatter_) chatter();

    const uint32_t timeout = screenTimeoutMs();
    const bool settingUp = overlay_ == Overlay::Wifi &&
                           (wifiStatus_ == WifiStatus::SetupRunning || wifiStatus_ == WifiStatus::Checking);
    if (!screenOff_ && timeout && fingers_ == 0 && nowMs - lastInputMs_ >= timeout && !settingUp) screenOff_ = true;
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
            if (species_ == Species::Horse) {  // born at its mother's side
                bornAt_ = now_ ? now_ : 1;
                for (int i = 0; i < 6; i++)
                    spawn(ParticleKind::Heart, 220 + (rand01() - 0.5f) * 80, 260, (rand01() - 0.5f) * 40, -50, 1400);
                for (int i = 0; i < 8; i++)
                    spawn(ParticleKind::Sparkle, 220 + (rand01() - 0.5f) * 120, 300 - rand01() * 60, 0, -30, 900);
                cue(Cue::Hatch);
                askNameAt_ = now_ + 9500;  // after its mother has gone back to the barn
                const Traits t = sim_.traits();
                if (t.rarity == Rarity::Mythic) say("A GOLDEN UNICORN IS BORN!");
                else if (t.rarity == Rarity::Epic) say("A UNICORN FOAL IS BORN!");
                else if (t.variant == Variant::Swamp) say("A RARE DAPPLE GREY FOAL!");
                else if (t.variant != Variant::None) say("A RARE WHITE FOAL!");
                else if (t.rarity == Rarity::Uncommon) say("A FOAL WITH AN UNUSUAL COAT!");
                else say("A FOAL IS BORN!");
                break;
            }
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
                std::snprintf(buf, sizeof buf, "NOW A %s", stageName(st));
                say(buf);
                cue(Cue::GrowUp);
            }
            break;
        }
        case EventType::BecameAdult:
            std::snprintf(buf, sizeof buf, "GREW UP: %s", formName(static_cast<AdultForm>(e.a)));
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
            say(species_ == Species::Horse ? "IT RAN OFF ACROSS THE FIELDS" : "IT RAN OFF INTO THE WOODS");
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
            say(species_ == Species::Horse ? "A FOAL IS ON THE WAY!" : "A NEW EGG!");
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
    updateVisitor(dt);
    if (tootAt_ && now_ >= tootAt_) {  // too many apples
        tootAt_ = 0;
        if (hatched() && !hiding()) {
            cue(Cue::Toot);
            react(React::Giggle, 1200);
            speak("OOPS! TOO MANY APPLES");
            for (int i = 0; i < 4; i++)  // a little puff, behind
                spawn(ParticleKind::Sparkle, squatchX() - faceDir_ * 50 * squatchScale() + (rand01() - 0.5f) * 20,
                      squatchFoot() - 50 * squatchScale(), -faceDir_ * (20 + rand01() * 30), -20, 900);
        }
    }

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
    lookTargetY_ = 0;
    if (hatched() && !s.asleep && sim_.trustLevel() == TrustLevel::Friendly) {
        float tx = -1, ty = 0;
        if (fetch_ == Fetch::Flying || fetch_ == Fetch::Chase) {  // watching the pinecone
            tx = coneX_;
            ty = coneY_ - coneZ_;
        } else if (now_ < fingerUntil_) {
            tx = fingerX_;
            ty = fingerY_;
        }
        if (tx >= 0) {
            lookTarget_ = std::max(-1.0f, std::min(1.0f, (tx - squatchX()) / 140));
            lookTargetY_ = std::max(-1.0f, std::min(1.0f, (ty - headY()) / 160));
        }
    }
    lookX_ += (lookTarget_ - lookX_) * std::min(1.0f, dt * 6);
    lookY_ += (lookTargetY_ - lookY_) * std::min(1.0f, dt * 6);
    updateFetch(dt);
    if (species_ == Species::Horse) {  // a hoofstep as each hoof comes down, near ones louder: clip... clop...
        static const float kLegPhase[4] = {0.0f, 3.1f, 1.6f, 4.7f};  // as drawHorse: far front, far hind, near front, near hind
        const bool walking = hatched() && !s.asleep && !hiding() && walkAmt_ > 0.15f;
        for (int i = 0; walking && i < 4; i++) {
            auto stride = [&](float p) { return std::floor((p + kLegPhase[i] - kPi / 2) / (2 * kPi)); };
            if (stride(gaitPhase_) != stride(stepPhase_)) cue(i >= 2 ? Cue::StepNear : Cue::StepFar);
        }
        stepPhase_ = gaitPhase_;
    }
    if (fireLit() && now_ >= nextEmber_) {
        spawn(ParticleKind::Ember, kFireX + (rand01() - 0.5f) * 16, kFireY - 30, (rand01() - 0.5f) * 20,
              -50 - rand01() * 30, 1300);
        nextEmber_ = now_ + 350 + static_cast<uint32_t>(rand01() * 400);
    }

    bool wandering = hatched() && !s.asleep && sim_.trustLevel() == TrustLevel::Wandering;
    if (wandering) {
        if (now_ >= nextWander_) {
            wanderTarget_ = 150 + rand01() * 180;
            nextWander_ = now_ + 4000 + static_cast<uint32_t>(rand01() * 4000);
        }
        if (species_ == Species::Horse) {  // speeds up and slows down rather than starting and stopping dead
            const float want = std::max(-36.0f, std::min(36.0f, (wanderTarget_ - wanderX_) * 1.4f));
            wanderV_ += (want - wanderV_) * std::min(1.0f, dt * 2.5f);
            wanderX_ += wanderV_ * dt;
        } else {
            float step = std::min(std::fabs(wanderTarget_ - wanderX_), 36 * dt);
            wanderX_ += wanderTarget_ > wanderX_ ? step : -step;
        }
    }
    if (species_ == Species::Horse) {  // its stride follows the ground it covers, so the hooves don't slide
        const float x = hatched() ? squatchX() : -1;
        const float moved = lastPetX_ >= 0 && x >= 0 ? std::fabs(x - lastPetX_) : 0;
        // A full stride covers about 36 px (less when it's only ambling), so the hooves don't slide.
        if (moved < 40) gaitPhase_ += moved / (36 * squatchScale() * std::max(0.35f, walkAmt_)) * 2 * kPi;
        const float want = dt > 0 ? std::min(1.0f, moved / dt / 28) : 0;
        walkAmt_ += (want - walkAmt_) * std::min(1.0f, dt * (want > walkAmt_ ? 6.0f : 4.0f));
        if (walkAmt_ < 0.02f && want == 0) {  // come to rest with the legs together
            walkAmt_ = 0;
        }
        lastPetX_ = x;
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
        if (p.kind == ParticleKind::Leaf) {  // drifts down, swaying
            p.vy += (70 - p.vy) * std::min(1.0f, dt * 3);
            p.vx = static_cast<float>(std::sin((now_ - p.born) * 0.006 + p.born)) * 40;
        }
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        particles_[keep++] = p;
    }
    particleCount_ = keep;
}

// ================================================================ where things are

bool Scene::hatched() const {  // and here in the clearing
    Stage st = sim_.state().stage;
    return st != Stage::Egg && st != Stage::Away && !sim_.state().foraging && !sim_.state().boarding;
}

bool Scene::drinking() const {
    return species_ == Species::Horse && drinkAt_ && now_ - drinkAt_ < 5200 && canPlay();
}

bool Scene::hiding() const { return hatched() && sim_.trustLevel() == TrustLevel::Hiding && !newborn(); }

// A foal's first few seconds: beside its mother, then off to hide (if it's shy, as they are).
bool Scene::newborn() const { return species_ == Species::Horse && bornAt_ && now_ - bornAt_ < 5000 && hatched(); }

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
    if (petOverride_) return gameX_;
    if (fetch_ == Fetch::Chase || fetch_ == Fetch::Carry) return fetchX_;
    if (drinking()) {  // over to the trough, a drink, and back
        const float since = static_cast<float>(now_ - drinkAt_) / 1000.0f, at = kFireX + 96;
        auto ease = [](float p) { return p * p * (3 - 2 * p); };  // gently away, gently to a stop
        if (since < 1.4f) return 240 + (at - 240) * ease(since / 1.4f);
        if (since < 3.8f) return at;
        return at + (240 - at) * ease(std::min(1.0f, (since - 3.8f) / 1.4f));
    }
    if (newborn()) {
        const float since = static_cast<float>(now_ - bornAt_) / 1000.0f;
        const float to = sim_.trustLevel() == TrustLevel::Hiding ? kHideX : 240.0f;
        const float p = std::min(1.0f, std::max(0.0f, (since - 3) / 2));
        return 214.0f + (to - 214.0f) * p * p * (3 - 2 * p);
    }
    switch (sim_.trustLevel()) {
        case TrustLevel::Hiding: return kHideX;
        case TrustLevel::Wandering: return wanderX_;
        case TrustLevel::Friendly: break;
    }
    if (species_ == Species::Horse) return 240;  // a horse stands square (a slow sway would be a shuffle)
    return 240 + static_cast<float>(std::sin(t_ * 0.7)) * 6;
}

float Scene::squatchFoot() const {
    if (petOverride_) return kFriendlyFoot;
    if (drinking()) {  // standing just behind the trough
        const float since = static_cast<float>(now_ - drinkAt_) / 1000.0f;
        const float near = std::max(0.0f, std::min(1.0f, std::min(since / 1.4f, (5.2f - since) / 1.4f)));
        return kFriendlyFoot + 30 * near;
    }
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
    if (st == Stage::Away || sim_.state().foraging || sim_.state().boarding) return {0, 0, 0, 0};
    if (st == Stage::Egg) return species_ == Species::Horse ? Rect{150, 196, 200, 150} : Rect{184, 222, 112, 128};
    if (hiding()) {
        if (species_ == Species::Horse) return {10, 200, 150, 150};  // all of it, trunk and all
        return {static_cast<int>(kHideX) - 40, static_cast<int>(kHideY) - 40, 80, 80};
    }
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
        case Page::More: return trailsAvailable_ ? 5 : 4;
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
        case Page::More: return kMoreSlots[!trailsAvailable_ && i >= 2 ? i + 1 : i];  // past TRAILS, if it's off
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
    if (now_ < shakeUntil_) {  // a tree is swaying: draw it all this frame, and redo the cache after
        drawScenery(c);
        sceneryKey_ = -1;
    } else if (cacheScenery_ && c.width() == S && c.height() == S) {
        const size_t n = static_cast<size_t>(c.pixelCount());
        if (scenery_.size() != n) {
            scenery_.assign(n, 0);
            sceneryKey_ = -1;
        }
        if (key != sceneryKey_) {
            Canvas sc(scenery_.data(), S * c.scale(), S * c.scale(), c.scale());
            sc.setShade(0x02040C, shade);
            drawScenery(sc);
            sceneryKey_ = key;
        }
        std::memcpy(c.pixels(), scenery_.data(), scenery_.size() * sizeof(uint16_t));
    } else {
        drawScenery(c);
    }
    drawStars(c);
    if (fireworksNight()) drawFireworks(c);
    if (halloween()) drawBats(c);
    if (holiday() != Holiday::None && !halloween()) drawDecoration(c);
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
    const bool petAway = overlay_ == Overlay::Game &&
                         (game_ == Game::Seek || (game_ == Game::Photo && !running_));  // hidden, or not running past
    if (overlay_ == Overlay::Game && game_ == Game::Seek) {
        drawBush(c, 160, 318);
        drawBush(c, 300, 318);
    }
    if (species_ == Species::Horse && now_ < barnPeekUntil_) {  // the barn cat in the hayloft
        const float rise = std::min(1.0f, std::min(static_cast<float>(barnPeekUntil_ - now_), 2600.0f - static_cast<float>(barnPeekUntil_ - now_)) / 300.0f);
        const float cx = 432, cy = 220 - 4 * rise;
        c.fillRect(cx - 8, 217, 16, 18, sh(0x3A2A20));  // the dark loft behind
        c.fillTriangle(cx - 7, cy - 3, cx - 2, cy - 5, cx - 6, cy - 11, sh(0xE0903C));
        c.fillTriangle(cx + 7, cy - 3, cx + 2, cy - 5, cx + 6, cy - 11, sh(0xE0903C));
        c.fillCircle(cx, cy, 7, sh(0xE0903C));
        c.fillCircle(cx - 2.5f, cy - 1, 1.3f, 0x1A1A1A);
        c.fillCircle(cx + 2.5f, cy - 1, 1.3f, 0x1A1A1A);
        c.fillCircle(cx, cy + 2, 1, 0xE88A9A);
    }
    drawVisitor(c, true);
    const bool coneBehind = fetch_ != Fetch::Carry && coneY_ < squatchFoot();
    if (fetch_ != Fetch::None && coneBehind) drawCone(c);
    if (species_ == Species::Horse && bornAt_ && now_ - bornAt_ < 9000 && s.stage != Stage::Egg) drawMare(c);
    if (s.stage == Stage::Egg) {
        if (species_ == Species::Horse) drawMare(c);
        else drawEgg(c);
    } else if (petAway) {}
    else if (hiding()) drawHidingEyes(c);
    else if (hatched()) drawPet(c);
    if (fetch_ != Fetch::None && !coneBehind) drawCone(c);
    drawVisitor(c, false);
    const float h = hourOfDay();
    if (fireLit() && !(h >= 17 && h < 23))  // lit by you: its light isn't in the scenery
        c.glow(kFireX, kFireY - 20, 90, 0xFFB347, 0.15f + 0.35f * dark_);
    drawCampfire(c, fireLit());
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

    if (!compact_ && overlay_ == Overlay::None) drawBubble(c);
    if (!compact_) drawStrip(c);
    if (s.foraging) drawForagingBanner(c);
    if (s.boarding && overlay_ == Overlay::None) drawBoarding(c);
    const bool playing = overlay_ == Overlay::Game && game_ != Game::Pick;  // the game has the screen to itself
    if (!playing && !compact_) {
        drawToast(c);
        drawTray(c);
    }
    if (drag_ == DragMode::Item && held_ != Item::None) {
        c.fillEllipse(dragX_, dragY_ - 8, 20, 6, 0x000000, 0.2f);
        drawItem(c, held_, dragX_, dragY_ - 40, 1.3f);  // above the finger, not under it
    }
    if (overlay_ == Overlay::Stats) drawStats(c);
    if (overlay_ == Overlay::Gallery) drawGallery(c);
    if (overlay_ == Overlay::Trails) drawTrails(c);
    if (overlay_ == Overlay::Settings) drawSettings(c);
    if (overlay_ == Overlay::Wifi) drawWifi(c);
    if (overlay_ == Overlay::Name) drawNaming(c);
    if (overlay_ == Overlay::StartOver) drawStartOver(c);
    if (overlay_ == Overlay::Game) drawGame(c);
    if (findingMe()) drawFindMe(c);
}

// Lost and pinged: a flashing banner over everything until someone touches the screen.
void Scene::drawFindMe(Canvas& c) const {
    const bool on = (now_ / 450) % 2 == 0;
    c.tint(on ? 0xFFF2B0 : 0x000000, on ? 0.35f : 0.25f);
    const float w = 420, h = bigText_ ? 150 : 128, x = 240 - w / 2, y = 150;
    c.fillRoundRect(x, y, w, h, 24, on ? kGold : 0x9E2B23);
    c.text(240, y + 22, chargeCall_ ? "CHARGE ME!" : "HERE I AM!", 4, on ? 0x3B2A1E : 0xFFFDF7, Align::Center);
    c.text(240, y + (bigText_ ? 86 : 78), chargeCall_ ? "MY BATTERY IS ALMOST EMPTY" : "TOUCH TO STOP", ts(2),
           on ? 0x3B2A1E : 0xFFFDF7, Align::Center);
}

void Scene::drawScenery(Canvas& c) {
    drawSky(c);
    drawClouds(c);
    drawGround(c);
    if (species_ == Species::Horse) drawFence(c);
    if (weather_.sky == Sky::Snow) drawSnowCover(c);
    else if (autumn()) drawFallenLeaves(c);
    drawCampfire(c, false);
    const float h = hourOfDay();
    if (h >= 17 && h < 23 && species_ != Species::Horse)
        c.glow(kFireX, kFireY - 20, 90, 0xFFB347, 0.15f + 0.35f * dark_);  // the fire's light
    if (species_ != Species::Horse) drawStump(c);
    drawTree(c, kLeftTreeX, kLeftTreeBase, 1.0f);
    drawTree(c, kRightTreeX, kRightTreeBase, 0.95f);
    drawLantern(c);
    if (species_ == Species::Horse) drawStump(c);  // the hay bale stands in front of the barn
    if (halloween()) drawPumpkin(c, dark_ > 0.25f);
    if (weather_.sky == Sky::Fog) drawFog(c);
}

// ---- the other holidays' decorations (Halloween's pumpkin is part of the scenery)

void Scene::tapDecoration() {
    const bool chatty = hatched() && !hiding() && !sim_.state().asleep;
    const float x = kPumpkinX, y = kPumpkinY;
    switch (holiday()) {
        case Holiday::Thanksgiving:
            say("GOBBLE GOBBLE!", true);
            cue(Cue::Gobble);
            fanUntil_ = now_ + 1600;
            if (chatty) speak(species_ == Species::Horse ? "A TURKEY! HI, TURKEY!" : "GOBBLE? GOBBLE!");
            break;
        case Holiday::Christmas:
            say("MERRY CHRISTMAS!", true);
            cue(Cue::HoHoHo);
            for (int i = 0; i < 10; i++)
                spawn(ParticleKind::Sparkle, x + (rand01() - 0.5f) * 60, y - 20 - rand01() * 80, 0, -30, 900);
            if (chatty) speak("IS SANTA COMING?");
            break;
        case Holiday::Valentine:
            say("HAPPY VALENTINE'S DAY!", true);
            cue(Cue::Happy);
            for (int i = 0; i < 6; i++)
                spawn(ParticleKind::Heart, x + (rand01() - 0.5f) * 40, y - 90, (rand01() - 0.5f) * 40, -50, 1300);
            if (chatty) speak("BE MY VALENTINE?");
            break;
        case Holiday::StPatrick:
            say("LUCKY YOU!", true);
            cue(Cue::Pickup);
            for (int i = 0; i < 8; i++)
                spawn(ParticleKind::Sparkle, x + (rand01() - 0.5f) * 40, y - 14, (rand01() - 0.5f) * 80, -90, 800);
            if (chatty) speak("I FEEL LUCKY!");
            break;
        case Holiday::Easter:
            say("HAPPY EASTER!", true);
            cue(Cue::NewEgg);
            if (chatty) speak(species_ == Species::Horse ? "EGG HUNT? FUN!" : "I CAME FROM AN EGG!");
            break;
        case Holiday::July4:
            say("HAPPY 4TH OF JULY!", true);
            cue(Cue::Crackle);
            for (int i = 0; i < 8; i++)
                spawn(ParticleKind::Sparkle, x + (rand01() - 0.5f) * 30, y - 70 - rand01() * 30, (rand01() - 0.5f) * 80, -60, 900);
            if (chatty) speak("OOH, FIREWORKS SOON!");
            break;
        default: break;
    }
}

void Scene::drawDecoration(Canvas& c) {
    const float x = kPumpkinX, y = kPumpkinY, t = static_cast<float>(t_);
    switch (holiday()) {
        case Holiday::Thanksgiving: {  // a turkey with things to do: struts, pecks, shows off its tail
            // A 15-second routine: strut left, peck, strut right, peck, puff up and fan the tail.
            const float xl = x - 92, xr = x - 14, cyc = std::fmod(t, 15.0f);
            float tx = xr, walk = 0, peck = 0;
            bool left = true, show = false;
            if (cyc < 4) tx = xr + (xl - xr) * (cyc / 4), walk = 1;
            else if (cyc < 6.5f) tx = xl, peck = 1;
            else if (cyc < 10.5f) tx = xl + (xr - xl) * ((cyc - 6.5f) / 4), walk = 1, left = false;
            else if (cyc < 12.5f) tx = xr, peck = 1, left = false;
            else tx = xr, show = true, left = std::fmod(t, 30.0f) >= 15;  // turns to show you
            const bool fan = show || now_ < fanUntil_;
            const float dir = left ? -1.0f : 1.0f;
            const float stride = walk * std::sin(t * 7);                      // legs swing as it walks
            const float bob = walk * std::fabs(std::sin(t * 7)) * 2.5f;       // up and down with each step
            const float nod = walk * std::sin(t * 7 + 1.2f) * 3.5f;           // the head jerks forward and back
            const float dip = peck * std::max(0.0f, std::sin(t * 9.0f)) * 1.0f;  // down to the ground and up
            const float puff = fan ? 1.12f : 1.0f;
            const float ty = y + 2 - bob;
            c.fillEllipse(tx, y + 16, 22, 5, 0x000000, 0.2f);
            // The tail: a fan of feathers behind, wider when shown off, with a little shiver.
            const Rgb feathers[] = {sh(0x8C4A22), sh(0xC8742E), sh(0xE0A33A), sh(0x6B3A1E)};
            const int n = fan ? 9 : 6;
            const float spread = fan ? 1.6f : 1.0f, len = fan ? 36 : 26;
            const float shiver = fan ? std::sin(t * 30) * 0.03f : walk * std::sin(t * 7) * 0.05f;
            for (int i = 0; i < n; i++) {
                const float a = (-0.5f + i / static_cast<float>(n - 1)) * spread - dir * (fan ? 0.1f : 0.35f) + shiver;
                c.line(tx - dir * 6, ty - 6, tx - dir * 6 + std::sin(a) * len, ty - 6 - std::cos(a) * len, 8, feathers[i % 4]);
            }
            c.line(tx - 3, ty + 8, tx - 4 + stride * 4, y + 16, 2, sh(0xC8962E));  // legs
            c.line(tx + 3, ty + 8, tx + 4 - stride * 4, y + 16, 2, sh(0xC8962E));
            c.fillEllipse(tx, ty, 13 * puff, 11 * puff, sh(0x5A3420));  // body
            c.fillEllipse(tx - dir * 2, ty - 2, 8 * puff, 6 * puff, sh(0x7A4A2A));
            // Neck and head: forward with each step, or right down to peck at the ground.
            const float hx = tx + dir * (11 + nod + dip * 9), hy = ty - 19 + dip * 30;
            c.line(tx + dir * 7, ty - 4, hx - dir * 1, hy + 3, 6, sh(0x5A3420));  // neck
            c.fillCircle(hx, hy, 5.5f, sh(0x9A7A6A));  // head
            c.fillTriangle(hx + dir * 4, hy - 1.5f, hx + dir * 4, hy + 1.5f, hx + dir * 9, hy + dip * 2, sh(0xE8A23A));  // beak
            const float swing = std::sin(t * 7 + 2) * (walk + peck) * 2;
            c.line(hx + dir * 4, hy + 3, hx + dir * 3 + swing, hy + 9 + (fan ? 2 : 0), 2.5f, sh(0xC0392B));  // wattle
            c.fillCircle(hx + dir * 1, hy - 1.5f, 1.4f, 0x1A1A1A);
            if (peck > 0 && dip > 0.95f && noise01(static_cast<uint32_t>(t * 9 / 6.2831853f)) < 0.5f)
                c.fillCircle(hx + dir * 9, y + 16, 1.2f, sh(0xD8C080));  // a seed, found
            break;
        }
        case Holiday::Christmas: {  // a little decorated fir, lights twinkling, presents from the 20th
            int m, d;
            localDate(m, d);
            c.fillEllipse(x, y + 12, 34, 6, 0x000000, 0.2f);
            c.fillRect(x - 4, y - 2, 8, 14, sh(kTrunk));
            const Rgb green = sh(0x2E6B3F), dark = sh(0x24563A);
            for (int i = 0; i < 3; i++) {
                const float ty = y - 2 - i * 24, hw = 30 - i * 7;
                c.fillTriangle(x - hw, ty, x + hw, ty, x, ty - 34, green);
                c.fillTriangle(x, ty, x + hw, ty, x, ty - 34, dark);
            }
            const Rgb bulbs[] = {0xE84A4A, 0xF2C14E, 0x4AA8E8, 0xE87AD0, 0x7AE87A};
            for (int i = 0; i < 12; i++) {
                const float bx = x + (noise01(i + 900) - 0.5f) * (44 - i * 2.5f), by = y - 8 - i * 6.0f;
                const float tw = 0.5f + 0.5f * std::sin(t * 3 + i * 1.7f);
                if (dark_ > 0.25f) c.glow(bx, by, 6, bulbs[i % 5], 0.35f * tw * dark_);
                c.fillCircle(bx, by, 2.2f, mix(sh(bulbs[i % 5]), bulbs[i % 5], dark_ * tw));
            }
            const float sy = y - 76;  // the star
            c.glow(x, sy, 12, 0xFFE08A, 0.3f + 0.2f * std::sin(t * 2));
            c.fillTriangle(x - 7, sy + 2, x + 7, sy + 2, x, sy - 9, 0xF2C14E);
            c.fillTriangle(x - 7, sy - 4, x + 7, sy - 4, x, sy + 7, 0xF2C14E);
            if (m == 12 && d >= 20) {  // presents
                c.fillRect(x - 30, y + 2, 16, 12, sh(0xC0392B));
                c.fillRect(x - 23, y + 2, 2, 12, sh(0xF2C14E));
                c.fillRect(x + 10, y + 4, 14, 10, sh(0x2E6FB0));
                c.fillRect(x + 16, y + 4, 2, 10, sh(0xF4EFE6));
            }
            break;
        }
        case Holiday::Valentine: {  // a heart balloon, bobbing on its string
            const float by = y - 84 + 4 * std::sin(t * 1.5f), bx = x - 4 + 3 * std::sin(t * 0.9f);
            c.line(x, y + 12, bx, by + 18, 1.5f, sh(0xF4EFE6));
            c.fillCircle(bx - 9, by - 4, 11, 0xE84A6A);
            c.fillCircle(bx + 9, by - 4, 11, 0xE84A6A);
            c.fillTriangle(bx - 19, by, bx + 19, by, bx, by + 20, 0xE84A6A);
            c.fillCircle(bx - 11, by - 8, 3.5f, 0xFFFFFF, 0.5f);
            c.fillCircle(x, y + 12, 3, sh(0x6B4A2F));  // a little peg in the ground
            break;
        }
        case Holiday::StPatrick: {  // a pot of gold among shamrocks
            for (int i = 0; i < 5; i++) {
                const float sx = x - 30 + i * 15.0f, sy = y + 14 - (i % 2) * 5.0f;
                for (int k = 0; k < 3; k++) c.fillCircle(sx + std::cos(k * 2.1f) * 3.5f, sy - 6 + std::sin(k * 2.1f) * 3.5f, 3.4f, sh(0x3E9A4A));
                c.line(sx, sy - 4, sx + 1, sy + 2, 1.5f, sh(0x2E7A3A));
            }
            c.fillEllipse(x, y + 10, 20, 5, 0x000000, 0.25f);
            c.fillEllipse(x, y, 18, 14, sh(0x2A2A2A));
            c.fillEllipse(x, y - 10, 18, 5, sh(0x3A3A3A));
            for (int i = 0; i < 6; i++) c.fillCircle(x - 12 + i * 5.0f, y - 12 - (i % 2) * 3.0f, 4, sh(0xF2C14E));
            c.fillCircle(x - 4, y - 16, 1.5f, 0xFFFFFF, 0.7f);
            break;
        }
        case Holiday::Easter: {  // a basket of painted eggs, and a few more hidden in the grass
            const Rgb eggs[] = {0xF7A8C8, 0xA8D8F2, 0xF6D65A, 0xB8F0C8, 0xC9A8F2};
            const float hidden[][2] = {{60, 440}, {205, 455}, {330, 300}};
            for (int i = 0; i < 3; i++) {
                c.fillEllipse(hidden[i][0], hidden[i][1], 6, 8, sh(eggs[i + 1]));
                c.line(hidden[i][0] - 5, hidden[i][1], hidden[i][0] + 5, hidden[i][1], 1.5f, sh(0xFFFFFF), 0.7f);
            }
            c.fillEllipse(x, y + 14, 26, 5, 0x000000, 0.2f);
            for (int i = 0; i < 4; i++) c.fillEllipse(x - 12 + i * 8.0f, y - 6 - (i % 2) * 3.0f, 6, 8, sh(eggs[i]));
            c.fillRoundRect(x - 22, y - 4, 44, 18, 6, sh(0xB8844A));
            for (int i = 0; i < 4; i++) c.line(x - 22, y + i * 4.0f, x + 22, y + i * 4.0f, 1.2f, sh(0x8C5A2A), 0.7f);
            c.ring(x, y - 4, 20, 2.5f, sh(0x8C5A2A));
            break;
        }
        case Holiday::July4: {  // a flag on a pole
            c.line(x - 14, y + 12, x - 14, y - 70, 2.5f, sh(0xC9C2B4));
            const float wave = std::sin(t * 2.2f) * 2;
            for (int r = 0; r < 7; r++)
                c.fillRect(x - 13, y - 70 + r * 4.0f + (r % 2 ? wave * 0.3f : 0), 34, 4, sh(r % 2 ? 0xF4EFE6 : 0xC0392B));
            c.fillRect(x - 13, y - 70, 15, 16, sh(0x2E4A8A));
            for (int i = 0; i < 6; i++) c.fillCircle(x - 9 + (i % 3) * 4.5f, y - 66 + (i / 3) * 6.0f, 0.9f, 0xFFFFFF);
            break;
        }
        default: break;
    }
}

// Bursts high over the trees on fireworks nights: each blooms, drifts down and fades.
void Scene::drawFireworks(Canvas& c) {
    const Rgb cols[] = {0xFF5A5A, 0xFFD24A, 0x6AC8FF, 0xFF7AE0, 0x8AFF8A, 0xFFFFFF};
    for (int k = 0; k < 4; k++) {
        const double period = 2.2 + k * 0.45;
        const double phase = std::fmod(t_ + k * 0.9, period) / period;  // 0..1 through one burst
        const int burst = static_cast<int>((t_ + k * 0.9) / period);
        const float bx = 70 + noise01(static_cast<uint32_t>(burst * 7 + k * 101)) * 340;
        const float by = 60 + noise01(static_cast<uint32_t>(burst * 13 + k * 37)) * 90;
        const Rgb col = cols[(burst + k) % 6];
        if (phase < 0.2) {  // the rocket going up
            const float p = static_cast<float>(phase / 0.2);
            c.fillCircle(bx, kHorizon - (kHorizon - by) * p, 2, 0xFFE8B0, 0.8f);
        } else {
            const float p = static_cast<float>((phase - 0.2) / 0.8), r = 8 + p * 46, a = std::sqrt(1 - p) * dark_;
            if (p < 0.15f) c.glow(bx, by, 40, col, 0.4f * dark_);
            for (int i = 0; i < 14; i++) {
                const float ang = i * (kPi * 2 / 14);
                c.fillCircle(bx + std::cos(ang) * r, by + std::sin(ang) * r + p * p * 18, 2.4f * (1 - p * 0.5f), col, a);
            }
        }
    }
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
        const float sunA = (1 - dark_ / 0.3f) * (1 - overcast());
        if (detail_) c.glow(150, 76, 80, 0xFFF4C8, 0.35f * sunA);
        c.fillCircle(150, 76, 22, 0xFFE9A8, sunA);
    }
}

void Scene::drawGround(Canvas& c) {
    if (detail_) {
        drawGroundDetail(c);
        return;
    }
    c.fillEllipse(110, kHorizon + 14, 210, 62, sh(0x5B7F8F));
    c.fillEllipse(390, kHorizon + 18, 230, 72, sh(0x4F7584));
    for (int i = 0; i < 16; i++) {  // distant tree line
        float x = static_cast<float>(i * 32 + (i % 3) * 7 - 10);
        float hgt = 46 + static_cast<float>((i * 37) % 28);
        c.fillTriangle(x - 18, kHorizon + 12, x + 18, kHorizon + 12, x, kHorizon + 12 - hgt, sh(kBackTrees));
    }
    c.fillRect(0, kHorizon, S, S - kHorizon, sh(kGrass));
    c.fillEllipse(240, 372, 252, 122, sh(species_ == Species::Horse ? kPaddock : kClearing));
    for (int i = 0; i < 18; i++) {  // grass tufts
        float x = static_cast<float>((i * 131 + 40) % S), y = kHorizon + 30 + static_cast<float>((i * 71) % 240);
        c.line(x - 4, y, x - 6, y - 9, 2, sh(0x4C8A3A));
        c.line(x, y, x, y - 11, 2, sh(0x4C8A3A));
        c.line(x + 4, y, x + 6, y - 9, 2, sh(0x4C8A3A));
    }
}

void Scene::drawTree(Canvas& c, float x, float base, float size) {
    if (species_ == Species::Horse) {  // the paddock: an apple tree on the left, the barn on the right
        if (x < 240) drawAppleTree(c, x, base);
        else drawBarn(c);
        return;
    }
    if (detail_) {
        drawTreeDetail(c, x, base, size);
        return;
    }
    c.fillRoundRect(x - 14 * size, base - 150 * size, 28 * size, 150 * size, 6, sh(kTrunk));
    c.fillEllipse(x, base, 30 * size, 7 * size, sh(0x3F5E2E));  // roots in shadow
    const float sway = treeSway(x), x0 = x;
    for (int i = 0; i < 4; i++) {
        x = x0 + sway * (i + 1) / 4;  // the top moves most
        float y = base - 90 * size - i * 45 * size, hw = (78 - i * 14) * size;
        c.fillTriangle(x - hw, y, x + hw, y, x, y - 85 * size, sh(i % 2 ? kLeaves2 : kLeaves1));
        if (weather_.sky == Sky::Snow)  // a cap of snow on each tier
            c.fillTriangle(x - hw * 0.42f, y - 50 * size, x + hw * 0.42f, y - 50 * size, x, y - 85 * size, sh(0xEEF3F8));
    }
}

void Scene::drawStump(Canvas& c) {
    if (species_ == Species::Horse) {
        drawHayBale(c);
        return;
    }
    float x = kStumpX, y = kStumpY;
    if (detail_) {
        const Rgb bark = sh(kTrunk), deep = sh(mix(kTrunk, 0x000000, 0.35f));
        c.fillEllipse(x + 14, y + 3, 44, 7, 0x000000, 0.16f);                   // its shadow
        c.fillTriangle(x - 40, y + 4, x - 20, y + 4, x - 26, y - 12, bark);      // roots
        c.fillTriangle(x + 40, y + 4, x + 20, y + 4, x + 26, y - 12, deep);
        c.fillRoundRect(x - 30, y - 26, 60, 30, 8, bark);
        c.line(x + 21, y - 22, x + 21, y + 1, 12, deep, 0.6f);                   // the shady side
        for (int j = 0; j < 6; j++) {                                           // bark grooves
            const float bx = x - 23 + j * 9.0f;
            c.line(bx, y - 20 + (j % 2) * 4.0f, bx + 1.5f, y + 1, 1.6f, deep, 0.75f);
        }
        const Rgb top = sh(0xC9A06F), ringC = sh(0xA67C52);
        const float rings[][2] = {{30, 9}, {23, 6.9f}, {21.4f, 6.4f}, {14, 4.2f}, {12.6f, 3.8f}, {5, 1.6f}};
        for (int r = 0; r < 6; r++)
            c.fillEllipse(x, y - 26, rings[r][0], rings[r][1], r % 2 ? ringC : top);
        c.fillEllipse(x - 10, y - 28, 9, 2.4f, 0xFFFFFF, 0.18f);  // a shine on the cut
    } else {
        c.fillRoundRect(x - 30, y - 26, 60, 30, 8, sh(kTrunk));
        c.fillEllipse(x, y - 26, 30, 9, sh(0xC49A6C));
        c.ring(x, y - 26, 14, 2, sh(0xA67C52));
    }
    if (foodLeftToday()) {
        c.fillEllipse(x, y - 30, 20, 8, sh(0x9C6B3A));
        c.fillCircle(x - 7, y - 36, 5, sh(0x8E2A4F));
        c.fillCircle(x + 3, y - 37, 5, sh(0x8E2A4F));
        c.fillCircle(x + 9, y - 33, 4, sh(0x8E2A4F));
    }
}

void Scene::drawCampfire(Canvas& c, bool flames) {
    if (species_ == Species::Horse) {
        drawTrough(c);
        return;
    }
    float x = kFireX, y = kFireY;
    if (!flames) {
        for (int i = 0; i < 7; i++) {
            float a = i * 2 * kPi / 7;
            c.fillEllipse(x + std::cos(a) * 26, y + std::sin(a) * 9, 8, 5, sh(0x8C8C86));
            if (detail_) c.fillEllipse(x + std::cos(a) * 26 - 1.5f, y + std::sin(a) * 9 - 1.8f, 5, 2.4f, sh(0xC4C4BC));
        }
        c.line(x - 16, y + 2, x + 14, y - 4, 7, sh(0x5A3A22));
        c.line(x - 14, y - 4, x + 16, y + 2, 7, sh(0x6B4A2F));
        return;
    }
    float fl = static_cast<float>(std::sin(t_ * 13) * 0.5 + std::sin(t_ * 7.3) * 0.5);  // its light is in the scenery
    c.fillTriangle(x - 14, y, x + 14, y, x + fl * 3, y - 38 - fl * 6, 0xF07B2B);
    c.fillTriangle(x - 8, y, x + 9, y, x - fl * 2, y - 26 + fl * 4, 0xFFD25E);
}

// ================================================================ the paddock (horse edition)

void Scene::drawFence(Canvas& c) {
    // A post-and-rail fence across the back of the paddock.
    const Rgb wood = sh(0xC9A27A), dark = sh(0x8C6A48);
    const float base = kHorizon + 34;
    for (int r = 0; r < 2; r++) {
        const float y = base - 22 + r * 11;
        c.fillRect(0, y, S, 5, wood);
        c.fillRect(0, y + 4, S, 1.5f, dark, 0.7f);
    }
    for (int i = 0; i < 9; i++) {
        const float x = 10 + i * 58.0f;
        c.fillRoundRect(x - 4, base - 30, 8, 32, 2, wood);
        c.fillRect(x + 1, base - 30, 3, 32, dark, 0.5f);
    }
}

void Scene::drawAppleTree(Canvas& c, float x, float base) {
    const float sway = treeSway(x);
    const Rgb trunk = sh(kTrunk), trunkDark = sh(mix(kTrunk, 0x000000, 0.35f));
    c.fillEllipse(x + 24, base + 2, 60, 10, 0x000000, 0.14f);
    c.fillRoundRect(x - 13, base - 150, 26, 150, 6, trunk);
    c.line(x + 7, base - 146, x + 7, base - 3, 8, trunkDark, 0.7f);
    c.line(x, base - 120, x + 34 + sway * 0.5f, base - 170, 9, trunk);  // a branch
    // A round crown of leaves in clumps, darker underneath.
    const float cx = x + 10 + sway, cy = base - 196;
    const Rgb leaf = sh(0x3F8A3E), deep = sh(0x2C6A30), light = sh(0x6BB04E);
    const float clumps[][3] = {{-40, 22, 44}, {36, 26, 42}, {0, 34, 46}, {-24, -16, 46}, {28, -14, 44}, {2, -40, 40}};
    for (const auto& k : clumps) c.fillCircle(cx + k[0], cy + k[1] + 4, k[2], deep);
    for (const auto& k : clumps) c.fillCircle(cx + k[0] - 3, cy + k[1] - 2, k[2] * 0.9f, leaf);
    c.fillCircle(cx - 24, cy - 30, 22, light, 0.5f);
    // Apples.
    for (int i = 0; i < 9; i++) {
        const float ax = cx + (noise01(i * 2 + 300) - 0.5f) * 120, ay = cy + (noise01(i * 2 + 301) - 0.4f) * 90;
        c.fillCircle(ax, ay, 5.5f, sh(0xC8323A));
        c.fillCircle(ax - 1.8f, ay - 1.8f, 1.8f, 0xFFFFFF, 0.4f);
    }
    if (weather_.sky == Sky::Snow)  // snow settled on the top of each clump
        for (const auto& k : clumps)
            if (k[1] < 0) c.fillEllipse(cx + k[0] - 3, cy + k[1] - k[2] * 0.7f, k[2] * 0.6f, k[2] * 0.22f, sh(0xEEF3F8));
}

void Scene::drawBarn(Canvas& c) {
    // A red barn on the right, cut off by the edge of the screen.
    const float l = 364, r = 500, top = 244, base = kRightTreeBase, peak = 186, mid = (l + r) / 2;
    const Rgb red = sh(0xB2392E), redDark = sh(0x86281F), trim = sh(0xF4EFE6), roof = sh(0x5A4A44);
    c.fillEllipse(mid - 10, base + 2, 90, 10, 0x000000, 0.14f);
    c.fillRect(l, top, r - l, base - top, red);
    for (int i = 1; i < 9; i++) c.fillRect(l, top + i * 11.0f, r - l, 1.2f, redDark, 0.6f);  // boards
    c.fillTriangle(l, top, r, top, mid, peak + 8, red);
    c.fillTriangle(l - 10, top + 2, mid, peak, mid, peak + 12, roof);  // the roof
    c.fillTriangle(r + 10, top + 2, mid, peak, mid, peak + 12, roof);
    c.line(l - 10, top + 2, mid, peak, 7, roof);
    c.line(mid, peak, r + 10, top + 2, 7, roof);
    c.fillRect(l, top, r - l, 3, trim);
    // The hayloft door and the big doors with their white X braces.
    c.fillRect(mid - 11, top - 30, 22, 24, trim);
    c.fillRect(mid - 8, top - 27, 16, 18, sh(0x3A2A20));
    const float dl = mid - 26, dr = mid + 26, dt = base - 62;
    c.fillRect(dl - 3, dt - 3, dr - dl + 6, base - dt + 3, trim);
    c.fillRect(dl, dt, dr - dl, base - dt, redDark);
    c.line(mid, dt, mid, base, 3, trim);
    for (float side : {-1.0f, 1.0f}) {
        const float a = side < 0 ? dl : mid, b = side < 0 ? mid : dr;
        c.line(a, dt, b, base, 3, trim);
        c.line(b, dt, a, base, 3, trim);
    }
    if (weather_.sky == Sky::Snow) {
        c.line(l - 10, top, mid, peak - 2, 5, sh(0xEEF3F8));
        c.line(mid, peak - 2, r + 10, top, 5, sh(0xEEF3F8));
    }
}

void Scene::drawHayBale(Canvas& c) {
    const float x = kStumpX, y = kStumpY;
    const Rgb hay = sh(0xE2C267), hayDark = sh(0xB8973F), twine = sh(0x8C5A3A);
    c.fillEllipse(x + 10, y + 3, 42, 7, 0x000000, 0.16f);
    c.fillRoundRect(x - 34, y - 28, 68, 32, 6, hay);
    c.fillRect(x - 34, y - 28, 68, 8, sh(0xEFD486));  // the sunny top
    for (int i = 0; i < 9; i++) {  // straw
        const float sx = x - 30 + i * 7.5f;
        c.line(sx, y - 18 + (i % 3) * 3.0f, sx + 4, y - 6 + (i % 2) * 4.0f, 1.3f, hayDark, 0.7f);
    }
    c.fillRect(x - 18, y - 28, 3, 32, twine);
    c.fillRect(x + 15, y - 28, 3, 32, twine);
    for (int i = 0; i < 5; i++)  // a few stray wisps
        c.line(x - 34 + i * 17.0f, y + 3, x - 30 + i * 17.0f, y + 6, 1.2f, hay, 0.8f);
}

void Scene::drawTrough(Canvas& c) {
    const float x = kFireX, y = kFireY;
    const Rgb wood = sh(0x8C6A48), woodDark = sh(0x5E452E);
    c.fillEllipse(x + 6, y + 10, 46, 6, 0x000000, 0.16f);
    c.fillRect(x - 34, y, 6, 12, woodDark);  // legs
    c.fillRect(x + 28, y, 6, 12, woodDark);
    c.fillRoundRect(x - 38, y - 22, 76, 26, 4, wood);
    c.fillRect(x - 38, y - 12, 76, 2, woodDark, 0.7f);
    c.fillRoundRect(x - 33, y - 21, 66, 8, 3, sh(0x5FA8CF));  // the water
    const float ripple = static_cast<float>(std::sin(t_ * 2)) * 6;
    c.fillEllipse(x - 8 + ripple, y - 18, 10, 1.6f, 0xFFFFFF, 0.5f);
}

void Scene::drawLantern(Canvas& c) {
    const bool horse = species_ == Species::Horse;
    float x = horse ? 384 : 402, y = horse ? 262 : 238;  // on the barn wall, or a branch
    bool lit = sim_.state().lightsOn;
    if (horse) c.line(x - 8, y - 16, x, y - 16, 3, sh(0x2A2A2A));  // an iron bracket
    else c.line(kRightTreeX - 14, y - 16, x, y - 16, 4, sh(kTrunk));  // a branch stub
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
    float blink, lookX, lookY, armL, armR;
    float mouth;  // -1 frown, 0 flat, 1 smile, 2 chewing
    float breath, wide;
    bool angry;
    bool hat;  // a witch's hat, Halloween week
    float gait = -1;  // walking: the legs' phase; below 0 standing still
    float stride = 1; // how much it's walking, 0..1: the steps' size, and the bob and nod with them
    float graze = 0;  // the horse's head lowered (0 up .. 1 at the ground), to drink or graze
};

// The horse edition's start: the foal's mother, waiting in the paddock. As the time comes
// near she glows a little (more for a rare foal) and looks back at her belly; a tap and she
// nods. After the birth she stands a moment, then walks back to her barn.
void Scene::drawMare(Canvas& c) {
    const State& s = sim_.state();
    Look L{};
    L.s = 1;
    L.head = 1;
    L.wide = 1.06f;  // expecting
    L.breath = 1 + 0.015f * static_cast<float>(std::sin(t_ * 1.6));
    L.blink = now_ < blinkUntil_ ? 1.0f : 0.0f;
    L.mouth = 1;
    L.ground = L.foot = kFriendlyFoot;
    const float saved = faceDir_;
    if (s.stage == Stage::Egg) {
        const float progress = static_cast<float>(sim_.ageSecs()) / static_cast<float>(sim_.tuning().eggEnd);
        L.x = 250;
        faceDir_ = -1;
        // A bed of straw for the foal to be born on.
        c.fillEllipse(L.x - 20, kFriendlyFoot + 2, 95, 14, sh(0xD9B655));
        for (int i = 0; i < 14; i++) {
            const float sx = L.x - 100 + i * 14.0f, sy = kFriendlyFoot - 4 + (i % 3) * 4.0f;
            c.line(sx, sy, sx + 12, sy - 4 + (i % 2) * 6.0f, 2, sh(i % 2 ? 0xEBCB6E : 0xC9A245));
        }
        if (react_ == React::Wobble && now_ < reactUntil_) L.armR = 0.4f;  // a nod and a hoof
        if (progress > 0.75f) {  // nearly time: she looks back, and a rare foal shows
            L.lookX = 0.6f;
            const Traits t = sim_.traits();
            if (t.rarity >= Rarity::Rare) {
                const Rgb tint = t.rarity == Rarity::Mythic ? 0xFFD34D : (t.rarity == Rarity::Epic ? 0xF2C8FF : 0xBFE3FF);
                c.glow(L.x, kFriendlyFoot - 80, 130, tint, 0.45f + 0.15f * std::sin(t_ * 2));
                if (noise01(static_cast<uint32_t>(t_ * 3) + 77) < 0.05f)
                    spawn(ParticleKind::Sparkle, L.x + (rand01() - 0.5f) * 140, kFriendlyFoot - 40 - rand01() * 100, 0, -25, 900);
            }
            if (noise01(static_cast<uint32_t>(t_ * 2)) < 0.02f)
                spawn(ParticleKind::Heart, L.x - 40, kFriendlyFoot - 140, 0, -30, 1200);
        }
    } else {
        // Born: she stays three seconds, then walks off to the right, into the barn.
        const float since = static_cast<float>(now_ - bornAt_) / 1000.0f;
        const float walk = std::max(0.0f, since - 3.0f) / 6.0f;
        L.x = 300 + walk * 230;
        L.foot = L.ground = kFriendlyFoot - walk * 30;  // toward the barn, further back
        L.s = 1 - walk * 0.15f;
        faceDir_ = walk > 0 ? 1.0f : -1.0f;
        if (walk > 0) L.gait = t_ * 8;
    }
    drawHorse(c, L, true);
    faceDir_ = saved;
}

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
    L.lookY = lookY_;
    L.breath = 1 + 0.018f * std::sin(t * (s.asleep ? 1.6f : 2.6f));
    L.mouth = s.happiness >= 2 ? 1.0f : (s.happiness < 1 ? -1.0f : 0.0f);
    L.angry = s.misbehaving;
    L.hat = hatWeek();

    if (sim_.trustLevel() == TrustLevel::Wandering && !s.asleep && std::fabs(wanderTarget_ - wanderX_) > 1) {
        if (species_ != Species::Horse) L.foot -= std::fabs(std::sin(t * 8)) * 4 * L.s;  // walking bob
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
        case React::Boop:  // scrunches its nose
            L.blink = 1;
            L.mouth = 1;
            L.s *= 0.98f;
            break;
        case React::Giggle:
            L.foot -= std::fabs(std::sin(t * 16)) * 5 * L.s;
            L.blink = 1;
            L.mouth = 2;
            L.armL = L.armR = 0.2f;
            break;
        case React::Pat:
            L.blink = 1;
            L.mouth = 1;
            L.breath *= 1.02f;
            break;
        case React::Swat:
            L.angry = true;
            L.mouth = -1;
            L.armR = 0.45f + 0.45f * std::sin(t * 14);
            break;
        default: break;
    }
    if (fetch_ == Fetch::Chase || fetch_ == Fetch::Carry) {
        if (species_ != Species::Horse) L.foot -= std::fabs(std::sin(t * 10)) * 5 * L.s;  // trotting
        if (fetch_ == Fetch::Carry) L.armR = 0.25f;
        L.gait = t * 10;
    }
    if (sim_.trustLevel() == TrustLevel::Wandering && !s.asleep && std::fabs(wanderTarget_ - wanderX_) > 1)
        L.gait = t * 8;
    if (species_ == Species::Horse) {  // its own walk: steps that match its pace (drawHorse bobs and nods with them)
        L.gait = walkAmt_ > 0 ? gaitPhase_ : -1;
        L.stride = walkAmt_;
    }
    if (species_ == Species::Horse && overlay_ == Overlay::Game && game_ == Game::Groom) {  // standing still for it
        faceDir_ = 1;
        L.x = kGroomX;
        L.foot = L.ground = kGroomFoot;
        L.s *= kGroomZoom;
        L.gait = -1;
        L.mouth = 1;
        if (groomStep_ == GroomStep::Hooves) L.armR = 0.85f;  // a front hoof up for you
        drawHorse(c, L);
        return;
    }
    if (species_ == Species::Horse && overlay_ == Overlay::Game && petOverride_ &&
        game_ == Game::Photo && running_) {  // galloping past
        faceDir_ = runRight_ ? 1.0f : -1.0f;
        L.gait = t * 16;
        L.stride = 1;
        L.ground = squatchFoot();
        L.mouth = 1;
        drawHorse(c, L);
        return;
    }
    if (species_ == Species::Horse && drinking()) {
        const float since = static_cast<float>(now_ - drinkAt_) / 1000.0f;
        const bool there = since >= 1.4f && since < 3.8f;
        faceDir_ = since < 3.8f ? -1.0f : 1.0f;
        if (there) L.mouth = 2;  // slurping
        // Head down to the water once it's there, and up again before it goes.
        L.graze = std::max(0.0f, std::min(1.0f, std::min((since - 1.4f) / 0.4f, (3.8f - since) / 0.4f)));
        if (there && now_ >= nextSlurp_) {  // a sip, a gulp, a pause, another
            cue(Cue::Sip);
            spawn(ParticleKind::Drop, kFireX + 20, kFireY - 22, (rand01() - 0.5f) * 30, -60, 500);
            nextSlurp_ = now_ + 800;
        }
        if (since >= 2.0f && since < 2.1f && !bubble()) speak("SLURP SLURP... AHH!");
        drawHorse(c, L);
        return;
    }
    if (species_ == Species::Horse && newborn()) {  // nuzzling its mother, then trotting off
        const bool off = now_ - bornAt_ >= 3000;
        const bool toHide = sim_.trustLevel() == TrustLevel::Hiding;
        faceDir_ = off ? (toHide ? -1.0f : 1.0f) : 1.0f;  // facing the way it goes
        drawHorse(c, L);
        return;
    }
    if (species_ == Species::Horse) {
        // It turns to face where it's looking, but not back and forth on every glance.
        if (L.gait >= 0 && fetch_ != Fetch::None) faceDir_ = (fetch_ == Fetch::Chase ? coneX_ : 240.0f) >= L.x ? 1.0f : -1.0f;
        else if (L.lookX > 0.35f) faceDir_ = 1;
        else if (L.lookX < -0.35f) faceDir_ = -1;
        drawHorse(c, L);
        return;
    }
    drawSquatch(c, L);
}

// ================================================================ the horse edition

void Scene::horseColours(Rgb& coat, Rgb& mane, Rgb& muzzle, bool& unicorn, bool& golden) const {
    const State& s = sim_.state();
    const Traits t = sim_.traits();
    unicorn = t.rarity == Rarity::Epic || t.rarity == Rarity::Mythic;
    golden = t.variant == Variant::Golden;
    switch (t.variant) {
        case Variant::Golden:  // a golden unicorn, the rarest of all
            coat = 0xE9C46A;
            mane = 0xFFF3D6;
            break;
        case Variant::Snowcoat:
        case Variant::Albino:  // a white unicorn with a rainbow mane (or, if only Rare, a white horse)
            coat = 0xF4F1EC;
            mane = 0xE7E2DA;
            break;
        case Variant::Swamp:  // a dapple grey
            coat = 0xA7AAAE;
            mane = 0x4A4C50;
            break;
        case Variant::None: {
            // The ordinary coats, from dark to light: black, bay, chestnut, buckskin, palomino.
            const float h = t.hue;  // browns; nudges each coat's warmth a little
            if (t.shade < 0.32f) {
                coat = hsv(h, 0.35f, 0.24f);
                mane = 0x18120F;
            } else if (t.shade < 0.5f) {
                coat = hsv(h - 6, 0.68f, 0.5f);
                mane = 0x1E1612;
            } else if (t.shade < 0.66f) {
                coat = hsv(h - 4, 0.66f, 0.66f);
                mane = hsv(h - 6, 0.7f, 0.5f);
            } else if (t.shade < 0.8f) {
                coat = hsv(h + 6, 0.45f, 0.78f);
                mane = 0x221A15;
            } else {
                coat = hsv(h + 12, 0.55f, 0.88f);
                mane = 0xFAEED2;
            }
            break;
        }
    }
    if (s.stage == Stage::Elder) coat = mix(coat, 0xC9C4BE, 0.35f);  // greying
    if (s.sick) coat = mix(coat, 0x9DB58A, 0.3f);
    muzzle = mix(coat, golden ? 0xFFF1D8 : (t.variant == Variant::None ? 0x6B5A52 : 0xE8C8C0), 0.35f);
    coat = mix(coat, kNight, dark_ * 0.35f);
    mane = mix(mane, kNight, dark_ * 0.35f);
    muzzle = mix(muzzle, kNight, dark_ * 0.35f);
}

// A cartoon horse side on, with its head turned a little toward you. Same poses as the
// Squatch: armR lifts a front hoof (a wave), mouth smiles or chews, blink closes the eye.
void Scene::drawHorse(Canvas& c, const Look& L, bool mother) {
    const State& st = sim_.state();
    const Traits dna = sim_.traits();
    Rgb coat, mane, muzzle;
    bool unicorn, golden;
    horseColours(coat, mane, muzzle, unicorn, golden);
    if (mother) unicorn = golden = false;  // the foal's mother: a plain horse with the same coat
    const Stage stage = mother ? Stage::Adult : st.stage;
    const float s = L.s, dir = faceDir_, b = L.breath, w = L.wide;
    // Walking: the body rises a little over each step and the head nods with it; the hooves stay on the ground.
    const float amp = L.gait >= 0 ? std::max(0.0f, std::min(1.0f, L.stride)) : 0;
    const float bob = amp * 2.2f * s * (0.5f - 0.5f * std::cos(2 * L.gait));
    const float hoofGround = L.foot, f = L.foot - bob;
    const float hd = L.head;  // foals have bigger heads
    // Foals are mostly legs.
    const float legLen = (stage == Stage::Baby ? 62 : stage == Stage::Child ? 56 : stage == Stage::Teen ? 52 : 50);
    const float bodyRx = (stage == Stage::Baby ? 46 : 58) * w, bodyRy = (stage == Stage::Baby ? 26 : 31) * b;
    const float bodyY = -legLen - bodyRy * 0.55f;
    auto X = [&](float dx) { return L.x + dir * dx * s; };
    auto Y = [&](float dy) { return f + dy * s; };
    const Rgb shade = mix(coat, 0x140C08, 0.3f), lit = mix(coat, 0xFFF6E8, 0.25f);
    const Rgb hoof = mix(0x3A2E28, kNight, dark_ * 0.35f);
    const bool socks = dna.markings == 1, points = dna.markings == 2, spots = dna.markings == 3;
    const Rgb white = mix(0xFAF7F2, kNight, dark_ * 0.35f);
    static const Rgb kRainbow[] = {0xF7A8C8, 0xC9A8F2, 0xA8D8F2, 0xB8F0C8, 0xFBE3A0};
    auto maneCol = [&](int i) -> Rgb {
        if (unicorn && !golden) return mix(kRainbow[i % 5], kNight, dark_ * 0.35f);
        if (golden && i % 2) return mix(0xFFE08A, kNight, dark_ * 0.35f);
        return mane;
    };

    // Its shadow.
    c.fillEllipse(L.x, L.ground, 72 * s * w, 10 * s, 0x000000, detail_ ? 0.12f : 0.18f);

    // Legs: far pair first (darker), then the body, then the near pair.
    const float legW = (stage == Stage::Baby ? 10.5f : 13) * s;
    auto leg = [&](float dx, bool near, float phase, float lift) {
        // Each hoof swings forward through the air (lifted, the joint bent) and pushes back along the ground.
        const float ph = L.gait + phase;
        const float swing = amp > 0 ? std::sin(ph) * 9 * amp : 0;
        const float up = amp > 0 ? std::max(0.0f, std::cos(ph)) : 0;  // in the air for half of each stride
        const float topX = X(dx), topY = Y(-legLen - 4);
        float footX = X(dx + swing), footY = hoofGround - up * 7 * amp * s;
        if (lift > 0) {  // a raised front hoof, bent at the knee
            footX = X(dx + 14 * lift);
            footY = Y(-legLen * 0.45f * lift - 4);
        }
        const Rgb col = near ? coat : shade;
        const Rgb low = points ? mix(0x1E1612, kNight, dark_ * 0.35f) : (socks && near ? white : col);
        // Knees bend forward, hocks (the back legs) back, more as the hoof comes up.
        const float bend = (lift > 0 ? 6 * s : up * 6 * amp * s) * (dx > 0 ? 1 : -1);
        const float kneeX = (topX + footX) / 2 + dir * bend, kneeY = (topY + footY) / 2;
        c.line(topX, topY, kneeX, kneeY, legW, col);
        c.line(kneeX, kneeY, footX, footY, legW * 0.85f, low);
        c.fillRoundRect(footX - legW * 0.62f, footY - 7 * s, legW * 1.24f, 8 * s, 3 * s, hoof);
    };
    const float lift = std::max(0.0f, std::min(1.0f, L.armR * 1.2f));
    leg(20, false, 0.0f, 0);
    leg(-48, false, 3.1f, 0);

    // The tail, swishing gently.
    const float swish = static_cast<float>(std::sin(t_ * 1.3)) * 4 + amp * 3 * std::sin(L.gait);
    for (int k = 0; k < 5; k++) {
        float px = X(-bodyRx + 4), py = Y(bodyY - bodyRy * 0.5f);
        for (int i = 1; i <= 6; i++) {
            const float nx = X(-bodyRx - 4 - i * 2.2f - (k - 2) * 2.5f) - dir * std::sin(i * 0.5f) * swish * s;
            const float ny = Y(bodyY - bodyRy * 0.5f + i * 9 + k * 1.5f);
            c.line(px, py, nx, ny, (9 - i * 0.9f) * s, maneCol(k));
            px = nx;
            py = ny;
        }
    }

    // The body: a darker belly and a lit back, so it looks round.
    const float bx = L.x, by = Y(bodyY);
    c.fillEllipse(bx, by, bodyRx * s, bodyRy * s, shade);
    c.fillEllipse(bx, by - bodyRy * s * 0.1f, bodyRx * s * 0.97f, bodyRy * s * 0.86f, coat);
    if (detail_) c.glow(bx - dir * bodyRx * s * 0.1f, by - bodyRy * s * 0.45f, bodyRx * s * 0.6f, lit, 0.3f);
    if (spots) {  // an appaloosa's spotted blanket over the rump
        c.fillEllipse(X(-bodyRx * 0.45f), by - bodyRy * s * 0.15f, bodyRx * s * 0.42f, bodyRy * s * 0.62f, white);
        for (int i = 0; i < 9; i++) {
            const float u = noise01(i * 3 + 11), v = noise01(i * 3 + 12);
            c.fillCircle(X(-bodyRx * (0.15f + 0.6f * u)), by + (v - 0.6f) * bodyRy * s * 0.9f, (2 + 2 * noise01(i + 40)) * s,
                         mix(coat, 0x1A120E, 0.35f));
        }
    }
    if (stage == Stage::Baby)  // a foal's fuzzy little tail-end and belly
        c.fillEllipse(bx, by + bodyRy * s * 0.55f, bodyRx * s * 0.6f, bodyRy * s * 0.25f, shade, 0.5f);

    leg(40, true, 1.6f, lift);
    leg(-30, true, 4.7f, 0);
    const bool grooming = !mother && overlay_ == Overlay::Game && game_ == Game::Groom;
    if (grooming) {  // where the mud goes
        gBodyX_ = bx;
        gBodyY_ = by;
        gBodyRx_ = bodyRx * s;
        gBodyRy_ = bodyRy * s;
        gLegX_ = X(-30);
        gLegY_ = Y(-legLen * 0.45f);
        gScale_ = s;
    }

    // The neck, up and forward to the head.
    const float nod = amp * 2.5f * s * std::sin(2 * L.gait + 0.9f) * (1 - L.graze);  // the head nods with each step
    const float hx = X(bodyRx * 0.95f + 6 + 16 * L.graze),
                hy = Y((bodyY - 50 - (hd - 1) * 12) * (1 - L.graze) + (-14 * hd) * L.graze) + nod;
    const float nbx = X(bodyRx * 0.6f), nby = Y(bodyY - bodyRy * 0.2f);
    // A broad base in the shoulders, tapering to the throat: two strokes and the wedge between.
    const float topX = nbx - dir * 16 * s, topY = nby - 10 * s, chestX = nbx + dir * 14 * s, chestY = nby + 14 * s;
    c.fillTriangle(topX, topY, chestX, chestY, hx + dir * 4 * s, hy + 12 * s * hd, coat);
    c.fillTriangle(topX, topY, hx - dir * 8 * s * hd, hy - 4 * s * hd, hx + dir * 4 * s, hy + 12 * s * hd, coat);
    c.line(topX, topY, hx - dir * 6 * s * hd, hy, 20 * s, coat);
    c.line(chestX, chestY, hx + dir * 2 * s, hy + 12 * s * hd, 18 * s, coat);
    if (detail_) c.line(chestX, chestY, hx + dir * 4 * s, hy + 16 * s * hd, 7 * s, shade, 0.45f);

    // The mane along the top of the neck.
    for (int i = 0; i < 7; i++) {
        const float u = i / 6.0f;
        const float mx = hx - dir * 10 * s * hd + (topX - (hx - dir * 10 * s * hd)) * u - dir * 4 * std::sin(u * 3.1f) * s;
        const float my = hy - 10 * s * hd + (topY - (hy - 10 * s * hd)) * u - 4 * s;
        c.fillCircle(mx, my, (8.5f - u * 2) * s, maneCol(i));
        c.fillTriangle(mx - 6 * s, my, mx + 6 * s, my, mx - dir * 9 * s, my + 16 * s, maneCol(i));
    }
    auto manePoint = [&](float u, float& px, float& py) {
        px = hx - dir * 10 * s * hd + (topX - (hx - dir * 10 * s * hd)) * u - dir * 4 * std::sin(u * 3.1f) * s;
        py = hy - 10 * s * hd + (topY - (hy - 10 * s * hd)) * u - 4 * s + 6 * s;
    };
    if (grooming) {
        for (int i = 0; i < kTangles; i++) manePoint(0.15f + i * 0.25f, gManeX_[i], gManeY_[i]);
        gDrawn_ = true;
    }
    if (!mother && ribbon_ && now_ < ribbonUntil_) {  // a ribbon in its mane, from grooming
        float px, py;
        manePoint(0.3f, px, py);
        drawBow(c, px, py - 4 * s, std::max(0.9f, s * 1.3f), ribbon_);
    }

    // The head: a round skull and a long, soft muzzle.
    const float r = 20 * s * hd;
    const float mxz = hx + dir * 18 * s * hd, myz = hy + 12 * s * hd;
    c.line(hx, hy, mxz, myz, 24 * s * hd, coat);
    c.fillCircle(hx, hy, r, coat);
    // Ears, the far one behind.
    c.fillTriangle(hx - dir * 12 * s * hd, hy - 12 * s * hd, hx - dir * 2 * s * hd, hy - 17 * s * hd,
                   hx - dir * 12 * s * hd, hy - 33 * s * hd, shade);
    c.fillTriangle(hx - dir * 6 * s * hd, hy - 15 * s * hd, hx + dir * 4 * s * hd, hy - 18 * s * hd,
                   hx - dir * 3 * s * hd, hy - 35 * s * hd, coat);
    c.fillTriangle(hx - dir * 3.5f * s * hd, hy - 18 * s * hd, hx + dir * 1.5f * s * hd, hy - 19 * s * hd,
                   hx - dir * 2.5f * s * hd, hy - 29 * s * hd, mix(0xE8A3A8, kNight, dark_ * 0.35f));
    c.fillEllipse(mxz, myz, 17 * s * hd, 12.5f * s * hd, muzzle);
    if (detail_) c.glow(hx - dir * 6 * s * hd, hy - 8 * s * hd, r * 0.9f, lit, 0.28f);
    if (socks)  // a white blaze down the face
        c.line(hx + dir * 1 * s * hd, hy - 14 * s * hd, mxz + dir * 2 * s * hd, myz - 6 * s * hd, 6.5f * s * hd, white);
    else if (points || spots)  // a little white star
        c.fillCircle(hx + dir * 3 * s * hd, hy - 10 * s * hd, 3.2f * s * hd, white);

    // Forelock over the forehead.
    c.fillCircle(hx - dir * 2 * s * hd, hy - 17 * s * hd, 6.5f * s * hd, maneCol(2));
    c.fillTriangle(hx - dir * 8 * s * hd, hy - 18 * s * hd, hx + dir * 4 * s * hd, hy - 20 * s * hd,
                   hx + dir * 7 * s * hd, hy - 5 * s * hd, maneCol(3));

    // The unicorn's horn, spiralled.
    if (unicorn) {
        const Rgb horn = golden ? 0xF2C14E : 0xF7EEDC, groove = golden ? 0xC8962E : 0xD9C9A8;
        const float bx0 = hx + dir * 4 * s * hd, by0 = hy - 19 * s * hd;
        const float tx = hx + dir * 13 * s * hd, ty = hy - 50 * s * hd;
        c.glow(tx, ty + 8 * s, 16 * s * hd, golden ? 0xFFE08A : 0xFFF4FF, 0.45f + 0.15f * std::sin(t_ * 3));
        c.fillTriangle(bx0 - 5 * s * hd, by0 + 1 * s, bx0 + 5 * s * hd, by0 - 1 * s, tx, ty, horn);
        for (int i = 1; i <= 3; i++) {
            const float u = i / 4.0f, cx0 = bx0 + (tx - bx0) * u, cy0 = by0 + (ty - by0) * u, half = 5 * s * hd * (1 - u);
            c.line(cx0 - half, cy0 + 2 * s * (1 - u), cx0 + half, cy0 - 2 * s * (1 - u), 1.4f * s, groove);
        }
    }

    // The eye: big, dark and shiny, with lashes. Closed, it's a soft curve.
    const float ex = hx + dir * 4 * s * hd + L.lookX * dir * 1.0f * s, ey = hy - 3 * s * hd + L.lookY * 1.2f * s;
    const float er = 6.8f * s * hd;
    if (L.blink >= 0.5f) {
        c.line(ex - er, ey, ex, ey + er * 0.45f, 2.2f * s, 0x2A1A14);
        c.line(ex, ey + er * 0.45f, ex + er, ey, 2.2f * s, 0x2A1A14);
    } else {
        c.fillEllipse(ex, ey, er, er * 1.08f, 0x1E120E);
        c.ring(ex, ey + er * 0.15f, er * 0.6f, er * 0.25f, golden ? 0x8A5A20 : 0x5A3420, 0.7f);
        c.fillCircle(ex + dir * er * 0.32f, ey - er * 0.35f, er * 0.34f, 0xFFFFFF, 0.95f);
        c.fillCircle(ex - dir * er * 0.3f, ey + er * 0.35f, er * 0.15f, 0xFFFFFF, 0.7f);
        for (int k = 0; k < 3; k++)  // lashes
            c.line(ex + dir * (er * 0.2f + k * er * 0.35f), ey - er * 0.95f, ex + dir * (er * 0.45f + k * er * 0.45f),
                   ey - er * 1.35f, 1.6f * s, 0x1E120E);
    }
    if (L.angry) c.line(ex - er * 1.1f, ey - er * 1.5f, ex + er * 1.1f, ey - er * 1.0f, 2.4f * s, 0x1E120E);
    if (detail_ || stage == Stage::Baby)  // rosy cheek
        c.glow(hx + dir * 8 * s * hd, hy + 8 * s * hd, 7 * s * hd, 0xF07A7A, 0.35f);

    // Nostril and mouth; or at Christmas, if you booped it on, a shiny red reindeer nose that glows in the dark.
    if (!mother && settings_.rudolph && holiday() == Holiday::Christmas) {
        const float nx = mxz + dir * 13 * s * hd, ny = myz - 1 * s * hd, r = std::max(6.0f, 9.0f * s * hd);
        const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(t_ * 3));
        c.glow(nx, ny, r * 4, 0xFF3A2A, 0.3f + 0.5f * dark_ + 0.15f * pulse);
        c.fillCircle(nx, ny, r, 0xD8261C);
        c.fillCircle(nx - dir * r * 0.15f, ny - r * 0.1f, r * 0.75f, 0xF0392B);
        c.fillCircle(nx - dir * r * 0.3f, ny - r * 0.35f, r * 0.28f, 0xFFD0C8);  // the shine
    } else {
        c.fillEllipse(mxz + dir * 9 * s * hd, myz - 2 * s * hd, 2.4f * s * hd, 1.8f * s * hd, mix(muzzle, 0x000000, 0.55f));
    }
    const float mmx = mxz + dir * 4 * s * hd, mmy = myz + 7 * s * hd;
    const Rgb lip = mix(muzzle, 0x000000, 0.5f);
    if (L.mouth >= 1.5f) {  // chewing
        const float open = std::fabs(std::sin(t_ * 14)) * 3 * s * hd;
        c.fillEllipse(mmx, mmy, 5 * s * hd, 1.5f * s * hd + open, lip);
    } else if (L.mouth > 0.5f) {
        c.line(mmx - dir * 7 * s * hd, mmy - 2 * s * hd, mmx, mmy + 1.5f * s * hd, 1.8f * s, lip);
        c.line(mmx, mmy + 1.5f * s * hd, mmx + dir * 5 * s * hd, mmy - 1 * s * hd, 1.8f * s, lip);
    } else if (L.mouth < -0.5f) {
        c.line(mmx - dir * 6 * s * hd, mmy + 1 * s * hd, mmx, mmy - 1.5f * s * hd, 1.8f * s, lip);
        c.line(mmx, mmy - 1.5f * s * hd, mmx + dir * 5 * s * hd, mmy + 1 * s * hd, 1.8f * s, lip);
    } else {
        c.line(mmx - dir * 6 * s * hd, mmy, mmx + dir * 5 * s * hd, mmy, 1.8f * s, lip);
    }

    // A unicorn sparkles now and then.
    if (unicorn && !compact_ && noise01(static_cast<uint32_t>(t_ * 4)) < 0.08f)
        spawn(ParticleKind::Sparkle, X((noise01(static_cast<uint32_t>(t_ * 7)) - 0.5f) * 120),
              Y(bodyY - 40 - 40 * noise01(static_cast<uint32_t>(t_ * 9))), 0, -20, 900);
}

void Scene::drawSquatch(Canvas& c, const Look& L) {
    const float s = L.s, x = L.x, f = L.foot, w = L.wide, b = L.breath;
    const Rgb dark = mix(L.fur, 0x1E140C, 0.45f);

    const Rgb lit = mix(L.fur, 0xFFF4E0, 0.22f), shade = mix(L.fur, 0x1E140C, 0.25f);
    if (detail_) {  // a soft shadow, darkest right under it
        c.fillEllipse(x, L.ground, 70 * s * w, 12 * s, 0x000000, 0.07f);
        c.fillEllipse(x, L.ground, 58 * s * w, 9.5f * s, 0x000000, 0.08f);
        c.fillEllipse(x, L.ground, 44 * s * w, 7 * s, 0x000000, 0.1f);
    } else {
        c.fillEllipse(x, L.ground, 60 * s * w, 10 * s, 0x000000, 0.18f);
    }
    c.fillEllipse(x - 26 * s * w, f - 9 * s, 24 * s, 11 * s, dark);
    c.fillEllipse(x + 26 * s * w, f - 9 * s, 24 * s, 11 * s, dark);
    if (detail_)
        for (float side : {-1.0f, 1.0f})
            for (int t = -1; t <= 1; t++)  // toes
                c.line(x + side * 26 * s * w + t * 8 * s, f - 3 * s, x + side * 26 * s * w + t * 9 * s, f - 1 * s,
                       2 * s, mix(dark, 0x000000, 0.3f), 0.6f);

    const float bodyY = f - 66 * s * b, bodyRx = 56 * s * w, bodyRy = 62 * s * b;
    if (detail_) {  // lit from the upper left: a darker rim low on the right, a soft highlight
        drawFringe(c, x, bodyY, bodyRx, bodyRy, L.fur, shade, s, 48, 0.0f, 2 * kPi);
        c.fillEllipse(x, bodyY, bodyRx, bodyRy, shade);
        c.fillEllipse(x - bodyRx * 0.06f, bodyY - bodyRy * 0.06f, bodyRx * 0.93f, bodyRy * 0.93f, L.fur);
        c.glow(x - bodyRx * 0.35f, bodyY - bodyRy * 0.4f, bodyRx * 0.75f, lit, 0.35f);
        drawFur(c, x, bodyY, bodyRx, bodyRy, L.fur, s, 150, 101, false);
    } else {
        c.fillEllipse(x, bodyY, bodyRx, bodyRy, L.fur);
    }
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
    if (detail_) {
        c.glow(x - 8 * s, f - 70 * s, 26 * s, mix(L.face, 0xFFFFFF, 0.3f), 0.3f);
        drawFur(c, x, f - 56 * s, 30 * s * w, 36 * s * b, L.face, s * 0.7f, 40, 202, false);
    }
    if (L.markings == 1) c.fillEllipse(x, f - 64 * s, 16 * s * w, 24 * s * b, L.mark);  // chest blaze
    if (L.markings == 3) {  // speckles on the shoulders
        const float dots[][2] = {{-38, -92}, {-30, -76}, {-44, -70}, {36, -94}, {28, -80}, {42, -72}, {-20, -104}};
        for (const auto& d : dots) c.fillCircle(x + d[0] * s * w, f + d[1] * s * b, 3.4f * s, L.mark);
    }

    auto arm = [&](float side, float raise) {
        float sx = x + side * 44 * s * w, sy = f - 102 * s * b;
        float hx = x + side * (60 * s * w + raise * 16 * s);
        float hy = (f - 46 * s) + raise * (-122 * s);
        if (detail_) {
            c.line(sx, sy, hx, hy, 23 * s, shade);
            c.line(sx - 2 * s, sy - 1 * s, hx - 2 * s, hy - 1 * s, 18 * s, L.fur);
            c.line(sx - 4 * s, sy - 2 * s, hx - 4 * s, hy - 2 * s, 6 * s, lit, 0.35f);
        } else {
            c.line(sx, sy, hx, hy, 22 * s, L.fur);
        }
        c.fillCircle(hx, hy, 12 * s, dark);
        if (detail_) c.fillCircle(hx - 3 * s, hy - 3 * s, 5 * s, mix(dark, 0xFFFFFF, 0.15f), 0.5f);
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
    if (detail_) {
        drawFringe(c, x, hy, hr, hr, L.fur, shade, s, 30, -0.8f * kPi, 0.8f * kPi);
        c.fillCircle(x, hy, hr, shade);
        c.fillCircle(x - hr * 0.05f, hy - hr * 0.05f, hr * 0.94f, L.fur);
        c.glow(x - hr * 0.4f, hy - hr * 0.45f, hr * 0.8f, lit, 0.35f);
        drawFur(c, x, hy, hr, hr, L.fur, s, 70, 303, true);
    } else {
        c.fillCircle(x, hy, hr, L.fur);
    }
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
    if (detail_) {
        c.fillEllipse(fx, hy + hr * 0.5f, hr * 0.5f, hr * 0.28f, mix(L.face, 0x000000, 0.08f), 0.6f);  // muzzle
        c.glow(fx - hr * 0.2f, hy + hr * 0.02f, hr * 0.4f, 0xFFFFFF, 0.18f);
        for (float side : {-1.0f, 1.0f})  // rosy cheeks
            c.glow(fx + side * hr * 0.46f, hy + hr * 0.36f, hr * 0.2f, 0xF07A7A, 0.4f);
    }
    if (L.markings == 2) c.fillEllipse(fx, hy + hr * 0.07f, hr * 0.56f, hr * 0.22f, L.mark);  // eye mask

    if (L.angry) {
        c.line(fx - hr * 0.52f, hy - hr * 0.24f, fx - hr * 0.1f, hy - hr * 0.06f, hr * 0.13f, dark);
        c.line(fx + hr * 0.52f, hy - hr * 0.24f, fx + hr * 0.1f, hy - hr * 0.06f, hr * 0.13f, dark);
    } else {
        c.line(fx - hr * 0.46f, hy - hr * 0.14f, fx + hr * 0.46f, hy - hr * 0.14f, hr * 0.14f, dark);
    }

    const float er = hr * 0.13f, ey0 = hy + hr * 0.07f;
    for (float side : {-1.0f, 1.0f}) {
        float ex = fx + side * hr * 0.28f + L.lookX * hr * 0.05f;
        const float ey = ey0 + L.lookY * hr * 0.04f;
        if (L.blink > 0.8f) {
            c.line(ex - er, ey, ex + er, ey, std::max(1.5f, er * 0.45f), kEye);
        } else {
            c.fillEllipse(ex, ey, er, er * (1 - L.blink * 0.8f), kEye);
            if (detail_ && L.blink < 0.3f) {  // a warm brown iris and a second glint
                c.ring(ex, ey + er * 0.1f, er * 0.62f, er * 0.28f, 0x6B3F1F, 0.75f);
                c.fillCircle(ex - er * 0.38f, ey + er * 0.4f, er * 0.16f, 0xFFFFFF, 0.7f);
            }
            c.fillCircle(ex + er * 0.35f, ey - er * 0.35f, er * 0.35f, 0xFFFFFF, 0.9f);
        }
    }

    c.fillEllipse(fx, hy + hr * 0.33f, hr * 0.15f, hr * 0.1f, dark);
    if (detail_) c.fillEllipse(fx - hr * 0.04f, hy + hr * 0.3f, hr * 0.06f, hr * 0.035f, 0xFFFFFF, 0.45f);  // a shiny nose
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

// Short strokes of lighter and darker fur over an ellipse. Radial strokes point out from the
// middle (a head); the rest hang down and a little outward (a body).
void Scene::drawFur(Canvas& c, float cx, float cy, float rx, float ry, Rgb fur, float s, int n, uint32_t seed,
                    bool radial) {
    const Rgb lite = mix(fur, 0xFFF4E0, 0.28f), deep = mix(fur, 0x120C06, 0.3f);
    const float len = 7 * s, w = std::max(0.9f, 1.4f * s);
    for (int i = 0; i < n; i++) {
        const uint32_t k3 = seed * 7919u + static_cast<uint32_t>(i) * 3u;
        const float u = noise01(k3), v = noise01(k3 + 1), k = noise01(k3 + 2);
        const float r = std::sqrt(u) * 0.88f, a = v * 2 * kPi;
        const float ox = std::cos(a) * r, oy = std::sin(a) * r;
        const float px = cx + ox * rx, py = cy + oy * ry;
        float dx = radial ? ox : ox * 0.45f, dy = radial ? oy - 0.2f : 1.0f;
        float dl = std::hypot(dx, dy);
        if (dl < 1e-3f) {
            dx = 0;
            dy = 1;
            dl = 1;
        }
        dx /= dl;
        dy /= dl;
        const float l = len * (0.6f + 0.7f * k);
        // Light strokes mostly on the upper left, dark ones on the lower right.
        const bool light = (k + 0.35f * (-ox - oy)) > 0.5f;
        c.line(px, py, px + dx * l, py + dy * l, w, light ? lite : deep, 0.3f + 0.3f * k);
    }
}

// Tufts sticking out round the edge of an ellipse, between two angles (0 is straight up,
// clockwise), so the outline looks furry rather than cut out.
void Scene::drawFringe(Canvas& c, float cx, float cy, float rx, float ry, Rgb lit, Rgb shade, float s, int n,
                       float from, float to) {
    for (int j = 0; j < n; j++) {
        const float a = from + (to - from) * (j + 0.5f) / static_cast<float>(n);
        const float px = cx + rx * std::sin(a), py = cy - ry * std::cos(a);
        float nx = std::sin(a) / rx, ny = -std::cos(a) / ry;
        const float nl = std::hypot(nx, ny);
        nx /= nl;
        ny /= nl;
        const float r1 = noise01(static_cast<uint32_t>(j) * 31u + 7u), r2 = noise01(static_cast<uint32_t>(j) * 31u + 8u);
        const float len = (3 + 3 * r1) * s, w = 2.6f * s;
        const Rgb col = mix(lit, shade, std::max(0.0f, 0.35f + 0.45f * (nx + ny) * 0.7071f));
        // Fur hangs: each wisp bends down a little, more on the sides than on top.
        const float dx = nx, dy = ny + 0.45f * std::fabs(nx);
        const float bx = px - nx * 2 * s, by = py - ny * 2 * s, sp = (r2 - 0.5f) * 3 * s;
        c.line(bx - ny * sp, by + nx * sp, bx - ny * sp + dx * len, by + nx * sp + dy * len, w, col);
    }
}

void Scene::drawGroundDetail(Canvas& c) {
    // Far hills, a misty ridge of trees, then the nearer tree line with a shady side.
    c.fillEllipse(270, kHorizon + 8, 300, 46, sh(0x7C9DA8));
    c.fillEllipse(110, kHorizon + 14, 210, 62, sh(0x5B7F8F));
    c.fillEllipse(390, kHorizon + 18, 230, 72, sh(0x4F7584));
    const Rgb mist = sh(mix(kBackTrees, 0x8FB0B8, 0.45f));
    for (int i = 0; i < 24; i++) {
        const float x = i * 21.0f + noise01(i + 900) * 10 - 6, hgt = 26 + noise01(i + 950) * 22;
        c.fillTriangle(x - 11, kHorizon + 8, x + 11, kHorizon + 8, x, kHorizon + 8 - hgt, mist);
    }
    const Rgb back = sh(kBackTrees), backDark = sh(mix(kBackTrees, 0x0A1A16, 0.35f));
    for (int i = 0; i < 16; i++) {
        const float x = static_cast<float>(i * 32 + (i % 3) * 7 - 10);
        const float hgt = 46 + static_cast<float>((i * 37) % 28), b = kHorizon + 12;
        c.fillTriangle(x - 18, b, x + 18, b, x, b - hgt, back);
        c.fillTriangle(x, b, x + 18, b, x, b - hgt, backDark);
    }

    // Grass, lighter toward the horizon and deeper near the bottom.
    c.fillRect(0, kHorizon, S, S - kHorizon, sh(kGrass));
    for (int b = 0; b < 16; b++) {  // thin bands, so the fades have no edges
        c.fillRect(0, kHorizon + b * 3.0f, S, 3, sh(0x93C070), 0.4f * (1 - b / 16.0f));
        c.fillRect(0, S - 60 + b * 3.75f, S, 3.75f, sh(0x2E5A2A), 0.22f * b / 16.0f);
    }

    // The clearing, with a soft edge, a sunny middle and patches of bare earth.
    for (int r = 0; r < 4; r++)
        c.fillEllipse(240, 372, 252 + (4 - r) * 4.0f, 122 + (4 - r) * 2.0f, sh(species_ == Species::Horse ? kPaddock : kClearing), 0.25f);
    c.fillEllipse(240, 372, 252, 122, sh(species_ == Species::Horse ? kPaddock : kClearing));
    for (int r = 0; r < 6; r++)  // a sunny middle, fading out
        c.fillEllipse(234, 356, 200 - r * 24.0f, 84 - r * 10.0f, sh(0xC4D88A), 0.09f);
    const float dirt[][4] = {{150, 398, 44, 11}, {300, 422, 36, 9}, {262, 334, 28, 6}};
    for (const auto& d : dirt)
        for (int r = 0; r < 3; r++) c.fillEllipse(d[0], d[1], d[2] * (1 - r * 0.25f), d[3] * (1 - r * 0.25f), sh(0xB89A68), 0.12f);

    // Blades of grass, bigger the nearer they are, sparse on the clearing.
    const Rgb greens[] = {sh(0x4C8A3A), sh(0x6FAF4E), sh(0x3E7A32), sh(0x83BD5C)};
    for (int i = 0; i < 520; i++) {
        const float y = kHorizon + 6 + noise01(i * 5 + 1) * (S - kHorizon - 4);
        const float x = noise01(i * 5 + 2) * S;
        const float ex = (x - 240) / 252, ey = (y - 372) / 122;
        const bool onClearing = ex * ex + ey * ey < 0.85f;
        if (onClearing && noise01(i * 5 + 3) < 0.8f) continue;
        const float near = (y - kHorizon) / (S - kHorizon);
        const float len = 3 + 10 * near, lean = (noise01(i * 5 + 4) - 0.5f) * 5 * (0.4f + near);
        const Rgb col = greens[i % 4];
        c.line(x, y, x + lean, y - len, 0.8f + 1.1f * near, col, 0.85f);
        if (near > 0.4f) c.line(x + 2, y, x + 2 + lean * 1.6f, y - len * 0.75f, 0.8f + near, col, 0.75f);
    }

    // A few flowers in the long grass, and pebbles on the clearing.
    const Rgb petals[] = {0xFFFFFF, 0xF6D65A, 0xB78AD8, 0xF4A1B8};
    for (int i = 0; i < 18; i++) {
        const float y = kHorizon + 24 + noise01(i + 400) * 250, x = noise01(i + 450) * S;
        const float ex = (x - 240) / 252, ey = (y - 372) / 122;
        if (ex * ex + ey * ey < 1.1f) continue;
        const float r = 1.6f + 1.8f * (y - kHorizon) / (S - kHorizon);
        c.fillCircle(x, y, r, sh(petals[i % 4]), 0.9f);
        c.fillCircle(x, y, r * 0.4f, sh(0xE0A030));
    }
    for (int i = 0; i < 12; i++) {
        const float a = noise01(i + 600) * 2 * kPi, r = 0.35f + 0.5f * noise01(i + 650);
        const float x = 240 + std::cos(a) * 252 * r, y = 372 + std::sin(a) * 122 * r;
        const float sz = 2.5f + 2.5f * noise01(i + 700);
        c.fillEllipse(x + 1, y + sz * 0.5f, sz * 1.3f, sz * 0.5f, 0x000000, 0.15f);
        c.fillEllipse(x, y, sz * 1.2f, sz * 0.75f, sh(0x9A968A));
        c.fillEllipse(x - sz * 0.3f, y - sz * 0.25f, sz * 0.6f, sz * 0.3f, sh(0xD0CCC0));
    }
}

void Scene::drawTreeDetail(Canvas& c, float x, float base, float size) {
    const Rgb trunk = sh(kTrunk), trunkDark = sh(mix(kTrunk, 0x000000, 0.35f));
    c.fillEllipse(x + 26 * size, base + 2, 62 * size, 11 * size, 0x000000, 0.14f);  // shadow, away from the sun
    c.fillRoundRect(x - 14 * size, base - 150 * size, 28 * size, 150 * size, 6, trunk);
    c.line(x + 8 * size, base - 146 * size, x + 8 * size, base - 3, 9 * size, trunkDark, 0.8f);
    for (int j = 0; j < 6; j++) {  // bark
        const float bx = x + (-10 + j * 4.2f) * size;
        const float y0 = base - (20 + (j * 37) % 70) * size, y1 = y0 - (30 + (j * 23) % 40) * size;
        c.line(bx, y0, bx + 1, y1, 1.5f * size, trunkDark, 0.6f);
    }
    c.fillTriangle(x - 24 * size, base + 2, x - 8 * size, base + 2, x - 10 * size, base - 14 * size, trunk);  // roots
    c.fillTriangle(x + 24 * size, base + 2, x + 8 * size, base + 2, x + 10 * size, base - 14 * size, trunkDark);
    c.fillEllipse(x, base, 30 * size, 6 * size, sh(0x3F5E2E), 0.6f);

    const float sway = treeSway(x), x0 = x;
    for (int i = 0; i < 4; i++) {
        x = x0 + sway * (i + 1) / 4;  // the top moves most
        const float y = base - 90 * size - i * 45 * size, hw = (78 - i * 14) * size, top = y - 85 * size;
        const Rgb leaf = sh(i % 2 ? kLeaves2 : kLeaves1);
        const Rgb deep = sh(mix(i % 2 ? kLeaves2 : kLeaves1, 0x0C2416, 0.4f));
        const Rgb light = sh(mix(i % 2 ? kLeaves2 : kLeaves1, 0xC8E890, 0.25f));
        // The whole tier in shadow, with drooping bough tips along the bottom.
        c.fillTriangle(x - hw, y, x + hw, y, x, top, deep);
        const int tips = 6;
        for (int k = 0; k < tips; k++) {
            const float bx = x - hw + (k + 0.5f) * (2 * hw / tips), half = hw / tips * 1.1f;
            c.fillTriangle(bx - half, y - 5 * size, bx + half, y - 5 * size, bx + half * 0.3f, y + 7 * size,
                           k < tips / 2 ? leaf : deep);
        }
        // The sunny left side, and a brighter edge on it.
        c.fillTriangle(x - hw * 0.94f, y - 2 * size, x + hw * 0.1f, y - 2 * size, x - hw * 0.02f, top + 4 * size, leaf);
        c.fillTriangle(x - hw * 0.8f, y - 8 * size, x - hw * 0.35f, y - 8 * size, x - hw * 0.08f, top + 16 * size,
                       light, 0.55f);
        // Needles: little strokes hanging off the boughs.
        for (int k = 0; k < 10; k++) {
            const float u = noise01(i * 40 + k + static_cast<uint32_t>(x0)), v = noise01(i * 40 + k + 500);
            const float py = top + (0.35f + 0.6f * v) * (y - top);
            const float span = hw * (py - top) / (y - top);
            const float px = x + (u * 2 - 1) * span * 0.85f;
            c.line(px, py, px + (px < x ? -3.0f : 3.0f) * size, py + 6 * size, 1.4f * size, px < x ? light : deep, 0.6f);
        }
        if (weather_.sky == Sky::Snow) {  // a cap of snow on each tier
            c.fillTriangle(x - hw * 0.42f, y - 50 * size, x + hw * 0.42f, y - 50 * size, x, top, sh(0xEEF3F8));
            c.fillTriangle(x, y - 50 * size, x + hw * 0.42f, y - 50 * size, x, top, sh(0xC8D4E2));
        }
    }
}

void Scene::drawHidingHead(Canvas& c) {
    if (react_ == React::Flinch) return;  // ducked out of sight
    if (species_ == Species::Horse) {  // a shy foal standing behind the trunk, looking out at you
        drawHidingHorse(c);
        return;
    }
    Rgb fur, face, mark;
    furColours(fur, face, mark);
    Rgb shadow = sh(mix(fur, 0x000000, 0.45f));  // in the tree's shade
    c.fillCircle(kHideX, kHideY, 24, shadow);
    c.fillEllipse(kHideX, kHideY - 22, 10, 7, shadow);
}

// The whole foal, half hidden: it stands just behind the apple tree (which is drawn over it),
// rump out one side and head out the other, in the tree's shade.
void Scene::drawHidingHorse(Canvas& c) {
    const State& s = sim_.state();
    Look L{};
    L.x = kHideHorseX + (squatchScale() - 0.55f) * 60;  // bigger ones stand further over, rump still on screen
    L.foot = L.ground = kLeftTreeBase - 8;  // a step behind the trunk
    L.s = squatchScale();
    L.head = headRatio(s.stage);
    L.wide = sim_.traits().build;
    L.blink = (s.asleep || now_ < blinkUntil_) ? 1.0f : 0.0f;
    L.lookX = 0.5f + lookX_ * 0.5f;  // peering out toward you
    L.lookY = lookY_;
    L.breath = 1 + 0.018f * static_cast<float>(std::sin(t_ * (s.asleep ? 1.6 : 2.6)));
    L.mouth = 0;
    const float saved = faceDir_;
    faceDir_ = 1;  // head out on the open side
    const float base = s.lightsOn ? 0.0f : 0.5f;
    c.setShade(0x02040C, base + (1 - base) * 0.25f);  // in the tree's shade
    drawHorse(c, L);
    c.setShade(0x02040C, base);
    faceDir_ = saved;
}

// A horse's head looking round something, at (x, y). Shaded: in the tree's shadow, with its
// eye drawn separately (drawHidingEyes); otherwise with the eye, for the Hide and Seek reveal.
void Scene::drawHorsePeek(Canvas& c, float x, float y, bool shaded) {
    Rgb coat, mane, muzzle;
    bool unicorn, golden;
    horseColours(coat, mane, muzzle, unicorn, golden);
    const float dim = shaded ? 0.3f : 0.0f;
    const Rgb shade = sh(mix(coat, 0x000000, dim)), mshade = sh(mix(muzzle, 0x000000, dim));
    c.fillTriangle(x - 10, y - 12, x + 2, y - 17, x - 7, y - 34, shade);  // ears
    c.fillTriangle(x - 2, y - 15, x + 9, y - 17, x + 3, y - 35, shade);
    c.line(x, y, x + 18, y + 11, 22, shade);
    c.fillCircle(x, y, 19, shade);
    c.fillEllipse(x + 19, y + 11, 15, 11, mshade);
    c.fillEllipse(x + 27, y + 9, 2.2f, 1.6f, sh(0x2A1A14));  // nostril
    c.fillCircle(x - 2, y - 16, 6, sh(mix(mane, 0x000000, dim)));  // forelock
    if (unicorn) {
        const Rgb horn = golden ? 0xF2C14E : 0xF7EEDC;
        c.fillTriangle(x + 1, y - 18, x + 9, y - 20, x + 11, y - 46, sh(horn));
    }
    if (!shaded) {  // found: a happy eye, lashes and all
        const float ex = x + 4, ey = y - 3;
        c.fillEllipse(ex, ey, 6.5f, 7, 0x1E120E);
        c.fillCircle(ex + 2, ey - 2.5f, 2.3f, 0xF3EED2);
        for (int k = 0; k < 3; k++) c.line(ex + 1 + k * 2.5f, ey - 6.5f, ex + 3 + k * 3.0f, ey - 9.5f, 1.4f, 0x1E120E);
    }
}

void Scene::drawHidingEyes(Canvas& c) {
    if (react_ == React::Flinch) return;
    const State& s = sim_.state();
    bool closed = s.asleep || now_ < blinkUntil_;
    if (species_ == Species::Horse) return;  // the hiding foal is drawn whole, eyes and all
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
            case ParticleKind::Leaf: {
                const Sky sky = weather_.sky;
                const Rgb col = sky == Sky::Snow ? 0xF2F6FA : (autumn() ? 0xD9822B : 0x3E7A3A);
                c.fillEllipse(p.x, p.y, 4, 2, sh(col), std::min(1.0f, a * 2));
                break;
            }
            case ParticleKind::Ember: c.fillCircle(p.x, p.y, 2, 0xFFB040, a); break;
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
    const int ls = ts(2);
    char wrapped[3][64];
    const int n = wrapText(t, ls, 440, wrapped, 3);
    float tw = 0;
    for (int i = 0; i < n; i++) tw = std::max(tw, static_cast<float>(Canvas::textWidth(wrapped[i], ls)));
    const float w = tw + 28, h = 18 + Canvas::textHeight(ls) + (n - 1) * lineStep(ls);
    c.fillRoundRect(240 - w / 2, 50, w, h, 16, kUiDark, 0.8f);
    for (int i = 0; i < n; i++) c.text(240, 59 + i * lineStep(ls), wrapped[i], ls, kUiLight, Align::Center);
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
        const bool horse = species_ == Species::Horse;
        const char* label = toString(slot);
        if (horse && slot == Slot::Berries) label = "APPLE";
        if (slot == Slot::Stable) label = sim_.state().boarding ? "HOME" : horse ? "STABLE" : "SITTER";
        if (horse && slot == Slot::Fish) label = "CARROT";
        if (horse && slot == Slot::Roots) label = "HAY";
        c.text(cx, top + 94, label, 2, kUiLight, Align::Center);
    }
}

// ---- the boarding stable

Scene::Rect Scene::homeArea() const { return {S / 2 - 110, bigText_ ? 300 : 262, 220, 64}; }

void Scene::toggleBoarding() {
    const State& s = sim_.state();
    const bool horse = species_ == Species::Horse;
    if (s.boarding) {
        if (sim_.board(false) != Result::Ok) return;
        saveRequested_ = true;
        trayTarget_ = 0;
        page_ = Page::Main;
        held_ = Item::None;
        cue(Cue::Returned);  // the horse whinnies hello
        say("HOME AGAIN!", true);
        speak("I MISSED YOU!");
        return;
    }
    if (sim_.board(true) != Result::Ok) {
        sayNotNow();
        return;
    }
    saveRequested_ = true;
    trayTarget_ = 0;
    page_ = Page::Main;
    held_ = Item::None;
    fetch_ = Fetch::None;
    cue(Cue::Tick);
    say(horse ? "OFF TO THE STABLE. SEE YOU SOON!" : "THE SITTER'S HERE. SEE YOU SOON!", true);
}

void Scene::drawBoarding(Canvas& c) {
    const bool horse = species_ == Species::Horse;
    const char* name = sim_.state().name[0] ? sim_.state().name : (horse ? "YOUR PONY" : "YOUR BIGFOOT");
    char line[48];
    std::snprintf(line, sizeof line, horse ? "%s IS AT THE STABLE" : "%s IS WITH THE SITTER", name);
    Rect b = homeArea();
    if (bigText_) {
        c.fillRoundRect(40, 120, 400, 264, 18, kUiDark, 0.82f);
        float y = para(c, 240, 138, line, 2, kUiLight, 380);
        y = para(c, 240, y + 8, "ALL LOOKED AFTER.", 2, 0xCFC7B5, 380);
        para(c, 240, y, "IT WILL WAIT FOR YOU.", 2, 0xCFC7B5, 380);
        c.fillRoundRect(b.x, b.y, b.w, b.h, 14, kGold);
        label(c, b, "BRING HOME", 2, kUiDark);
        return;
    }
    int scale = Canvas::textWidth(line, 2) <= 380 ? 2 : 1;
    c.fillRoundRect(40, 150, 400, 190, 18, kUiDark, 0.82f);
    c.text(240, 172, line, scale, kUiLight, Align::Center);
    c.text(240, 202, "ALL LOOKED AFTER.", 2, 0xCFC7B5, Align::Center);
    c.text(240, 228, "IT WILL WAIT FOR YOU.", 2, 0xCFC7B5, Align::Center);
    c.fillRoundRect(b.x, b.y, b.w, b.h, 14, kGold);
    c.text(240, b.y + b.h / 2.0f - Canvas::textHeight(2) / 2.0f, "BRING HOME", 2, kUiDark, Align::Center);
}

void Scene::drawForagingBanner(Canvas& c) {
    const State& s = sim_.state();
    char name[24], buf[64];
    landmarkName(s.forageHash, name, sizeof name);
    int mins = static_cast<int>((sim_.forageSecsLeft() + 59) / 60);
    std::snprintf(buf, sizeof buf, "%s - %d MIN", name, std::max(1, mins));
    int scale = Canvas::textWidth(buf, ts(2)) <= 440 ? ts(2) : Canvas::textWidth(buf, 2) <= 440 ? 2 : 1;
    float w = static_cast<float>(Canvas::textWidth(buf, scale)) + 24, h = scale >= 3 ? 34.0f : 26.0f;
    c.fillRoundRect(240 - w / 2, 88, w, h, h / 2, kUiDark, 0.7f);
    c.text(240, 88 + h / 2 - Canvas::textHeight(scale) / 2.0f, buf, scale, kUiLight, Align::Center);
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
    c.text(240, bigText_ ? kMapY + 8.0f : kMapY + 10.0f, "TRAIL MAP", ts(2), ink, Align::Center);

    for (int i = 0; i < s.trailCount; i++) {
        const Trail& tr = s.trails[i];
        Rect r = trailRowArea(i);
        bool out = s.foraging && s.forageHash == tr.hash;
        c.fillRoundRect(r.x, r.y + 3, r.w, r.h - 6, 12, out ? 0xF2D98A : 0xE6DCC6);
        drawLandmarkIcon(c, landmarkKind(tr.hash), r.x + 34.0f, r.cy(), 0.95f);
        char name[24];
        landmarkName(tr.hash, name, sizeof name);
        char sub[32];
        std::snprintf(sub, sizeof sub, "%s%s", toString(trailDistance(tr.rssi)), out ? (bigText_ ? " - HERE NOW" : "  - IT'S HERE NOW") : "");
        const bool isNew = !sim_.trailVisited(tr.hash);  // a place it's never been
        if (bigText_) {
            const float room = r.w - 68.0f - (isNew ? 82.0f : 12.0f);
            c.text(r.x + 68, r.y + 11, name, Canvas::textWidth(name, 3) <= room ? 3 : 2, ink);
            c.text(r.x + 68, r.y + 37, sub, 2, faint);
        } else {
            c.text(r.x + 68, r.y + 14, name, 2, ink);
            c.text(r.x + 68, r.y + 36, sub, 2, faint);
        }
        if (isNew) {
            const float bw = bigText_ ? 70.0f : 52.0f, bh = bigText_ ? 32.0f : 24.0f;
            c.fillRoundRect(r.x + r.w - 12 - bw, r.cy() - bh / 2, bw, bh, bh / 2, kGold);
            c.text(r.x + r.w - 12 - bw / 2, r.cy() - Canvas::textHeight(ts(2)) / 2.0f, "NEW", ts(2), ink, Align::Center);
        }
    }
    if (s.trailCount == 0) {
        c.text(240, kRowY + 60, scanning_ ? "LISTENING..." : "NO TRAILS YET", ts(2), faint, Align::Center);
        para(c, 240, bigText_ ? kRowY + 96 : kRowY + 90, "NEARBY WI-FI BECOMES PLACES TO EXPLORE", 1, faint, bigText_ ? 300 : 440);
    }

    if (trailNote_[0]) {  // under the title, above the rows
        int scale = Canvas::textWidth(trailNote_, 2) <= kMapW - 24 ? 2 : 1;
        c.text(240, bigText_ ? kMapY + 34.0f : kMapY + 30.0f, trailNote_, scale, 0x5E8A3C, Align::Center);
    }
    Rect b = rescanArea();
    c.fillRoundRect(b.x, b.y + 4, b.w, b.h - 8, 14, scanning_ ? 0xB8AE9A : kButton);
    label(c, {b.x, b.y + 4, b.w, b.h - 8}, scanning_ ? "LISTENING..." : "LOOK AGAIN", 2, kUiLight);
}

// ================================================================ games

namespace {
constexpr float kSeekSpotXY[Scene::kSeekSpots][2] = {{48, 290}, {160, 318}, {300, 318}, {388, 345}, {432, 262}};
constexpr const char* kGameNames[Scene::kGameChoices] = {"HIDE AND SEEK", "BERRY CATCH", "BLURRY PHOTO", "QUICK CATCH"};
constexpr const char* kGameHints[Scene::kGameChoices] = {"FIND WHERE IT HID: THREE GUESSES", "CATCH 10 FALLING BERRIES",
                                                         "SNAP IT AS IT RUNS PAST", "NO RUSHING: A GAME OF CATCH"};
constexpr uint32_t kBerryRoundMs = 25000, kBerryEveryMs = 900, kRunMs = 2000;
// The horse edition's picker: which game each row starts.
constexpr const char* kHorseGameNames[5] = {"HIDE AND SEEK", "APPLE CATCH", "GROOMING", "BLURRY PHOTO", "QUICK CATCH"};
constexpr const char* kHorseGameHints[5] = {"FIND WHERE IT HID: THREE GUESSES", "CATCH 10 FALLING APPLES",
                                            "BRUSH, HOOVES, MANE AND A RIBBON", "SNAP IT AS IT GALLOPS PAST",
                                            "NO RUSHING: A GAME OF CATCH"};
// Shorter, for big text.
constexpr const char* kHorseGameHintsShort[5] = {"THREE GUESSES TO FIND IT", "CATCH 10 FALLING APPLES",
                                                 "BRUSH, HOOVES, MANE, RIBBON", "SNAP IT GALLOPING PAST", "NO RUSHING, JUST CATCH"};
constexpr const char* kGameHintsShort[Scene::kGameChoices] = {"THREE GUESSES TO FIND IT", "CATCH 10 FALLING BERRIES",
                                                              "SNAP IT AS IT RUNS PAST", "NO RUSHING, JUST CATCH"};
// Grooming: the horse stands big in the middle; the hoof close-up and ribbon box sit on a card.
constexpr float kHoofX = 240, kHoofY = 268;  // the hoof close-up's centre
constexpr float kPebbleXY[4][Scene::kPebbles][2] = {{{-34, -18}, {30, 22}}, {{28, -26}, {-30, 20}},
                                                    {{-30, 24}, {34, -12}}, {{0, -34}, {-36, 4}}};
constexpr Rgb kRibbonColours[Scene::kRibbons] = {0xD8322A, 0x2E6FB0, 0xE86AA8, 0x8E5CC8};
constexpr const char* kHoofNames[4] = {"FRONT HOOF", "OTHER FRONT HOOF", "BACK HOOF", "LAST HOOF!"};
constexpr float kBasketY = 420, kFrameX0 = 130, kFrameX1 = 350;
}  // namespace

Scene::Rect Scene::gameChoiceArea(int i) const {
    if (species_ == Species::Horse) return {56, 100 + i * 64, 368, 64};  // five rows
    return {56, 112 + i * 74, 368, 68};
}
Scene::Rect Scene::gameQuitArea() const { return {S - 112, 0, 112, 64}; }
Scene::Rect Scene::seekSpotArea(int i) const {
    return {static_cast<int>(kSeekSpotXY[i][0]) - 36, static_cast<int>(kSeekSpotXY[i][1]) - 36, 72, 72};
}
Scene::Rect Scene::snapArea() const { return {S / 2 - 100, 392, 200, 80}; }
bool Scene::petInFrame() const {
    return game_ == Game::Photo && running_ && gameX_ > kFrameX0 + 10 && gameX_ < kFrameX1 - 10;
}

void Scene::openGames() {
    const State& s = sim_.state();
    if (!hatched() || s.asleep) {
        sayNotNow();
        return;
    }
    if (hiding()) {
        say("IT'S TOO SHY TO PLAY YET", true);
        return;
    }
    trayTarget_ = 0;
    held_ = Item::None;
    page_ = Page::Main;
    overlay_ = Overlay::Game;
    game_ = Game::Pick;
}

void Scene::startGame(Game g) {
    game_ = g;
    gameOver_ = gameWon_ = false;
    score_ = 0;
    gameMsg_[0] = '\0';
    petOverride_ = false;
    running_ = false;
    switch (g) {
        case Game::Seek:
            seekSpot_ = static_cast<int>(rand01() * kSeekSpots) % kSeekSpots;
            seekWrong_ = 0;
            tries_ = 3;
            nextEvent_ = now_ + 2500;
            break;
        case Game::Berry:
            gameUntil_ = now_ + kBerryRoundMs;
            nextEvent_ = now_ + 600;
            basketX_ = 240;
            for (FallingBerry& b : berries_) b.live = false;
            petOverride_ = true;  // it watches from beside the tree
            gameX_ = 104;
            break;
        case Game::Photo:
            runsLeft_ = 3;
            nextEvent_ = now_ + 1500;
            petOverride_ = true;
            gameX_ = -100;
            break;
        case Game::Groom:
            petOverride_ = true;
            gameX_ = kGroomX;
            groomStep_ = GroomStep::Brush;
            for (float& d : groomDirt_) d = 1;
            hoof_ = 0;
            pebbles_ = (1u << kPebbles) - 1;
            rubX_ = rubY_ = -1;
            gDrawn_ = false;
            break;
        default: break;
    }
}

// ---- Grooming

bool Scene::groomSpot(int i, float& x, float& y) const {
    if (!gDrawn_ || game_ != Game::Groom) return false;
    if (groomStep_ == GroomStep::Brush) {
        if (i < 0 || i >= kMudSpots || groomDirt_[i] <= 0) return false;
        static const float kUV[kMudSpots - 1][2] = {{-0.6f, -0.25f}, {-0.15f, 0.3f}, {0.35f, -0.3f}, {0.6f, 0.2f}, {-0.4f, 0.45f}};
        if (i == kMudSpots - 1) {  // one on a leg
            x = gLegX_;
            y = gLegY_;
        } else {
            x = gBodyX_ + kUV[i][0] * gBodyRx_ * 0.8f;
            y = gBodyY_ + kUV[i][1] * gBodyRy_ * 0.8f;
        }
        return true;
    }
    if (groomStep_ == GroomStep::Mane) {
        if (i < 0 || i >= kTangles || groomDirt_[i] <= 0) return false;
        x = gManeX_[i];
        y = gManeY_[i];
        return true;
    }
    return false;
}

Scene::Rect Scene::pebbleArea(int i) const {
    const int h = hoof_ < 4 ? hoof_ : 0;
    return {static_cast<int>(kHoofX + kPebbleXY[h][i][0]) - 32, static_cast<int>(kHoofY + kPebbleXY[h][i][1]) - 32, 64, 64};
}

Scene::Rect Scene::ribbonArea(int i) const { return {64 + i * 92, 240, 84, 84}; }

void Scene::groomRub(float x, float y, float amount) {
    if (amount <= 0) return;
    const float reach = std::max(30.0f, 26 * gScale_);
    const int n = groomStep_ == GroomStep::Brush ? kMudSpots : kTangles;
    bool any = false;
    for (int i = 0; i < n; i++) {
        float sx, sy;
        if (!groomSpot(i, sx, sy) || std::hypot(x - sx, y - sy) > reach) continue;
        groomDirt_[i] = std::max(0.0f, groomDirt_[i] - amount);
        any = true;
        if (rand01() < 0.5f)
            spawn(groomStep_ == GroomStep::Brush ? ParticleKind::Crumb : ParticleKind::Sparkle, sx, sy,
                  (rand01() - 0.5f) * 80, -40 - rand01() * 40, 500);
        if (groomDirt_[i] <= 0) cue(Cue::Pickup);
    }
    if (any && now_ >= nextBrushCue_) {
        cue(Cue::Brush);
        nextBrushCue_ = now_ + 450;
    }
    for (int i = 0; i < n; i++)
        if (groomDirt_[i] > 0) return;
    groomNext();
}

void Scene::groomNext() {
    rubX_ = rubY_ = -1;
    switch (groomStep_) {
        case GroomStep::Brush:
            saved(sim_.brush());  // a real brushing, as far as it's concerned
            groomStep_ = GroomStep::Hooves;
            hoof_ = 0;
            pebbles_ = (1u << kPebbles) - 1;
            speak("AHH, ALL CLEAN!");
            break;
        case GroomStep::Hooves:
            if (++hoof_ < 4) {
                pebbles_ = (1u << kPebbles) - 1;
                cue(Cue::Snort);
                return;
            }
            groomStep_ = GroomStep::Mane;
            for (float& d : groomDirt_) d = 1;
            speak("MY HOOVES FEEL GREAT!");
            break;
        case GroomStep::Mane:
            groomStep_ = GroomStep::Ribbon;
            speak("NOW A RIBBON?");
            break;
        case GroomStep::Ribbon: break;
    }
    cue(Cue::Happy);
}

void Scene::drawBow(Canvas& c, float x, float y, float s, int colour) {
    const Rgb col = kRibbonColours[(colour - 1) % kRibbons], dark = mix(col, 0x000000, 0.3f),
              light = mix(col, 0xFFFFFF, 0.35f);
    c.line(x - 1 * s, y + 2 * s, x - 6 * s, y + 10 * s, 3.5f * s, dark);  // two short tails
    c.line(x + 1 * s, y + 2 * s, x + 6 * s, y + 9 * s, 3.5f * s, dark);
    for (float side : {-1.0f, 1.0f}) {  // two round loops
        c.fillEllipse(x + side * 7 * s, y - 1 * s, 7.5f * s, 5.5f * s, col);
        c.fillEllipse(x + side * 8 * s, y - 2.5f * s, 3.5f * s, 2 * s, light);
    }
    c.fillCircle(x, y, 3.2f * s, dark);
}

void Scene::drawGroom(Canvas& c) {
    // Mud on its coat, or tangles in its mane, fading as they're brushed out.
    if (groomStep_ == GroomStep::Brush || groomStep_ == GroomStep::Mane) {
        const int n = groomStep_ == GroomStep::Brush ? kMudSpots : kTangles;
        for (int i = 0; i < n; i++) {
            float x, y;
            if (!groomSpot(i, x, y)) continue;
            const float a = std::min(1.0f, groomDirt_[i] * 1.3f), k = gScale_;
            if (groomStep_ == GroomStep::Brush) {
                // A splat of mud: dark clumps with a lighter, drier crust on top, and a drip.
                c.fillEllipse(x, y, 12 * k, 8 * k, 0x3E2814, 0.95f * a);
                c.fillCircle(x - 9 * k, y + 3 * k, 5 * k, 0x3E2814, 0.95f * a);
                c.fillCircle(x + 9 * k, y - 3 * k, 4.5f * k, 0x3E2814, 0.95f * a);
                c.line(x + 2 * k, y + 5 * k, x + 3 * k, y + 13 * k, 3.5f * k, 0x3E2814, 0.9f * a);
                c.fillEllipse(x - 2 * k, y - 2 * k, 7 * k, 4 * k, 0x8A6A40, 0.9f * a);
                c.fillCircle(x + 8 * k, y - 4 * k, 2 * k, 0x9C7C50, 0.9f * a);
            } else {  // a scribbly knot
                for (int j = 0; j < 3; j++)
                    c.ring(x + (j - 1) * 4 * k, y + (j % 2) * 3 * k, (4 + j) * k, 1.6f * k, 0x3A2A1E, a);
            }
            // A soft ring round each, so it's clear where to brush (no hurry).
            const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(t_ * 3 + i));
            c.ring(x, y, 15 * k, 2, kGold, 0.2f + 0.25f * pulse);
        }
    } else if (groomStep_ == GroomStep::Hooves) {  // a close-up of the hoof, from underneath
        c.fillRoundRect(110, 150, 260, 236, 22, kUiLight, 0.96f);
        const char* hn = kHoofNames[hoof_ < 4 ? hoof_ : 3];
        c.text(240, 162, hn, Canvas::textWidth(hn, ts(2)) <= 248 ? ts(2) : 2, 0x3B2A1E, Align::Center);
        const float x = kHoofX, y = kHoofY;
        c.fillEllipse(x, y + 4, 70, 76, 0x4A3A30);   // the hoof wall
        c.fillEllipse(x, y + 4, 60, 66, 0x9C8468);   // the sole
        c.fillTriangle(x, y - 30, x - 20, y + 54, x + 20, y + 54, 0x7A6450);  // the frog
        for (int i = 0; i < 14; i++) {  // its shoe, round the rim, open at the heel
            const float a0 = 3.1415927f * (-0.25f - i / 14.0f * 1.5f), a1 = 3.1415927f * (-0.25f - (i + 1) / 14.0f * 1.5f);
            c.line(x + std::cos(a0) * 64, y + 4 + std::sin(a0) * 70, x + std::cos(a1) * 64, y + 4 + std::sin(a1) * 70, 9, 0xB8B8C0);
        }
        for (int i = 0; i < kPebbles; i++) {
            if (!(pebbles_ & (1u << i))) continue;
            const Rect r = pebbleArea(i);
            c.fillEllipse(r.cx(), r.cy(), 11, 9, 0x8A8A86);
            c.fillEllipse(r.cx() - 3, r.cy() - 3, 4, 3, 0xC8C8C2);
            const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(t_ * 3 + i));
            c.ring(r.cx(), r.cy(), 22, 2.5f, kGold, 0.35f + 0.3f * pulse);
        }
    } else {  // choose a ribbon
        c.fillRoundRect(48, 196, 384, 150, 22, kUiLight, 0.96f);
        c.text(240, bigText_ ? 206 : 210, "PICK A RIBBON", ts(2), 0x3B2A1E, Align::Center);
        for (int i = 0; i < kRibbons; i++) {
            const Rect r = ribbonArea(i);
            c.fillRoundRect(r.x + 4, r.y + 4, r.w - 8, r.h - 8, 14, 0xE6DCC6);
            drawBow(c, r.cx(), r.cy() - 2, 2.2f, i + 1);
        }
    }
}

void Scene::finishGame(bool won, const char* msg) {
    gameOver_ = true;
    gameWon_ = won;
    std::snprintf(gameMsg_, sizeof gameMsg_, "%s", msg);
    resultUntil_ = now_ + 2600;
    cue(won ? Cue::Returned : Cue::Sulk);
}

void Scene::endGame(bool reward) {
    const bool won = gameWon_, photo = game_ == Game::Photo;
    overlay_ = Overlay::None;
    game_ = Game::None;
    petOverride_ = running_ = false;
    if (!reward) return;
    saved(sim_.finishedGame(won, photo));
    react(React::Happy, won ? 1600 : 900);
}

void Scene::gameTap(int x, int y) {
    if (game_ == Game::Pick) {
        for (int i = 0; i < gameChoiceCount(); i++) {
            if (!gameChoiceArea(i).contains(x, y)) continue;
            cue(Cue::Tick);
            if (species_ == Species::Horse && i < 4) {  // its own order, with Grooming in it
                static const Game kOrder[4] = {Game::Seek, Game::Berry, Game::Groom, Game::Photo};
                startGame(kOrder[i]);
                return;
            }
            if (i == gameChoiceCount() - 1) {  // quick catch: the old, relaxed game
                overlay_ = Overlay::None;
                game_ = Game::None;
                doPlay();
            } else {
                startGame(i == 0 ? Game::Seek : i == 1 ? Game::Berry : Game::Photo);
            }
            return;
        }
        if (!Rect{40, 56, 400, 380}.contains(x, y) || y < 100) {  // outside the panel, or its title
            overlay_ = Overlay::None;
            game_ = Game::None;
        }
        return;
    }
    if (gameOver_) {  // a tap on the result moves on
        endGame(true);
        return;
    }
    if (gameQuitArea().contains(x, y)) {
        cue(Cue::Tick);
        endGame(false);
        return;
    }
    if (game_ == Game::Seek) {
        for (int i = 0; i < kSeekSpots; i++) {
            if (!seekSpotArea(i).contains(x, y) || (seekWrong_ & (1u << i))) continue;
            if (i == seekSpot_) {
                cue(Cue::Happy);
                finishGame(true, "FOUND YOU!");
            } else {
                seekWrong_ |= static_cast<uint8_t>(1u << i);
                cue(Cue::Tick);
                if (--tries_ == 0) finishGame(false, "THERE IT WAS!");
                else std::snprintf(gameMsg_, sizeof gameMsg_, "NOT THERE...");
            }
            return;
        }
    } else if (game_ == Game::Groom) {
        if (groomStep_ == GroomStep::Hooves) {
            for (int i = 0; i < kPebbles; i++) {
                if (!(pebbles_ & (1u << i)) || !pebbleArea(i).contains(x, y)) continue;
                pebbles_ &= static_cast<uint8_t>(~(1u << i));
                cue(Cue::Clop);
                const Rect r = pebbleArea(i);
                for (int k = 0; k < 4; k++)
                    spawn(ParticleKind::Crumb, r.cx(), r.cy(), (rand01() - 0.5f) * 160, -120 - rand01() * 60, 600);
                if (!pebbles_) groomNext();
                return;
            }
        } else if (groomStep_ == GroomStep::Ribbon) {
            for (int i = 0; i < kRibbons; i++) {
                if (!ribbonArea(i).contains(x, y)) continue;
                ribbon_ = static_cast<uint8_t>(i + 1);
                ribbonUntil_ = now_ + 3u * 3600u * 1000u;  // it wears it for a few hours
                for (int k = 0; k < 6; k++)
                    spawn(ParticleKind::Sparkle, gManeX_[0] + (rand01() - 0.5f) * 60, gManeY_[0] + (rand01() - 0.5f) * 40,
                          (rand01() - 0.5f) * 40, -40, 1000);
                cue(Cue::Happy);
                finishGame(true, "SO PRETTY!");
                return;
            }
        } else {
            groomRub(static_cast<float>(x), static_cast<float>(y), 0.34f);  // a tap does a bit; no need to drag
        }
    } else if (game_ == Game::Photo && snapArea().contains(x, y) && running_ && !snapped_) {
        snapped_ = true;
        flashUntil_ = now_ + 300;
        cue(Cue::Photo);
        if (petInFrame()) finishGame(true, "GOT A PHOTO!");
        else std::snprintf(gameMsg_, sizeof gameMsg_, "MISSED IT!");
    }
}

void Scene::updateGame() {
    if (game_ == Game::Pick) return;
    if (gameOver_) {
        if (now_ >= resultUntil_) endGame(true);
        return;
    }
    switch (game_) {
        case Game::Seek:
            if (now_ >= nextEvent_) {  // the right spot rustles now and then: a hint
                const float x = kSeekSpotXY[seekSpot_][0], y = kSeekSpotXY[seekSpot_][1];
                for (int i = 0; i < 4; i++)
                    spawn(ParticleKind::Crumb, x + (rand01() - 0.5f) * 40, y - 30, (rand01() - 0.5f) * 60, -60, 700);
                nextEvent_ = now_ + 3500;
            }
            break;
        case Game::Berry: {
            if (now_ >= nextEvent_ && now_ + 1500 < gameUntil_) {
                for (FallingBerry& b : berries_) {
                    if (b.live) continue;
                    b = FallingBerry{40 + rand01() * 400, 130 + rand01() * 70, now_, true};
                    break;
                }
                nextEvent_ = now_ + kBerryEveryMs;
            }
            for (FallingBerry& b : berries_) {
                if (!b.live) continue;
                const float y = 70 + (now_ - b.born) / 1000.0f * b.vy;
                if (y >= kBasketY - 8 && y < kBasketY + 14 && std::fabs(b.x - basketX_) < 58) {
                    b.live = false;
                    score_++;
                    cue(Cue::Pickup);
                    spawn(ParticleKind::PlusOne, basketX_, kBasketY - 20, 0, -50, 700);
                } else if (y > S) {
                    b.live = false;
                }
            }
            if (now_ >= gameUntil_) {
                char msg[32];
                std::snprintf(msg, sizeof msg, "CAUGHT %d!", score_);
                finishGame(score_ >= kBerriesToWin, msg);
            }
            break;
        }
        case Game::Photo:
            if (!running_ && now_ >= nextEvent_) {
                running_ = true;
                snapped_ = false;
                runStart_ = now_;
                runRight_ = rand01() < 0.5f;
                gameMsg_[0] = '\0';
            }
            if (running_) {
                const float p = static_cast<float>(now_ - runStart_) / kRunMs;
                gameX_ = runRight_ ? -60 + 600 * p : 540 - 600 * p;
                lookX_ = runRight_ ? 0.8f : -0.8f;
                if (p >= 1) {
                    running_ = false;
                    if (--runsLeft_ == 0) finishGame(false, "IT GOT AWAY!");
                    else nextEvent_ = now_ + 1500 + static_cast<uint32_t>(rand01() * 2500);
                }
            }
            break;
        default: break;
    }
}

void Scene::drawBush(Canvas& c, float x, float y) {
    c.fillEllipse(x, y + 26, 40, 8, 0x000000, 0.18f);
    c.fillCircle(x - 22, y + 8, 22, sh(0x3E7B3A));
    c.fillCircle(x + 22, y + 8, 22, sh(0x3E7B3A));
    c.fillCircle(x, y - 6, 28, sh(0x4C8F44));
}

void Scene::drawPeek(Canvas& c, float x, float y) {
    if (species_ == Species::Horse) {
        drawHorsePeek(c, x, y - 30, false);
        return;
    }
    Rgb fur, face, mark;
    furColours(fur, face, mark);
    c.fillCircle(x, y - 34, 22, sh(fur));
    c.fillEllipse(x, y - 30, 14, 11, sh(face));
    for (float dx : {-6.0f, 6.0f}) c.fillCircle(x + dx, y - 33, 3, kEye);
    c.line(x - 5, y - 24, x + 5, y - 24, 2, 0x3A1E14);
}

void Scene::drawGamePicker(Canvas& c) {
    const Rgb ink = 0x3B2A1E, faint = 0x8A7A66;
    c.tint(0x000000, 0.55f);
    const bool horse = species_ == Species::Horse;
    c.fillRoundRect(40, bigText_ ? 50 : 56, 400, bigText_ ? 400 : 380, 20, kUiLight);
    c.text(240, bigText_ ? 66 : 76, "LET'S PLAY!", 3, ink, Align::Center);
    for (int i = 0; i < gameChoiceCount(); i++) {
        Rect r = gameChoiceArea(i);
        c.fillRoundRect(r.x, r.y + 3, r.w, r.h - 6, 14, i == gameChoiceCount() - 1 ? 0xD9CFBC : 0xE6DCC6);
        const char* name = horse ? kHorseGameNames[i] : kGameNames[i];
        if (bigText_) {
            c.text(r.cx(), r.y + 10.0f, name, 3, ink, Align::Center);
            c.text(r.cx(), r.y + 38.0f, horse ? kHorseGameHintsShort[i] : kGameHintsShort[i], 2, faint, Align::Center);
            continue;
        }
        c.text(r.cx(), r.y + (horse ? 12.0f : 14.0f), name, 2, ink, Align::Center);
        c.text(r.cx(), r.y + (horse ? 37.0f : 40.0f), horse ? kHorseGameHints[i] : kGameHints[i], 1, faint, Align::Center);
    }
    if (bigText_) c.text(240, horse ? 426.0f : 422.0f, "TAP OUTSIDE TO CLOSE", 2, faint, Align::Center);
    else c.text(240, horse ? 424.0f : 412.0f, "TAP OUTSIDE TO CLOSE", 1, faint, Align::Center);
}

void Scene::drawGame(Canvas& c) {
    if (game_ == Game::Pick) {
        drawGamePicker(c);
        return;
    }
    // The playing field's furniture.
    if (game_ == Game::Seek) {
        for (int i = 0; i < kSeekSpots; i++) {
            if (!(seekWrong_ & (1u << i))) continue;
            const float x = kSeekSpotXY[i][0], y = kSeekSpotXY[i][1];
            c.line(x - 12, y - 12, x + 12, y + 12, 5, 0xB3261E, 0.8f);
            c.line(x - 12, y + 12, x + 12, y - 12, 5, 0xB3261E, 0.8f);
        }
        if (gameOver_) drawPeek(c, kSeekSpotXY[seekSpot_][0], kSeekSpotXY[seekSpot_][1]);
    } else if (game_ == Game::Berry) {
        for (const FallingBerry& b : berries_) {
            if (!b.live) continue;
            drawItem(c, Item::Berries, b.x, 70 + (now_ - b.born) / 1000.0f * b.vy, 0.9f);
        }
        const float x = basketX_, y = kBasketY;
        c.fillEllipse(x, y + 30, 54, 8, 0x000000, 0.2f);
        c.fillRoundRect(x - 50, y, 100, 30, 10, 0x9C6B3A);
        for (int i = -2; i <= 2; i++) c.line(x + i * 18, y + 3, x + i * 18, y + 27, 2, 0x7A5028);
        c.fillRoundRect(x - 54, y - 4, 108, 9, 4, 0xB8844A);
    } else if (game_ == Game::Groom) {
        drawGroom(c);
    } else if (game_ == Game::Photo) {
        const Rgb corner = 0xF3EBD9;
        const float x0 = kFrameX0, x1 = kFrameX1, y0 = 190, y1 = 372;
        for (float sx : {x0, x1})
            for (float sy : {y0, y1}) {
                const float dx = sx == x0 ? 22.0f : -22.0f, dy = sy == y0 ? 22.0f : -22.0f;
                c.line(sx, sy, sx + dx, sy, 4, corner);
                c.line(sx, sy, sx, sy + dy, 4, corner);
            }
        Rect b = snapArea();
        c.fillRoundRect(b.x, b.y + 4, b.w, b.h - 8, 18, running_ && !snapped_ ? 0xC0392B : 0x8A5A50);
        c.text(b.cx(), b.cy() - 10.0f, "SNAP", 3, kUiLight, Align::Center);
    }

    // The banner along the top: what to do, how it's going, and a way out.
    c.fillRect(0, 0, S, 64, kUiDark);
    char status[32];
    switch (game_) {
        case Game::Seek: std::snprintf(status, sizeof status, "GUESSES LEFT: %d", tries_); break;
        case Game::Berry: {
            const int secs = gameUntil_ > now_ ? static_cast<int>((gameUntil_ - now_ + 999) / 1000) : 0;
            std::snprintf(status, sizeof status, "%s %d/%d  %ds", species_ == Species::Horse ? "APPLES" : "BERRIES", score_, kBerriesToWin, secs);
            break;
        }
        case Game::Photo: std::snprintf(status, sizeof status, "CHANCES LEFT: %d", runsLeft_); break;
        case Game::Groom:
            std::snprintf(status, sizeof status, "%s", groomStep_ == GroomStep::Brush    ? "1 BRUSH  2  3  4"
                                                       : groomStep_ == GroomStep::Hooves ? "1  2 HOOVES  3  4"
                                                       : groomStep_ == GroomStep::Mane   ? "1  2  3 MANE  4"
                                                                                         : "1  2  3  4 RIBBON");
            break;
        default: status[0] = '\0'; break;
    }
    const char* title = game_ == Game::Groom ? "GROOMING"
                      : species_ == Species::Horse && game_ == Game::Berry ? "APPLE CATCH"
                      : kGameNames[game_ == Game::Seek ? 0 : game_ == Game::Berry ? 1 : 2];
    c.text(16, bigText_ ? 8 : 12, title, ts(2), kGold);
    c.text(16, bigText_ ? 36 : 38, status, Canvas::textWidth(status, ts(2)) <= S - 136 ? ts(2) : 2, kUiLight);
    Rect q = gameQuitArea();
    c.fillRoundRect(q.x + 8, q.y + 10, q.w - 16, q.h - 20, 12, kButton);
    label(c, {q.x + 8, q.y + 10, q.w - 16, q.h - 20}, "QUIT", 2, kUiLight);

    const char* msg = gameMsg_[0] ? gameMsg_ : nullptr;
    if (!msg && !gameOver_) {
        if (game_ == Game::Seek) msg = "WHERE DID IT HIDE?";
        if (game_ == Game::Berry) msg = "MOVE THE BASKET";
        if (game_ == Game::Groom)
            msg = groomStep_ == GroomStep::Brush    ? "BRUSH OFF THE MUD"
                : groomStep_ == GroomStep::Hooves ? "TAP THE STONES OUT"
                : groomStep_ == GroomStep::Mane   ? "COMB OUT THE TANGLES"
                                                  : nullptr;
        if (game_ == Game::Photo) msg = running_ ? nullptr : "WAIT FOR IT...";
    }
    if (msg) {
        const int scale = gameOver_ ? 3 : ts(2);
        char wrapped[3][64];
        const int n = wrapText(msg, scale, 430, wrapped, 3);
        float tw = 0;
        for (int i = 0; i < n; i++) tw = std::max(tw, static_cast<float>(Canvas::textWidth(wrapped[i], scale)));
        const float w = tw + 32, pad = gameOver_ ? 11.0f : scale >= 3 ? 8.0f : 9.0f;
        const float h = 2 * pad + Canvas::textHeight(scale) + (n - 1) * lineStep(scale);
        c.fillRoundRect(240 - w / 2, 80, w, h, 16, kUiDark, 0.85f);
        for (int i = 0; i < n; i++)
            c.text(240, 80 + pad + i * lineStep(scale), wrapped[i], scale, gameOver_ && gameWon_ ? kGold : kUiLight, Align::Center);
    }
}

void Scene::drawNaming(Canvas& c) {
    const Rgb ink = 0x3B2A1E, faint = 0x8A7A66, key = 0xE6DCC6;
    c.tint(0x000000, 0.55f);
    c.fillRoundRect(8, 10, 464, 460, 20, kUiLight);
    c.text(240, bigText_ ? 19 : 22, "WHAT'S ITS NAME?", ts(2), ink, Align::Center);

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
    label(c, {s.x + 3, s.y + 4, s.w - 6, s.h - 8}, "SUGGEST", 2, kUiLight);
    bool ready = false;
    for (size_t i = 0; i < len; i++) ready |= nameDraft_[i] != ' ';
    c.fillRoundRect(d.x + 3, d.y + 4, d.w - 6, d.h - 8, 14, ready ? kButton : 0xB8AE9A);
    label(c, {d.x + 3, d.y + 4, d.w - 6, d.h - 8}, "DONE", 2, kUiLight);
    (void)faint;
}

void Scene::drawStartOver(Canvas& c) {
    const Rgb ink = 0x3B2A1E, faint = 0x8A7A66;
    c.tint(0x000000, 0.6f);
    if (factoryAvailable_) {
        drawStartOverFour(c);
        return;
    }
    c.fillRoundRect(24, 40, 432, 400, 20, kUiLight);
    c.text(240, bigText_ ? 54 : 62, "START OVER?", 3, 0x7A2E1E, Align::Center);
    const char* name = sim_.state().name[0] ? sim_.state().name : "THIS PET";
    char line[48];
    std::snprintf(line, sizeof line, "%.10s WILL BE GONE FOR GOOD,", name);
    if (bigText_) {
        char all[96];
        std::snprintf(all, sizeof all, "%s %s", line, species_ == Species::Horse ? "AND A NEW FOAL WILL COME." : "AND A NEW EGG WILL BE LAID.");
        para(c, 240, 86, all, 2, ink, 400);
        Rect k = startKeepArea(), w = startWipeArea(), x = startCancelArea();
        c.fillRoundRect(k.x, k.y + 3, k.w, k.h - 6, 14, kButton);
        label(c, {k.x, k.y + 3, k.w, k.h - 6}, species_ == Species::Horse ? "NEW FOAL, KEEP PHOTOS" : "NEW EGG, KEEP PHOTOS", 2, kUiLight);
        c.fillRoundRect(w.x, w.y + 3, w.w, w.h - 6, 14, 0x8A3A2A);
        label(c, {w.x, w.y + 3, w.w, w.h - 6}, species_ == Species::Horse ? "NEW FOAL, WIPE EVERYTHING" : "NEW EGG, WIPE EVERYTHING", 2, kUiLight);
        c.fillRoundRect(x.x, x.y + 3, x.w, x.h - 6, 14, 0xD9CFBC);
        label(c, {x.x, x.y + 3, x.w, x.h - 6}, "CANCEL", 2, ink);
        c.text(240, 416, "SETTINGS AND WI-FI STAY", 2, faint, Align::Center);
        return;
    }
    c.text(240, 112, line, 2, ink, Align::Center);
    c.text(240, 136, species_ == Species::Horse ? "AND A NEW FOAL WILL COME." : "AND A NEW EGG WILL BE LAID.", 2, ink, Align::Center);
    c.text(240, 166, "SETTINGS AND WI-FI STAY AS THEY ARE.", 1, faint, Align::Center);
    Rect k = startKeepArea(), w = startWipeArea(), x = startCancelArea();
    c.fillRoundRect(k.x, k.y + 3, k.w, k.h - 6, 14, kButton);
    c.text(k.cx(), k.y + 13.0f, species_ == Species::Horse ? "NEW FOAL, KEEP PHOTOS" : "NEW EGG, KEEP PHOTOS", 2, kUiLight, Align::Center);
    c.text(k.cx(), k.y + 39.0f, !trailsAvailable_ ? (species_ == Species::Horse ? "AND HOOFPRINTS" : "AND FOOTPRINTS") : species_ == Species::Horse ? "AND HOOFPRINTS AND THE TRAIL MAP" : "AND FOOTPRINTS AND THE TRAIL MAP", 1, 0xD9CFBC, Align::Center);
    c.fillRoundRect(w.x, w.y + 3, w.w, w.h - 6, 14, 0x8A3A2A);
    c.text(w.cx(), w.y + 13.0f, species_ == Species::Horse ? "NEW FOAL, WIPE EVERYTHING" : "NEW EGG, WIPE EVERYTHING", 2, kUiLight, Align::Center);
    c.text(w.cx(), w.y + 39.0f, "A COMPLETELY FRESH START", 1, 0xF2D0C4, Align::Center);
    c.fillRoundRect(x.x, x.y + 3, x.w, x.h - 6, 14, 0xD9CFBC);
    c.text(x.cx(), x.cy() - 7.0f, "CANCEL", 2, ink, Align::Center);
}

// The start-over page with the fourth choice, "like new" (boards), and its confirmation.
void Scene::drawStartOverFour(Canvas& c) {
    const Rgb ink = 0x3B2A1E, faint = 0x8A7A66, red = 0x8A3A2A;
    const bool horse = species_ == Species::Horse;
    c.fillRoundRect(24, bigText_ ? 30 : 24, 432, 428, 20, kUiLight);
    const Rect k = startKeepArea(), w = startWipeArea(), n = startNewArea(), x = startCancelArea();
    auto button = [&](const Rect& r, Rgb fill, const char* top, const char* sub, Rgb col, Rgb subCol) {
        c.fillRoundRect(r.x, r.y + 3, r.w, r.h - 6, 14, fill);
        if (bigText_ || !sub) {
            label(c, {r.x, r.y + 3, r.w, r.h - 6}, top, 2, col);
            return;
        }
        c.text(r.cx(), r.y + 13.0f, top, 2, col, Align::Center);
        c.text(r.cx(), r.y + 39.0f, sub, 1, subCol, Align::Center);
    };
    if (confirmNew_) {
        c.text(240, bigText_ ? 46 : 44, "ERASE IT ALL?", 3, 0x7A2E1E, Align::Center);
        para(c, 240, bigText_ ? 84 : 90,
             "THE PET, ITS PHOTOS, THE WI-FI AND THE SETTINGS ARE ALL ERASED. IT RESTARTS LIKE NEW, WITH THE WELCOME SCREEN.",
             2, ink, 400);
        button(n, red, "YES, ERASE IT ALL", nullptr, kUiLight, kUiLight);
        button(x, 0xD9CFBC, "CANCEL", nullptr, ink, ink);
        return;
    }
    c.text(240, bigText_ ? 46 : 40, "START OVER?", 3, 0x7A2E1E, Align::Center);
    const char* name = sim_.state().name[0] ? sim_.state().name : "THIS PET";
    char all[96];
    std::snprintf(all, sizeof all, "%.10s WILL BE GONE FOR GOOD, %s", name,
                  horse ? "AND A NEW FOAL WILL COME." : "AND A NEW EGG WILL BE LAID.");
    if (bigText_) {
        para(c, 240, 78, all, 2, ink, 400);
    } else {
        const float y = para(c, 240, 76, all, 2, ink, 400);
        c.text(240, y + 4, "THE FIRST TWO KEEP THE WI-FI AND SETTINGS.", 1, faint, Align::Center);
    }
    button(k, kButton, horse ? "NEW FOAL, KEEP PHOTOS" : "NEW EGG, KEEP PHOTOS",
           !trailsAvailable_ ? (horse ? "AND HOOFPRINTS" : "AND FOOTPRINTS") : horse ? "AND HOOFPRINTS AND THE TRAIL MAP" : "AND FOOTPRINTS AND THE TRAIL MAP", kUiLight, 0xD9CFBC);
    button(w, red, horse ? "NEW FOAL, WIPE EVERYTHING" : "NEW EGG, WIPE EVERYTHING", "A COMPLETELY FRESH START", kUiLight, 0xF2D0C4);
    button(n, 0x5A2A20, bigText_ ? "LIKE NEW: ERASE WI-FI TOO" : "LIKE NEW, AS OUT OF THE BOX", "ERASES THE WI-FI AND SETTINGS TOO",
           kUiLight, 0xE8C8BC);
    button(x, 0xD9CFBC, "CANCEL", nullptr, ink, ink);
}

void Scene::drawSettings(Canvas& c) {
    const Rgb ink = 0x3B2A1E, faint = 0x8A7A66, off = 0xD9CFBC;
    c.tint(0x000000, 0.55f);
    c.fillRoundRect(kSetX, kSetY, kSetW, kSetH, 20, kUiLight);
    c.text(240, bigText_ ? kSetY + 8 : kSetY + 12, "SETTINGS", ts(2), ink, Align::Center);

    const char* labels[kSettingRows] = {"BRIGHTNESS", "SCREEN OFF AFTER", "VOLUME"};
    for (int row = 0; row < kSettingRows; row++) {
        const int y = kSetRowY[row];
        c.text(240, bigText_ ? y - 26.0f : y - 24.0f, labels[row], ts(2), faint, Align::Center);
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

    Rect o = startOverArea(), d = settingsDoneArea();
    c.fillRoundRect(o.x, o.y + 4, o.w, o.h - 8, 14, 0xD9CFBC);
    label(c, {o.x, o.y + 4, o.w, o.h - 8}, "START OVER", 2, 0x7A2E1E);
    if (wifiAvailable_) {
        Rect w = wifiButtonArea();
        c.fillRoundRect(w.x, w.y + 4, w.w, w.h - 8, 14, 0x6E8F5A);
        label(c, {w.x, w.y + 4, w.w, w.h - 8}, "WI-FI", 2, kUiLight);
    } else if (privacyAvailable_) {
        Rect w = privacyArea();
        c.fillRoundRect(w.x, w.y + 4, w.w, w.h - 8, 14, 0xD9CFBC);
        label(c, {w.x, w.y + 4, w.w, w.h - 8}, "PRIVACY", 2, ink);
    }
    c.fillRoundRect(d.x, d.y + 4, d.w, d.h - 8, 14, kButton);
    label(c, {d.x, d.y + 4, d.w, d.h - 8}, "DONE", 2, kUiLight);
}

// The Wi-Fi page: what's saved, and the steps while the setup hotspot is up.
void Scene::drawWifi(Canvas& c) {
    const Rgb ink = 0x3B2A1E, faint = 0x8A7A66, good = 0x3F7A3A, bad = 0x8A3A2A;
    c.tint(0x000000, 0.55f);
    c.fillRoundRect(kSetX, kSetY, kSetW, kSetH, 20, kUiLight);
    const bool scan = wifiStatus_ == WifiStatus::SetupRunning && qrSize_ > 0;
    c.text(240, kSetY + 12, !welcome_ ? "WI-FI" : wifiStatus_ == WifiStatus::Joined ? "ALL SET!" : "HELLO!", 3, ink, Align::Center);
    if (!scan) c.text(240, kSetY + (bigText_ ? 42 : 46), "FOR THE CLOCK AND THE WEATHER", 2, faint, Align::Center);
    if (bigText_) {
        drawWifiBig(c);
        return;
    }
    float y = kSetY + 84;
    auto line = [&](const char* text, int scale, Rgb col) {
        c.text(240, y, text, scale, col, Align::Center);
        y += scale == 1 ? 16.0f : scale == 2 ? 26.0f : 36.0f;
    };
    char buf[48];
    switch (wifiStatus_) {
        case WifiStatus::NotSet:
            line("NOT SET UP YET", 2, ink);
            y += 10;
            line("TAP SET UP, THEN USE A PHONE", 2, faint);
            line("TO PICK YOUR NETWORK", 2, faint);
            break;
        case WifiStatus::Saved:
        case WifiStatus::Joined:
            line("NETWORK:", 2, faint);
            line(wifiSsid_, 2, ink);
            y += 6;
            line(wifiStatus_ == WifiStatus::Joined ? "CONNECTED" : "SAVED", 2, good);
            if (wifiTown_[0]) {
                y += 12;
                line("TOWN:", 2, faint);
                line(wifiTown_, 2, std::strcmp(wifiTown_, "TOWN NOT FOUND") == 0 ? bad : ink);
            }
            break;
        case WifiStatus::SetupRunning:
            if (scan) {  // a code to scan, and the network's name for typing in by hand
                if (setupPhone_) {  // joined: the page should be open; if not, its address
                    c.text(240, kSetY + 46, "PHONE JOINED! THE PAGE OPENS", 2, 0x3F7A3A, Align::Center);
                    drawQr(c, 240, kSetY + 68, 7);
                    c.text(240, kSetY + 290, "NO PAGE? SCAN THIS, OR GO TO", 2, faint, Align::Center);
                    c.text(240, kSetY + 312, "192.168.4.1", 2, 0x7A4E1E, Align::Center);
                    break;
                }
                c.text(240, kSetY + 46, "SCAN THIS WITH A PHONE CAMERA", 2, ink, Align::Center);
                drawQr(c, 240, kSetY + 68, 7);
                c.text(240, kSetY + 284, "THEN OPEN 192.168.4.1 IN A BROWSER", 2, 0x7A4E1E, Align::Center);
                char join[48];
                std::snprintf(join, sizeof join, "(OR JOIN THE WI-FI %s)", wifiHotspot_);
                c.text(240, kSetY + 308, join, Canvas::textWidth(join, 2) <= kSetW - 24 ? 2 : 1, faint, Align::Center);
                break;
            }
            line("1. ON A PHONE, JOIN THE WI-FI", 2, ink);
            y += 2;
            line(wifiHotspot_, 3, 0x7A4E1E);
            y += 4;
            line("2. A PAGE OPENS. IF NOT, GO TO", 2, ink);
            line("192.168.4.1 IN THE BROWSER", 2, ink);
            y += 4;
            line("3. PICK YOUR NETWORK, TYPE", 2, ink);
            line("ITS PASSWORD AND YOUR TOWN", 2, ink);
            break;
        case WifiStatus::Checking:
            line("CHECKING...", 2, ink);
            line(wifiSsid_, 2, ink);
            break;
        case WifiStatus::Failed:
            std::snprintf(buf, sizeof buf, "COULDN'T JOIN");
            line(buf, 2, bad);
            line(wifiSsid_, 2, ink);
            y += 6;
            line("CHECK THE PASSWORD", 2, faint);
            line("AND TAP SET UP TO TRY AGAIN", 2, faint);
            break;
    }
    if (backupCode_[0] && !scan) {  // the backup code, to write down (between the town and the version)
        char line[40];
        std::snprintf(line, sizeof line, "BACKUP CODE %s", backupCode_);
        c.text(240, kSetY + kSetH - 184, line, 2, faint, Align::Center);
    }
    if (updateShown()) {  // the version, and a row to tap to look for a newer one
        const Rect u = updateArea();
        c.fillRoundRect(u.x, u.y + 4, u.w, u.h - 8, 14, 0xE6DCC6);
        char top[40], sub[40];
        std::snprintf(top, sizeof top, updateBuild_ ? "VERSION %d" : "VERSION: YOUR OWN BUILD", updateBuild_);
        switch (updateStatus_) {
            case UpdateStatus::Checking: std::snprintf(sub, sizeof sub, "LOOKING FOR AN UPDATE..."); break;
            case UpdateStatus::UpToDate: std::snprintf(sub, sizeof sub, "UP TO DATE"); break;
            case UpdateStatus::Downloading:
                std::snprintf(top, sizeof top, "UPDATING... %d%%", updatePercent_);
                sub[0] = '\0';
                break;
            case UpdateStatus::Failed: std::snprintf(sub, sizeof sub, "COULDN'T UPDATE. TAP AGAIN"); break;
            case UpdateStatus::Restarting: std::snprintf(sub, sizeof sub, "UPDATED! RESTARTING"); break;
            default: std::snprintf(sub, sizeof sub, "TAP TO CHECK FOR AN UPDATE"); break;
        }
        c.text(u.cx(), u.y + 12, top, 2, ink, Align::Center);
        if (sub[0]) c.text(u.cx(), u.y + 36, sub, 2, updateStatus_ == UpdateStatus::Failed ? bad : faint, Align::Center);
        if (updateStatus_ == UpdateStatus::Downloading) {
            c.fillRoundRect(u.x + 40, u.y + 40, u.w - 80, 10, 5, 0xCFC4AE);
            c.fillRoundRect(u.x + 40, u.y + 40, (u.w - 80) * updatePercent_ / 100.0f, 10, 5, good);
        }
    }
    const bool running = wifiStatus_ == WifiStatus::SetupRunning, checking = wifiStatus_ == WifiStatus::Checking;
    Rect a = wifiSetupArea(), d = wifiDoneArea();
    c.fillRoundRect(a.x, a.y + 4, a.w, a.h - 8, 14, checking ? 0xB8AE9A : running ? 0xD9CFBC : 0x6E8F5A);
    c.text(a.cx(), a.cy() - 7.0f, running ? "CANCEL" : wifiStatus_ == WifiStatus::NotSet ? "SET UP" : "SET UP AGAIN", 2,
           running ? 0x7A2E1E : kUiLight, Align::Center);
    c.fillRoundRect(d.x, d.y + 4, d.w, d.h - 8, 14, kButton);
    c.text(d.cx(), d.cy() - 7.0f, welcomeLater() ? "LATER" : "DONE", 2, kUiLight, Align::Center);
}

// The Wi-Fi page with big text: the same things, wrapped, with short labels above their values.
void Scene::drawWifiBig(Canvas& c) {
    const Rgb ink = 0x3B2A1E, faint = 0x8A7A66, good = 0x3F7A3A, bad = 0x8A3A2A;
    const float w = kSetW - 40.0f;
    float y = kSetY + 70;
    auto small = [&](const char* text, Rgb col) {  // a label
        c.text(240, y, text, 2, col, Align::Center);
        y += 20;
    };
    switch (wifiStatus_) {
        case WifiStatus::NotSet:
            y = para(c, 240, y + 6, "NOT SET UP YET", 2, ink, w) + 10;
            para(c, 240, y, "TAP SET UP, THEN USE A PHONE TO PICK YOUR NETWORK", 2, faint, w);
            break;
        case WifiStatus::Saved:
        case WifiStatus::Joined:
            small("NETWORK:", faint);
            y = para(c, 240, y, wifiSsid_, 2, ink, w);
            y = para(c, 240, y, wifiStatus_ == WifiStatus::Joined ? "CONNECTED" : "SAVED", 2, good, w);
            if (wifiTown_[0]) {
                y += 4;
                small("TOWN:", faint);
                const Rgb col = std::strcmp(wifiTown_, "TOWN NOT FOUND") == 0 ? bad : ink;
                const int ts3 = Canvas::textWidth(wifiTown_, 3) <= w ? 3 : 2;
                c.text(240, y, wifiTown_, ts3, col, Align::Center);
                y += lineStep(ts3);
            }
            break;
        case WifiStatus::SetupRunning:
            if (qrSize_ > 0) {
                c.text(240, kSetY + 42, setupPhone_ ? "PHONE JOINED!" : "SCAN WITH A PHONE", 3, setupPhone_ ? good : ink, Align::Center);
                drawQr(c, 240, kSetY + 68, 7);
                if (setupPhone_) {
                    c.text(240, kSetY + 276, "NO PAGE? SCAN THIS OR OPEN", 2, faint, Align::Center);
                    c.text(240, kSetY + 296, "192.168.4.1", 3, 0x7A4E1E, Align::Center);
                    c.text(240, kSetY + 322, "IN THE PHONE'S BROWSER", 2, faint, Align::Center);
                } else {
                    c.text(240, kSetY + 278, "THEN OPEN 192.168.4.1", 3, 0x7A4E1E, Align::Center);
                    c.text(240, kSetY + 304, "IN THE PHONE'S BROWSER", 2, ink, Align::Center);
                    char join[48];
                    std::snprintf(join, sizeof join, "(OR JOIN THE WI-FI %s)", wifiHotspot_);
                    c.text(240, kSetY + 324, join, Canvas::textWidth(join, 2) <= w ? 2 : 1, faint, Align::Center);
                }
                break;
            }
            y = para(c, 240, y + 4, "1. ON A PHONE, JOIN", 2, ink, w);
            y = para(c, 240, y, wifiHotspot_, 2, 0x7A4E1E, w) + 8;
            y = para(c, 240, y, "2. A PAGE OPENS. IF NOT, GO TO 192.168.4.1", 2, ink, w) + 8;
            para(c, 240, y, "3. PICK YOUR NETWORK AND TYPE ITS PASSWORD", 2, ink, w);
            break;
        case WifiStatus::Checking:
            y = para(c, 240, y + 6, "CHECKING...", 2, ink, w);
            para(c, 240, y, wifiSsid_, 2, ink, w);
            break;
        case WifiStatus::Failed:
            y = para(c, 240, y + 6, "COULDN'T JOIN", 2, bad, w);
            y = para(c, 240, y, wifiSsid_, 2, ink, w) + 8;
            para(c, 240, y, "CHECK THE PASSWORD AND TAP SET UP TO TRY AGAIN", 2, faint, w);
            break;
    }
    if (updateShown()) {  // the backup code, then the version row (the steps don't leave room for them)
        if (backupCode_[0]) {
            const float by = std::min(y + 14, kSetY + kSetH - 204.0f);  // just under the network (and town)
            c.text(240, by, "BACKUP CODE", 2, faint, Align::Center);
            c.text(240, by + 19, backupCode_, 3, ink, Align::Center);
        }
        const Rect u = updateArea();
        c.fillRoundRect(u.x, u.y + 4, u.w, u.h - 8, 14, 0xE6DCC6);
        char top[40];
        const char* sub = "TAP TO CHECK FOR AN UPDATE";
        std::snprintf(top, sizeof top, updateBuild_ ? "VERSION %d" : "YOUR OWN BUILD", updateBuild_);
        switch (updateStatus_) {
            case UpdateStatus::Checking: sub = "LOOKING FOR AN UPDATE..."; break;
            case UpdateStatus::UpToDate: sub = "UP TO DATE"; break;
            case UpdateStatus::Downloading:
                std::snprintf(top, sizeof top, "UPDATING... %d%%", updatePercent_);
                sub = "";
                break;
            case UpdateStatus::Failed: sub = "COULDN'T UPDATE. TAP AGAIN"; break;
            case UpdateStatus::Restarting: sub = "UPDATED! RESTARTING"; break;
            default: break;
        }
        c.text(u.cx(), u.y + 12, top, 3, ink, Align::Center);
        if (sub[0]) c.text(u.cx(), u.y + 40, sub, 2, updateStatus_ == UpdateStatus::Failed ? bad : faint, Align::Center);
        if (updateStatus_ == UpdateStatus::Downloading) {
            c.fillRoundRect(u.x + 40, u.y + 42, u.w - 80, 10, 5, 0xCFC4AE);
            c.fillRoundRect(u.x + 40, u.y + 42, (u.w - 80) * updatePercent_ / 100.0f, 10, 5, good);
        }
    }
    const bool running = wifiStatus_ == WifiStatus::SetupRunning, checking = wifiStatus_ == WifiStatus::Checking;
    Rect a = wifiSetupArea(), d = wifiDoneArea();
    c.fillRoundRect(a.x, a.y + 4, a.w, a.h - 8, 14, checking ? 0xB8AE9A : running ? 0xD9CFBC : 0x6E8F5A);
    label(c, {a.x, a.y + 4, a.w, a.h - 8}, running ? "CANCEL" : wifiStatus_ == WifiStatus::NotSet ? "SET UP" : "SET UP AGAIN", 2,
          running ? 0x7A2E1E : kUiLight);
    c.fillRoundRect(d.x, d.y + 4, d.w, d.h - 8, 14, kButton);
    label(c, {d.x, d.y + 4, d.w, d.h - 8}, welcomeLater() ? "LATER" : "DONE", 2, kUiLight);
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
        case Slot::Play:
            if (species_ == Species::Horse) {  // a lucky horseshoe, open end up, nail holes and all
                const float pi = 3.1415927f, cy = y - 2, r = 14;
                auto at = [&](float u, float& px, float& py) {  // u 0..1 round the shoe; its tips curl in a little
                    const float a = pi * (-0.22f + u * 1.44f);
                    const float rr = r * (1 - 0.12f * std::fabs(2 * u - 1) * std::fabs(2 * u - 1));
                    px = x + std::cos(a) * rr;
                    py = cy + std::sin(a) * rr * 1.1f;
                };
                for (int i = 0; i < 16; i++) {
                    float x0, y0, x1, y1;
                    at(i / 16.0f, x0, y0);
                    at((i + 1) / 16.0f, x1, y1);
                    c.line(x0, y0, x1, y1, 9, 0xB8B8C0);
                    c.line(x0, y0 - 1.5f, x1, y1 - 1.5f, 3, 0xE4E4EA);  // a shine along it
                }
                for (int i = 0; i < 6; i++) {
                    float hx, hy;
                    at(0.12f + i * 0.152f, hx, hy);
                    c.fillCircle(hx, hy, 1.5f, 0x55555C);
                }
            } else {
                drawItem(c, Item::Pinecone, x, y, 1);
            }
            break;
        case Slot::Stable: {  // a little barn with a door (a cabin for the Bigfoot's sitter)
            const Rgb wall = species_ == Species::Horse ? 0xB8332A : 0x8A5A2B;
            c.fillRect(x - 17, y - 6, 34, 26, wall);
            c.fillTriangle(x - 22, y - 5, x + 22, y - 5, x, y - 22, 0xCFC7B5);
            c.fillRect(x - 7, y + 4, 14, 16, sim_.state().boarding ? 0x2A1E14 : kUiLight);
            break;
        }
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
    if (species_ == Species::Horse) {  // the horse's foods take the same places
        switch (it) {
            case Item::Berries:  // an apple
                c.fillCircle(x - 4 * k, y + 2 * k, 11 * k, 0xC8323A);
                c.fillCircle(x + 4 * k, y + 2 * k, 11 * k, 0xC8323A);
                c.fillCircle(x - 6 * k, y - 3 * k, 3.5f * k, 0xFFFFFF, 0.45f);
                c.line(x, y - 8 * k, x + 2 * k, y - 16 * k, 2.5f * k, 0x5A3A22);
                c.fillEllipse(x + 7 * k, y - 14 * k, 6 * k, 3 * k, 0x5C8F3E);
                return;
            case Item::Fish:  // a carrot with its leafy top
                for (int i = -1; i <= 1; i++)
                    c.line(x - 9 * k, y - 9 * k, x - 14 * k + i * 6 * k, y - 22 * k - std::abs(i) * -3 * k, 3.5f * k, 0x5C8F3E);
                c.line(x - 8 * k, y - 7 * k, x + 5 * k, y + 6 * k, 13 * k, 0xE8792B);
                c.fillTriangle(x - 1 * k, y + 11 * k, x + 10 * k, y, x + 15 * k, y + 15 * k, 0xE8792B);
                c.line(x - 10 * k, y - 6 * k, x + 2 * k, y + 6 * k, 4 * k, 0xF59A4C, 0.8f);
                for (int i = 0; i < 3; i++)
                    c.line(x - 3 * k + i * 5 * k, y - 4 * k + i * 5 * k, x + 2 * k + i * 5 * k, y - 8 * k + i * 5 * k, 1.6f * k, 0xB85A1E);
                return;
            case Item::Roots:  // a bundle of hay tied with twine
                for (int i = -4; i <= 4; i++)
                    c.line(x + i * 2.5f * k, y + 14 * k, x + i * 3.5f * k, y - 14 * k, 2.6f * k, i % 2 ? 0xD9B655 : 0xEBCB6E);
                c.fillRect(x - 12 * k, y - 2 * k, 24 * k, 4 * k, 0x8C5A3A);
                return;
            case Item::Brush: {  // a grooming brush: a wooden back, a leather strap for your hand, stiff bristles
                for (int i = -4; i <= 4; i++)
                    c.line(x + i * 3.2f * k, y + 3 * k, x + i * 3.4f * k, y + 13 * k, 2.2f * k, i % 2 ? 0xD8C49A : 0xE8D8B0);
                c.fillRoundRect(x - 17 * k, y - 8 * k, 34 * k, 13 * k, 6 * k, 0x9A6436);
                c.fillRoundRect(x - 15 * k, y - 7 * k, 30 * k, 4 * k, 2 * k, 0xB8834E);  // light on the top edge
                c.line(x - 13 * k, y - 8 * k, x - 9 * k, y - 18 * k, 4 * k, 0x5A3420);   // the strap
                c.line(x - 9 * k, y - 18 * k, x + 9 * k, y - 18 * k, 4 * k, 0x5A3420);
                c.line(x + 9 * k, y - 18 * k, x + 13 * k, y - 8 * k, 4 * k, 0x5A3420);
                return;
            }
            case Item::Poultice:  // a first-aid kit: a white cross on red, with a handle
                c.line(x - 7 * k, y - 13 * k, x - 7 * k, y - 17 * k, 3 * k, 0x8A1E18);
                c.line(x - 7 * k, y - 17 * k, x + 7 * k, y - 17 * k, 3 * k, 0x8A1E18);
                c.line(x + 7 * k, y - 17 * k, x + 7 * k, y - 13 * k, 3 * k, 0x8A1E18);
                c.fillRoundRect(x - 18 * k, y - 13 * k, 36 * k, 28 * k, 6 * k, 0xD8322A);
                c.fillRoundRect(x - 18 * k, y - 13 * k, 36 * k, 6 * k, 3 * k, 0xE8584E);  // the lid's lit edge
                c.fillRect(x - 3.5f * k, y - 8 * k, 7 * k, 20 * k, 0xFFFFFF);
                c.fillRect(x - 10 * k, y - 1.5f * k, 20 * k, 7 * k, 0xFFFFFF);
                return;
            default: break;
        }
    }
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

// ---- names: the horse edition has its own words for stages, grown-up forms and coats

const char* Scene::stageName(Stage st) const {
    if (species_ != Species::Horse) return toString(st);
    switch (st) {
        case Stage::Egg: return "ON THE WAY";
        case Stage::Baby: return "FOAL";
        case Stage::Child: return "YEARLING";
        case Stage::Teen: return "YOUNG HORSE";
        case Stage::Adult: return "HORSE";
        case Stage::Elder: return "OLD HORSE";
        case Stage::Away: return "AWAY";
    }
    return "?";
}

const char* Scene::formName(AdultForm f) const {
    if (species_ != Species::Horse) return toString(f);
    switch (f) {  // the same paths as the Squatch's, in horse terms
        case AdultForm::None: return "NONE";
        case AdultForm::Guardian: return "TRUSTY STEED";
        case AdultForm::RidgeRunner: return "RACER";
        case AdultForm::CouchSquatch: return "LAZY PONY";
        case AdultForm::Hermit: return "WILD MUSTANG";
        case AdultForm::Legend: return "CHAMPION";
    }
    return "?";
}

void Scene::horseCoatName(char* out, size_t n) const {
    const Traits t = sim_.traits();  // the same split as horseColours()
    if (t.rarity == Rarity::Mythic) {
        std::snprintf(out, n, "GOLDEN UNICORN (MYTHIC)");
        return;
    }
    if (t.rarity == Rarity::Epic) {
        std::snprintf(out, n, "UNICORN (EPIC)");
        return;
    }
    if (t.variant == Variant::Swamp) {
        std::snprintf(out, n, "DAPPLE GREY (RARE)");
        return;
    }
    if (t.variant != Variant::None) {
        std::snprintf(out, n, "WHITE (RARE)");
        return;
    }
    const char* colour = t.shade < 0.32f ? "BLACK" : t.shade < 0.5f ? "BAY" : t.shade < 0.66f ? "CHESTNUT"
                       : t.shade < 0.8f ? "BUCKSKIN" : "PALOMINO";
    const char* mark = t.markings == 1 ? ", BLAZE" : t.markings == 2 ? ", STAR" : t.markings == 3 ? ", SPOTS" : "";
    std::snprintf(out, n, "%s%s%s", colour, mark, t.rarity == Rarity::Uncommon ? " (UNCOMMON)" : "");
}

void Scene::drawStats(Canvas& c) {
    const State& s = sim_.state();
    c.tint(0x000000, 0.55f);
    const Rgb ink = 0x3B2A1E;
    const char* name = s.name[0] ? s.name : "NO NAME YET";
    if (bigText_) {
        c.fillRoundRect(40, 52, 400, 380, 20, kUiLight);
        c.text(240, 64, name, Canvas::textWidth(name, 3) <= 380 ? 3 : 2, ink, Align::Center);
        if (s.stage != Stage::Egg) c.text(240, 92, "TAP THE NAME TO CHANGE IT", 2, 0x8A7A66, Align::Center);
    } else {
        c.fillRoundRect(40, 76, 400, 312, 20, kUiLight);
        c.text(240, 92, name, Canvas::textWidth(name, 3) <= 380 ? 3 : 2, ink, Align::Center);
        if (s.stage != Stage::Egg) c.text(240, 118, "TAP THE NAME TO CHANGE IT", 1, 0x8A7A66, Align::Center);
    }

    char lines[10][40];
    int n = 0;
    Stage stage = s.stage == Stage::Away ? s.stageBeforeAway : s.stage;
    if (s.adult != AdultForm::None)
        std::snprintf(lines[n++], 40, "STAGE: %s (%s)", stageName(stage), formName(s.adult));
    else
        std::snprintf(lines[n++], 40, "STAGE: %s", stageName(stage));
    int64_t age = sim_.ageSecs();
    int days = static_cast<int>(age / 86400), hours = static_cast<int>(age % 86400 / 3600);
    std::snprintf(lines[n++], 40, "AGE: %d DAY%s %d HR%s", days, days == 1 ? "" : "S", hours, hours == 1 ? "" : "S");
    std::snprintf(lines[n++], 40, "GENERATION: %u", static_cast<unsigned>(s.generation));
    std::snprintf(lines[n++], 40, "TRUST: %s", s.stage == Stage::Away ? "AWAY" : trustName(sim_.trustLevel()));
    std::snprintf(lines[n++], 40, "PHOTOS: %d OF %d", sim_.photoCount(), Sim::kPhotoCount);
    std::snprintf(lines[n++], 40, "%s: %u", species_ == Species::Horse ? "HOOFPRINTS" : "FOOTPRINTS", static_cast<unsigned>(s.footprints));
    const Traits t = sim_.traits();
    if (species_ == Species::Horse) {
        char coat[34];
        horseCoatName(coat, sizeof coat);
        std::snprintf(lines[n++], 40, "COAT: %s", coat);
    } else if (t.variant != Variant::None) {
        std::snprintf(lines[n++], 40, "COAT: %s %s", toString(t.rarity), toString(t.variant));
    } else {
        std::snprintf(lines[n++], 40, "COAT: %s", toString(t.rarity));
    }
    char code[12];
    dnaCode(s.genome, code, sizeof code);
    if (!bigText_)  // too long to fit big, and the least needed
        std::snprintf(lines[n++], 40, trailsAvailable_ ? "DNA: %s  TRAILS: %u" : "DNA: %s", code, static_cast<unsigned>(s.trailsVisited));
    if (batteryPct_ >= 0 && !bigText_)  // the strip at the top shows it too
        std::snprintf(lines[n++], 40, "BATTERY: %d%%%s", batteryPct_, charging_ ? " (CHARGING)" : "");
    if (weather_.sky != Sky::Unknown && n < 10) {
        char sky[12];
        std::snprintf(sky, sizeof sky, "%s", toString(weather_.sky));
        for (char* p = sky; *p; p++) *p = static_cast<char>(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
        const float temp = fahrenheit_ ? weather_.tempC * 9 / 5 + 32 : weather_.tempC;
        std::snprintf(lines[n++], 40, "WEATHER: %s, %d%s", sky, static_cast<int>(std::lround(temp)), fahrenheit_ ? "F" : "C");
    }
    if (bigText_) {  // each line wrapped if it must be, the rest of it indented
        float y = 124;
        const float bottom = 396.0f;
        for (int i = 0; i < n; i++) {
            char wrapped[3][64];
            const int k = wrapText(lines[i], 3, 372, wrapped, 3);
            for (int j = 0; j < k && y + 21 <= bottom; j++, y += 27) c.text(j ? 76.0f : 54.0f, y, wrapped[j], 3, ink);
        }
        c.text(240, 410, "TAP TO CLOSE", 2, 0x8A7A66, Align::Center);
    } else {
        for (int i = 0; i < n; i++) c.text(64, 136 + i * 23.0f, lines[i], 2, ink);
        c.text(240, 366, "TAP TO CLOSE", 2, 0x8A7A66, Align::Center);
    }
}

void Scene::drawGallery(Canvas& c) {
    const State& s = sim_.state();
    c.tint(0x000000, 0.55f);
    c.fillRoundRect(16, 56, 448, 380, 20, kUiLight);
    char title[32];
    std::snprintf(title, sizeof title, "SIGHTINGS %d / %d", sim_.photoCount(), Sim::kPhotoCount);
    c.text(240, bigText_ ? 70.0f : 74.0f, title, ts(2), 0x3B2A1E, Align::Center);
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
            c.text(x + 32, y + 32 - Canvas::textHeight(ts(2)) / 2.0f, "?", ts(2), 0xA89C86, Align::Center);
        }
    }
    c.text(240, bigText_ ? 400.0f : 404.0f, "TAP TO CLOSE", ts(2), 0x8A7A66, Align::Center);
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
        case Scene::Slot::Stable: return "SITTER";
        case Scene::Slot::Back: return "BACK";
        case Scene::Slot::Berries: return "BERRIES";
        case Scene::Slot::Fish: return "FISH";
        case Scene::Slot::Roots: return "ROOTS";
    }
    return "?";
}

}  // namespace squatch
