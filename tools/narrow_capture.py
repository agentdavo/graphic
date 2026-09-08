#!/usr/bin/env python3
"""Find the first observable attachment divergence by replaying events in order.

Starts at frame begin, so a temporary difference later overwritten is not missed.
The first differing coarse checkpoint bounds the search. Each probe is a fresh
replay with all prior history. Requires corresponding event sequences; this is
not an alignment algorithm for unrelated recordings or a shader-causality proof.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import struct

from build_inspector import build, child
from inspect_frame import (capture_metadata, checkpoints_directory, convert,
                           file_hash, run, unsigned_float)


def components(raw, fmt):
    codes = {0: '<4B', 1: '<4B', 2: '<4B', 8: '<I', 9: '<4e',
             10: '<f', 11: '<I', 12: '<2H'}
    if fmt not in codes:
        raise ValueError(f'Unsupported format {fmt}')
    for values in struct.iter_unpack(codes[fmt], raw):
        if fmt == 8:
            v = values[0]
            values = (unsigned_float(v & 2047, 6), unsigned_float((v >> 11) & 2047, 6),
                      unsigned_float(v >> 22, 5))
        yield values


def compare(a, b, numeric=False, absolute=0, relative=0):
    """Compare complete attachment sets, rejecting absent or incompatible data."""
    if not all(math.isfinite(x) and x >= 0 for x in (absolute, relative)):
        raise ValueError('Tolerances must be finite and nonnegative')
    aa = json.loads((a / 'images.json').read_text())
    bb = json.loads((b / 'images.json').read_text())
    key = lambda x: (str(x['slot']), x.get('id'), int(x.get('samples', 1)), int(x['width']), int(x['height']), int(x['format']))
    aa.sort(key=key); bb.sort(key=key)
    if list(map(key, aa)) != list(map(key, bb)):
        raise ValueError('Attachment layouts or resource identities differ')
    for x, y in zip(aa, bb):
        if int(x.get('samples', 1)) > 1: continue  # only resolved/single-sample texels are inspectable
        ar = child(a, x['file']).read_bytes(); br = child(b, y['file']).read_bytes()
        w, h, fmt = int(x['width']), int(x['height']), int(x['format'])
        stride = 8 if fmt == 9 else 4
        if w < 1 or h < 1 or len(ar) != w*h*stride or len(br) != len(ar):
            raise ValueError('Truncated or oversized raw attachment')
        if ar == br:
            continue
        if numeric:
            for pixel, (av, bv) in enumerate(zip(components(ar, fmt), components(br, fmt))):
                # Identical NaN payloads match, distinct NaNs never pass tolerance.
                if ar[pixel*stride:(pixel+1)*stride] == br[pixel*stride:(pixel+1)*stride]:
                    continue
                if any(v != t and (not math.isfinite(v) or not math.isfinite(t) or
                       abs(v-t) > absolute + relative*max(abs(v), abs(t))) for v, t in zip(av, bv)):
                    return {'slot': x['slot'], 'pixel': [pixel % w, pixel // w]}
        else:
            byte = next(i for i, (v, t) in enumerate(zip(ar, br)) if v != t)
            return {'slot': x['slot'], 'pixel': [(byte//stride) % w, (byte//stride)//w]}
    return None


def candidates(events_a, events_b, frame, upper):
    aa = [e for e in events_a if int(e['frame']) == frame and int(e['event']) <= upper]
    bb = [e for e in events_b if int(e['frame']) == frame and int(e['event']) <= upper]
    if [(e['event'], e['op']) for e in aa] != [(e['event'], e['op']) for e in bb]:
        raise ValueError('Event sequences differ; cannot establish correspondence')
    active = False
    result = []
    for e in aa:
        if e['op'] == 'frame_begin':
            active = True
        if active:
            result.append((int(e['event']), e['op']))
        if e['op'] == 'frame_end':
            active = False
    if not result:
        raise ValueError('No frame events found')
    return result


def narrow(a, b, out, replay_a=None, replay_b=None, numeric=False, absolute=0, relative=0):
    roots = [a, b]
    metas = [json.loads((root / 'capture.json').read_text()) for root in roots]
    if metas[0]['frame'] != metas[1]['frame'] or metas[0]['checkpoints'] != metas[1]['checkpoints']:
        raise ValueError('Captures require matching frame and checkpoint selections')
    traces = []
    for root in roots:
        with (root / 'events.tsv').open() as stream:
            traces.append(list(csv.DictReader(stream, delimiter='\t')))
    coarse = None
    for event, _ in metas[0]['checkpoints']:
        if compare(a / checkpoints_directory(event), b / checkpoints_directory(event), numeric, absolute, relative):
            coarse = event if event is not None else max(int(e['event']) for e in traces[0] if int(e['frame']) == metas[0]['frame'])
            break
    if coarse is None:
        return {'status': 'match', 'scope': 'All supplied checkpoints; intermediate events were not probed'}
    probes = candidates(*traces, metas[0]['frame'], coarse)
    executables = []
    for meta, override in zip(metas, (replay_a, replay_b)):
        if file_hash(Path(meta['journal'])) != meta['sha256']:
            raise ValueError('Journal changed since capture')
        saved = meta.get('metadata', {})
        executable = override or saved.get('replay_executable')
        if not executable:
            raise ValueError('Old capture: supply --replay-a and --replay-b')
        executable = Path(executable).resolve()
        if not override and file_hash(executable) != saved.get('replay_sha256'):
            raise ValueError('Replay binary changed; supply an explicit replay override')
        executables.append(executable)
    out.mkdir(parents=True, exist_ok=False)
    out = out.resolve()
    outputs = [out / 'a', out / 'b']
    for root, output in zip(roots, outputs):
        output.mkdir()
        (output / 'events.tsv').write_bytes((root / 'events.tsv').read_bytes())
        if (root / 'push-schema.json').is_file():
            (output / 'push-schema.json').write_bytes((root / 'push-schema.json').read_bytes())
    checkpoints = []
    result = {'status': 'not_reproduced', 'scope': 'The supplied coarse difference did not recur in fresh sequential replays'}
    for event, op in probes:
        for meta, exe, output in zip(metas, executables, outputs):
            directory = output / checkpoints_directory(event); directory.mkdir()
            command = [str(exe), '--replay', meta['journal'], '--frame', str(meta['frame']),
                       '--path=' + meta.get('path', 'legacy'), '--stop-after-event', str(event),
                       '--inspect-dir', str(directory), '--metrics', str(directory / 'metrics.json')]
            run(command, directory / 'replay.log'); convert(directory)
        checkpoints.append([event, op])
        delta = compare(*(output / checkpoints_directory(event) for output in outputs), numeric, absolute, relative)
        print(f'Event {event} {op}: {"different" if delta else "match"}', flush=True)
        if delta:
            result = {'status': 'different', 'event': event, 'operation': op, **delta,
                      'scope': 'First observable single-sample attachment divergence in sequential probes; not proof of causal shader instruction'}
            break
    for meta, exe, output in zip(metas, executables, outputs):
        updated = dict(meta, checkpoints=checkpoints, capture_directory=str(output.resolve()), comparison={'numeric': numeric, 'absolute': absolute, 'relative': relative}, metadata=capture_metadata(exe, output / checkpoints_directory(checkpoints[0][0])))
        (output / 'capture.json').write_text(json.dumps(updated, indent=2))
    result['comparison'] = {'numeric': numeric, 'absolute': absolute, 'relative': relative}
    (out / 'result.json').write_text(json.dumps(result, indent=2))
    build(outputs[0], out / 'inspector.html', outputs[1])
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture_a', type=Path); parser.add_argument('capture_b', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--replay-a', type=Path); parser.add_argument('--replay-b', type=Path)
    parser.add_argument('--numeric', action='store_true')
    parser.add_argument('--absolute', type=float, default=0); parser.add_argument('--relative', type=float, default=0)
    args = parser.parse_args()
    print(json.dumps(narrow(args.capture_a, args.capture_b, args.out, args.replay_a, args.replay_b,
                           args.numeric, args.absolute, args.relative), indent=2))
