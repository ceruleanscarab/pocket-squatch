// Wi-Fi setup from a phone. The board puts up its own open hotspot; a phone that joins it
// gets a page (the "sign in to network" page pops up by itself, like hotel Wi-Fi) listing
// the networks the board can hear. Pick one, type its password, and the board saves it,
// takes its hotspot down and tries joining, so the page on the board can say if it worked.
// An optional town is looked up while it's joined (Open-Meteo's geocoding): the weather's
// location and the clock's time zone come from it.
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
// The town found from the page's "Your town" box, in capitals ("" if none was asked for,
// "TOWN NOT FOUND" if the lookup found nothing).
void town(char* out, size_t n);
// A pet asked for by its backup code: 1 brought back (cloud::takeRestored has it), -1 the code
// had no backup, 0 none asked for.
int restoreResult();
// How many phones have joined the hotspot (while it's up).
int phonesJoined();

// True once after the page saved a new bedtime (in NVS: storage::bedtime), for the game loop to use.
bool takeBedtimeChanged();
// True once after the page saved a new owner's name (storage::ownerName).
bool takeOwnerChanged();

}  // namespace wifisetup
