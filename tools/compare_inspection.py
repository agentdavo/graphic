#!/usr/bin/env python3
"""Compare raw checkpoint captures; report the first divergent stage and image.
Use captures of the same journal/frame made by inspect_frame.py --passes.
"""
import argparse
import json
from pathlib import Path


def compare(a, b):
    left=json.loads((a/'capture.json').read_text())
    right=json.loads((b/'capture.json').read_text())
    if (left['sha256'],left['frame'],left['checkpoints']) != (right['sha256'],right['frame'],right['checkpoints']):
        raise ValueError('captures must use the same journal, frame and checkpoints')
    differences=[]
    for event,label in left['checkpoints']:
        name=f'event_{event:06d}' if event else 'complete'
        images_a=json.loads((a/name/'images.json').read_text())
        images_b=json.loads((b/name/'images.json').read_text())
        if [(r['slot'],r['width'],r['height'],r['format']) for r in images_a] != [(r['slot'],r['width'],r['height'],r['format']) for r in images_b]:
            raise ValueError(f'{name}: resource layouts differ')
        for x,y in zip(images_a,images_b):
            data_a=(a/name/x['file']).read_bytes(); data_b=(b/name/y['file']).read_bytes()
            stride=8 if int(x['format'])==9 else 4
            expected=int(x['width'])*int(x['height'])*stride
            if len(data_a)!=expected or len(data_b)!=expected: raise ValueError(f'{name}: truncated raw image')
            if data_a != data_b:
                changed=[i//stride for i in range(0,len(data_a),stride) if data_a[i:i+stride]!=data_b[i:i+stride]]
                differences.append({'checkpoint':name,'pass':label,'image':x['label'],'texels':len(changed),
                                    'first_xy':[changed[0]%int(x['width']),changed[0]//int(x['width'])]})
    return differences


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('a',type=Path);parser.add_argument('b',type=Path)
    args=parser.parse_args()
    differences=compare(args.a,args.b)
    print(json.dumps({'match':not differences,'first_divergence':differences[0] if differences else None,'differences':differences},indent=2))
    raise SystemExit(bool(differences))
