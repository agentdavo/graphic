"""Capture the complete native Omega loop and mux its original stereo score."""
import os
from pathlib import Path
import subprocess
import imageio_ffmpeg

ROOT = Path(__file__).resolve().parents[1]
work = ROOT / 'build/omega-video'
frames = work / 'frames'
frames.mkdir(parents=True, exist_ok=True)
output = ROOT / 'exports/omega-through-the-blue.mp4'
output.parent.mkdir(exist_ok=True)
exe = ROOT / 'build/omega-clang/ex_21_omega.exe'
env = os.environ.copy()
env['PATH'] = 'C:/msys64/clang64/bin;' + env['PATH']
pattern = 'OMEGA - Through the Blue_{:04d}.png'
with (work / 'capture.log').open('w') as log:
    for start in range(0, 1200, 63):
        end = min(start + 63, 1200)
        previous = frames / pattern.format(start - 1)
        saved = previous.read_bytes() if start else None
        # One preceding tick warms motion history at each process boundary.
        ticks = range(max(0, start - 1), end)
        subprocess.run([str(exe), '--headless', '--size', '1920', '1080',
                        '--frames', ','.join(map(str, ticks)), '--out-dir', str(frames)],
                       cwd=ROOT, env=env, stdout=log, stderr=log, check=True)
        if saved is not None:
            previous.write_bytes(saved)
        print(f'Captured {end}/1200 frames', flush=True)
    wav = work / 'omega.wav'
    subprocess.run([str(exe), '--audio-only', '--audio-out', str(wav)],
                   cwd=ROOT, env=env, stdout=log, stderr=log, check=True)
    assert len(list(frames.glob('*.png'))) == 1200
    subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), '-y', '-framerate', '60',
                    '-i', str(frames / 'OMEGA - Through the Blue_%04d.png'),
                    '-i', str(wav), '-c:v', 'libx264', '-preset', 'slow', '-crf', '18',
                    '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '320k',
                    '-t', '20', '-movflags', '+faststart', str(output)],
                   stdout=log, stderr=log, check=True)
print(f'Saved {output}', flush=True)
