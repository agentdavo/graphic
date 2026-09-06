"""The ambient example is the test: replay, compact golden, headroom and tail."""
import array
import gzip
import hashlib
from pathlib import Path
import subprocess
import os
import sys
import wave

build = Path(sys.argv[1])
out = Path("tests/out/sndmin")
name = "05_ambience"
exe = build / ("test.exe" if sys.platform == "win32" else "test")
wav = out / f"{name}.wav"
replay = out / f"{name}-replay.wav"
frozen = Path("tests/journals/sndmin") / f"{name}.jrnl.gz"
golden = Path(os.environ.get("SNDMIN_GOLDEN", "tests/golden/sndmin")) / f"{name}.sha256"
golden.parent.mkdir(parents=True, exist_ok=True)
data = wav.read_bytes()
digest = hashlib.sha256(data).hexdigest()
if "--write-golden" in sys.argv[2:]:
    frozen.write_bytes(gzip.compress((out / f"{name}.jrnl").read_bytes(), mtime=0))
    golden.write_text(digest + "\n", encoding="ascii")
assert golden.exists(), f"no golden in {golden.parent} (run `make sndmin-golden` with SNDMIN_GOLDEN={golden.parent} on this platform)"
assert digest == golden.read_text(encoding="ascii").strip(), "ambient WAV golden differs"
frozen_input = out / f"{name}-frozen.jrnl"
frozen_input.write_bytes(gzip.decompress(frozen.read_bytes()))
for journal in (out / f"{name}.jrnl", frozen_input):
    result = subprocess.run([str(exe), "--replay", str(journal), "7440", str(replay)],
                            check=True, capture_output=True, text=True)
    assert "voices=0 stolen=0 underruns=0 late=0 dropped=0" in result.stdout, result.stdout
    assert data == replay.read_bytes(), f"ambient replay differs: {journal}"
with wave.open(str(wav)) as stream:
    assert (stream.getnchannels(), stream.getsampwidth(), stream.getframerate(),
            stream.getnframes()) == (2, 2, 48000, 7440 * 800)
    samples = array.array("h", stream.readframes(stream.getnframes()))
if sys.byteorder != "little":
    samples.byteswap()
peak = max(abs(sample) for sample in samples) / 32768
assert 0.01 < peak < 0.8, f"silent or insufficient headroom: {peak}"
# Every phrase must contain audio; the last second must have decayed below -60 dBFS RMS.
for start in range(0, 120 * 96000, 15 * 96000):
    section = samples[start:start + 15 * 96000]
    assert sum(sample * sample for sample in section) / len(section) > (0.001 * 32768) ** 2
tail = samples[-96000:]
assert sum(sample * sample for sample in tail) / len(tail) < (0.001 * 32768) ** 2
print(f"Morning water: direct == replay == frozen == SHA256 golden {digest}; 124 s, peak {peak:.5f}, tail passed.")
