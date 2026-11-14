// The pet's save, in the board's flash (NVS), in the same format as the PC's save file.
#pragma once

#include "squatch/scene.hpp"
#include "squatch/sim.hpp"

namespace storage {

void init();  // NVS; call once before anything else touches flash or Wi-Fi
bool loadPet(squatch::State& out);
void savePet(const squatch::State& s);

// Brightness, screen timeout and volume, kept apart from the pet so a new egg doesn't reset them.
bool loadSettings(squatch::Scene::Settings& out);
void saveSettings(const squatch::Scene::Settings& s);

// The Wi-Fi network chosen on the board's setup page (kept like the settings, apart from the
// pet). Without one, the network from menuconfig is used.
bool loadWifi(char* ssid, size_t ssidLen, char* password, size_t passwordLen);
void saveWifi(const char* ssid, const char* password);

// The town chosen on the setup page: where the weather is for, and its time zone. zone is
// a POSIX TZ string, or empty when the board follows the weather's UTC offset instead.
struct Location {
    float lat = 0, lon = 0;
    char zone[48] = {};
    char name[40] = {};
};
bool loadLocation(Location& out);
void saveLocation(const Location& l);

// The network to use: the one saved on the board, or else menuconfig's. False if neither.
bool wifiCredentials(char* ssid, size_t ssidLen, char* password, size_t passwordLen);
// menuconfig's Wi-Fi and town are only defaults, built in; an update over the air is a build
// without them. At boot this copies them into the saved settings (if nothing's saved yet), so
// they outlive the build that brought them.
void keepBuiltInDefaults();
// The town to look up the first time it's online: one saved from menuconfig, if any.
bool townToFind(char* out, size_t n);

}  // namespace storage
