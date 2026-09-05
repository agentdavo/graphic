"""Create the redistributable streaming fixture from deterministic noise.
Developer-only: numpy + soundfile/libsndfile. The Ogg itself is committed;
test runs need neither an encoder nor these Python packages.
"""
from pathlib import Path
import numpy as np
import soundfile as sf

count = 48000 * 3
x = np.arange(count, dtype=np.uint32)
x ^= x >> 16
x *= np.uint32(0x7FEB352D)
x ^= x >> 15
x *= np.uint32(0x846CA68B)
x ^= x >> 16
noise = (x >> 8).astype(np.float32) / 8388608 - 1
smooth = np.convolve(noise, np.ones(11, dtype=np.float32) / 11, "same")
left = smooth * 0.48 + noise * 0.055
right = np.roll(smooth, 131) * 0.48 + np.roll(noise, 67) * 0.055
# Crossfade the ends before encoding a loopable ambience.
fade = np.linspace(0, 1, 2400, dtype=np.float32)
for channel in (left, right):
    channel[:2400] = channel[:2400] * fade + channel[-2400:] * (1 - fade)
sf.write(Path("tests/assets/sndmin_river.ogg"), np.column_stack([left, right]), 48000, format="OGG", subtype="VORBIS")
print("Wrote original procedural river fixture (CC0).")
