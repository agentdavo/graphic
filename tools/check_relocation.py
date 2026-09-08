#!/usr/bin/env python3
"""Typed pointer/integer collision and unaligned interior relocation regression."""
import argparse
from pathlib import Path
import struct
from check_journal import check
from inspect_frame import run

def records(path):
    with path.open('rb') as f:
        f.read(32)
        while raw:=f.read(16):
            op,h,b,n=struct.unpack('<4I',raw)
            header,data=f.read(h),f.read(b)
            relocs=[struct.unpack('<2I',f.read(8)) for _ in range(n)]
            yield op,header,data,relocs

def check_relocation(build,out):
    out.mkdir(parents=True,exist_ok=False)
    suffix='.exe' if (build/'replay.exe').exists() else ''
    journal=out/'typed.vkj'
    run([str((build/('test_relocation'+suffix)).resolve()),'--frames','0,1,2','--out-dir',str(out),
         '--record',str(journal)],out/'record.log')
    report=check(journal)
    assert report['frames']==3
    integer=None
    source=None
    counts={1:0,3:0,11:0,20:0}
    for op,header,data,relocs in records(journal):
        if op==1 and b'address source' in header:
            source=struct.unpack_from('<I',header,8)[0]
        if op in (1,3) and relocs:
            assert relocs==[(1,3)]
            assert struct.unpack_from('<Q',data,1)[0] == (source<<32)|4
            value=struct.unpack_from('<Q',data,9)[0]
            if integer is None: integer=value
            assert value==integer
            counts[op]+=1
        if op==11:
            assert relocs==[(1,3),(17,4)]
            assert struct.unpack_from('<Q',data,17)[0]==1
            assert struct.unpack_from('<Q',data,9)[0]==integer
            counts[op]+=1
        if op==20:
            assert relocs==[(0,3)]
            assert struct.unpack_from('<Q',data,0)[0] == (source<<32)|4
            assert struct.unpack_from('<Q',data,8)[0]==integer
            counts[op]+=1
    assert counts=={1:1,3:1,11:3,20:3},counts
    paths=['legacy']+(['modern'] if 'path = modern' in (out/'record.log').read_text() else [])
    for path in paths:
        output=out/(path+'.png')
        run([str((build/('replay'+suffix)).resolve()),'--replay',str(journal),'--frame','1',
             '--path='+path,'--out',str(output),'+r_ring_mb','8'],out/(path+'.log'))
        assert output.read_bytes()==(out/'vkmin_0001.png').read_bytes()
    rerecorded=out/'rerecorded.vkj'
    run([str((build/('replay'+suffix)).resolve()),'--replay',str(journal),'--frame','1','--record',str(rerecorded)],out/'rerecord.log')
    check(rerecorded)
    run([str((build/('replay'+suffix)).resolve()),'--replay',str(rerecorded),'--frame','1','--out',str(out/'rereplayed.png')],out/'rereplay.log')
    assert (out/'rereplayed.png').read_bytes()==(out/'vkmin_0001.png').read_bytes()
    print('Typed unaligned buffer/ring fields, interior offsets, integer collision and multi-frame replay: passed')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();check_relocation(a.build,a.out)
