#include "ota.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "netjoin.hpp"
#include "radio.hpp"
#include "sdkconfig.h"
#include "storage.hpp"

namespace ota {
namespace {

const char* TAG = "ota";
std::atomic<State> gState{State::Off};
std::atomic<int> gPercent{0};
std::atomic<bool> gBusy{false};
int gLastNight = -1;  // the day of the year it last checked at night

// This edition and board's build, in the release: e.g. pony-amoled18.bin.
void fileName(char* out, size_t n) {
#if CONFIG_SQUATCH_PET_HORSE
    const char* pet = "pony";
#else
    const char* pet = "squatch";
#endif
#if CONFIG_SQUATCH_BOARD_AMOLED_18
    const char* board = "amoled18";
#elif CONFIG_SQUATCH_BOARD_AMOLED_175
    const char* board = "amoled175";
#else
    const char* board = "lcd4b";
#endif
    std::snprintf(out, n, "%s-%s.bin", pet, board);
}

void markHealthy(void*) {
    // It has run a minute: keep it. (Until now a crash and restart would go back to the old one.)
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) ESP_LOGI(TAG, "build %d marked good", build());
}

void work(void*) {
#if CONFIG_SQUATCH_OTA
    gState = State::Checking;
    gPercent = 0;
    char url[200], file[40];
    fileName(file, sizeof file);
    std::snprintf(url, sizeof url, "%s%s", CONFIG_SQUATCH_OTA_URL, file);
    State result = State::Failed;
    radio::take();
    char ssid[33], pass[65];
    if (storage::wifiCredentials(ssid, sizeof ssid, pass, sizeof pass) && netjoin::join(ssid, pass)) {
        esp_http_client_config_t http = {};
        http.url = url;
        http.crt_bundle_attach = esp_crt_bundle_attach;  // GitHub, then where it redirects the download
        http.timeout_ms = 20000;
        http.buffer_size = 4096;
        http.buffer_size_tx = 2048;  // the redirect's address is long
        http.keep_alive_enable = true;
        esp_https_ota_config_t cfg = {};
        cfg.http_config = &http;
        esp_https_ota_handle_t h = nullptr;
        if (esp_https_ota_begin(&cfg, &h) == ESP_OK) {
            esp_app_desc_t next;
            const esp_app_desc_t* now = esp_app_get_description();
            if (esp_https_ota_get_img_desc(h, &next) != ESP_OK || std::strcmp(next.project_name, now->project_name) != 0) {
                ESP_LOGW(TAG, "%s isn't a build of this project", url);
                esp_https_ota_abort(h);
            } else if (std::atoi(next.version) <= std::atoi(now->version)) {
                ESP_LOGI(TAG, "up to date (build %s; the release has %s)", now->version, next.version);
                esp_https_ota_abort(h);
                result = State::UpToDate;
            } else {
                ESP_LOGI(TAG, "updating from build %s to %s", now->version, next.version);
                gState = State::Downloading;
                const int size = esp_https_ota_get_image_size(h);
                esp_err_t err;
                while ((err = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                    if (size > 0) gPercent = static_cast<int>(100LL * esp_https_ota_get_image_len_read(h) / size);
                    vTaskDelay(1);  // let the game draw
                }
                if (err == ESP_OK && esp_https_ota_is_complete_data_received(h) && esp_https_ota_finish(h) == ESP_OK) {
                    ESP_LOGI(TAG, "build %s installed; restarting into it", next.version);
                    result = State::Restarting;
                } else {
                    ESP_LOGW(TAG, "update failed: %s", esp_err_to_name(err));
                    esp_https_ota_abort(h);
                }
            }
        } else {
            ESP_LOGW(TAG, "couldn't fetch %s", url);
        }
    }
    netjoin::leave();
    radio::give();
    gPercent = result == State::Restarting ? 100 : 0;
    gState = result;
#endif
    gBusy = false;
    vTaskDelete(nullptr);
}

}  // namespace

void begin() {
#if CONFIG_SQUATCH_OTA
    gState = State::Idle;
    ESP_LOGI(TAG, "build %d", build());
    esp_ota_img_states_t st;
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        const esp_timer_create_args_t args = {.callback = markHealthy, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
                                              .name = "ota-ok", .skip_unhandled_events = false};
        esp_timer_handle_t t;
        if (esp_timer_create(&args, &t) == ESP_OK) esp_timer_start_once(t, 60ULL * 1000 * 1000);
    }
#endif
}

void checkNow() {
#if CONFIG_SQUATCH_OTA
    if (gBusy.exchange(true)) return;
    if (xTaskCreatePinnedToCore(work, "ota", 8192, nullptr, 3, nullptr, 0) != pdPASS) gBusy = false;
#endif
}

void tick(bool screenOff, bool batteryOk) {
#if CONFIG_SQUATCH_OTA
    const time_t now = time(nullptr);
    if (now < 1700000000) return;  // the clock isn't set yet
    struct tm local;
    localtime_r(&now, &local);
    if (local.tm_hour != 3 || local.tm_yday == gLastNight || !screenOff || !batteryOk) return;
    gLastNight = local.tm_yday;
    checkNow();
#else
    (void)screenOff;
    (void)batteryOk;
#endif
}

State state() { return gState; }
int percent() { return gPercent; }
int build() { return std::atoi(esp_app_get_description()->version); }
bool restartDue() { return gState == State::Restarting; }

void keep() {
#if CONFIG_SQUATCH_OTA
    esp_ota_img_states_t st;
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) markHealthy(nullptr);
#endif
}

}  // namespace ota
