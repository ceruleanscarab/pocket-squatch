#include "storage.hpp"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "squatch/save.hpp"

namespace storage {

namespace {
const char* TAG = "storage";
const char* kNamespace = "squatch";
const char* kKey = "pet";
const char* kSettingsKey = "settings";
const char* kWifiSsidKey = "wifi_ssid";
const char* kWifiPassKey = "wifi_pass";
const char* kLocationKey = "location";
const char* kTownWishKey = "town_wish";
constexpr uint8_t kLocationVersion = 1;
constexpr uint8_t kSettingsVersion = 4;  // 1 had no volume, 2 no reindeer nose, 3 no glasses
}  // namespace

void init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // The NVS layout itself changed (not our save format): start it fresh.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

bool loadPet(squatch::State& out) {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
    static uint8_t buf[squatch::kSaveBytes];
    size_t len = sizeof buf;
    esp_err_t err = nvs_get_blob(h, kKey, buf, &len);
    nvs_close(h);
    if (err != ESP_OK) return false;
    squatch::LoadResult r = squatch::load(buf, len, out);
    if (r != squatch::LoadResult::Ok) ESP_LOGW(TAG, "save not loaded: %s", squatch::toString(r));
    return r == squatch::LoadResult::Ok;
}

void savePet(const squatch::State& s) {
    static uint8_t buf[squatch::kSaveBytes];
    size_t len = squatch::save(s, buf, sizeof buf);
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, kKey, buf, len) != ESP_OK || nvs_commit(h) != ESP_OK) ESP_LOGW(TAG, "save failed");
    nvs_close(h);
}

bool loadSettings(squatch::Scene::Settings& out) {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t b[6] = {};
    size_t len = sizeof b;
    esp_err_t err = nvs_get_blob(h, kSettingsKey, b, &len);
    nvs_close(h);
    if (err != ESP_OK || len < 3) return false;
    if (!((b[0] == 1 && len == 3) || (b[0] == 2 && len == 4) || (b[0] == 3 && len == 5) ||
          (b[0] == kSettingsVersion && len == 6)))
        return false;
    out.brightness = b[1];
    out.timeout = b[2];
    if (b[0] >= 2) out.volume = b[3];  // version 1: the default volume
    if (b[0] >= 3) out.rudolph = b[4];
    return true;
}

void saveSettings(const squatch::Scene::Settings& s) {
    const uint8_t b[6] = {kSettingsVersion, s.brightness, s.timeout, s.volume, s.rudolph, 0};  // [5] was the glasses
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, kSettingsKey, b, sizeof b) != ESP_OK || nvs_commit(h) != ESP_OK)
        ESP_LOGW(TAG, "settings not saved");
    nvs_close(h);
}

bool loadWifi(char* ssid, size_t ssidLen, char* password, size_t passwordLen) {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
    size_t a = ssidLen, b = passwordLen;
    const bool ok = nvs_get_str(h, kWifiSsidKey, ssid, &a) == ESP_OK && nvs_get_str(h, kWifiPassKey, password, &b) == ESP_OK;
    nvs_close(h);
    return ok && ssid[0];
}

void saveWifi(const char* ssid, const char* password) {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_str(h, kWifiSsidKey, ssid) != ESP_OK || nvs_set_str(h, kWifiPassKey, password) != ESP_OK ||
        nvs_commit(h) != ESP_OK)
        ESP_LOGW(TAG, "Wi-Fi not saved");
    nvs_close(h);
}

bool loadLocation(Location& out) {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t b[1 + sizeof(Location)] = {};
    size_t len = sizeof b;
    const esp_err_t err = nvs_get_blob(h, kLocationKey, b, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof b || b[0] != kLocationVersion) return false;
    std::memcpy(&out, b + 1, sizeof out);
    out.zone[sizeof out.zone - 1] = out.name[sizeof out.name - 1] = '\0';
    return true;
}

void saveLocation(const Location& l) {
    uint8_t b[1 + sizeof(Location)];
    b[0] = kLocationVersion;
    std::memcpy(b + 1, &l, sizeof l);
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, kLocationKey, b, sizeof b) != ESP_OK || nvs_commit(h) != ESP_OK) ESP_LOGW(TAG, "town not saved");
    nvs_close(h);
}

bool wifiCredentials(char* ssid, size_t ssidLen, char* password, size_t passwordLen) {
    if (loadWifi(ssid, ssidLen, password, passwordLen)) return true;
    std::snprintf(ssid, ssidLen, "%s", CONFIG_SQUATCH_WIFI_SSID);
    std::snprintf(password, passwordLen, "%s", CONFIG_SQUATCH_WIFI_PASSWORD);
    return ssid[0] != '\0';
}

void keepBuiltInDefaults() {
    {  // not after "like new": that's meant to start with nothing
        nvs_handle_t h;
        uint8_t v = 0;
        if (nvs_open(kNamespace, NVS_READONLY, &h) == ESP_OK) {
            nvs_get_u8(h, "nodefaults", &v);
            nvs_close(h);
        }
        if (v) return;
    }
    char ssid[33], pass[65];
    if (!loadWifi(ssid, sizeof ssid, pass, sizeof pass) && CONFIG_SQUATCH_WIFI_SSID[0])
        saveWifi(CONFIG_SQUATCH_WIFI_SSID, CONFIG_SQUATCH_WIFI_PASSWORD);
#if CONFIG_SQUATCH_WEATHER
    Location here;
    char wish[64];
    if (CONFIG_SQUATCH_TOWN[0] && !loadLocation(here) && !townToFind(wish, sizeof wish)) {
        nvs_handle_t h;
        if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
        if (nvs_set_str(h, kTownWishKey, CONFIG_SQUATCH_TOWN) != ESP_OK || nvs_commit(h) != ESP_OK) ESP_LOGW(TAG, "town not kept");
        nvs_close(h);
    }
#endif
}

bool townToFind(char* out, size_t n) {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = n;
    const bool ok = nvs_get_str(h, kTownWishKey, out, &len) == ESP_OK && out[0];
    nvs_close(h);
    return ok;
}

bool welcomed() {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t v = 0;
    const bool got = nvs_get_u8(h, "welcomed", &v) == ESP_OK;
    nvs_close(h);
    return got && v;
}

void setWelcomed() {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "welcomed", 1);
    nvs_commit(h);
    nvs_close(h);
}

void eraseAll() {
    nvs_flash_deinit();
    if (nvs_flash_erase() != ESP_OK || nvs_flash_init() != ESP_OK) {
        ESP_LOGE(TAG, "couldn't erase");
        return;
    }
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "nodefaults", 1);
    nvs_commit(h);
    nvs_close(h);
}

}  // namespace storage
