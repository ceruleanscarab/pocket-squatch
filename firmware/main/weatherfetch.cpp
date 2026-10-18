#include "weatherfetch.hpp"

#include <atomic>
#include <cstdio>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "netjoin.hpp"
#include "radio.hpp"
#include "sdkconfig.h"

namespace weatherfetch {

namespace {

const char* TAG = "weather";
constexpr int kEveryMin = 60;
constexpr int kFirstAfterS = 45;  // after the clock sync at start-up has had the radio

std::atomic<bool> gReady{false};
squatch::Weather gLatest;
char gBody[2048];

#if CONFIG_SQUATCH_WEATHER
bool fetch(squatch::Weather& out) {
    char url[200];
    std::snprintf(url, sizeof url,
                  "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
                  "&current=temperature_2m,weather_code,wind_speed_10m",
                  CONFIG_SQUATCH_WEATHER_LAT, CONFIG_SQUATCH_WEATHER_LON);
    esp_http_client_config_t cfg{};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 10000;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;
    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int n = 0, got;
        while (n < static_cast<int>(sizeof gBody) - 1 &&
               (got = esp_http_client_read(client, gBody + n, sizeof gBody - 1 - n)) > 0)
            n += got;
        gBody[n] = '\0';
        const int status = esp_http_client_get_status_code(client);
        ok = status == 200 && squatch::parseOpenMeteo(gBody, out);
        if (!ok) ESP_LOGW(TAG, "no weather (HTTP %d)", status);
    } else {
        ESP_LOGW(TAG, "couldn't reach the weather service");
    }
    esp_http_client_cleanup(client);
    return ok;
}

void weatherTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(kFirstAfterS * 1000));
    for (;;) {
        radio::take();
        squatch::Weather w;
        bool ok = netjoin::join(CONFIG_SQUATCH_WIFI_SSID, CONFIG_SQUATCH_WIFI_PASSWORD) && fetch(w);
        netjoin::leave();
        radio::give();
        if (ok) {
            gLatest = w;
            gReady = true;
            ESP_LOGI(TAG, "%s, %.1f C, wind %.0f km/h", squatch::toString(w.sky), w.tempC, w.windKmh);
        }
        vTaskDelay(pdMS_TO_TICKS((ok ? kEveryMin : 15) * 60 * 1000));  // a failure tries again sooner
    }
}
#endif

}  // namespace

void start() {
#if CONFIG_SQUATCH_WEATHER
    if (!CONFIG_SQUATCH_WIFI_SSID[0] || !CONFIG_SQUATCH_WEATHER_LAT[0] || !CONFIG_SQUATCH_WEATHER_LON[0]) {
        ESP_LOGI(TAG, "no location or Wi-Fi set (menuconfig: Pocket Squatch): no weather");
        return;
    }
    xTaskCreatePinnedToCore(weatherTask, "weather", 6144, nullptr, 3, nullptr, 0);
#endif
}

bool take(squatch::Weather& out) {
    if (!gReady) return false;
    out = gLatest;
    gReady = false;
    return true;
}

}  // namespace weatherfetch
