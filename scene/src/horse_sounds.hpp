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
extern const HorseClip kStepClips[2];  // a hoof on soft ground, two ways
extern const HorseClip kSplashClip, kSipClip;  // water: the trough, and a horse drinking
// More takes of each (horse_takes.cpp, made by tools/horse_takes.py), so it isn't the same every time.
extern const HorseClip kNeighTakes[4], kNickerTakes[4], kSnortTakes[3], kMunchTakes[1], kHayTakes[2];
extern const HorseClip kTootClip;  // a pony after too many apples
// The visitors' real calls (farm_sounds.cpp, made by tools/farm_sounds.py): the barn cat, the goat (the sheep
// too, a little higher), the farm dog and the owl.
extern const HorseClip kMeowClip, kBleatClip, kWoofClip, kHootClip;

}  // namespace squatch
