#include "walltime.hpp"

#include <sys/time.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "radio.hpp"

namespace walltime {

namespace {

const char* TAG = "walltime";

// PCF85063: seven BCD registers from 0x04 (seconds, minutes, hours, day, weekday, month,
// year). Bit 7 of seconds is set when the oscillator stopped and the time was lost.
constexpr uint8_t kRtcAddress = 0x51;
constexpr uint8_t kRegSeconds = 0x04;
constexpr int64_t kEarliest = 1767225600;  // 2026-01-01: anything earlier means "not set"

i2c_master_dev_handle_t gRtc = nullptr;

int fromBcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
uint8_t toBcd(int v) { return static_cast<uint8_t>((v / 10) << 4 | (v % 10)); }

// Days since 1970-01-01 (Howard Hinnant's days_from_civil), so we needn't rely on timegm.
int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = static_cast<int>(y - era * 400);
    const int doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int64_t toEpoch(const std::tm& t) {
    return daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) * 86400 + t.tm_hour * 3600 + t.tm_min * 60 +
           t.tm_sec;
}

void setSystemClock(int64_t epoch) {
    timeval tv{static_cast<time_t>(epoch), 0};
    settimeofday(&tv, nullptr);
}

bool readRtc(int64_t& epoch) {
    uint8_t reg = kRegSeconds, b[7];
    if (!gRtc || i2c_master_transmit_receive(gRtc, &reg, 1, b, sizeof b, 100) != ESP_OK) return false;
    if (b[0] & 0x80) return false;
    std::tm t{};
    t.tm_sec = fromBcd(b[0] & 0x7F);
    t.tm_min = fromBcd(b[1] & 0x7F);
    t.tm_hour = fromBcd(b[2] & 0x3F);  // 24-hour mode
    t.tm_mday = fromBcd(b[3] & 0x3F);
    t.tm_mon = fromBcd(b[5] & 0x1F) - 1;
    t.tm_year = 100 + fromBcd(b[6]);  // the RTC keeps 2000-2099
    if (t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1) return false;
    epoch = toEpoch(t);
    return epoch >= kEarliest;
}

void writeRtc(int64_t epoch) {
    time_t secs = static_cast<time_t>(epoch);
    std::tm t{};
    gmtime_r(&secs, &t);  // the RTC keeps UTC; the time zone is applied on top
    const uint8_t b[8] = {kRegSeconds,
                          toBcd(t.tm_sec),  // also clears the "time lost" bit
                          toBcd(t.tm_min),
                          toBcd(t.tm_hour),
                          toBcd(t.tm_mday),
                          static_cast<uint8_t>(t.tm_wday),
                          toBcd(t.tm_mon + 1),
                          toBcd(t.tm_year % 100)};
    if (!gRtc || i2c_master_transmit(gRtc, b, sizeof b, 100) != ESP_OK) ESP_LOGW(TAG, "couldn't write the RTC");
}

// ---- network time

constexpr EventBits_t kGotIp = 1, kGaveUp = 2;
EventGroupHandle_t gNet = nullptr;
int gRetries = 0;
char gSsid[33], gPassword[65];

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

void networkTask(void*) {
    gNet = xEventGroupCreate();
    radio::take();  // a trail scan may have the radio
    esp_netif_t* netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init);
    esp_event_handler_instance_t wifiHandler, ipHandler;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onNetEvent, nullptr, &wifiHandler);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onNetEvent, nullptr, &ipHandler);

    wifi_config_t cfg{};
    std::strncpy(reinterpret_cast<char*>(cfg.sta.ssid), gSsid, sizeof cfg.sta.ssid);
    std::strncpy(reinterpret_cast<char*>(cfg.sta.password), gPassword, sizeof cfg.sta.password);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_start();

    EventBits_t bits = xEventGroupWaitBits(gNet, kGotIp | kGaveUp, pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    if (bits & kGotIp) {
        esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_netif_sntp_init(&sntp);
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) == ESP_OK) {
            writeRtc(time(nullptr));
            ESP_LOGI(TAG, "clock set from the network");
        } else {
            ESP_LOGW(TAG, "no answer from the time server");
        }
        esp_netif_sntp_deinit();
    } else {
        ESP_LOGW(TAG, "couldn't join Wi-Fi \"%s\"", gSsid);
    }

    // Wi-Fi only runs for this and for trail scans; switch it off to save the battery.
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiHandler);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ipHandler);
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(netif);
    radio::give();
    vTaskDelete(nullptr);
}

}  // namespace

void init(i2c_master_bus_handle_t bus, const char* posixTz) {
    setenv("TZ", posixTz, 1);
    tzset();

    i2c_device_config_t dev{};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = kRtcAddress;
    dev.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(bus, &dev, &gRtc) != ESP_OK) gRtc = nullptr;

    int64_t epoch = 0;
    if (readRtc(epoch)) {
        setSystemClock(epoch);
        ESP_LOGI(TAG, "clock set from the RTC");
    } else {
        ESP_LOGW(TAG, "the RTC doesn't have the time yet");
    }
}

bool known() { return time(nullptr) >= kEarliest; }

void syncFromNetwork(const char* ssid, const char* password) {
    if (!ssid || !ssid[0]) return;
    std::strncpy(gSsid, ssid, sizeof gSsid - 1);
    std::strncpy(gPassword, password ? password : "", sizeof gPassword - 1);
    xTaskCreatePinnedToCore(networkTask, "net", 4096, nullptr, 3, nullptr, 0);
}

void assume(int64_t epoch) { setSystemClock(epoch); }

int32_t tzOffset() {
    time_t now = time(nullptr);
    std::tm local{}, utc{};
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
    return static_cast<int32_t>(toEpoch(local) - toEpoch(utc));
}

}  // namespace walltime
