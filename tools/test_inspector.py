"""Offline inspector packaging tests; no GPU or browser required."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import struct
from narrow_capture import compare, candidates, narrow
from inspect_frame import file_hash
from build_inspector import build, capture


class PackageTests(unittest.TestCase):
    def fixture(self, root):
        (root/'complete').mkdir()
        (root/'capture.json').write_text(json.dumps({'frame':0,'sha256':'abc','journal':'demo.vkj','checkpoints':[[None,'end']]}))
        (root/'events.tsv').write_text('event\tframe\top\tdetail\n1\t0\tdraw\tlabel=</script><script>alert(1)</script>\n')
        (root/'complete'/'images.json').write_text(json.dumps([{'slot':'0','format':'0','width':'1','height':'1','file':'image.raw','label':'</script>'}]))
        (root/'complete'/'image.raw').write_bytes(bytes([255,0,0,255]))

    def test_self_contained_and_escaped(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);self.fixture(root);out=root/'viewer.html';build(root,out)
            html=out.read_text(encoding='utf-8')
            self.assertNotIn('</script><script>alert',html)
            self.assertNotIn('src="app.js"',html)
            self.assertIn('\\u003c/script\\u003e',html)
            self.assertEqual(len(capture(root)['assets']),1)

    def test_missing_and_truncated_are_explicit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);self.fixture(root)
            (root/'complete'/'image.raw').write_bytes(b'x')
            self.assertIn('Truncated',capture(root)['points'][0]['images'][0]['error'])
            (root/'complete'/'image.raw').unlink()
            self.assertIn('error',capture(root)['points'][0]['images'][0])
            (root/'complete'/'images.json').unlink()
            self.assertTrue(capture(root)['points'][0]['error'])

    def test_path_escape_and_unsupported_format(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);self.fixture(root);p=root/'complete'/'images.json';rows=json.loads(p.read_text())
            rows[0]['file']='../../outside.raw';p.write_text(json.dumps(rows))
            self.assertIn('escapes',capture(root)['points'][0]['images'][0]['error'])
            rows[0]['file']='image.raw';rows[0]['format']='99';p.write_text(json.dumps(rows))
            self.assertIn('Unsupported',capture(root)['points'][0]['images'][0]['error'])

    def test_javascript(self):
        subprocess.run(['node','--check','inspector/app.js'],check=True)
        subprocess.run(['node','inspector/test_core.js'],check=True)

    def test_application_state(self):
        with tempfile.TemporaryDirectory() as tmp:
            roots=[Path(tmp)/name for name in ('good','defect')]
            for root,gain in zip(roots,(255,128)):
                root.mkdir()
                meta={'frame':0,'sha256':root.name,'journal':'fixture.vkj',
                      'checkpoints':[[12,'left'],[16,'right'],[None,'complete']]}
                (root/'capture.json').write_text(json.dumps(meta))
                (root/'events.tsv').write_text('event\tframe\top\tdetail\n12\t0\tpass_end\t\n15\t0\tdraw\tpipeline=4 label=sample texture push_hex=000000000000000000000000'+gain.to_bytes(4,'little').hex()+'\n16\t0\tpass_end\t\n')
                for event,_ in meta['checkpoints']:
                    directory=root/(f'event_{event:06d}' if event else 'complete');directory.mkdir()
                    (directory/'buffers.json').write_text(json.dumps([{'id':'7','kind':'buffer','label':'instances','offset':'0','size':'4','file':'buffer.raw'}]))
                    (directory/'buffer.raw').write_bytes(struct.pack('<I',gain))
                    (directory/'images.json').write_text(json.dumps([{'slot':'0','width':'64','height':'32','format':'0','file':'image.raw','label':'backbuffer'}]))
                    right=bytes([0,0,0,255]) if event==12 else bytes([gain,0,0,255])
                    (directory/'image.raw').write_bytes((bytes([255,0,0,255])*32+right*32)*32)
            page=Path(tmp)/'comparison.html'
            build(roots[0],page,roots[1],Path('inspector/consumer-schema.json'))
            subprocess.run(['node','inspector/test_app.js',str(page)],check=True)


class DiagnosticTests(unittest.TestCase):
    def point(self, root, value, fmt=10):
        root.mkdir(parents=True, exist_ok=True)
        (root/'images.json').write_text(json.dumps([{'slot':'0','width':'1','height':'1','format':str(fmt),'file':'image.raw'}]))
        (root/'image.raw').write_bytes(struct.pack('<f', value))

    def test_multisample_manifest_is_explicit(self):
        from inspect_frame import convert
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);PackageTests().fixture(root);point=root/'complete'
            (point/'images.tsv').write_text('slot\tlabel\twidth\theight\tformat\tfile\tid\tsamples\n0\tMSAA\t1\t1\t0\t\t7\t4\n1\tresolved\t1\t1\t0\timage.raw\t8\t1\n')
            rows=convert(point)
            self.assertIn('Multisample',rows[0]['error'])
            self.assertIn('sha256',rows[1])
            bundle=capture(root)
            self.assertIn('Multisample',bundle['points'][0]['images'][0]['error'])
            self.assertIn('data',bundle['points'][0]['images'][1])
            self.assertIsNone(compare(point,point))

    def test_numeric_and_truncated(self):
        with tempfile.TemporaryDirectory() as tmp:
            a,b=Path(tmp)/'a',Path(tmp)/'b'
            self.point(a,1);self.point(b,1.01)
            self.assertIsNotNone(compare(a,b))
            self.assertIsNone(compare(a,b,True,.02))
            self.assertIsNotNone(compare(a,b,True,.001))
            (b/'image.raw').write_bytes(b'x')
            with self.assertRaisesRegex(ValueError,'Truncated'): compare(a,b)

    def test_event_correspondence(self):
        trace=[{'event':'1','frame':'0','op':'make_buffer'}, {'event':'2','frame':'0','op':'frame_begin'},
               {'event':'3','frame':'0','op':'draw'}, {'event':'4','frame':'0','op':'frame_end'}]
        self.assertEqual(candidates(trace,trace,0,4),[(2,'frame_begin'),(3,'draw'),(4,'frame_end')])
        with self.assertRaisesRegex(ValueError,'correspondence'): candidates(trace,trace[:-1],0,4)

    def test_narrow_finds_transient_before_coarse_difference(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);a,b=root/'a',root/'b';exe=root/'replay';exe.write_bytes(b'fixture')
            for folder in (a,b):
                folder.mkdir();journal=folder/'record.vkj';journal.write_bytes(folder.name.encode())
                (folder/'capture.json').write_text(json.dumps({'frame':0,'sha256':file_hash(journal),'journal':str(journal),
                    'path':'legacy','checkpoints':[[4,'pass_end'],[6,'pass_end']],
                    'metadata':{'replay_executable':str(exe),'replay_sha256':file_hash(exe)}}))
                (folder/'events.tsv').write_text('event\tframe\top\tdetail\n2\t0\tframe_begin\t\n3\t0\tdraw\t\n4\t0\tpass_end\t\n5\t0\tdraw\t\n6\t0\tpass_end\t\n')
                self.point(folder/'event_000004',0)
                self.point(folder/'event_000006',1 if folder==a else 2)
            def replay(command, log):
                event=int(command[command.index('--stop-after-event')+1]);directory=log.parent
                side=Path(command[command.index('--replay')+1]).parent.name
                value=2 if side=='b' and event==3 else 1
                (directory/'images.tsv').write_text('slot\tlabel\twidth\theight\tformat\tfile\n0\tcolor\t1\t1\t10\timage.raw\n')
                (directory/'image.raw').write_bytes(struct.pack('<f',value));log.write_text('fixture')
            with patch('narrow_capture.run',side_effect=replay):
                result=narrow(a,b,root/'result')
            self.assertEqual(result['event'],3)
            self.assertTrue((root/'result'/'inspector.html').is_file())

    def test_buffer_packaging(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);PackageTests().fixture(root)
            row={'id':'1048577','kind':'buffer','label':'instances','offset':'0','size':'4','file':'buffer.raw'}
            (root/'complete'/'buffers.json').write_text(json.dumps([row]))
            (root/'complete'/'buffer.raw').write_bytes(struct.pack('<I',42))
            self.assertIn('data',capture(root)['points'][0]['buffers'][0])
            row['file']='../../escape';(root/'complete'/'buffers.json').write_text(json.dumps([row]))
            self.assertIn('escapes',capture(root)['points'][0]['buffers'][0]['error'])


if __name__=='__main__':
    unittest.main()
