// The real clock: the board's PCF85063 RTC (kept running by the battery), set from the
// internet over Wi-Fi when a network is configured.
//
// The system clock (time()) is what the game reads. At boot it is loaded from the RTC;
// a network sync corrects both.
#pragma once

#include <cstdint>

#include "driver/i2c_master.h"

namespace walltime {

// Sets the time zone and loads the system clock from the RTC, if the RTC kept good time.
void init(i2c_master_bus_handle_t bus, const char* posixTz);

// True once the system clock holds a real date (from the RTC or the network).
bool known();

// In the background: join Wi-Fi, fetch the time over NTP, store it in the RTC, and turn
// Wi-Fi off again. Does nothing if ssid is empty.
void syncFromNetwork(const char* ssid, const char* password);

// Sets the system clock without touching the RTC, for when no real time is available.
void assume(int64_t epoch);

// Seconds east of UTC right now, daylight saving included.
int32_t tzOffset();

}  // namespace walltime
