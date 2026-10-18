// Joining the configured Wi-Fi network for a moment: the clock sync and the weather each
// take the radio, join, do their one job over the network, and leave again.
#pragma once

namespace netjoin {

// Starts Wi-Fi and joins ssid; waits up to 30 s for an address. Call with the radio taken
// (radio::take()). Whether it worked or not, call leave() afterwards.
bool join(const char* ssid, const char* password);
// Wi-Fi off again. Then radio::give().
void leave();

}  // namespace netjoin
