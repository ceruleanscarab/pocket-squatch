#include "wifiscan.hpp"

#include <atomic>
#include <cstring>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "radio.hpp"
#include "sdkconfig.h"

namespace wifiscan {

namespace {

constexpr int kMaxNetworks = 20;

std::atomic<bool> gReady{false};  // results are waiting to be taken
squatch::Sighting gResults[kMaxNetworks];
size_t gCount = 0;

#if CONFIG_SQUATCH_TRAILS
const char* TAG = "wifiscan";
std::atomic<bool> gBusy{false};           // a scan task is running
wifi_ap_record_t gRecords[kMaxNetworks];  // static: too big for the task's stack

void scanTask(void*) {
    radio::take();  // waits if the clock is being set
    esp_netif_t* netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    bool ok = esp_wifi_init(&init) == ESP_OK;
    ok = ok && esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK;
    ok = ok && esp_wifi_start() == ESP_OK;

    wifi_scan_config_t cfg{};
    cfg.show_hidden = false;
    ok = ok && esp_wifi_scan_start(&cfg, true) == ESP_OK;  // blocks this task, about two seconds

    uint16_t n = kMaxNetworks;
    ok = ok && esp_wifi_scan_get_ap_records(&n, gRecords) == ESP_OK;
    if (ok) {
        size_t count = 0;
        for (int i = 0; i < n; i++) {
            const char* ssid = reinterpret_cast<const char*>(gRecords[i].ssid);
            if (!ssid[0]) continue;  // hidden networks aren't places
            gResults[count++] = squatch::Sighting{squatch::trailHash(ssid), gRecords[i].rssi};
        }
        gCount = count;
        gReady = true;
        ESP_LOGI(TAG, "heard %u networks", static_cast<unsigned>(count));
    } else {
        ESP_LOGW(TAG, "scan failed");
    }

    // Switch Wi-Fi off again to save the battery.
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(netif);
    radio::give();
    gBusy = false;
    vTaskDelete(nullptr);
}
#endif  // CONFIG_SQUATCH_TRAILS

}  // namespace

void request() {
#if CONFIG_SQUATCH_TRAILS
    bool expected = false;
    if (!gBusy.compare_exchange_strong(expected, true)) return;
    gReady = false;
    if (xTaskCreatePinnedToCore(scanTask, "scan", 4096, nullptr, 3, nullptr, 0) != pdPASS) gBusy = false;
#endif
}

bool take(squatch::Sighting* out, size_t cap, size_t& n) {
    if (!gReady) return false;
    n = gCount < cap ? gCount : cap;
    std::memcpy(out, gResults, n * sizeof(squatch::Sighting));
    gReady = false;
    return true;
}

}  // namespace wifiscan
