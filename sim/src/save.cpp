#include "squatch/save.hpp"

#include <cstddef>
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
    if (h.version == 0 || h.version > kSaveVersion) return LoadResult::UnknownVersion;

    // Fields are only ever added at the end of State, so an older save is a prefix of
    // today's State: version 1 ended with rng, just before the genome.
    const size_t expected = h.version == 1 ? offsetof(State, genome) : sizeof(State);
    if (h.size != expected || len < sizeof(SaveHeader) + h.size) return LoadResult::BadSize;
    if (crc32(data + sizeof(SaveHeader), h.size) != h.crc) return LoadResult::BadChecksum;

    State loaded{};  // defaults for anything the old version didn't have
    std::memcpy(&loaded, data + sizeof(SaveHeader), h.size);
    if (h.version == 1) loaded.genome = randomGenome(loaded.rng);  // a pet from before DNA gets some now
    s = loaded;
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
