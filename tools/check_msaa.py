#!/usr/bin/env python3
"""GPU MSAA regression: build test_msaa/replay first; use Debug for validation.

Checks supported sample counts, MRT color/depth resolves, alpha-to-coverage,
record/replay agreement, and the optional EXT path when the device supports it.
Only standard-library dependencies; all outputs go into a new directory.
"""
import argparse
import json
import os
import subprocess
from pathlib import Path
import struct

from inspect_frame import convert, run


def check(build, out, require_samples=(), require_single=False):
    suffix = '.exe' if (build / 'replay.exe').exists() else ''
    fixture = str((build / ('test_msaa' + suffix)).resolve())
    replay = str((build / ('replay' + suffix)).resolve())
    out.mkdir(parents=True, exist_ok=False)
    checked = 0
    covered_samples = set()
    covered_single = False
    skipped = []
    validation = False
    for samples, coverage, single in [(n, 0, 0) for n in (1, 2, 4, 8, 16, 32, 64)] + [(4, 1, 0), (4, 0, 1), (4, 1, 1)]:
        case = out / f'{samples}x-coverage{coverage}-single{single}'; case.mkdir()
        journal = case / 'frame.vkj'
        run([fixture, '--frame', '0', '--out', str(case / 'direct.png'), '--record', str(journal),
             '+r_msaa', str(samples), '+r_alpha_to_coverage', str(coverage), '+r_msaa_single', str(single)], case / 'record.log')
        if 'SKIP ' in (case / 'record.log').read_text():
            skipped.append(case.name)
            print(case.name + ': unsupported, skipped', flush=True)
            continue
        covered_samples.add(samples)
        covered_single |= bool(single)
        validation = 'debug pipelines' in (case/'record.log').read_text()
        paths = ['legacy'] + (['modern'] if 'path = modern' in (case/'record.log').read_text() else [])
        for path in paths:
            capture = case / path; capture.mkdir()
            run([replay, '--replay', str(journal), '--frame', '0', '--path=' + path,
                 '--inspect-dir', str(capture), '--out', str(capture / 'replayed.png')], capture / 'replay.log')
            rows = convert(capture)
            # PNGs are produced by the same library/settings, so byte equality
            # checks full images as well as the analytical pixel tests below.
            if (case / 'direct.png').read_bytes() != (capture / 'replayed.png').read_bytes():
                raise AssertionError('Direct/replayed image mismatch: ' + case.name)
            color_label = 'color samples' if single else 'backbuffer'
            extra_label = 'extra samples' if single else 'extra resolve'
            depth_label = 'depth samples' if single or samples == 1 else 'depth resolve'
            raw = {}
            for label in (color_label, extra_label, depth_label):
                image = next(r for r in rows if r['label'] == label)
                raw[label] = (capture / image['file']).read_bytes()
            at = (32*64 + 32)*4
            color = raw[color_label][at:at+4]
            extra = raw[extra_label][at:at+4]
            depth = struct.unpack_from('<f', raw[depth_label], at)[0]
            if coverage:
                assert color == bytes(4) and extra == bytes(4) and depth == 1.0, (case.name, color, extra, depth)
            else:
                assert all(0 < v < 255 for v in color[:3]), (case.name, color)
                assert all(abs(a+b-255) <= 1 for a,b in zip(color[:3],extra[:3])), (case.name, color, extra)
                assert depth == 0.0, (case.name, depth)
            for helper_label, expected in [('helper.color0', raw[color_label]),
                                            ('helper.color1', raw[extra_label]),
                                            ('helper.depth0', raw[depth_label])]:
                helper = next(r for r in rows if r['label'] == helper_label)
                assert (capture/helper['file']).read_bytes() == expected, 'Target helper resolve mismatch: ' + helper_label
            vertex = next(r for r in rows if r['label'] == 'vertex sample.color0')
            assert (capture/vertex['file']).read_bytes()[at:at+3] == raw[color_label][at:at+3], 'Vertex-stage texture read differs'
            assert struct.unpack_from('<f', raw[depth_label], 0)[0] == 1.0
        checked += 1
        print(case.name + ': passed', flush=True)
    # Wrapper transport/arena rules are checked in every build. API rules belong
    # to Khronos validation: deliberately invalid Vulkan calls run only in Debug.
    invalid_cases = [('push', 'passed no push block'), ('ring', 'host ring region exhausted'),
                     ('upload', 'overruns a'), ('fill', 'fill overruns buffer'),
                     ('barrier', 'barrier requires'), ('indirect', 'indirect commands overrun buffer')]
    if validation:
        invalid_cases += [(case, 'validation: VUID-') for case in ('pipeline', 'resolve', 'depth', 'attachment')]
    for invalid, diagnostic in invalid_cases:
        env = dict(os.environ, VKMIN_TEST_INVALID=invalid)
        result = subprocess.run([fixture, '--frame', '0', '--out', str(out/'invalid.png'), '+r_msaa', '4'],
                                env=env, capture_output=True, text=True, timeout=120)
        (out/('invalid-'+invalid+'.log')).write_text(result.stdout+result.stderr)
        assert result.returncode != 0 and diagnostic in result.stderr, (invalid, result.stderr)
    print('Wrapper bounds/transport and available Khronos validation checks: passed', flush=True)
    run([fixture, '--frames', '0,1,2', '+r_msaa', '4'], out/'sparse-timestamps.log')
    print('Sparse timestamps through frame-slot reuse: passed', flush=True)
    for overrides in ([], ['+r_vsync', '0', '+r_sync_naive', '0', '+r_readback', '1']):
        result = subprocess.run([fixture, '--cvars']+overrides, env=dict(os.environ, VKMIN_TEST_CONFIG='1'),
                                capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        values = {parts[0]: parts[1] for line in result.stdout.splitlines() if len(parts := line.split()) >= 2}
        assert [values[key] for key in ('r_vsync', 'r_sync_naive', 'r_readback')] == (['0','0','1'] if overrides else ['1','1','0'])
    print('Descriptor defaults and explicit command-line overrides: passed', flush=True)
    for args in (['--frame','garbage'], ['--frame','-1'], ['--frames','1,2junk'], ['--frames','1,'],
                 ['--frame','2147483648'], ['--size','0','64'], ['--size','x','64'], ['--budget','nan'], ['+r_msaa']):
        result = subprocess.run([fixture]+args, capture_output=True, text=True)
        assert result.returncode != 0 and 'device[0]' not in result.stderr, (args, result.stderr)
    print('Malformed CLI input rejected before device creation: passed', flush=True)
    assert checked, 'No configurations tested'
    # OMEGA has ring-addressed scene data and a 16 MiB host ring; replay defaults
    # to 64 MiB. Its second frame catches absolute-ring relocation mistakes that
    # a push-only triangle cannot. Also change replay's frames-in-flight policy.
    omega = (build / ('omega' + suffix)).resolve()
    if not omega.is_file(): raise RuntimeError('Build the omega target for the ring replay regression')
    case = out / 'omega-ring'; case.mkdir()
    journal = case / 'frames.vkj'
    run([str(omega), '--headless', '--size', '320', '180', '--frames', '299,300,301',
         '--out-dir', str(case), '--record', str(journal), '+r_msaa', '4'], case / 'record.log')
    for extra in ([], ['--sync-naive']):
        output = case / ('replay-naive.png' if extra else 'replay.png')
        run([replay, '--replay', str(journal), '--frame', '300', '--path=legacy', '--out', str(output)] + extra,
            output.with_suffix('.log'))
        assert output.read_bytes() == (case/'OMEGA - Through the Blue_0300.png').read_bytes(), 'Frame-ring relocation mismatch'
    print('Omega multi-frame ring relocation: passed', flush=True)
    missing = sorted(set(require_samples)-covered_samples)
    report = {'passed_configurations': checked, 'omega_ring_replay': True, 'samples': sorted(covered_samples),
              'single_sampled_extension': covered_single, 'skipped': skipped,
              'required_coverage_met': not missing and (not require_single or covered_single)}
    (out / 'result.json').write_text(json.dumps(report, indent=2))
    if not report['required_coverage_met']:
        raise RuntimeError(f'Required hardware coverage missing: samples={missing}, extension={require_single and not covered_single}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--require-samples', type=int, nargs='*', default=[])
    parser.add_argument('--require-single', action='store_true', help='fail rather than skip when EXT is unavailable')
    args = parser.parse_args()
    check(args.build, args.out, args.require_samples, args.require_single)
