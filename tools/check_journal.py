#!/usr/bin/env python3
"""Bounded, offline journal admission. Never creates a Vulkan instance or executes shaders.
A pass proves framing/transport checks only; GPU execution still requires trusted input.
"""
import argparse
import json
from pathlib import Path
import struct
import tempfile

MAGIC = 0x4A4D4B56
SIGNATURE = 0x395249574E494D56
SIZES = [0,56,4,16,80,4,16,20,20,108,160,16,16,8,56,56,120,4,56,16,20,56,20,4,16,108]
PAYLOAD = {1,3,4,6,9,11,13,15,20,21,22,25}

def abi_tag():
    # Explicit supported wire shapes, independently stated from the C structs.
    shapes = [32,16,8,56,80,108,16,160,56,120,20,56,8,16,8,32,24,136,108]
    raw = struct.pack('<19I', *shapes) + struct.pack('<If',0x01020304,1.0)
    value = 14695981039346656037
    for byte in raw: value = ((value ^ byte)*1099511628211) & ((1<<64)-1)
    return value

def exact(stream, size):
    raw = stream.read(size)
    if len(raw) != size: raise ValueError('truncated journal')
    return raw

def check_stream(stream, max_record=128<<20, max_records=1_000_000, allow_legacy=False):
    magic, version, width, height, abi, signature = struct.unpack('<4I2Q',exact(stream,32))
    if magic != MAGIC or version not in range(3,10): raise ValueError('unsupported journal header')
    if not (0 < width <= 32768 and 0 < height <= 32768): raise ValueError('invalid extent')
    if version == 9 and (abi != abi_tag() or signature != SIGNATURE): raise ValueError('incompatible journal ABI')
    if version < 9 and not allow_legacy: raise ValueError('legacy journal has no ABI fingerprint; use --allow-legacy only for a known-compatible capture')
    records = frames = 0
    in_frame = False
    ring = 0
    buffers = {}
    while True:
        record = stream.read(16)
        if not record: break
        if len(record) != 16: raise ValueError('truncated record header')
        op, header_size, size, count = struct.unpack('<4I',record)
        records += 1
        if records > max_records or size > max_record or count > 4096: raise ValueError('admission budget exceeded')
        if not 0 < op < len(SIZES): raise ValueError('unknown opcode')
        expected = SIZES[op]
        if version < 7 and op == 4: expected = 72
        if version < 7 and op in (9,25): expected = 100
        if version < 7 and op == 16: expected = 56 if version < 5 else 96
        if header_size != expected or (size and op not in PAYLOAD): raise ValueError('invalid record framing')
        header, data = exact(stream,header_size), exact(stream,size)
        previous = -8
        for _ in range(count):
            offset, kind = struct.unpack('<2I',exact(stream,8))
            if offset < previous+8 or offset+8 > size: raise ValueError('overlapping or out-of-range relocation')
            previous = offset
            if kind not in ({3,4} if version >= 9 else {1,2,3,4}): raise ValueError('unknown relocation kind')
            value, = struct.unpack_from('<Q',data,offset)
            if kind == 3 and (value>>32 not in buffers or (value & 0xffffffff) >= buffers[value>>32]):
                raise ValueError('relocation references an absent buffer or invalid offset')
            if kind == 4 and (not in_frame or value >= ring): raise ValueError('relocation outside current ring')
        if op == 1:
            capacity, handle, has_data = struct.unpack_from('<Q2I',header)
            if not capacity or not handle or handle in buffers or size > capacity or (size and not has_data): raise ValueError('invalid buffer creation')
            buffers[handle] = capacity
        elif op == 2:
            handle, = struct.unpack('<I',header)
            if handle not in buffers: raise ValueError('free of absent buffer')
            del buffers[handle]
        elif op == 3:
            handle, _, offset = struct.unpack('<2IQ',header)
            if handle not in buffers or offset > buffers[handle] or size > buffers[handle]-offset: raise ValueError('upload outside buffer')
        elif op == 10:
            if in_frame: raise ValueError('nested frame')
            in_frame, ring = True, 0
        elif op == 12:
            if not in_frame: raise ValueError('ring allocation outside frame')
            amount, = struct.unpack_from('<Q',header,8)
            ring = (ring+63) & ~63
            ring += amount
            if ring > max_record: raise ValueError('ring budget exceeded')
        elif op == 11:
            used, = struct.unpack_from('<Q',header,8)
            if not in_frame or used != ring or size != ring: raise ValueError('invalid frame ring payload')
            in_frame = False
            frames += 1
    if in_frame: raise ValueError('incomplete frame')
    return {'version':version,'width':width,'height':height,'records':records,'frames':frames,
            'abi_fingerprint':version >= 9,'gpu_executed':False}

def check(path, max_file=1<<30, **options):
    if path.stat().st_size > max_file: raise ValueError('file budget exceeded')
    with path.open('rb') as source:
        magic = exact(source,8)
        source.seek(0)
        if magic != b'JRNL\x01\0\0\0': return check_stream(source,**options)
        source.read(8)
        with tempfile.TemporaryFile() as video:
            last_frame = -1
            total = 0
            while True:
                header = source.read(12)
                if not header: break
                if len(header) != 12: raise ValueError('truncated shared packet')
                tag, frame, size = struct.unpack('<3I',header)
                if size > 512<<20: raise ValueError('shared packet budget exceeded')
                if tag == 1:
                    if frame < last_frame: raise ValueError('video frames go backwards')
                    last_frame = frame
                while size:
                    block = exact(source,min(size,65536)); size -= len(block)
                    total += len(block)
                    if total > max_file: raise ValueError('stream budget exceeded')
                    if tag == 1: video.write(block)
            video.seek(0)
            return check_stream(video,**options)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journal',type=Path)
    parser.add_argument('--allow-legacy',action='store_true')
    args = parser.parse_args()
    try: print(json.dumps(check(args.journal,allow_legacy=args.allow_legacy),indent=2))
    except (ValueError,OSError) as error: parser.exit(1,str(error)+'\n')
