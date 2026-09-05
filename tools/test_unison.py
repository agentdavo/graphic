"""Public API client: input -> game state -> matching image/audio timestamps."""
from pathlib import Path
import struct
import subprocess
import sys
import wave

audio_build = Path(sys.argv[1])
suffix = ".exe" if sys.platform == "win32" else ""
out = Path("tests/out/sndmin/unison")
out.mkdir(exist_ok=True)
base = out / "audio"
subprocess.run([str(audio_build / ("06_unison" + suffix)), str(base), "--script"], check=True)
subprocess.run([str(audio_build / ("test" + suffix)), "--replay", str(base) + ".jrnl", "300", str(out / "replay.wav")], check=True)
data = base.with_suffix(".wav").read_bytes()
assert data == (out / "replay.wav").read_bytes()
with wave.open(str(base.with_suffix(".wav"))) as wav:
    assert wav.getnframes() == 240000 and wav.getframerate() == 48000
    pcm = wav.readframes(wav.getnframes())
# Scripted P/M/R presses pause, resume, mute, restore and restart transport.
# Visible cues occur on the same absolute ticks as the three audible attacks.
for onset in (60, 150, 260):
    before = pcm[(onset - 1) * 3200:onset * 3200]
    after = pcm[onset * 3200:(onset + 1) * 3200]
    assert not any(before) and any(after), f"audio onset differs at tick {onset}"
if len(sys.argv) > 2:
    graphics = str(Path(sys.argv[2])) + suffix
    selected = "0,60,61,100,130,150,160,180,200,260,299"
    for mode in ("direct", "legacy", "modern"):
        folder = out / mode
        folder.mkdir(exist_ok=True)
        if mode == "direct":
            args = [graphics, str(out / "av"), "--script"]
        else:
            # The small public example captures; vkmin's existing replay example
            # consumes the video packets independently of audio replay above.
            replay_exe = Path(sys.argv[2]).parent / ("ex_07_replay" + suffix)
            args = [str(replay_exe), "--replay", str(out / "av.jrnl"), f"--path={mode}"]
        args += ["--headless", "--frames", selected, "--size", "64", "64", "--out-dir", str(folder)]
        with (folder / "run.log").open("w") as log:
            subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, check=True)
        if mode == "direct":
            assert data == (out / "av.wav").read_bytes(), "render cadence changed transport audio"
            cue = (folder / "unison_0060.png").read_bytes()
            assert cue == (folder / "unison_0150.png").read_bytes() == (folder / "unison_0260.png").read_bytes()
            assert cue != (folder / "unison_0061.png").read_bytes(), "missing visible note cue"
        else:
            # Output names follow each program's title; compare by frame suffix.
            for tick in map(int, selected.split(",")):
                expected = list((out / "direct").glob(f"*_{tick:04}.png"))
                actual = list(folder.glob(f"*_{tick:04}.png"))
                assert len(expected) == len(actual) == 1 and expected[0].read_bytes() == actual[0].read_bytes()
    # A changed outer video timestamp must not survive stream extraction.
    stream = bytearray((out / "av.jrnl").read_bytes())
    at = 8
    while at + 12 <= len(stream):
        tag, frame, size = struct.unpack_from("<III", stream, at)
        if tag == 1:
            struct.pack_into("<I", stream, at + 4, 0xffffffff)
            break
        at += 12 + size
    (out / "bad-frame.jrnl").write_bytes(stream)
    result = subprocess.run([str(replay_exe), "--headless", "--replay", str(out / "bad-frame.jrnl")], capture_output=True)
    assert result.returncode != 0, "invalid video packet ordering accepted"
print("Unison: public API, three timed attacks, pause/mute/restart and deterministic replay passed.")
