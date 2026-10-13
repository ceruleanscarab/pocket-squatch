#include "squatch/save.hpp"

#include <cstring>

namespace squatch {

uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

size_t save(const State& s, uint8_t* out, size_t cap) {
    if (cap < kSaveBytes) return 0;
    SaveHeader h{};
    h.magic = kSaveMagic;
    h.version = kSaveVersion;
    h.size = sizeof(State);
    std::memcpy(out + sizeof(SaveHeader), &s, sizeof(State));
    h.crc = crc32(out + sizeof(SaveHeader), sizeof(State));
    std::memcpy(out, &h, sizeof(SaveHeader));
    return kSaveBytes;
}

LoadResult load(const uint8_t* data, size_t len, State& s) {
    if (len < sizeof(SaveHeader)) return LoadResult::TooShort;
    SaveHeader h{};
    std::memcpy(&h, data, sizeof(SaveHeader));
    if (h.magic != kSaveMagic) return LoadResult::BadMagic;
    if (h.version != kSaveVersion) return LoadResult::UnknownVersion;  // migrations go here
    if (h.size != sizeof(State) || len < sizeof(SaveHeader) + h.size) return LoadResult::BadSize;
    if (crc32(data + sizeof(SaveHeader), h.size) != h.crc) return LoadResult::BadChecksum;
    std::memcpy(&s, data + sizeof(SaveHeader), sizeof(State));
    return LoadResult::Ok;
}

const char* toString(LoadResult r) {
    switch (r) {
        case LoadResult::Ok: return "ok";
        case LoadResult::TooShort: return "too short";
        case LoadResult::BadMagic: return "not a save file";
        case LoadResult::UnknownVersion: return "unknown version";
        case LoadResult::BadSize: return "wrong size";
        case LoadResult::BadChecksum: return "checksum mismatch";
    }
    return "?";
}

}  // namespace squatch
