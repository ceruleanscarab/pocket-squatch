// Updates over the air: the board fetches a newer build of itself from the "firmware"
// release over HTTPS and installs it into the other app slot. A new build that crashes
// before it has run for a minute is rolled back by the bootloader.
#pragma once

namespace ota {

enum class State { Off, Idle, Checking, UpToDate, Downloading, Failed, Restarting };

// Once at boot (after radio::begin): starts the one-minute "this build is healthy" timer.
void begin();
// Check now (from Settings > Wi-Fi). Does nothing if a check is already running.
void checkNow();
// Every minute or so from the game loop: checks once a night, about 3 am, if the screen is
// off and the battery can take it.
void tick(bool screenOff, bool batteryOk);

State state();
int percent();  // while downloading
int build();    // the running build's number (0 for one made by hand)
// The new build is in place: save everything and restart.
bool restartDue();

}  // namespace ota
