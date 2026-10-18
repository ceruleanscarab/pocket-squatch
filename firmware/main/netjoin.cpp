#include "netjoin.hpp"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace netjoin {

namespace {

const char* TAG = "net";
constexpr EventBits_t kGotIp = 1, kGaveUp = 2;
EventGroupHandle_t gNet = nullptr;
int gRetries = 0;
esp_netif_t* gNetif = nullptr;
esp_event_handler_instance_t gWifiHandler, gIpHandler;

void onNetEvent(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (++gRetries < 5) esp_wifi_connect();
        else xEventGroupSetBits(gNet, kGaveUp);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(gNet, kGotIp);
    }
}

}  // namespace

bool join(const char* ssid, const char* password) {
    if (!gNet) gNet = xEventGroupCreate();
    xEventGroupClearBits(gNet, kGotIp | kGaveUp);
    gRetries = 0;
    gNetif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onNetEvent, nullptr, &gWifiHandler);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onNetEvent, nullptr, &gIpHandler);

    wifi_config_t cfg{};
    std::strncpy(reinterpret_cast<char*>(cfg.sta.ssid), ssid, sizeof cfg.sta.ssid);
    std::strncpy(reinterpret_cast<char*>(cfg.sta.password), password ? password : "", sizeof cfg.sta.password);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_start();

    EventBits_t bits = xEventGroupWaitBits(gNet, kGotIp | kGaveUp, pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    if (!(bits & kGotIp)) ESP_LOGW(TAG, "couldn't join Wi-Fi \"%s\"", ssid);
    return bits & kGotIp;
}

void leave() {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, gWifiHandler);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, gIpHandler);
    esp_wifi_stop();
    esp_wifi_deinit();
    if (gNetif) esp_netif_destroy_default_wifi(gNetif);
    gNetif = nullptr;
}

}  // namespace netjoin
