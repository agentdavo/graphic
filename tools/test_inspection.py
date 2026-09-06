#!/usr/bin/env python3
"""GPU integration regressions for journal event inspection. Requires a Vulkan driver."""
import argparse
import csv
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from inspect_frame import preview, unsigned_float
from compare_inspection import compare


class DecodeTests(unittest.TestCase):
    def test_hdr_and_depth(self):
        self.assertEqual(unsigned_float(15 << 6, 6), 1)
        self.assertEqual(unsigned_float(1, 6), 2**-20)
        self.assertEqual(preview(struct.pack('<f', .5), 10), bytes([128,128,128,255]))
        self.assertEqual(preview(struct.pack('<4e', 1,0,0,1), 9), bytes([186,0,0,255]))
    def test_channels_and_normals(self):
        self.assertEqual(preview(bytes([1,2,3,0]), 2), bytes([3,2,1,255]))
        self.assertEqual(preview(struct.pack('<2H', 32768,32768), 12), bytes([128,128,255,255]))
    def test_comparison_finds_changed_texel(self):
        with tempfile.TemporaryDirectory() as tmp:
            roots=[Path(tmp)/name for name in ('left','right')]
            for root in roots:
                (root/'complete').mkdir(parents=True)
                (root/'capture.json').write_text(json.dumps({'sha256':'same','frame':0,'checkpoints':[[None,'end']]}))
                (root/'complete'/'images.json').write_text(json.dumps([{'slot':'0','width':'2','height':'1','format':'0','file':'image.raw','label':'colour'}]))
                (root/'complete'/'image.raw').write_bytes(bytes(8))
            self.assertEqual(compare(*roots), [])
            (roots[1]/'complete'/'image.raw').write_bytes(bytes(4)+bytes([1,0,0,0]))
            difference=compare(*roots)[0]
            self.assertEqual((difference['checkpoint'],difference['image'],difference['texels'],difference['first_xy']), ('complete','colour',1,[1,0]))
            (roots[1]/'complete'/'image.raw').write_bytes(bytes(3))
            with self.assertRaises(ValueError): compare(*roots)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    args=parser.parse_args()
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(DecodeTests)
    if not unittest.TextTestRunner().run(suite).wasSuccessful(): raise SystemExit(1)
    def exe(name):
        p=args.build/name
        return str((p if p.exists() else p.with_suffix('.exe')).resolve())
    def run(command, success=True):
        result=subprocess.run(command, capture_output=True)
        if (result.returncode == 0) != success:
            raise AssertionError(f'{command}\n{result.stdout!r}\n{result.stderr!r}')
        if b'Validation Error' in result.stderr: raise AssertionError(result.stderr)
        return result
    with tempfile.TemporaryDirectory(prefix='inspection-', dir=args.build) as tmp:
        root=Path(tmp)
        journal=root/'two.vkj'
        run([exe('inspection'), '--headless','--size','64','32','--frame','0','--record',str(journal)])
        replay=[exe('ex_07_replay'),'--replay',str(journal),'--frame','0']
        events=root/'events.tsv'
        run(replay+['--events',str(events)])
        rows=list(csv.DictReader(events.open(), delimiter='\t'))
        assert [int(r['event']) for r in rows] == list(range(1,len(rows)+1))
        assert any('two triangles' in r['detail'] for r in rows)
        draws=[int(r['event']) for r in rows if r['op']=='draw']
        assert len(draws)==2
        captures={}
        for path in ('legacy','modern'):
            for stop in (None,*draws):
                directory=root/f'{path}-{stop}'; directory.mkdir()
                command=replay+['--path='+path,'--inspect-dir',str(directory),'--metrics',str(directory/'metrics.json')]
                if stop: command+=['--stop-after-event',str(stop)]
                run(command)
                data=(directory/'image_0.raw').read_bytes()
                assert len(data)==64*32*4
                captures[path,stop]=data
                metrics=json.loads((directory/'metrics.json').read_text())
                assert metrics['frames']==metrics['gpu_frames']==1 and metrics['inspection']
                assert metrics['gpu_render_ms'] >= 0 and metrics['host_frame_work_ms'] >= 0
        full, first=captures['legacy',None],captures['legacy',draws[0]]
        left=lambda data: b''.join(data[y*256:y*256+128] for y in range(32))
        right=lambda data: b''.join(data[y*256+128:(y+1)*256] for y in range(32))
        assert left(full)==left(first) and any(left(first)[0::4])
        assert any(right(full)[0::4]) and not any(right(first)[0::4])
        for stop in (None,*draws): assert captures['legacy',stop]==captures['modern',stop]
        assert captures['legacy',draws[1]]==full
        run(replay+['--stop-after-event','999999'],False)
        run(replay+['--stop-after-event','1'],False)
        run(replay+['--stop-after-event','-1'],False)
        run(replay+['--stop-after-event','4294967296'],False)
        run(replay+['--events',str(root/'missing'/'events.tsv')],False)
        # Truncation after the stop must not submit stale ring bytes.
        broken=root/'truncated.vkj'; broken.write_bytes(journal.read_bytes()[:-4])
        run([exe('ex_07_replay'),'--replay',str(broken),'--stop-after-event',str(draws[0])],False)
        print('inspection: pass labels, stable events, late ring data, draw cutoff, cross-path pixels and invalid requests passed')


if __name__=='__main__': main()
