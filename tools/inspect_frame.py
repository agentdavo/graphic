#!/usr/bin/env python3
"""Replay a journal into an event trace, raw buffers and a local HTML inspection report.
Standard library only. Every checkpoint starts a fresh replay, preserving all history.
"""
import argparse
import csv
import hashlib
import html
import json
import math
from pathlib import Path
import struct
import subprocess
import zlib


def png(path, w, h, pixels):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y*w*4:(y+1)*w*4] for y in range(h))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', w, h, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def unsigned_float(bits, mantissa):
    exponent, fraction = bits >> mantissa, bits & ((1 << mantissa) - 1)
    if exponent == 31:
        return math.inf if fraction == 0 else math.nan
    return math.ldexp(fraction / (1 << mantissa), -14) if exponent == 0 else math.ldexp(1 + fraction / (1 << mantissa), exponent - 15)


def preview(raw, fmt, label=''):
    """Display transform only: raw bytes remain the authoritative comparison."""
    pixels = bytearray()
    def byte(v):
        return round(max(0, min(1, v if math.isfinite(v) else 0)) * 255)
    if fmt in (0, 1, 2):
        pixels = bytearray(raw)
        for i in range(0, len(pixels), 4):
            if fmt == 2: pixels[i], pixels[i+2] = pixels[i+2], pixels[i]
            pixels[i+3] = 255
        return pixels
    codes = {8: '<I', 9: '<4e', 10: '<f', 11: '<I', 12: '<2H'}
    if fmt not in codes: raise ValueError(f'unsupported format {fmt}')
    for values in struct.iter_unpack(codes[fmt], raw):
        if fmt == 11:
            ident = values[0]
            color = (0, 0, 0) if ident == 0 else tuple(((ident * p) & 255) / 255 for p in (97, 57, 23))
        elif fmt == 10:
            color = (values[0],) * 3
        elif fmt == 12:
            x, y = (v / 65535 * 2 - 1 for v in values)
            z = 1 - abs(x) - abs(y)
            if z < 0: x, y = (1-abs(y)) * (1 if x >= 0 else -1), (1-abs(x)) * (1 if y >= 0 else -1)
            length = math.sqrt(x*x + y*y + z*z)
            color = tuple(v / length * .5 + .5 for v in (x, y, z))
        else:
            if fmt == 8:
                packed = values[0]
                values = (unsigned_float(packed & 2047, 6), unsigned_float((packed >> 11) & 2047, 6), unsigned_float(packed >> 22, 5))
            color = tuple((max(0, v) / (1 + max(0, v))) ** (1/2.2) if math.isfinite(v) else 0 for v in values[:3])
        pixels.extend((*map(byte, color), 255))
    return pixels


def convert(directory):
    rows = list(csv.DictReader((directory / 'images.tsv').open(), delimiter='\t'))
    for row in rows:
        raw_path = directory / row['file']
        raw = raw_path.read_bytes()
        fmt, w, h = int(row['format']), int(row['width']), int(row['height'])
        expected = w*h*(8 if fmt == 9 else 4)
        if len(raw) != expected: raise ValueError(f'{raw_path}: expected {expected} bytes, got {len(raw)}')
        png(raw_path.with_suffix('.png'), w, h, preview(raw, fmt, row['label']))
        row['sha256'] = hashlib.sha256(raw).hexdigest()
    (directory / 'images.json').write_text(json.dumps(rows, indent=2))
    return rows


def run(command, log):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write_bytes(result.stdout)
    if result.returncode: raise RuntimeError(f'exit {result.returncode}: {command}\n{result.stdout.decode(errors="replace")}')


