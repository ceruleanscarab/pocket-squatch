// Sound cues and a tiny synthesizer that plays them.
//
// Hardware-free like the rest of the scene: the scene says which cue, the synth turns
// cues into 16-bit mono samples, and the front end sends those to a speaker (SDL audio
// on the PC, the ES8311 codec on the board). Every cue is a few notes made in code:
// sine, triangle and square tones with pitch sweeps, and filtered noise for swishes and
// cracks, so there are no sound files to carry around.
#pragma once

#include <cstdint>

namespace squatch {

enum class Cue : uint8_t {
    Tick,        // a tray or settings button
    Pickup,      // an item taken from the tray
    Eat,
    Clean,
    Heal,
    Play,
    LightOn,
    LightOff,
    Happy,
    Sneeze,
    Sulk,
    Knock,       // it's calling you from the trees
    Hatch,
    GrowUp,
    NewEgg,
    Sick,
    RanAway,
    Returned,
    ForageBack,
    Photo,
    Warning,     // low battery
    Thunder,
    Count
};

const char* toString(Cue c);

class Synth {
public:
    static constexpr int kRate = 16000;      // samples per second
    static constexpr int kVolumeSteps = 4;   // off, quiet, medium, loud

    // Start a cue. With all voices busy the oldest gives way. Volume 0 plays nothing.
    void play(Cue c, int volume);
    // Mixes the next n samples into out (overwriting it). Returns true while anything
    // is still playing, false once it's all silence.
    bool render(int16_t* out, int n);
    bool active() const;
    void stopAll();

    // How long a cue lasts, in ms (for tests).
    static int lengthMs(Cue c);

private:
    struct Voice {
        bool on = false;
        Cue cue = Cue::Tick;
        float gain = 0;
        int note = 0;       // which note of the cue
        int pos = 0;        // samples into this note (its gap included)
        float phase = 0;    // 0..1
        float lp = 0;       // the noise filter's state
        uint32_t age = 0;   // when it started, to pick the oldest
    };
    static constexpr int kVoices = 3;
    Voice voices_[kVoices];
    uint32_t started_ = 0;
    uint32_t noise_ = 0x1234567u;
};

}  // namespace squatch
