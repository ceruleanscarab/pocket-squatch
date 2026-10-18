// Pocket Squatch on the Waveshare ESP32-S3-Touch-LCD-4B.
//
// The board's counterpart of pc/main.cpp: it owns the panel, touch, flash and the real
// clock, and hands the same sim and scene the same things the PC does. Touch events and
// milliseconds go into the scene, real seconds go into Sim::tick(), and the scene draws
// each frame into the frame buffer that isn't on screen.
#include <cstdio>
#include <cstring>
#include <ctime>

#include "audio.hpp"
#include "board.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power.hpp"
#include "radio.hpp"
#include "squatch/canvas.hpp"
#include "squatch/scene.hpp"
#include "squatch/sim.hpp"
#include "storage.hpp"
#include "walltime.hpp"
#include "wifiscan.hpp"

using namespace squatch;

namespace {

const char* TAG = "squatch";
constexpr int64_t kAutosaveSecs = 5 * 60;  // plus right after every care action
constexpr int kMaxFingers = 2;
constexpr uint32_t kBatteryEveryMs = 2000;
constexpr uint32_t kSettingsSaveAfterMs = 3000;  // after the last change, so each tap isn't a flash write
constexpr uint32_t kDarkPollMs = 50;             // how often touch is checked while the screen is off
constexpr uint32_t kKeyEveryMs = 100;            // the PWR key, asked of the power chip

// Charge currents for the battery each board is usually fitted with. The AXP2101's own
// default is over an amp, far too strong for these small cells.
#if CONFIG_SQUATCH_BOARD_AMOLED_18
constexpr int kChargeMa = 150, kTrickleMa = 25;  // pocket-tank's setting for the 1.8's ~200 mAh cell
constexpr bool kAudioRail = true;  // the codec's analog side is on the AXP2101's ALDO1
#else
constexpr bool kAudioRail = false;
constexpr int kChargeMa = 300, kTrickleMa = 50;  // gentle enough for a cell of 300 mAh or more
#endif

Sim gSim;
bool gHavePower = false;  // the AXP2101 answered
Scene gScene(gSim);

uint32_t millis() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// Noon on the build date: a stand-in "now" for a first egg when the clock is unknown.
int64_t buildDate() {
    static const char* kMonths = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {};
    int day = 1, year = 2026;
    std::sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
    const char* at = std::strstr(kMonths, mon);
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = at ? static_cast<int>(at - kMonths) / 3 : 0;
    t.tm_mday = day;
    t.tm_hour = 12;
    t.tm_isdst = -1;
    return static_cast<int64_t>(std::mktime(&t));  // local time; the time zone is set by now
}

// Turns the touch controller's fingers (each with its own track number) into the scene's
// finger 0 and 1, with Down, Move and Up events.
class Fingers {
public:
    void update(const board_touch_t* pts, int n, uint32_t ms) {
        bool matched[kMaxFingers] = {};
        for (Slot& s : slots_) {
            if (!s.down) continue;
            int i = find(pts, n, s.track);
            if (i < 0) {
                s.down = false;
                send(TouchPhase::Up, s, ms);
                continue;
            }
            matched[i] = true;
            if (pts[i].x != s.x || pts[i].y != s.y) {
                s.x = pts[i].x;
                s.y = pts[i].y;
                send(TouchPhase::Move, s, ms);
            }
        }
        for (int i = 0; i < n; i++) {
            if (matched[i]) continue;
            for (Slot& s : slots_) {
                if (s.down) continue;
                s = Slot{true, pts[i].track, pts[i].x, pts[i].y, s.id};
                send(TouchPhase::Down, s, ms);
                break;
            }
        }
    }

private:
    struct Slot {
        bool down;
        uint8_t track;
        int16_t x, y;
        uint8_t id;
    };

    static int find(const board_touch_t* pts, int n, uint8_t track) {
        for (int i = 0; i < n; i++)
            if (pts[i].track == track) return i;
        return -1;
    }

