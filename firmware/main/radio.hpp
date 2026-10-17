// The Wi-Fi radio is shared by two jobs: setting the clock (walltime) and listening for
// trails (wifiscan). Each takes the radio, starts Wi-Fi, does its job and switches Wi-Fi
// off again, so they must never overlap.
#pragma once

namespace radio {

// Once per boot, from app_main before any Wi-Fi task starts: the network stack, the
// default event loop and the lock.
void begin();

// Wait for the radio, and give it back. Every Wi-Fi user brackets its work with these.
void take();
void give();

}  // namespace radio
