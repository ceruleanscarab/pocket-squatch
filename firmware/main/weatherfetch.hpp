// The real weather, from Open-Meteo (api.open-meteo.com: free, no account), for the place
// set in menuconfig (Pocket Squatch > Weather). About once an hour a background task takes
// the radio, joins Wi-Fi, asks for the current conditions, and switches Wi-Fi off again.
#pragma once

#include "squatch/weather.hpp"

namespace weatherfetch {

// Starts the hourly task, if a location and a Wi-Fi network are set. Call radio::begin() first.
void start();
// The newest report, once; false if none has arrived since the last take().
bool take(squatch::Weather& out);

}  // namespace weatherfetch
