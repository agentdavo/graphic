"""One valley journal -> both PNGs and WAV; no live audio device."""
from pathlib import Path
import subprocess
import sys

exe = str(Path(sys.argv[1]))
if sys.platform == "win32" and not exe.endswith(".exe"):
    exe += ".exe"
root = Path("tests/out/sndmin/shared")
for folder in ("direct", "replay"):
    (root / folder).mkdir(parents=True, exist_ok=True)
journal = root / "valley.jrnl"
common = [exe, "--profile=lavapipe", "--frames", "0,3"]
for mode in ("direct", "replay"):
    args = common + ["--out-dir", str(root / mode), "--audio-out", str(root / f"{mode}.wav"),
                     "--audio-png", str(root / f"{mode}.png")]
    args += (["--shared-journal", str(journal)] if mode == "direct" else
             ["--path=legacy", "--replay", str(journal), "--audio-frames", "4"])
    with (root / f"{mode}.log").open("w") as log:
        subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, check=True)
for name in ("valley_0000.png", "valley_0003.png"):
    assert (root / "direct" / name).read_bytes() == (root / "replay" / name).read_bytes(), name
assert (root / "direct.wav").read_bytes() == (root / "replay.wav").read_bytes()
assert (root / "direct.png").read_bytes() == (root / "replay.png").read_bytes()
print("Shared valley journal: two identical frames, WAV and spectrogram; modern record -> legacy replay.")

if "--full" in sys.argv[2:]:
    # 7,200 simulation ticks under both schedules; 4 or 61 rendered images.
    checkpoints = [0, 2400, 4800, 7199]
    full = root / "route"
    full.mkdir(exist_ok=True)
    for mode in ("sparse", "legacy", "modern", "denser"):
        folder = full / mode
        folder.mkdir(exist_ok=True)
        selected = list(range(0, 7200, 120)) + [7199] if mode == "denser" else checkpoints
        args = [exe, "--profile=lavapipe", "--size", "160", "90", "r_shadow_atlas=256",
                "--frames", ",".join(map(str, selected)), "--out-dir", str(folder),
                "--audio-out", str(folder / "audio.wav"), "--audio-png", str(folder / "audio.png")]
        if mode == "sparse":
            args += ["--path=modern", "--shared-journal", str(full / "route.jrnl")]
        elif mode in ("legacy", "modern"):
            args += [f"--path={mode}", "--replay", str(full / "route.jrnl"), "--audio-frames", "7200"]
        with (folder / "run.log").open("w") as log:
            subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, check=True)
        if mode != "sparse":
            for name in ["audio.wav", "audio.png"] + [f"valley_{tick:04}.png" for tick in checkpoints]:
                assert (folder / name).read_bytes() == (full / "sparse" / name).read_bytes(), (mode, name)
        print(f"Full valley route: {mode} passed", flush=True)
    print("120 seconds: every audio tick, identical PCM across 4/61 rendered frames and both Vulkan replays.")
