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
constexpr uint8_t kLocationVersion = 1;
constexpr uint8_t kSettingsVersion = 3;  // 1 had no volume, 2 no reindeer nose
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
    uint8_t b[5] = {};
    size_t len = sizeof b;
    esp_err_t err = nvs_get_blob(h, kSettingsKey, b, &len);
    nvs_close(h);
    if (err != ESP_OK || len < 3) return false;
    if (!((b[0] == 1 && len == 3) || (b[0] == 2 && len == 4) || (b[0] == kSettingsVersion && len == 5))) return false;
    out.brightness = b[1];
    out.timeout = b[2];
    if (b[0] >= 2) out.volume = b[3];  // version 1: the default volume
    if (b[0] >= 3) out.rudolph = b[4];
    return true;
}

void saveSettings(const squatch::Scene::Settings& s) {
    const uint8_t b[5] = {kSettingsVersion, s.brightness, s.timeout, s.volume, s.rudolph};
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

}  // namespace storage
