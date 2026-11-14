#include "squatch/sounds.hpp"

#include "horse_sounds.hpp"

#include <algorithm>
#include <cmath>

namespace squatch {

namespace {

enum Wave : uint8_t { Sine, Tri, Square, Noise };

// One note: a tone sliding from f0 to f1 Hz (for Noise, a filter sweeping between those
// cut-offs), then a silent gap before the next note.
struct Note {
    Wave wave;
    float f0, f1;
    uint16_t ms, gapMs;
    float amp;
};

struct Shape {
    const Note* notes;
    int count;
};

// Scale tones (Hz), so the tunes stay in key.
constexpr float C5 = 523.3f, D5 = 587.3f, E5 = 659.3f, G5 = 784.0f, A5 = 880.0f, B5 = 987.8f;
constexpr float C6 = 1046.5f, D6 = 1174.7f, E6 = 1318.5f, G6 = 1568.0f, G4 = 392.0f;

constexpr Note kTick[] = {{Tri, 1500, 1400, 18, 0, 0.45f}};
constexpr Note kPickup[] = {{Sine, 600, 950, 70, 0, 0.6f}};
constexpr Note kEat[] = {{Square, 320, 210, 60, 70, 0.35f}, {Square, 300, 200, 60, 70, 0.35f},
                         {Square, 330, 220, 70, 0, 0.35f}};
constexpr Note kClean[] = {{Noise, 1500, 6000, 110, 20, 0.5f}, {Noise, 6000, 1500, 120, 0, 0.45f}};
constexpr Note kHeal[] = {{Sine, C5, C5, 80, 10, 0.6f}, {Sine, E5, E5, 80, 10, 0.6f},
                          {Sine, G5, G5, 80, 10, 0.6f}, {Sine, C6, C6, 180, 0, 0.6f}};
constexpr Note kPlay[] = {{Sine, 280, 900, 150, 0, 0.7f}, {Sine, 900, 520, 120, 0, 0.6f}};
constexpr Note kLightOn[] = {{Tri, A5, A5, 45, 15, 0.5f}, {Tri, E6, E6, 70, 0, 0.5f}};
constexpr Note kLightOff[] = {{Tri, E6, E6, 45, 15, 0.5f}, {Tri, A5, A5, 70, 0, 0.5f}};
constexpr Note kHappy[] = {{Sine, 1200, 1800, 70, 45, 0.55f}, {Sine, 1300, 2050, 90, 0, 0.55f}};
constexpr Note kSneeze[] = {{Sine, 450, 750, 140, 90, 0.45f}, {Noise, 5000, 900, 160, 0, 0.75f}};
constexpr Note kSulk[] = {{Square, 170, 125, 280, 0, 0.3f}};
constexpr Note kKnock[] = {{Tri, 190, 110, 45, 130, 0.95f}, {Tri, 190, 110, 45, 0, 0.95f}};
constexpr Note kHatch[] = {{Noise, 3000, 3000, 35, 90, 0.6f}, {Noise, 3500, 3500, 40, 160, 0.7f},
                           {Sine, G5, G5, 80, 0, 0.55f},     {Sine, B5, B5, 80, 0, 0.55f},
                           {Sine, D6, D6, 80, 0, 0.55f},     {Sine, G6, G6, 220, 0, 0.55f}};
constexpr Note kGrowUp[] = {{Square, C5, C5, 100, 20, 0.3f}, {Square, E5, E5, 100, 20, 0.3f},
                            {Square, G5, G5, 100, 20, 0.3f}, {Square, C6, C6, 320, 0, 0.3f}};
constexpr Note kNewEgg[] = {{Sine, C6, C6, 120, 30, 0.5f}, {Sine, E6, E6, 120, 30, 0.5f},
                            {Sine, G6, G6, 320, 0, 0.5f}};
constexpr Note kSick[] = {{Sine, 620, 380, 340, 0, 0.5f}};
constexpr Note kRanAway[] = {{Tri, G5, G5, 110, 10, 0.55f}, {Tri, E5, E5, 110, 10, 0.55f},
                             {Tri, C5, C5, 110, 10, 0.55f}, {Tri, G4, G4, 260, 0, 0.55f}};
constexpr Note kReturned[] = {{Tri, C5, C5, 90, 10, 0.55f}, {Tri, E5, E5, 90, 10, 0.55f},
                              {Tri, G5, G5, 90, 10, 0.55f}, {Tri, C6, C6, 200, 0, 0.55f}};
constexpr Note kForageBack[] = {{Tri, 170, 120, 50, 140, 0.8f},   {Tri, 170, 120, 50, 160, 0.8f},
                                {Sine, 1200, 1800, 70, 45, 0.5f}, {Sine, 1300, 2050, 90, 0, 0.5f}};
constexpr Note kPhoto[] = {{Noise, 6000, 6000, 25, 45, 0.6f}, {Noise, 4000, 4000, 45, 0, 0.55f}};
constexpr Note kThunder[] = {{Noise, 260, 70, 300, 0, 0.9f}, {Noise, 120, 50, 900, 0, 0.8f}};
constexpr Note kBoo[] = {{Sine, 240, 430, 420, 0, 0.55f}, {Sine, 430, 220, 560, 0, 0.5f}};
constexpr Note kBoop[] = {{Sine, 900, 1400, 60, 0, 0.5f}, {Sine, 1400, 1000, 50, 0, 0.45f}};
constexpr Note kGiggle[] = {{Sine, 1000, 1300, 55, 35, 0.45f}, {Sine, 1100, 1450, 55, 35, 0.45f},
                            {Sine, 1050, 1400, 55, 35, 0.45f}, {Sine, 1200, 1650, 70, 0, 0.45f}};
constexpr Note kRustle[] = {{Noise, 2500, 5000, 130, 20, 0.4f}, {Noise, 4500, 2000, 180, 0, 0.35f}};
constexpr Note kCrackle[] = {{Noise, 4000, 4000, 15, 60, 0.5f}, {Noise, 2500, 2500, 12, 90, 0.45f},
                             {Noise, 5000, 5000, 10, 40, 0.4f}, {Noise, 3000, 3000, 14, 0, 0.45f}};
constexpr Note kTweet[] = {{Sine, 2700, 4100, 60, 45, 0.3f}, {Sine, 3000, 4400, 55, 40, 0.3f}, {Sine, 4300, 3100, 80, 0, 0.3f}};
constexpr Note kQuack[] = {{Square, 460, 330, 110, 90, 0.28f}, {Square, 440, 310, 130, 0, 0.28f}};
constexpr Note kHoot[] = {{Sine, 520, 480, 240, 140, 0.5f}, {Sine, 500, 470, 150, 60, 0.45f}, {Sine, 500, 450, 300, 0, 0.45f}};
constexpr Note kBabble[] = {{Tri, 700, 900, 45, 30, 0.3f}, {Tri, 600, 750, 40, 25, 0.3f}, {Tri, 800, 650, 60, 0, 0.3f}};
constexpr Note kNeigh[] = {{Tri, 620, 1250, 130, 0, 0.5f},
                           {Tri, 1250, 1020, 42, 0, 0.500f},
                           {Tri, 965, 1195, 42, 0, 0.485f},
                           {Tri, 1140, 910, 42, 0, 0.470f},
                           {Tri, 855, 1085, 42, 0, 0.455f},
                           {Tri, 1030, 800, 42, 0, 0.440f},
                           {Tri, 745, 975, 42, 0, 0.425f},
                           {Tri, 920, 690, 42, 0, 0.410f},
                           {Tri, 635, 865, 42, 0, 0.395f},
                           {Tri, 810, 580, 42, 0, 0.380f},
                           {Tri, 525, 755, 42, 0, 0.365f},
                           {Tri, 640, 380, 170, 0, 0.36f},
                           {Noise, 700, 300, 140, 0, 0.3f}};
constexpr Note kNicker[] = {{Square, 230, 190, 42, 24, 0.540f}, {Square, 224, 184, 42, 24, 0.495f}, {Square, 218, 178, 42, 24, 0.450f}, {Square, 212, 172, 42, 24, 0.405f}, {Square, 206, 166, 42, 24, 0.360f}, {Square, 200, 160, 42, 24, 0.315f}};
constexpr Note kSnort[] = {{Noise, 900, 380, 150, 30, 0.55f}, {Noise, 2600, 600, 90, 0, 0.38f}};
constexpr Note kClop[] = {{Tri, 420, 150, 22, 75, 0.55f}, {Tri, 360, 140, 22, 0, 0.5f}};
constexpr Note kMunch[] = {{Noise, 1900, 900, 40, 80, 0.55f}, {Noise, 1700, 800, 40, 80, 0.5f},
                           {Noise, 2000, 900, 40, 90, 0.5f}, {Noise, 1600, 800, 45, 0, 0.45f}};
constexpr Note kBrush[] = {{Noise, 3000, 5000, 120, 40, 0.35f}, {Noise, 3000, 5000, 120, 0, 0.35f}};
constexpr Note kGobble[] = {{Square, 520, 430, 40, 0, 0.28f}, {Square, 430, 560, 40, 0, 0.28f}, {Square, 560, 420, 40, 0, 0.28f},
                            {Square, 420, 540, 40, 0, 0.28f}, {Square, 540, 410, 40, 0, 0.26f}, {Square, 410, 500, 40, 0, 0.24f},
                            {Square, 500, 380, 70, 0, 0.22f}};
constexpr Note kWarning[] = {{Square, A5, A5, 130, 90, 0.3f}, {Square, E5, E5, 220, 0, 0.3f}};

#define SHAPE(a) {a, static_cast<int>(sizeof a / sizeof a[0])}
constexpr Shape kShapes[static_cast<int>(Cue::Count)] = {
    SHAPE(kTick),    SHAPE(kPickup),   SHAPE(kEat),    SHAPE(kClean),     SHAPE(kHeal),    SHAPE(kPlay),
    SHAPE(kLightOn), SHAPE(kLightOff), SHAPE(kHappy),  SHAPE(kSneeze),    SHAPE(kSulk),    SHAPE(kKnock),
    SHAPE(kHatch),   SHAPE(kGrowUp),   SHAPE(kNewEgg), SHAPE(kSick),      SHAPE(kRanAway), SHAPE(kReturned),
    SHAPE(kForageBack), SHAPE(kPhoto), SHAPE(kWarning), SHAPE(kThunder), SHAPE(kBoo),
    SHAPE(kBoop), SHAPE(kGiggle), SHAPE(kRustle), SHAPE(kCrackle), SHAPE(kBabble),
    SHAPE(kNeigh), SHAPE(kNicker), SHAPE(kSnort), SHAPE(kClop), SHAPE(kMunch), SHAPE(kBrush), SHAPE(kGobble), SHAPE(kGobble),
    SHAPE(kSnort), SHAPE(kNicker), SHAPE(kNicker), SHAPE(kClop), SHAPE(kTweet), SHAPE(kQuack), SHAPE(kHoot), SHAPE(kClop), SHAPE(kClop),
};
#undef SHAPE

// Overall loudness per volume step. The speaker is small and close: loud is not full scale.
constexpr float kGain[Synth::kVolumeSteps] = {0.0f, 0.1f, 0.25f, 0.5f};

constexpr float kTwoPi = 6.2831853f;

int samples(int ms) { return ms * Synth::kRate / 1000; }

// The recording behind a cue, if it has one.
// The recordings are few, so some cues play one of them differently: higher and softer,
// lower and slower, or just its start.
struct ClipStyle {
    const HorseClip* clip;
    float pitch;  // times the voice's own pitch
    float gain;
    float part;   // how much of the clip, from its start
};

ClipStyle clipFor(Cue c) {
    const HorseClip* k = kHorseClips;
    switch (c) {
        case Cue::Neigh: return {&k[kClipNeigh], 1, 1, 1};
        case Cue::Nicker: return {&k[kClipNicker], 1, 1, 1};
        case Cue::Snort: return {&k[kClipSnort], 1, 1, 1};
        case Cue::Clop: return {&k[kClipClop], 1, 1, 1};
        case Cue::Munch: return {&k[kClipMunch], 1, 1, 1};
        case Cue::Brush: return {&k[kClipBrush], 1, 1, 1};
        case Cue::Gobble: return {&k[kClipGobble], 1, 1, 1};
        case Cue::HoHoHo: return {&k[kClipHoHoHo], 1, 1, 1};
        case Cue::Blow: return {&k[kClipSnort], 1.3f, 0.55f, 1};      // a little puff, not a warning
        case Cue::Chuckle: return {&k[kClipNicker], 1.3f, 0.9f, 0.55f};  // quick and bright
        case Cue::Sigh: return {&k[kClipNicker], 0.8f, 0.7f, 1};       // low and slow
        case Cue::Rustle: return {&kRustleClip, 1, 0.8f, 1};           // for both editions
        case Cue::Thud: return {&kThudClip, 1, 0.8f, 1};
        case Cue::StepNear: return {&kStepClips[0], 1, 0.75f, 1};
        case Cue::StepFar: return {&kStepClips[1], 1, 0.45f, 1};
        default: return {nullptr, 1, 1, 1};
    }
}

// G.711 mu-law to -1..1.
float fromMulaw(uint8_t u) {
    u = static_cast<uint8_t>(~u);
    const int sign = u & 0x80, exp = (u >> 4) & 7, mant = u & 0x0F;
    const int mag = ((mant << 3) + 0x84) << exp;
    return static_cast<float>(sign ? 0x84 - mag : mag - 0x84) / 32768.0f;
}

}  // namespace

const char* toString(Cue c) {
    static const char* kNames[] = {"tick",     "pickup",   "eat",       "clean",  "heal",    "play",
                                   "light on", "light off", "happy",    "sneeze", "sulk",    "knock",
                                   "hatch",    "grow up",  "new egg",   "sick",   "ran away", "returned",
                                   "forage back", "photo", "warning", "thunder", "boo",
                                   "boop", "giggle", "rustle", "crackle", "babble",
                                   "neigh", "nicker", "snort", "clop", "munch", "brush", "gobble", "ho ho ho",
                                   "blow", "chuckle", "sigh", "thud", "tweet", "quack", "hoot", "step", "step (far)"};
    int i = static_cast<int>(c);
    return i < static_cast<int>(Cue::Count) ? kNames[i] : "?";
}

int Synth::lengthMs(Cue c) {
    const ClipStyle st = clipFor(c);
    if (st.clip) return static_cast<int>(st.clip->length * st.part) * 1000 / kRate;
    const Shape& s = kShapes[static_cast<int>(c)];
    int ms = 0;
    for (int i = 0; i < s.count; i++) ms += s.notes[i].ms + s.notes[i].gapMs;
    return ms;
}

void Synth::play(Cue c, int volume, float pitch) {
    if (c >= Cue::Count || volume <= 0) return;
    volume = std::min(volume, kVolumeSteps - 1);
    Voice* v = &voices_[0];
    for (Voice& x : voices_) {
        if (!x.on) {
            v = &x;
            break;
        }
        if (x.age < v->age) v = &x;
    }
    *v = Voice{};
    v->on = true;
    v->cue = c;
    v->gain = kGain[volume];
    v->age = ++started_;
    const ClipStyle st = clipFor(c);
    v->pitch = std::max(0.5f, std::min(2.0f, pitch * st.pitch));
    if (st.clip) {
        v->clip = st.clip->data;
        v->clipLength = static_cast<int>(st.clip->length * st.part);
        v->gain *= st.gain;
    }
}

bool Synth::active() const {
    for (const Voice& v : voices_)
        if (v.on) return true;
    return false;
}

void Synth::stopAll() {
    for (Voice& v : voices_) v.on = false;
}

bool Synth::render(int16_t* out, int n) {
    for (int i = 0; i < n; i++) {
        float mix = 0;
        for (Voice& v : voices_) {
            if (!v.on) continue;
            if (v.clip) {  // a recording, already levelled: just a little louder than the tones
                const int i0 = static_cast<int>(v.clipPos);
                const float fr = v.clipPos - static_cast<float>(i0);
                const float a = fromMulaw(v.clip[i0]), b = i0 + 1 < v.clipLength ? fromMulaw(v.clip[i0 + 1]) : 0.0f;
                const float tail = std::min(1.0f, (static_cast<float>(v.clipLength) - v.clipPos) / 900.0f);  // no click if cut short
                mix += (a + (b - a) * fr) * v.gain * 1.4f * tail;
                v.clipPos += v.pitch;
                if (v.clipPos >= static_cast<float>(v.clipLength)) v.on = false;
                continue;
            }
            const Shape& sh = kShapes[static_cast<int>(v.cue)];
            const Note& note = sh.notes[v.note];
            const int len = samples(note.ms), total = len + samples(note.gapMs);
            if (v.pos < len) {
                const float t = static_cast<float>(v.pos) / static_cast<float>(len);
                const float f = (note.f0 + (note.f1 - note.f0) * t) * v.pitch;
                float s = 0;
                switch (note.wave) {
                    case Sine: s = std::sin(v.phase * kTwoPi); break;
                    case Tri: s = 4 * std::fabs(v.phase - 0.5f) - 1; break;
                    case Square: s = v.phase < 0.5f ? 0.8f : -0.8f; break;
                    case Noise: {
                        noise_ = noise_ * 1664525u + 1013904223u;
                        float white = static_cast<float>(static_cast<int32_t>(noise_)) / 2147483648.0f;
                        float k = std::min(1.0f, kTwoPi * f / kRate);  // one-pole low-pass at f
                        v.lp += k * (white - v.lp);
                        s = v.lp * 1.6f;
                        break;
                    }
                }
                if (note.wave != Noise) {
                    v.phase += f / kRate;
                    v.phase -= std::floor(v.phase);
                }
                // A quick fade in and a longer fade out, so notes don't click.
                const float atk = std::min(1.0f, static_cast<float>(v.pos) / samples(4));
                const float rel = std::min(1.0f, static_cast<float>(len - v.pos) / (len * 0.35f + 1));
                mix += s * note.amp * v.gain * atk * rel;
            }
            if (++v.pos >= total) {
                v.pos = 0;
                v.phase = 0;
                if (++v.note >= sh.count) v.on = false;
            }
        }
        mix = std::max(-1.0f, std::min(1.0f, mix));
        out[i] = static_cast<int16_t>(mix * 32000.0f);
    }
    return active();
}

}  // namespace squatch
