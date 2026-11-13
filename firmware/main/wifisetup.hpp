// Wi-Fi setup from a phone. The board puts up its own open hotspot; a phone that joins it
// gets a page (the "sign in to network" page pops up by itself, like hotel Wi-Fi) listing
// the networks the board can hear. Pick one, type its password, and the board saves it,
// takes its hotspot down and tries joining, so the page on the board can say if it worked.
#pragma once

#include <cstddef>

namespace wifisetup {

enum class State { Idle, Running, Checking, Joined, Failed };

// Starts the hotspot with this name (in its own task; it takes the radio). Stops by itself
// after ten minutes, or with cancel().
void start(const char* hotspotName);
void cancel();
State state();
// The network last saved here (or being checked).
void network(char* out, size_t n);

}  // namespace wifisetup