    static void send(TouchPhase ph, const Slot& s, uint32_t ms) { gScene.touch(TouchEvent{ph, s.id, s.x, s.y, ms}); }

    Slot slots_[kMaxFingers] = {{false, 0, 0, 0, 0}, {false, 0, 0, 0, 1}};
};

void showMessage(const char* text) {
    Canvas c(board_draw_buffer(), BOARD_WIDTH, BOARD_HEIGHT);
    c.clear(0x0B1026);
    c.text(BOARD_WIDTH / 2, BOARD_HEIGHT / 2 - 7, text, 2, 0xF3EBD9, Align::Center);
    board_present();
    board_backlight(gScene.brightnessPercent());
}

// Load the pet and catch up on the time the board was off, or lay a first egg.
void startPet() {
    State saved;
    if (storage::loadPet(saved)) {
        if (!walltime::known()) {
            // The RTC lost the time and the network didn't help: carry on from the save
            // rather than from 1970. A later network sync moves the clock forward.
            walltime::assume(saved.now);
            ESP_LOGW(TAG, "clock unknown: resuming from the save's time");
        }
        gSim.mutableState() = saved;
        gSim.mutableState().tzOffset = walltime::tzOffset();
        int64_t now = time(nullptr);
        if (now > saved.now) gSim.tick(now - saved.now);
        ESP_LOGI(TAG, "loaded: %s, generation %u", toString(gSim.state().stage),
                 static_cast<unsigned>(gSim.state().generation));
        return;
    }

    // A new egg should hatch on the real date, so give the network a moment to set the clock.
    if (!walltime::known() && CONFIG_SQUATCH_WIFI_SSID[0]) {
        showMessage("SETTING THE CLOCK");
        for (int i = 0; i < 300 && !walltime::known(); i++) vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!walltime::known()) {
        walltime::assume(buildDate());
        ESP_LOGW(TAG, "clock unknown: starting the egg on the build date");
    }
    uint64_t seed = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
    gSim.newGame(time(nullptr), walltime::tzOffset(), seed);
    storage::savePet(gSim.state());
    ESP_LOGI(TAG, "a new egg");
}

void gameLoop(void*) {
    Fingers fingers;
    int64_t lastWall = time(nullptr);
    int64_t lastSave = lastWall, lastTz = lastWall;
    uint32_t statsFrom = millis();
    uint32_t frames = 0, drawMs = 0;
    uint32_t lastBattery = 0;
    power::Battery battery{false, -1, false, false};
    int lit = -1;                // the panel's brightness now, in percent
    uint32_t settingsDirty = 0;  // when the settings last changed and weren't saved yet
    uint32_t lastKey = 0;

    for (;;) {
        const uint32_t ms = millis();

        board_touch_t pts[kMaxFingers];
        fingers.update(pts, board_read_touch(pts, kMaxFingers), ms);

        // Real seconds into the sim. If the clock is set backwards, the pet just waits.
        const int64_t wall = time(nullptr);
        if (wall > lastWall) gSim.tick(wall - lastWall);
        lastWall = wall;
        if (wall - lastTz >= 60) {  // daylight saving changes
            gSim.mutableState().tzOffset = walltime::tzOffset();
            lastTz = wall;
        }

        // The trail map asked for a Wi-Fi scan; its results arrive a couple of seconds later.
        if (gScene.takeScanRequest()) wifiscan::request();
        squatch::Sighting heard[20];
        size_t heardCount = 0;
        if (wifiscan::take(heard, 20, heardCount)) gSim.scoutTrails(heard, heardCount);

        if (gHavePower && (lastBattery == 0 || ms - lastBattery >= kBatteryEveryMs)) {
            lastBattery = ms ? ms : 1;
            if (power::read(battery)) gScene.setBattery(battery.present ? battery.percent : -1, battery.charging);
        }

        // PWR key: a press turns the screen off or on; a 1.5 s hold saves and switches off.
        if (gHavePower && ms - lastKey >= kKeyEveryMs) {
            lastKey = ms;
            switch (power::pollKey()) {
                case power::Key::Short:
                    if (gScene.screenOff()) gScene.wake();
                    else gScene.screenOffNow();
                    break;
                case power::Key::Long:
                    storage::savePet(gSim.state());
                    if (settingsDirty) storage::saveSettings(gScene.settings());
                    ESP_LOGI(TAG, "power key held: saved, switching off");
                    showMessage("SEE YOU SOON");
                    vTaskDelay(pdMS_TO_TICKS(1200));
                    board_backlight(0);
                    power::powerOff();
                    vTaskDelay(pdMS_TO_TICKS(2000));  // on USB the board stays on: carry on
                    lit = -1;
                    break;
                case power::Key::None: break;
            }
        }

        gScene.update(ms);
        for (Cue c; gScene.takeCue(c);) audio::play(c, gScene.volume());
        if (gScene.takeSaveRequest() || wall - lastSave >= kAutosaveSecs) {
            storage::savePet(gSim.state());
            lastSave = wall;
        }
        if (gScene.takeSettingsChanged()) settingsDirty = ms ? ms : 1;
        if (settingsDirty && ms - settingsDirty >= kSettingsSaveAfterMs) {
            storage::saveSettings(gScene.settings());
            settingsDirty = 0;
        }

        // Screen off: dark panel, no drawing, and touch checked a little less often.
        // The pet's time, the battery and saves all carry on above.
        if (gScene.screenOff()) {
            if (lit != 0) {
                board_backlight(0);
                lit = 0;
                ESP_LOGI(TAG, "screen off");
            }
            vTaskDelay(pdMS_TO_TICKS(kDarkPollMs));
            statsFrom = millis();
            frames = drawMs = 0;
            continue;
        }

        const uint32_t t0 = millis();
        Canvas canvas(board_draw_buffer(), BOARD_WIDTH, BOARD_HEIGHT);
        gScene.draw(canvas);
        drawMs += millis() - t0;
        board_present();

        ++frames;
        if (lit != gScene.brightnessPercent()) {  // after a fresh picture, so waking never shows a stale one
            if (lit == 0) ESP_LOGI(TAG, "screen on");
            lit = gScene.brightnessPercent();
            board_backlight(lit);
        }
        if (ms - statsFrom >= 10000) {
            if (battery.present)
                ESP_LOGI(TAG, "%.1f fps, %lu ms per draw, battery %d%%%s", frames * 1000.0f / (ms - statsFrom),
                         static_cast<unsigned long>(drawMs / frames), battery.percent,
                         battery.charging ? " (charging)" : (battery.usb ? " (on USB)" : ""));
            else
                ESP_LOGI(TAG, "%.1f fps, %lu ms per draw, no battery", frames * 1000.0f / (ms - statsFrom),
                         static_cast<unsigned long>(drawMs / frames));
            statsFrom = ms;
            frames = drawMs = 0;
        }
    }
}

}  // namespace

extern "C" void app_main(void) {
    storage::init();
    Scene::Settings settings;
    if (storage::loadSettings(settings)) gScene.setSettings(settings);
    ESP_ERROR_CHECK(board_init());
    gHavePower = power::init(board_i2c(), kChargeMa, kTrickleMa);
    if (gHavePower) power::keyInit();
    audio::init(board_i2c(), kAudioRail && gHavePower);
    radio::begin();
    walltime::init(board_i2c(), CONFIG_SQUATCH_TZ);
    walltime::syncFromNetwork(CONFIG_SQUATCH_WIFI_SSID, CONFIG_SQUATCH_WIFI_PASSWORD);

    startPet();
    xTaskCreatePinnedToCore(gameLoop, "game", 8192, nullptr, 5, nullptr, 1);
}
