// Real recordings: the horse edition's sounds and the holidays' (the turkey, Santa) (see horse_sounds.cpp for where
// they're from). 16 kHz, 8-bit mu-law. Generated; the order matches HorseClipId.
#pragma once

#include <cstdint>

namespace squatch {

struct HorseClip {
    const uint8_t* data;
    int length;  // samples
};

enum HorseClipId { kClipNeigh, kClipNicker, kClipSnort, kClipClop, kClipMunch, kClipBrush, kClipGobble, kClipHoHoHo, kHorseClipCount };

extern const HorseClip kHorseClips[kHorseClipCount];
// Made in code by tools/leaf_sounds.py (leaf_sounds.cpp): a shaken tree, and something landing on the grass.
extern const HorseClip kRustleClip, kThudClip;

}  // namespace squatch
