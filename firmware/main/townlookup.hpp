// Finding a town: Open-Meteo's geocoding gives its location and time zone, which are saved
// for the weather and the clock (storage::Location) and the zone applied at once.
#pragma once

#include "storage.hpp"

namespace townlookup {

// Needs the board joined to Wi-Fi (radio taken). True, with the saved place in out, if found.
bool find(const char* town, storage::Location& out);

}  // namespace townlookup
