import sys, glob, wave, numpy as np
from scipy.signal import resample_poly, butter, sosfilt
src, out = sys.argv[1], sys.argv[2]
RATE = 16000

def load(pattern):
    f = glob.glob(f"{src}/*{pattern}*")[0]
    w = wave.open(f); n = w.getnframes(); ch = w.getnchannels(); sw = w.getsampwidth(); fr = w.getframerate()
    raw = w.readframes(n)
    if sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(x & 0x800000, x - 0x1000000, x) / 8388608.0
    else:
        x = np.frombuffer(raw, np.int16) / 32768.0
    x = x.reshape(-1, ch).mean(axis=1)
    from math import gcd
    g = gcd(fr, RATE)
    x = resample_poly(x, RATE // g, fr // g)
    # The board's little speaker can't do deep bass: take it out so it doesn't waste the volume.
    x = sosfilt(butter(2, 140, 'highpass', fs=RATE, output='sos'), x)
    return x

def trim(x, db=-38):
    env = np.convolve(np.abs(x), np.ones(160) / 160, 'same')
    on = np.where(env > env.max() * 10 ** (db / 20))[0]
    return x[max(0, on[0] - 80): on[-1] + 400]

def window(x, secs):  # the loudest stretch of this length
    n = int(secs * RATE); e = np.convolve(x ** 2, np.ones(n), 'valid'); i = int(e.argmax()); return x[i:i + n]

def fade(x, inMs=5, outMs=40):
    a, b = int(RATE * inMs / 1000), int(RATE * outMs / 1000)
    x = x.copy(); x[:a] *= np.linspace(0, 1, a); x[-b:] *= np.linspace(1, 0, b); return x

def norm(x, peak): return x / np.abs(x).max() * peak

clips = {}
clips['neigh'] = norm(fade(trim(load('neighing_5'))), 0.95)
clips['nicker'] = norm(fade(trim(load('Little_neighing_call_2'))), 0.9)
clips['snort'] = norm(fade(trim(load('Breath_of_a_horse_1'))), 0.9)
clips['munch'] = norm(fade(window(load('eats_carrot'), 1.3), 10, 120), 0.9)
clips['brush'] = norm(fade(window(load('brush'), 1.4), 10, 150), 0.85)
# One hoof on concrete: the loudest step, from just before it lands.
steps = load('steps_on_concrete')
env = np.convolve(np.abs(steps), np.ones(80) / 80, 'same')
i = int(env.argmax()); start = max(0, i - int(0.02 * RATE))
while start > 0 and env[start] > env[i] * 0.1: start -= 40
clips['clop'] = norm(fade(steps[start:start + int(0.22 * RATE)], 2, 60), 0.85)

for k, x in clips.items():
    pcm = (np.clip(x, -1, 1) * 32000).astype(np.int16)
    w = wave.open(f"{out}/horse-{k}.wav", 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(pcm.tobytes()); w.close()
    print(f"{k:7s} {len(x)/RATE:.2f} s")
