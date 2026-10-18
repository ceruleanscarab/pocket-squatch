// The speaker: the scene's sound cues, made by the Synth, out through I2S to the board's
// ES8311 codec and its little amplifier.
//
// The codec and amplifier are only powered while sounds are playing: they come up when a
// cue starts (about a quarter of a second, hidden in silence) and go down two seconds after
// the last one ends. Sound plays in its own task, so drawing never waits for it.
#pragma once

#include "driver/i2c_master.h"
#include "squatch/sounds.hpp"

namespace audio {

// False (and silent from then on) if no ES8311 answers on the bus.
bool init(i2c_master_bus_handle_t bus, bool switchRail);
void play(squatch::Cue c, int volume);

}  // namespace audio
