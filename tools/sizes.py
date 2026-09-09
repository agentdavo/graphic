#!/usr/bin/env python3
"""Report code size per owner, and fail if a group is over budget.

The tree used to enforce line budgets from its test suite. That suite was
removed, so for a while the budgets were numbers in a README that nothing
checked -- which is the failure mode CLAUDE.md section 4 names outright: a
check that quietly becomes a no-op is worse than no check.

This counts *code*, not lines. Comments and blanks are stripped first, because
a budget that counts comments charges you for explaining the code and makes a
documentation pass look like growth. C and GLSL are both C-like, so one
stripper serves both.

    python tools/sizes.py            # report
    python tools/sizes.py --check    # report, and exit 1 if anything is over
"""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

# Budgets are on code lines. They exist to make growth a decision rather than a
# drift; raising one deliberately is fine, discovering you passed it is not.
GROUPS = {
    'vkmin core':        (4200, ['src/vkmin.c', 'src/vkmin_cvar.c', 'src/vkmin_stb.c']),
    'public header':     ( 300, ['src/vkmin.h']),
    'gpu headers':       ( 900, ['src/vkmin_gpu.h', 'src/vkmin_spirv.h', 'src/vkmin_inspect.h',
                                 'src/vkmin_cvar.h', 'src/vkmin_math.h', 'src/vkmin_stb.h', 'src/vkmin_pure.h', 'src/vkmin_arena.h']),
    'common':            ( 700, ['src/min_types.h', 'src/min_math.h', 'src/min_jrnl.h']),
    'platform':          (1100, ['src/vkmin_plat.h', 'src/vkmin_plat_common.h', 'src/vkmin_plat_sdl.h',
                                 'src/vkmin_plat_glfw.c', 'src/vkmin_plat_sdl2.c',
                                 'src/vkmin_plat_sdl3.c', 'src/vkmin_plat_win32.c']),
    'render layer':      (2600, ['src/render.c', 'src/render_geometry.c', 'src/render_ktx2.c',
                                 'src/render_scene.c']),
    'render headers':    (1000, ['src/render.h', 'src/render_geometry.h', 'src/render_ktx2.h',
                                 'src/render_scene.h', 'src/render_shared.h', 'src/render_format.h',
                                 'src/render_pack.h']),
    'sndmin':            (2200, ['src/sndmin.c', 'src/sndmin_acoustics.c', 'src/sndmin_song.c',
                                 'src/sndmin_output.c', 'src/sndmin_null.c']),
    'sndmin headers':    ( 900, ['src/sndmin.h', 'src/sndmin_dsp.h', 'src/sndmin_internal.h',
                                 'src/sndmin_plat.h', 'src/sndmin_io.h']),
    'shaders':           (2000, ['src/shaders/*.vert', 'src/shaders/*.frag', 'src/shaders/*.comp',
                                 'src/shaders/*.glsl', 'src/shaders/lib/*.glsl']),
    'demos':             (1700, ['omega/omega.c', 'omega/omega_weapons.h', 'omega/omega_shared.h', 'demo/scene.c', 'demo/gamekit.h', 'demo/anim.h']),
}

# Generated, vendored or data: measured for information, never budgeted. The
# font and the model are baked arrays, and third_party is not ours to shrink.
EXCLUDED = ['src/render_font.h', 'omega/omega_model.h', 'omega/omega_model.c',
            'omega/omega_fury.h', 'omega/omega_fury.c',
            'omega/omega_mounts.h', 'omega/omega_surface.h', 'src/third_party/*']


def code_lines(text):
    """Lines left after removing comments, string-aware, and blanks."""
    out, i, n = [], 0, len(text)
    line, in_block, in_str, quote = '', False, False, ''
    while i < n:
        c, nxt = text[i], text[i + 1] if i + 1 < n else ''
        if in_block:
            if c == '*' and nxt == '/':
                in_block, i = False, i + 2
                continue
        elif in_str:
            line += c
            if c == '\\':
                line += nxt
                i += 2
                continue
            if c == quote:
                in_str = False
        elif c == '/' and nxt == '*':
            in_block, i = True, i + 2
            continue
        elif c == '/' and nxt == '/':
            while i < n and text[i] != '\n':
                i += 1
            continue
        elif c in '"\'':
            in_str, quote = True, c
            line += c
        elif c != '\n':
            line += c
        if c == '\n':
            if line.strip():
                out.append(line)
            line = ''
        i += 1
    if line.strip():
        out.append(line)
    return len(out)


def measure(patterns):
    total, missing = 0, []
    for pattern in patterns:
        matches = sorted(ROOT.glob(pattern)) if '*' in pattern else [ROOT / pattern]
        if not matches:
            missing.append(pattern)
        for path in matches:
            if not path.is_file():
                missing.append(pattern)
                continue
            total += code_lines(path.read_text(encoding='utf-8', errors='replace'))
    return total, missing


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='exit 1 if any group is over budget')
    args = parser.parse_args()

    over, gone, width = [], [], max(len(g) for g in GROUPS)
    print(f'{"":{width}}   code  budget')
    for name, (budget, patterns) in GROUPS.items():
        count, missing = measure(patterns)
        gone += missing
        flag = ''
        if count > budget:
            over.append((name, count, budget))
            flag = '  OVER'
        print(f'{name:{width}} {count:6} {budget:7}{flag}   {count * 100 // budget:3}%')
    excluded, _ = measure(EXCLUDED)
    print(f'\n{"generated and vendored":{width}} {excluded:6}       -   not budgeted')

    if gone:
        print('\nlisted but not found (a rename that outran this file):', ', '.join(sorted(set(gone))))
    if over:
        print('\nover budget:')
        for name, count, budget in over:
            print(f'  {name}: {count} of {budget}, by {count - budget}')
        print('Raising a budget on purpose is fine. Passing one without noticing is the thing\n'
              'this exists to prevent, so change the number in the same commit as the code.')
    if args.check and (over or gone):
        sys.exit(1)


if __name__ == '__main__':
    main()
