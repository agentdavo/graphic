"""Compare GPU timestamps without PNG captures between frames.
Single blades and denser patches differ in coverage: this is a quality/cost
comparison, not an equivalent-work speedup measurement.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--exe', default='build/ex_20_valley')
parser.add_argument('--out', default='build/valley-profile.json')
parser.add_argument('--trials', type=int, default=3)
args = parser.parse_args()
assert args.trials > 0
records = []
output = Path(args.out)
output.parent.mkdir(parents=True, exist_ok=True)
for trial in range(args.trials):
    for mode, patch, taa in [('single', 0, 0), ('patch', 1, 0), ('patch-taa', 1, 1)]:
        command = [str(Path(args.exe).resolve()), '--headless', '--timings', '--frames', ','.join(map(str, range(64))),
                   '+r_width', '1280', '+r_height', '720', '+r_shadow_atlas', '4096', '+r_overlay', '0',
                   '+r_grass_patch', str(patch), '+taa', str(taa)]
        run = subprocess.run(command, capture_output=True, text=True)
        log = run.stdout + run.stderr
        match = re.search(r'GPU timestamps: (\d+) samples, mean ([\d.]+) ms \(min ([\d.]+), max ([\d.]+)\); scatter ([\d.]+), grass ([\d.]+), sky ([\d.]+), water ([\d.]+), TAA ([\d.]+) ms', log)
        if run.returncode or 'validation:' in log or not match:
            raise RuntimeError(log)
        values = dict(zip(['samples', 'mean_ms', 'min_ms', 'max_ms', 'scatter_ms', 'grass_ms', 'sky_ms', 'water_ms', 'taa_ms'], map(float, match.groups())))
        record = dict(mode=mode, trial=trial, **values)
        records.append(record)
        print(json.dumps(record), flush=True)
        output.with_suffix(f'.{trial}-{mode}.log').write_text(log, encoding='utf-8')
output.write_text(json.dumps(records, indent=2)+'\n', encoding='utf-8')
