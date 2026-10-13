// Save files: a small header plus the raw State.
//
// The layout is the in-memory State, so a save is only portable between builds for
// the same architecture. That's fine: the device saves to its own flash and the PC
// saves to its own disk. Bump kSaveVersion whenever State changes, and add a
// migration in load() when an old version should still load.
#pragma once

#include <cstddef>
#include <cstdint>

#include "squatch/sim.hpp"

namespace squatch {

constexpr uint32_t kSaveMagic = 0x48435153;  // "SQCH"
constexpr uint16_t kSaveVersion = 1;

struct SaveHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t size;
    uint32_t crc;
};

constexpr size_t kSaveBytes = sizeof(SaveHeader) + sizeof(State);

enum class LoadResult : uint8_t { Ok, TooShort, BadMagic, UnknownVersion, BadSize, BadChecksum };

// Writes kSaveBytes into out (which must hold at least that many). Returns bytes written.
size_t save(const State& s, uint8_t* out, size_t cap);

// Validates and copies into s. On anything but Ok, s is untouched.
LoadResult load(const uint8_t* data, size_t len, State& s);

uint32_t crc32(const uint8_t* data, size_t len);

const char* toString(LoadResult r);

}  // namespace squatch
