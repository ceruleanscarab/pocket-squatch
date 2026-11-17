#include "cloud.hpp"

#include <atomic>
#include <mutex>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "netjoin.hpp"
#include "nvs.h"
#include "radio.hpp"
#include "sdkconfig.h"
#include "squatch/save.hpp"
#include "storage.hpp"

namespace cloud {
namespace {

const char* TAG = "cloud";
const char* kNamespace = "squatch";
const char* kCodeKey = "bk_code";
constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";  // Crockford's: no I, L, O or U

std::atomic<bool> gBusy{false};
uint8_t gOut[squatch::kSaveBytes];
size_t gOutLen = 0;
std::atomic<int> gLastNight{-1};  // the day (tm_yday) whose nightly backup got through
std::atomic<int> gNightTry{-1};    // the day the backup in progress is for (-1: not a nightly one)
int gLastAttempt = -1;             // day * 24 + hour of the last nightly try
std::mutex gSnapLock;              // the pet as of the last tick, for backupJoined
uint8_t gSnap[squatch::kSaveBytes];
size_t gSnapLen = 0;
squatch::State gRestored;
std::atomic<bool> gHaveRestored{false};
std::atomic<bool> gPinged{false};
std::atomic<int64_t> gLastPingCheckUs{-1};
constexpr int64_t kPingEveryUs = 60LL * 60 * 1000000;

#ifdef CONFIG_SQUATCH_CLOUD_URL
const char* kUrl = CONFIG_SQUATCH_CLOUD_URL;
#else
const char* kUrl = "";
#endif

bool loadCode(char out[17]) {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 17;
    const bool ok = nvs_get_str(h, kCodeKey, out, &len) == ESP_OK && std::strlen(out) == 16;
    nvs_close(h);
    return ok;
}

void saveCode(const char* code16) {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_str(h, kCodeKey, code16) != ESP_OK || nvs_commit(h) != ESP_OK) ESP_LOGW(TAG, "code not kept");
    nvs_close(h);
}

void rawCode(char out[17]) {
    if (loadCode(out)) return;
    for (int i = 0; i < 16; i++) out[i] = kAlphabet[esp_random() % 32];
    out[16] = '\0';
    saveCode(out);
}

// The Worker's ping address for a code: the backups address with save/ swapped for ping/.
bool pingUrl(const char* code16, char* url, size_t n) {
    const size_t len = std::strlen(kUrl);
    if (len < 5 || std::strcmp(kUrl + len - 5, "save/") != 0) return false;
    std::snprintf(url, n, "%.*sping/%s", static_cast<int>(len - 5), kUrl, code16);
    return true;
}

// One request to the Worker for this code: PUT (body) or GET (into buf). Joined already.
int request(const char* code16, bool put, const uint8_t* body, size_t bodyLen, uint8_t* buf, size_t cap, size_t& got,
            const char* fullUrl = nullptr) {
    char url[200];
    if (fullUrl) std::snprintf(url, sizeof url, "%s", fullUrl);
    else std::snprintf(url, sizeof url, "%s%s", kUrl, code16);
    esp_http_client_config_t cfg{};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 15000;
    cfg.method = put ? HTTP_METHOD_PUT : HTTP_METHOD_GET;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -1;
    int status = -1;
    got = 0;
    if (esp_http_client_open(client, put ? static_cast<int>(bodyLen) : 0) == ESP_OK) {
        if (put) esp_http_client_write(client, reinterpret_cast<const char*>(body), static_cast<int>(bodyLen));
        esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (!put) {
            int n;
            while (got < cap && (n = esp_http_client_read(client, reinterpret_cast<char*>(buf) + got, cap - got)) > 0)
                got += static_cast<size_t>(n);
        }
    }
    esp_http_client_cleanup(client);
    return status;
}

// Its own hourly check, for boards with no weather (no town set): skipped whenever the weather
// task has checked within the hour.
void pingTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(3 * 60 * 1000));
    for (;;) {
        const int64_t last = gLastPingCheckUs.load();
        if (last < 0 || esp_timer_get_time() - last >= kPingEveryUs) {
            char ssid[33], pass[65];
            if (storage::wifiCredentials(ssid, sizeof ssid, pass, sizeof pass)) {
                radio::take();
                if (netjoin::join(ssid, pass)) {
                    checkPingJoined();
                    backupJoined();
                }
                netjoin::leave();
                radio::give();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10 * 60 * 1000));
    }
}

void backupTask(void*) {
    char code16[17];
    rawCode(code16);
    radio::take();
    char ssid[33], pass[65];
    int status = -1;
    if (storage::wifiCredentials(ssid, sizeof ssid, pass, sizeof pass) && netjoin::join(ssid, pass)) {
        size_t got;
        char url[200];
        std::snprintf(url, sizeof url, "%s%s?daily=1", kUrl, code16);  // the night's copy is the day's keepsake
        status = request(code16, true, gOut, gOutLen, nullptr, 0, got, gNightTry >= 0 ? url : nullptr);
    }
    netjoin::leave();
    radio::give();
    if (status == 200) {
        ESP_LOGI(TAG, "backed up (%u bytes)", static_cast<unsigned>(gOutLen));
        if (gNightTry >= 0) gLastNight = gNightTry.load();
    } else {
        ESP_LOGW(TAG, "backup failed (HTTP %d)%s", status, gNightTry >= 0 ? ": trying again in an hour" : "");
    }
    gNightTry = -1;
    gBusy = false;
    vTaskDelete(nullptr);
}

}  // namespace

