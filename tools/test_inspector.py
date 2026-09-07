"""Offline inspector packaging tests; no GPU or browser required."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
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
                    (directory/'images.json').write_text(json.dumps([{'slot':'0','width':'64','height':'32','format':'0','file':'image.raw','label':'backbuffer'}]))
                    right=bytes([0,0,0,255]) if event==12 else bytes([gain,0,0,255])
                    (directory/'image.raw').write_bytes((bytes([255,0,0,255])*32+right*32)*32)
            page=Path(tmp)/'comparison.html'
            build(roots[0],page,roots[1],Path('inspector/consumer-schema.json'))
            subprocess.run(['node','inspector/test_app.js',str(page)],check=True)


if __name__=='__main__':
    unittest.main()
