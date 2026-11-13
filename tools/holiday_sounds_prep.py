# Trims the holiday recordings (16 kHz mono WAVs made with ffmpeg -ac 1 -ar 16000) into
# clips for horse_sounds_pack.py: the turkey's gobble and Santa's "ho ho ho, merry Christmas".
# Usage: python3 holiday_sounds_prep.py RAW_DIR OUT_DIR
import sys, glob, wave, numpy as np
from scipy.signal import butter, sosfilt
src, out = sys.argv[1], sys.argv[2]
RATE = 16000

def load(pattern):
    w = wave.open(glob.glob(f"{src}/*{pattern}*")[0])
    x = np.frombuffer(w.readframes(w.getnframes()), np.int16) / 32768.0
    return sosfilt(butter(2, 140, 'highpass', fs=RATE, output='sos'), x)

def fade(x, inMs=5, outMs=60):
    a, b = int(RATE * inMs / 1000), int(RATE * outMs / 1000)
    x = x.copy(); x[:a] *= np.linspace(0, 1, a); x[-b:] *= np.linspace(1, 0, b); return x

def cut(x, a, b): return x[int(a * RATE):int(b * RATE)]
def norm(x, peak): return x / np.abs(x).max() * peak

clips = {
    'gobble': norm(fade(cut(load('hiltonproductions'), 0.0, 0.86)), 0.9),
    'gobble-b': norm(fade(cut(load('jarredgibb'), 0.0, 0.6)), 0.9),
    'hohoho': norm(fade(cut(load('cloud-10'), 0.98, 4.6), 5, 150), 0.92),   # ho ho ho... merry Christmas!
    'hohoho-b': norm(fade(cut(load('metaepitome'), 0.18, 3.3), 5, 200), 0.92),
}
for k, x in clips.items():
    pcm = (np.clip(x, -1, 1) * 32000).astype(np.int16)
    w = wave.open(f"{out}/clip-{k}.wav", 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(pcm.tobytes()); w.close()
    print(f"{k:9s} {len(x)/RATE:.2f} s")
