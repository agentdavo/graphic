#!/usr/bin/env python3
"""Capture the complete native Omega loop and mux its own stereo score.

Writes an MP4 of the whole sequence, a short GIF excerpt that a Markdown
renderer will animate inline, and the individual stills the README uses.
Needs ffmpeg on PATH; on this machine that is C:/msys64/ucrt64/bin.

The demo is deterministic, so this is a pure function of the binary: the same
build produces the same 1800 frames and the same score every time.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TITLE = 'OMEGA - Through the Blue'          # vkmin names PNGs after the window title
SEQUENCE = 1800                             # OMEGA_SEQUENCE_TICKS, 60 a second
BATCH = 63                                  # VKMIN_MAX_FRAME_LIST is 64, less the warm-up tick

# t seconds -> tick. The beats are the demo's own, from omega.c's camera cuts.
STILLS = {
    'gate-charging':   180,   # t=3   pylons charging, the mouth still dark
    'gate-open':       480,   # t=8   the flash has opened the funnel
    'emergence':       660,   # t=11  cut to the broadside two-shot
    'engines':        1000,   # t=16.7 low stern-quarter, engines in the foreground
    'broadside':      1400,   # t=23  reverse along the lead hull, toward the attacker
    'fleet':          1700,   # t=28  high widening tableau, the final salvo
}


def run(command, log):
    log.write(f'\n$ {" ".join(map(str, command))}\n')
    log.flush()
    subprocess.run([str(c) for c in command], stdout=log, stderr=log, check=True, cwd=ROOT)


def capture(exe, frames, width, height, log):
    """Every checkpoint of the sequence, in batches the frame list can hold."""
    frames.mkdir(parents=True, exist_ok=True)
    for start in range(0, SEQUENCE, BATCH):
        end = min(start + BATCH, SEQUENCE)
        previous = frames / f'{TITLE}_{start - 1:04d}.png'
        saved = previous.read_bytes() if start else None
        # One preceding tick warms the motion history across the process
        # boundary, so the shutter smear does not restart at every batch.
        ticks = range(max(0, start - 1), end)
        run([exe, '--headless', '--size', width, height,
             '--frames', ','.join(map(str, ticks)), '--out-dir', frames], log)
        if saved is not None:
            previous.write_bytes(saved)     # the warm-up tick overwrote it
        print(f'captured {end}/{SEQUENCE}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/omega.exe')
    parser.add_argument('--work', type=Path, default=ROOT / 'build/video')
    parser.add_argument('--out', type=Path, default=ROOT / 'docs')
    parser.add_argument('--size', type=int, nargs=2, default=(1280, 720))
    parser.add_argument('--gif', type=int, nargs=2, default=(600, 930),
                        help='first and last tick of the GIF excerpt')
    parser.add_argument('--skip-capture', action='store_true', help='reuse frames already on disk')
    args = parser.parse_args()

    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        sys.exit('ffmpeg is not on PATH; add C:/msys64/ucrt64/bin')
    if not args.exe.is_file():
        sys.exit(f'{args.exe} does not exist; build the omega target first')

    frames = args.work / 'frames'
    images = args.out / 'images'
    images.mkdir(parents=True, exist_ok=True)
    args.work.mkdir(parents=True, exist_ok=True)
    width, height = args.size

    with (args.work / 'export.log').open('w', encoding='utf-8') as log:
        if not args.skip_capture:
            capture(args.exe, frames, width, height, log)
        present = len(list(frames.glob('*.png')))
        if present != SEQUENCE:
            sys.exit(f'{frames}: expected {SEQUENCE} frames, found {present}')

        wav = args.work / 'omega.wav'
        run([args.exe, '--audio-only', '--audio-out', wav], log)

        pattern = frames / f'{TITLE}_%04d.png'
        mp4 = args.out / 'omega-through-the-blue.mp4'
        run([ffmpeg, '-y', '-framerate', '60', '-i', pattern, '-i', wav,
             '-c:v', 'libx264', '-preset', 'slow', '-crf', '20', '-pix_fmt', 'yuv420p',
             '-c:a', 'aac', '-b:a', '192k', '-t', '30', '-movflags', '+faststart', mp4], log)

        # A GIF needs its own palette or the gate's blues band badly. Markdown
        # animates a GIF from a plain image link, which no video format gets.
        #
        # Bound the excerpt on the input side with -t. -frames:v truncates
        # *after* the filter chain, so with an fps filter it does not shorten
        # the excerpt at all: it emits that many output frames of a sequence
        # already stretched to the output rate, which looks like a slideshow.
        first, last = args.gif
        gif = images / 'omega.gif'
        run([ffmpeg, '-y', '-framerate', '60', '-start_number', first,
             '-t', f'{(last - first) / 60:.4f}', '-i', pattern,
             '-vf', 'fps=15,scale=480:-1:flags=lanczos,split[a][b];'
                    '[a]palettegen=max_colors=160[p];[b][p]paletteuse=dither=bayer:bayer_scale=3',
             '-loop', '0', gif], log)

        # JPEG for the rendered stills. At -q:v 2 they are indistinguishable
        # from the PNGs and about a seventeenth of the size, which matters for
        # something every clone carries forever. The inspector screenshots stay
        # PNG: they are UI text, which JPEG smears.
        for name, tick in STILLS.items():
            run([ffmpeg, '-y', '-i', frames / f'{TITLE}_{tick:04d}.png',
                 '-q:v', '2', images / f'omega-{name}.jpg'], log)
        weapon = args.work / 'weapons.png'
        run([args.exe, '--headless', '--weapon-view', '--size', width, height,
             '--frame', 812, '--out', weapon], log)
        run([ffmpeg, '-y', '-i', weapon, '-q:v', '2', images / 'omega-weapons.jpg'], log)

    for path in sorted(args.out.rglob('*')):
        if path.is_file():
            print(f'{path.relative_to(ROOT)}  {path.stat().st_size / 1e6:.1f} MB')


if __name__ == '__main__':
    main()
