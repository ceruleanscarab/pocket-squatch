# Makes the paddock's sounds and writes them as clips, the same 16 kHz 8-bit mu-law as
# horse_sounds.cpp: a shaken tree's leaves (two rustling swells, each a crowd of tiny
# leaf-on-leaf crackles over a soft swish, and a twig creak), the thud of an apple or pinecone
# landing on the grass, and two hoofsteps on soft ground (the recorded step on concrete from
# horse_sounds.cpp, softened, with a little crunch of dirt), so a walk goes clip... clop;
# and water: a splash in the trough, and a horse's sip (a draw of water with its bubbles).
# Usage: python3 leaf_sounds.py OUT.cpp [PREVIEW_DIR]   (run from the repo's top folder)
import re, sys, wave, numpy as np
from scipy.signal import butter, sosfilt
RATE = 16000
rng = np.random.default_rng(7)
out = sys.argv[1]
preview = sys.argv[2] if len(sys.argv) > 2 else None

def band(x, lo, hi, order=2):
    return sosfilt(butter(order, [lo, hi], 'bandpass', fs=RATE, output='sos'), x)

def swell(n, at, peak, end):
    t = np.arange(n) / RATE
    up = np.clip((t - at) / max(1e-3, peak - at), 0, 1)
    down = np.clip((end - t) / max(1e-3, end - peak), 0, 1)
    return np.where(t < peak, up ** 1.5, down ** 2) * (t >= at) * (t <= end)

def rustle():
    secs = 1.15
    n = int(secs * RATE)
    env = swell(n, 0.0, 0.12, 0.62) + 0.8 * swell(n, 0.38, 0.52, 1.12)  # shake... shake
    # The swish: leaves brushing past each other, a soft band of noise.
    swish = band(rng.standard_normal(n), 1800, 5200) * 0.22
    # The crackles: thousands of tiny clicks, denser where the shaking is strongest.
    grains = np.zeros(n)
    for _ in range(1400):
        i = int(rng.uniform(0, n - 400))
        if rng.uniform() > env[i] / env.max():
            continue
        length = int(rng.uniform(0.002, 0.012) * RATE)
        g = rng.standard_normal(length) * np.exp(-np.linspace(0, rng.uniform(4, 9), length))
        grains[i:i + length] += g * rng.uniform(0.3, 1.0)
    crackle = band(grains, 2200, 7200) * 0.55
    x = (swish + crackle) * env
    # A twig creaks as the branch bends, early on.
    t = np.arange(int(0.16 * RATE)) / RATE
    f = 520 + 160 * np.sin(2 * np.pi * 3 * t)
    creak = np.sign(np.sin(2 * np.pi * np.cumsum(f) / RATE)) * np.exp(-t * 14) * 0.05
    creak = band(creak, 400, 2500)
    at = int(0.05 * RATE)
    x[at:at + len(creak)] += creak
    return x

def thud():
    n = int(0.22 * RATE)
    t = np.arange(n) / RATE
    body = np.sin(2 * np.pi * np.cumsum(260 - 110 * t / t[-1]) / RATE) * np.exp(-t * 28)
    knock = band(rng.standard_normal(n), 300, 1800) * np.exp(-t * 90) * 0.7
    grass = band(rng.standard_normal(n), 2500, 6500) * np.exp(-t * 40) * 0.25
    return body * 0.9 + knock + grass

def concrete_step():
    src = open('scene/src/horse_sounds.cpp').read()
    body = re.search(r'k_clop\[\d+\] = \{(.*?)\};', src, re.S).group(1)
    u = ~np.array([int(h, 16) for h in re.findall(r'0x([0-9a-f]{2})', body)], dtype=np.uint8)
    e = (u >> 4) & 7
    mag = ((((u & 0x0F).astype(np.int32) << 3) + 0x84) << e) - 0x84
    return np.where(u & 0x80, -mag, mag) / 32768.0

def soft_step(variant):
    x = concrete_step()[:int(0.16 * RATE)]
    # On dirt, not concrete: the sharp click rounded off, the body kept.
    hi = 1900 if variant == 0 else 1500
    x = sosfilt(butter(2, [160, hi], 'bandpass', fs=RATE, output='sos'), x)
    n = len(x)
    t = np.arange(n) / RATE
    crunch = band(rng.standard_normal(n), 1200, 4200) * np.exp(-t * 55) * (0.18 if variant == 0 else 0.12)
    return x * np.exp(-t * 18) + crunch

