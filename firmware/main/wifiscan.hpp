// Listening for trails: a Wi-Fi scan for the trail map.
//
// The scan runs in its own task so the clearing keeps drawing. It only looks:
// it never joins a network, and only a hash of each name is handed to the game.
#pragma once

#include <cstddef>

#include "squatch/trails.hpp"

namespace wifiscan {

// Start a scan in the background. Does nothing while one is already running.
void request();

// The finished scan's networks, once. Returns false if no new results are waiting
// (none requested, still scanning, or the scan failed).
bool take(squatch::Sighting* out, size_t cap, size_t& n);

}  // namespace wifiscan
