#include "radio.hpp"

#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace radio {

namespace {
SemaphoreHandle_t gLock = nullptr;
}  // namespace

void begin() {
    if (gLock) return;
    gLock = xSemaphoreCreateMutex();
    esp_netif_init();
    esp_event_loop_create_default();
}

void take() { xSemaphoreTake(gLock, portMAX_DELAY); }

void give() { xSemaphoreGive(gLock); }

}  // namespace radio
