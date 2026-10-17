// Trails: nearby Wi-Fi networks become places in the forest.
//
// The front end scans for networks (without joining them) and hands the sim a hash of
// each network's name plus its signal strength. The hash picks a fixed landmark kind and
// a generated name, so the same network is always the same place, and the signal sets
// how far away it is. Only the hashes are ever stored, never network names.
//
// Hardware-free: trailHash() is plain arithmetic, so the sim and tests can use it too.
#pragma once

#include <cstddef>
#include <cstdint>

namespace squatch {

enum class LandmarkKind : uint8_t { Falls, Cabin, Cave, BerryPatch, Lookout, HotSpring, Pond, Ridge };
constexpr int kLandmarkKinds = 8;

enum class TrailDistance : uint8_t { Near, Mid, Far };

// One network seen in a scan.
struct Sighting {
    uint32_t hash;  // trailHash() of the network name
    int8_t rssi;    // signal strength in dBm, about -30 (very close) to -95 (barely there)
};

// A trail on the map, as kept in the save.
struct Trail {
    uint32_t hash;
    int8_t rssi;
    uint8_t reserved[3];
};

// Hash of a network name. Empty and hidden names should be skipped by the caller.
uint32_t trailHash(const char* ssid);

LandmarkKind landmarkKind(uint32_t hash);
TrailDistance trailDistance(int8_t rssi);

// Upper-case name for the screen, like "WHISPERING FALLS". Writes up to cap bytes.
void landmarkName(uint32_t hash, char* out, size_t cap);

const char* toString(LandmarkKind k);
const char* toString(TrailDistance d);

}  // namespace squatch
