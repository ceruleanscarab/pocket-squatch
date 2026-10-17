#include "squatch/trails.hpp"

#include <cstdio>

namespace squatch {

namespace {

// Names are "<adjective> <place>". The adjective uses different bits of the hash from
// the kind, so two cabins rarely share a name.
const char* const kAdjectives[16] = {
    "MOSSY", "WHISPERING", "OLD",    "CROOKED", "HOLLOW", "MISTY", "BEARCLAW", "LONGPINE",
    "SILVER", "FOGGY",     "TALLFERN", "LOST",  "QUIET",  "CEDAR", "ECHO",     "THUNDER",
};

const char* const kPlaces[kLandmarkKinds] = {
    "FALLS", "CABIN", "CAVE", "BERRY PATCH", "LOOKOUT", "SPRINGS", "POND", "RIDGE",
};

}  // namespace

uint32_t trailHash(const char* ssid) {
    // FNV-1a with a fixed salt, finished with a mix so short names spread well.
    uint32_t h = 2166136261u ^ 0x5A75A7C8u;
    for (const char* p = ssid; p && *p; p++) h = (h ^ static_cast<uint8_t>(*p)) * 16777619u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h ? h : 1;  // 0 is kept for "no trail"
}

LandmarkKind landmarkKind(uint32_t hash) { return static_cast<LandmarkKind>(hash % kLandmarkKinds); }

TrailDistance trailDistance(int8_t rssi) {
    if (rssi >= -60) return TrailDistance::Near;
    if (rssi >= -75) return TrailDistance::Mid;
    return TrailDistance::Far;
}

void landmarkName(uint32_t hash, char* out, size_t cap) {
    std::snprintf(out, cap, "%s %s", kAdjectives[(hash >> 8) % 16], kPlaces[hash % kLandmarkKinds]);
}

const char* toString(LandmarkKind k) {
    switch (k) {
        case LandmarkKind::Falls: return "Falls";
        case LandmarkKind::Cabin: return "Cabin";
        case LandmarkKind::Cave: return "Cave";
        case LandmarkKind::BerryPatch: return "Berry Patch";
        case LandmarkKind::Lookout: return "Lookout";
        case LandmarkKind::HotSpring: return "Hot Spring";
        case LandmarkKind::Pond: return "Pond";
        case LandmarkKind::Ridge: return "Ridge";
    }
    return "?";
}

const char* toString(TrailDistance d) {
    switch (d) {
        case TrailDistance::Near: return "NEAR";
        case TrailDistance::Mid: return "A WALK";
        case TrailDistance::Far: return "FAR";
    }
    return "?";
}

}  // namespace squatch
