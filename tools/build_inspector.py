#!/usr/bin/env python3
"""Package existing inspection artifacts as a self-contained, offline HTML workbench.

The source viewer also opens capture directories directly, without packaging.
This command never executes journals and never changes the source captures.
"""
import argparse
import base64
import csv
import hashlib
import json
from pathlib import Path

VIEWER = Path(__file__).resolve().parent.parent / 'inspector'


def child(root, name):
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError(f'Path escapes capture directory: {name}')
    return path


def read_optional(path):
    return path.read_text(encoding='utf-8') if path.is_file() else None


def capture(root, schema=None):
    meta = json.loads((root / 'capture.json').read_text(encoding='utf-8'))
    if not isinstance(meta.get('frame'), int) or meta['frame'] < 0 or not isinstance(meta.get('checkpoints'), list) or not meta['checkpoints']:
        raise ValueError('Invalid capture metadata')
    with (root / 'events.tsv').open(encoding='utf-8', newline='') as stream:
        events = [dict(row, event=int(row['event']), frame=int(row['frame'])) for row in csv.DictReader(stream, delimiter='\t')]
    assets, points = {}, []
    for event, label in meta['checkpoints']:
        if event is not None and (not isinstance(event, int) or event < 1):
            raise ValueError('Invalid checkpoint event')
        directory = f'event_{event:06d}' if event is not None else 'complete'
        point = {'event': event, 'label': label, 'dir': directory, 'images': [], 'error': None,
                 'resources': read_optional(root / directory / 'resources.txt'), 'metrics': None}
        metrics = read_optional(root / directory / 'metrics.json')
        if metrics:
            try:
                point['metrics'] = json.loads(metrics)
            except ValueError:
                point['metrics'] = {'error': 'Malformed metrics.json'}
        try:
            images = json.loads((root / directory / 'images.json').read_text(encoding='utf-8'))
            if not isinstance(images, list):
                raise ValueError('Invalid image list')
            for original in images:
                image = dict(original)
                try:
                    raw = child(root, directory + '/' + image['file']).read_bytes()
                    w, h, fmt = int(image['width']), int(image['height']), int(image['format'])
                    if w < 1 or h < 1 or w*h > 16777216:
                        raise ValueError('Invalid or oversized image (limit: 16 million pixels)')
                    if fmt not in (0, 1, 2, 8, 9, 10, 11, 12):
                        raise ValueError(f'Unsupported raw format {fmt}')
                    if len(raw) != w*h*(8 if fmt == 9 else 4):
                        raise ValueError('Truncated or oversized raw image')
                    digest = hashlib.sha256(raw).hexdigest()
                    if image.get('sha256') and image['sha256'] != digest:
                        raise ValueError('Raw image does not match recorded SHA-256')
                    image['data'] = digest
                    if digest not in assets:
                        assets[digest] = base64.b64encode(raw).decode('ascii')
                except (OSError, ValueError, KeyError, TypeError) as error:
                    image['error'] = str(error)
                point['images'].append(image)
        except (OSError, ValueError, TypeError) as error:
            point['error'] = str(error)
        points.append(point)
    schema_path = schema or root / 'push-schema.json'
    schemas = json.loads(schema_path.read_text(encoding='utf-8')) if schema_path.is_file() else {}
    return {'meta': meta, 'points': points, 'events': events, 'assets': assets, 'schemas': schemas, 'name': root.name}


def inline(boot=None):
    """The viewer as a single file: stylesheet and both scripts inlined, leaving
    no external reference and no request to make. It therefore opens straight
    from the filesystem, with no server. With boot=None no capture is embedded
    and the directory picker supplies one."""
    page = (VIEWER / 'index.html').read_text(encoding='utf-8')
    page = page.replace('<link rel="stylesheet" href="style.css">', '<style>' + (VIEWER / 'style.css').read_text(encoding='utf-8') + '</style>')
    if boot is not None:
        page = page.replace('/*__BOOT__*/', 'window.INSPECTOR_BOOT=' + boot + ';')
    for script in ('core.js', 'app.js'):
        page = page.replace(f'<script src="{script}"></script>', '<script>' + (VIEWER / script).read_text(encoding='utf-8') + '</script>')
    return page


def build(root, out, compare=None, schema=None):
    data = {'a': capture(root, schema), 'b': capture(compare, schema) if compare else None}
    # JSON is data in a script element: escape markup delimiters, including hostile labels.
    boot = json.dumps(data, ensure_ascii=True).replace('<', '\\u003c').replace('>', '\\u003e').replace('&', '\\u0026')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(inline(boot), encoding='utf-8')
    return data


def build_viewer(out):
    """The viewer alone, no capture embedded: one small file that opens any
    capture directory through the picker."""
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(inline(), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path, nargs='?')
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--schema', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--viewer-only', action='store_true',
                        help='write the standalone viewer with no capture embedded')
    args = parser.parse_args()
    if args.viewer_only:
        if args.capture or args.compare:
            parser.error('--viewer-only embeds no capture')
        build_viewer(args.out)
    else:
        if args.capture is None:
            parser.error('a capture directory is required unless --viewer-only')
        build(args.capture, args.out, args.compare, args.schema)
    print(f'Wrote {args.out.resolve()}')