bool enabled() { return kUrl[0] != '\0'; }

void code(char* out, size_t n) {
    char c[17];
    rawCode(c);
    std::snprintf(out, n, "%.4s-%.4s-%.4s-%.4s", c, c + 4, c + 8, c + 12);
}

bool tidy(const char* typed, char out[17]) {
    int n = 0;
    for (const char* p = typed; *p; p++) {
        char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
        if (ch == ' ' || ch == '-' || ch == '+') continue;  // ('+' is a space, from a form)
        if (ch == 'O') ch = '0';
        if (ch == 'I' || ch == 'L') ch = '1';
        if (!std::strchr(kAlphabet, ch) || n == 16) return false;
        out[n++] = ch;
    }
    out[n] = '\0';
    return n == 16;
}

void backupNow(const squatch::State& s) {
    if (!enabled() || gBusy.exchange(true)) return;
    gOutLen = squatch::save(s, gOut, sizeof gOut);
    if (xTaskCreatePinnedToCore(backupTask, "backup", 6144, nullptr, 3, nullptr, 0) != pdPASS) gBusy = false;
}

void tick(const squatch::State& s, bool batteryOk) {
    if (!enabled()) return;
    {
        std::lock_guard<std::mutex> lock(gSnapLock);
        gSnapLen = squatch::save(s, gSnap, sizeof gSnap);
    }
    const time_t now = time(nullptr);
    if (now < 1700000000) return;  // the clock isn't set yet
    struct tm local;
    localtime_r(&now, &local);
    // About 2 am; if that one didn't get through, again at 3 and at 4. One try an hour.
    const int attempt = local.tm_yday * 24 + local.tm_hour;
    if (local.tm_hour < 2 || local.tm_hour > 4 || local.tm_yday == gLastNight || attempt == gLastAttempt || !batteryOk)
        return;
    if (gBusy) return;  // another backup is going: try again next minute
    gLastAttempt = attempt;
    gNightTry = local.tm_yday;
    backupNow(s);
}

void backupJoined() {
    if (!enabled() || gBusy.exchange(true)) return;
    {
        std::lock_guard<std::mutex> lock(gSnapLock);
        if (gSnapLen == 0) {
            gBusy = false;
            return;
        }
        std::memcpy(gOut, gSnap, gSnapLen);
        gOutLen = gSnapLen;
    }
    char code16[17];
    rawCode(code16);
    size_t got;
    const int status = request(code16, true, gOut, gOutLen, nullptr, 0, got);
    if (status == 200) ESP_LOGI(TAG, "hourly backup (%u bytes)", static_cast<unsigned>(gOutLen));
    else ESP_LOGW(TAG, "hourly backup failed (HTTP %d)", status);
    gBusy = false;
}

bool fetchJoined(const char* code16, squatch::State& out) {
    if (!enabled()) return false;
    static uint8_t buf[squatch::kSaveBytes + 64];
    size_t got = 0;
    const int status = request(code16, false, nullptr, 0, buf, sizeof buf, got);
    if (status != 200) {
        ESP_LOGW(TAG, "no backup for that code (HTTP %d)", status);
        return false;
    }
    const squatch::LoadResult r = squatch::load(buf, got, out);
    if (r != squatch::LoadResult::Ok) ESP_LOGW(TAG, "backup not loaded: %s", squatch::toString(r));
    return r == squatch::LoadResult::Ok;
}

void offerRestored(const squatch::State& s, const char* code16) {
    gRestored = s;
    saveCode(code16);  // from now on this board backs up to that pet's code
    gHaveRestored = true;
}

bool takeRestored(squatch::State& out) {
    if (!gHaveRestored.exchange(false)) return false;
    out = gRestored;
    return true;
}

void checkPingJoined() {
    if (!enabled()) return;
    gLastPingCheckUs = esp_timer_get_time();
    char code16[17], url[200];
    rawCode(code16);
    if (!pingUrl(code16, url, sizeof url)) return;
    uint8_t buf[8];
    size_t got = 0;
    const int status = request(code16, false, nullptr, 0, buf, sizeof buf, got, url);
    if (status == 200 && got >= 4 && std::memcmp(buf, "ring", 4) == 0) {
        ESP_LOGW(TAG, "pinged: calling out so it can be found");
        gPinged = true;
    } else {
        ESP_LOGI(TAG, "no ping (HTTP %d)", status);
    }
}

void startPingChecks() {  // does nothing without a backups address, or once running
    static bool started = false;
    if (started || !enabled()) return;
    started = true;
    xTaskCreatePinnedToCore(pingTask, "ping", 6144, nullptr, 3, nullptr, 0);
}

bool takePing() { return gPinged.exchange(false); }

}  // namespace cloud
