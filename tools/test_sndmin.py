"""Bit equality of direct rendering, shared journal replay and committed WAVs."""
import hashlib
import gzip
from pathlib import Path
import struct
import subprocess
import sys

build = Path(sys.argv[1])
write = "--write-golden" in sys.argv[2:]
exe = build / ("test.exe" if sys.platform == "win32" else "test")
out = Path("tests/out/sndmin")
golden = Path("tests/golden/sndmin")
golden.mkdir(parents=True, exist_ok=True)
frozen = Path("tests/journals/sndmin")
frozen.mkdir(parents=True, exist_ok=True)
for name, frames in [("01_beep", 120), ("02_song", 2400), ("03_room", 840), ("04_valley", 600)]:
    wav = out / f"{name}.wav"
    replay = out / f"{name}-replay.wav"
    subprocess.run([str(exe), "--replay", str(out / f"{name}.jrnl"), str(frames), str(replay)], check=True)
    data = wav.read_bytes()
    assert data == replay.read_bytes(), f"{name}: journal replay differs"
    assert data[:4] == b"RIFF" and len(data) == struct.unpack_from("<I", data, 4)[0] + 8
    target = golden / wav.name
    if write:
        target.write_bytes(data)
        (frozen / f"{name}.jrnl.gz").write_bytes(gzip.compress((out / f"{name}.jrnl").read_bytes(), mtime=0))
    assert data == target.read_bytes(), f"{name}: golden differs"
    journal = out / f"{name}-frozen.jrnl"
    journal.write_bytes(gzip.decompress((frozen / f"{name}.jrnl.gz").read_bytes()))
    subprocess.run([str(exe), "--replay", str(journal), str(frames), str(replay)], check=True)
    assert data == replay.read_bytes(), f"{name}: committed journal differs from current example"
    print(f"{name}: direct == replay == golden; sha256 {hashlib.sha256(data).hexdigest()}")

# Truncation and unsupported opcode are rejected by the actual replay parser.
source = (out / "01_beep.jrnl").read_bytes()
cases = [source[:7], source[:-1], source + b"\x00"]
bad_opcode = bytearray(source)
at = 8
while at + 12 <= len(bad_opcode):
    tag, frame, size = struct.unpack_from("<III", bad_opcode, at)
    if tag == 2:
        struct.pack_into("<I", bad_opcode, at + 12 + 16, 99)
        break
    at += 12 + size
cases.append(bad_opcode)
# Reject incompatible ABI, layout and timebase metadata in new captures.
tag, frame, size = struct.unpack_from("<III", source, 8)
assert tag == 0x204 and frame == 0 and size == 28
for field, value in [(0, 99), (1, 44100), (2, 801), (3, 6), (4, 1), (5, 1), (6, 0x04030201)]:
    incompatible = bytearray(source)
    struct.pack_into("<I", incompatible, 20 + field * 4, value)
    cases.append(incompatible)
for index, bad in enumerate(cases):
    path = out / "invalid.jrnl"
    path.write_bytes(bad)
    result = subprocess.run([str(exe), "--replay", str(path), "1", str(out / "invalid.wav")], capture_output=True)
    # A sanitizer crash is not successful parser rejection. Require the normal
    # CHECK failure, with no preceding memory/undefined-behavior diagnostic.
    assert result.returncode == 1 and result.stderr.startswith(b"sndmin test:") and \
        result.stderr.rstrip().endswith(b"sndmin_replay(c,argv[2])"), \
        f"malformed journal {index}: unexpected exit {result.returncode}: {result.stderr!r}"
print(f"{len(cases)} malformed/incompatible journal cases rejected.")
core = list(Path("src").glob("sndmin*.c")) + list(Path("src").glob("sndmin*.h")) + [Path("src/jrnl.h")]
lines = sum(len(p.read_text().splitlines()) for p in core)
header = len(Path("src/sndmin.h").read_text().splitlines())
song = len(Path("examples/sndmin/02_song.snd").read_text().splitlines())
assert lines < 4000 and header < 250 and song < 60, (lines, header, song)
print(f"Line budgets: core {lines}/4000, header {header}/250, song {song}/60.")