def bubble(n, at, f0, rise, decay, amp):
    # One bubble: a quick sine whose pitch rises as it shrinks (the "bloop" of water).
    out = np.zeros(n)
    length = int(decay * 5 * RATE)
    t = np.arange(min(length, n - at)) / RATE
    f = f0 * (1 + rise * t / max(t[-1], 1e-6)) if len(t) else t
    out[at:at + len(t)] = np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-t / decay) * amp
    return out

def splash():
    n = int(0.75 * RATE)
    t = np.arange(n) / RATE
    hit = band(rng.standard_normal(n), 700, 5500) * np.exp(-t * 14) * 0.6  # the slap of the water
    spray = band(rng.standard_normal(n), 2500, 7500) * np.exp(-t * 6) * 0.18 * (t > 0.03)
    x = hit + spray
    for _ in range(14):  # and the bubbles and drips after
        x += bubble(n, int(rng.uniform(0.02, 0.6) * RATE), rng.uniform(500, 1400), rng.uniform(0.4, 1.2),
                    rng.uniform(0.008, 0.02), rng.uniform(0.15, 0.4))
    return x

def sip():
    n = int(0.55 * RATE)
    t = np.arange(n) / RATE
    # Drawing water in: a soft, wet hiss that swells and stops.
    draw = band(rng.standard_normal(n), 400, 2600) * np.clip(t / 0.12, 0, 1) * np.clip((0.4 - t) / 0.08, 0, 1) * 0.22
    x = draw
    for _ in range(9):  # little bubbles as the water moves
        x += bubble(n, int(rng.uniform(0.03, 0.38) * RATE), rng.uniform(350, 900), rng.uniform(0.5, 1.5),
                    rng.uniform(0.006, 0.015), rng.uniform(0.2, 0.45))
    # and the gulp at the end
    g = np.arange(int(0.12 * RATE)) / RATE
    gulp = np.sin(2 * np.pi * np.cumsum(240 - 90 * g / g[-1]) / RATE) * np.exp(-g * 30) * 0.6
    at = int(0.42 * RATE)
    x[at:at + len(gulp)] += gulp[:n - at]
    return x

def finish(x, peak):
    x = x - x.mean()
    a = int(0.004 * RATE); b = int(0.05 * RATE)
    x[:a] *= np.linspace(0, 1, a); x[-b:] *= np.linspace(1, 0, b)
    return x / np.abs(x).max() * peak

def mulaw(x):
    x = np.clip(x, -32635, 32635).astype(np.int32)
    sign = np.where(x < 0, 0x80, 0); x = np.abs(x) + 0x84
    exp = np.zeros_like(x)
    for e in range(7, -1, -1):
        exp = np.where((exp == 0) & (x >= (1 << (e + 7))), e, exp)
    mant = (x >> (exp + 3)) & 0x0F
    return (~(sign | (exp << 4) | mant)) & 0xFF

clips = {'rustle': finish(rustle(), 0.9), 'thud': finish(thud(), 0.8),
         'step_a': finish(soft_step(0), 0.85), 'step_b': finish(soft_step(1), 0.8),
         'splash': finish(splash(), 0.85), 'sip': finish(sip(), 0.8)}
lines = ["// Generated by tools/leaf_sounds.py: made in code, not recorded. 16 kHz 8-bit mu-law.",
         '#include "horse_sounds.hpp"', "", "namespace squatch {", "namespace {", ""]
for name, x in clips.items():
    pcm = (x * 32767).astype(np.int16)
    if preview:
        w = wave.open(f"{preview}/{name}.wav", 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(pcm.tobytes()); w.close()
    u = mulaw(pcm.astype(np.int32))
    lines.append(f"const uint8_t k_{name}[{len(u)}] = {{")
    for i in range(0, len(u), 20):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in u[i:i + 20]) + ",")
    lines.append("};")
    lines.append("")
lines += ["}  // namespace", "", "const HorseClip kRustleClip = {k_rustle, static_cast<int>(sizeof k_rustle)};",
          "const HorseClip kThudClip = {k_thud, static_cast<int>(sizeof k_thud)};",
          "const HorseClip kStepClips[2] = {{k_step_a, static_cast<int>(sizeof k_step_a)},",
          "                                 {k_step_b, static_cast<int>(sizeof k_step_b)}};",
          "const HorseClip kSplashClip = {k_splash, static_cast<int>(sizeof k_splash)};",
          "const HorseClip kSipClip = {k_sip, static_cast<int>(sizeof k_sip)};", "", "}  // namespace squatch"]
open(out, 'w').write("\n".join(lines) + "\n")
print({k: len(v) for k, v in clips.items()})
