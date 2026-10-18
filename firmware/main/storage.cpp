#include "storage.hpp"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "squatch/save.hpp"

namespace storage {

namespace {
const char* TAG = "storage";
const char* kNamespace = "squatch";
const char* kKey = "pet";
const char* kSettingsKey = "settings";
constexpr uint8_t kSettingsVersion = 2;  // 1 had no volume
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
    uint8_t b[4] = {};
    size_t len = sizeof b;
    esp_err_t err = nvs_get_blob(h, kSettingsKey, b, &len);
    nvs_close(h);
    if (err != ESP_OK || len < 3) return false;
    if (!((b[0] == 1 && len == 3) || (b[0] == kSettingsVersion && len == 4))) return false;
    out.brightness = b[1];
    out.timeout = b[2];
    if (b[0] >= 2) out.volume = b[3];  // version 1: the default volume
    return true;
}

void saveSettings(const squatch::Scene::Settings& s) {
    const uint8_t b[4] = {kSettingsVersion, s.brightness, s.timeout, s.volume};
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, kSettingsKey, b, sizeof b) != ESP_OK || nvs_commit(h) != ESP_OK)
        ESP_LOGW(TAG, "settings not saved");
    nvs_close(h);
}

}  // namespace storage
