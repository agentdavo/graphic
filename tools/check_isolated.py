#!/usr/bin/env python3
"""Verify isolated replay policy, pixel agreement, driver version and CPU termination."""
import argparse
import json
from pathlib import Path
import struct
from check_relocation import records
from check_replay_errors import pack
from replay_isolated import replay_isolated

def verify(typed,captures,out,image,api,limit_test,samples):
    out.mkdir(parents=True,exist_ok=False)
    source=typed/'typed.vkj'
    cases=[('typed',source,typed/'vkmin_0001.png',1)]
    available=list(sorted(captures.glob('*/direct.png')))
    selected=[p for p in available if not samples or int(p.parent.name.split('x',1)[0]) in samples]
    if samples: assert set(samples) <= {int(p.parent.name.split('x',1)[0]) for p in selected}, 'required sample captures missing'
    cases += [(p.parent.name,p.parent/'frame.vkj',p,0) for p in selected]
    for name,journal,reference,frame in cases:
        output=out/(name+'.png')
        replay_isolated(journal,output,frame=frame,image=image)
        assert output.read_bytes()==reference.read_bytes(),name
        assert f'(Vulkan {api})' in output.with_suffix('.log').read_text(),name
        policy=json.loads(output.with_suffix('.policy.json').read_text());host=policy['host_config']
        assert host['NetworkMode']=='none' and host['ReadonlyRootfs'] and not host['Privileged']
        assert host['Memory']==1<<30 and host['PidsLimit']==256 and host['CpuQuota']==200000
        assert not host['Devices'] and not host['CapAdd'] and 'no-new-privileges' in host['SecurityOpt']
        assert policy['container_user']=='65532:65532'
        print(f'Isolated Vulkan {api} {name}: exact output and restrictions passed',flush=True)
    if limit_test:
        items=list(records(source));first=next(i for i,r in enumerate(items) if r[0]==10)
        last=next(i for i,r in enumerate(items[first:],first) if r[0]==11)
        long=out/'long.vkj'
        with long.open('wb') as f:
            f.write(source.read_bytes()[:32])
            for r in items[:first]: f.write(pack(*r))
            for frame in range(50000):
                for op,h,data,relocs in items[first:last+1]:
                    if op==10: h=struct.pack('<I',frame)+h[4:]
                    f.write(pack(op,h,data,relocs))
        try: replay_isolated(long,out/'must-not-exist.png',timeout=1,image=image)
        except RuntimeError as error:
            assert 'exceeded' in str(error) or 'exited 137' in str(error) or 'exited 152' in str(error),str(error)
        else: raise AssertionError('CPU/wall limit did not terminate oversized replay')
        assert not (out/'must-not-exist.png').exists()
        print('Oversized replay stopped by enforced CPU/wall limit',flush=True)
    (out/'result.json').write_text(json.dumps({'api':api,'cases':len(cases),'resource_limit_test':limit_test,
        'excluded_configurations':[p.parent.name for p in available if p not in selected]},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--typed',type=Path,required=True);p.add_argument('--captures',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--image',default='localhost/vkmin-replay:9')
    p.add_argument('--api',default='1.4');p.add_argument('--limit-test',action='store_true')
    p.add_argument('--samples',type=int,nargs='+',help='require these counts; explicitly exclude captures at other counts')
    a=p.parse_args();verify(a.typed,a.captures,a.out,a.image,a.api,a.limit_test,a.samples)
