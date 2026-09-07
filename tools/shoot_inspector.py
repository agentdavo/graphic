#!/usr/bin/env python3
"""Screenshot the inspector with a capture loaded, for the README.

The workbench page embeds its capture and makes no request, so a headless
browser opens it straight from the filesystem. Selecting a checkpoint is the
one thing a still cannot do for itself, so a small script does the click the
reader would: it sets the same <select> and dispatches the same event.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_inspector import build                                    # noqa: E402

BROWSERS = [
    Path('C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'),
    Path('C:/Program Files/Microsoft/Edge/Application/msedge.exe'),
    Path('C:/Program Files/Google/Chrome/Application/chrome.exe'),
    Path('C:/Program Files (x86)/Google/Chrome/Application/chrome.exe'),
]

# name -> checkpoint index in the capture's own order, or None to leave the
# page as it opens (the complete frame).
SHOTS = {'inspector-omega': None, 'inspector-shadow': 0}

SELECT = """<script>
addEventListener('load', () => setTimeout(() => {
    const element = document.getElementById('checkpoint');
    if (!element) return;
    element.value = '%d';
    element.dispatchEvent(new Event('change', {bubbles: true}));
}, 250));
</script>"""


def browser(explicit):
    if explicit:
        return explicit
    for candidate in BROWSERS:
        if candidate.is_file():
            return candidate
    sys.exit('no Chrome or Edge found; pass --browser')


def shoot(exe, page, out, profile, size):
    subprocess.run([str(exe), '--headless=new', '--disable-gpu', '--no-first-run',
                    f'--user-data-dir={profile}', f'--window-size={size[0]},{size[1]}',
                    '--virtual-time-budget=20000', f'--screenshot={out}',
                    page.resolve().as_uri()],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    if not out.is_file():
        sys.exit(f'{out}: the browser wrote no screenshot')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--out', type=Path, default=ROOT / 'docs/images')
    parser.add_argument('--work', type=Path, default=ROOT / 'build/shots')
    parser.add_argument('--browser', type=Path)
    parser.add_argument('--size', type=int, nargs=2, default=(1500, 950))
    args = parser.parse_args()

    exe = browser(args.browser)
    args.out.mkdir(parents=True, exist_ok=True)
    args.work.mkdir(parents=True, exist_ok=True)
    profile = args.work / 'profile'

    page = args.work / 'workbench.html'
    build(args.capture, page)
    for name, checkpoint in SHOTS.items():
        target = page
        if checkpoint is not None:
            target = args.work / f'{name}.html'
            target.write_text(page.read_text(encoding='utf-8') + SELECT % checkpoint, encoding='utf-8')
        out = args.out / f'{name}.png'
        shoot(exe, target, out, profile, args.size)
        print(f'{out.relative_to(ROOT)}  {out.stat().st_size / 1e3:.0f} KB')
    shutil.rmtree(profile, ignore_errors=True)


if __name__ == '__main__':
    main()