def file_hash(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journal', type=Path)
    parser.add_argument('--replay', type=Path, required=True)
    parser.add_argument('--frame', type=int, required=True)
    parser.add_argument('--out', type=Path, required=True, help='new output directory (never reuses stale captures)')
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--event', type=int, help='stop after this global one-based journal event')
    selection.add_argument('--passes', action='store_true', help='replay separately through each pass end')
    parser.add_argument('--path', choices=['legacy', 'modern'], default='legacy')
    args = parser.parse_args()
    if args.frame < 0 or (args.event is not None and args.event < 1): parser.error('frame must be nonnegative and event positive')
    args.out.mkdir(parents=True, exist_ok=False)
    base = [str(args.replay.resolve()), '--replay', str(args.journal.resolve()), '--frame', str(args.frame), '--path='+args.path]
    events = args.out / 'events.tsv'
    run(base + ['--events', str(events)], args.out / 'trace.log')
    records = list(csv.DictReader(events.open(), delimiter='\t'))
    checkpoints = [(args.event, 'event '+str(args.event))] if args.event else [(None, 'complete frame')]
    if args.passes:
        label = 'default pass'
        checkpoints = []
        for row in records:
            if int(row['frame']) != args.frame: continue
            if row['op'] == 'pass_begin': label = row['detail']
            if row['op'] == 'pass_end': checkpoints.append((int(row['event']), label))
        checkpoints.append((None, 'complete frame'))
    sections = []
    for event, label in checkpoints:
        directory = args.out / (f'event_{event:06d}' if event else 'complete')
        directory.mkdir()
        command = base + ['--inspect-dir', str(directory), '--metrics', str(directory / 'metrics.json')]
        if event: command += ['--stop-after-event', str(event)]
        run(command, directory / 'replay.log')
        rows = convert(directory)
        print(f'{directory}: {len(rows)} buffers', flush=True)
        cards = ''.join(f'<figure><img loading="lazy" src="{directory.name}/image_{r["slot"]}.png"><figcaption>'
                        f'{html.escape(r["label"])} · {r["width"]}×{r["height"]} · format {r["format"]} '
                        f'<a href="{directory.name}/{r["file"]}">raw</a></figcaption></figure>' for r in rows)
        sections.append(f'<section><h2>{event or "End"}: {html.escape(label)}</h2><div class="grid">{cards}</div></section>')
    document = '''<!doctype html><meta charset="utf-8"><title>vkmin frame inspection</title>
<style>body{background:#15171c;color:#e5e8ef;font:16px system-ui;margin:28px}a{color:#8bcaff}button{padding:8px 18px;margin:8px}header{position:sticky;top:0;background:#15171c;padding:8px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(360px,1fr));gap:16px}figure{margin:0;background:#22252e;padding:12px}img{width:100%;image-rendering:pixelated}figcaption{padding:8px}section{display:none}section.active{display:block}</style>
<header><h1>vkmin frame inspection</h1><button id="prev">Previous</button><button id="next">Next</button><span id="position"></span> · <a href="events.tsv">Event trace</a>
<p>Left/right arrows step checkpoints. HDR uses a display-only Reinhard curve; depth is raw 0–1, IDs are hashed colours, RG16 is decoded as an octahedral normal. Raw files preserve exact texels.</p></header>'''
    document += ''.join(sections) + '''<script>const pages=[...document.querySelectorAll('section')];let index=0;function show(delta){index=Math.max(0,Math.min(pages.length-1,index+delta));pages.forEach((p,i)=>p.classList.toggle('active',i===index));document.querySelector('#position').textContent=`${index+1} / ${pages.length}`;}document.querySelector('#prev').onclick=()=>show(-1);document.querySelector('#next').onclick=()=>show(1);document.onkeydown=e=>{if(e.key==='ArrowLeft')show(-1);if(e.key==='ArrowRight')show(1);};show(0);</script>'''
    (args.out / 'index.html').write_text(document, encoding='utf-8')
    (args.out / 'capture.json').write_text(json.dumps({'journal': str(args.journal.resolve()), 'sha256': file_hash(args.journal), 'frame': args.frame, 'path': args.path, 'checkpoints': checkpoints}, indent=2))


if __name__ == '__main__': main()
