// Real horse recordings for the horse edition's sounds (see horse_sounds.cpp for where
// they're from). 16 kHz, 8-bit mu-law. Generated; the order matches HorseClipId.
#pragma once

#include <cstdint>

namespace squatch {

struct HorseClip {
    const uint8_t* data;
    int length;  // samples
};

enum HorseClipId { kClipNeigh, kClipNicker, kClipSnort, kClipClop, kClipMunch, kClipBrush, kHorseClipCount };

extern const HorseClip kHorseClips[kHorseClipCount];

}  // namespace squatch
