#!/usr/bin/env python3
"""Malformed wrapper commands must fail replay normally, before illegal GPU calls."""
import argparse
from pathlib import Path
import struct
import subprocess
from check_relocation import records

def pack(op,h,data=b'',relocs=()):
    return struct.pack('<4I',op,len(h),len(data),len(relocs))+h+data+b''.join(struct.pack('<2I',*r) for r in relocs)

def check_errors(replay,journal,out):
    out.mkdir(parents=True,exist_ok=False)
    prefix=journal.read_bytes()[:32]
    cases={'unknown-opcode':prefix+pack(99,b''),
           'header-shape':prefix+pack(1,bytes(57)),
           'dead-buffer':prefix+pack(2,struct.pack('<I',0xffffffff))}
    for op,h,data,relocs in records(journal):
        if op==3 and 'upload-range' not in cases:
            bad=bytearray(h);struct.pack_into('<Q',bad,8,(1<<64)-1)
            cases['upload-range']=prefix+pack(op,bad,data,relocs)
            cases['overlapping-relocations']=prefix+pack(op,h,data,relocs*2)
        if op==9 and 'shader-layout' not in cases:
            bad=bytearray(data);bad[:4]=bytes(4)
            cases['shader-layout']=prefix+pack(op,h,bad,relocs)
        prefix+=pack(op,h,data,relocs)
        if op==10:
            cases['nested-frame']=prefix+pack(op,h)
            cases['timestamp-range']=prefix+pack(23,struct.pack('<I',32))
            cases['barrier-count']=prefix+pack(17,bytes(4))+pack(13,struct.pack('<2I',0,17),bytes(17*8))
            break
    assert len(cases)==9,cases.keys()
    for name,raw in cases.items():
        path=out/(name+'.vkj');path.write_bytes(raw)
        with (out/(name+'.log')).open('w') as log:
            result=subprocess.run([str(replay.resolve()),'--replay',str(path),'--frame','0','--events',str(out/(name+'.tsv'))],
                                  stdout=log,stderr=subprocess.STDOUT,timeout=30)
        text=(out/(name+'.log')).read_text()
        assert result.returncode==1 and 'journal ' in text and 'validation:' not in text,(name,result.returncode,text)
    print('9 malformed replay cases returned failure without wrapper aborts or Vulkan validation errors')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--replay',type=Path,required=True)
    p.add_argument('--journal',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();check_errors(a.replay,a.journal,a.out)
