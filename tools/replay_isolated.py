#!/usr/bin/env python3
"""Execute an admitted journal with CPU Vulkan in a restricted local Podman container.
Build first: podman build -f tools/Containerfile.replay -t localhost/vkmin-replay:9 .
No network, host GPU, home directory or writable host mount is exposed. A container
shares its runtime kernel; this is not a claim of immunity to kernel vulnerabilities.
"""
import argparse
import json
from pathlib import Path
import subprocess
import uuid
import time
from check_journal import check

def command(*args,timeout=30,cwd=None):
    return subprocess.run(['podman',*args],capture_output=True,text=True,check=True,timeout=timeout,cwd=cwd).stdout.strip()

def replay_isolated(journal,output,frame=0,timeout=30,image='localhost/vkmin-replay:9',allow_legacy=False):
    journal=journal.resolve();output=output.resolve()
    if ',' in str(journal): raise ValueError('Podman mount syntax cannot represent a comma in this path')
    if output.exists(): raise ValueError('output already exists')
    if not 0 <= frame <= 2147483647 or not 1 <= timeout <= 300: raise ValueError('invalid frame or timeout')
    admission=check(journal,allow_legacy=allow_legacy)
    output.parent.mkdir(parents=True,exist_ok=True)
    image_id=command('image','inspect',image,'--format','{{.Id}}')
    name='vkmin-replay-'+uuid.uuid4().hex
    created=False
    try:
        command('create','--name',name,'--network','none','--ipc','none','--read-only',
                '--log-driver','k8s-file','--log-opt','max-size=1048576',
                '--cap-drop','ALL','--security-opt','no-new-privileges','--user','65532:65532',
                '--pids-limit','256','--cpus','2','--memory','1g','--memory-swap','1g',
                '--ulimit',f'cpu={timeout}:{timeout}','--ulimit','fsize=268435456:268435456',
                '--tmpfs','/tmp:rw,noexec,nosuid,size=64m','--tmpfs','/out:rw,noexec,nosuid,size=256m',
                '--mount',f'type=bind,source={journal},target=/input/journal.vkj,readonly',
                '--entrypoint','/bin/sh',image_id,'-c',
                '/usr/local/bin/replay "$@"; printf "%s\\n" "$?" > /out/status; sleep 300','replay',
                '--replay','/input/journal.vkj','--frame',str(frame),'--path=legacy',
                '--out','/out/result.png','+r_arena_mb','64','+r_image_arena_mb','64','+r_ring_mb','8')
        created=True
        policy=json.loads(command('inspect',name))[0]
        command('start',name)
        try:
            deadline=time.monotonic()+timeout
            while True:
                status=subprocess.run(['podman','exec',name,'head','-c','32','/out/status'],capture_output=True,text=True,timeout=5)
                if status.returncode==0:
                    code=int(status.stdout.strip());break
                if command('inspect',name,'--format','{{.State.Running}}') != 'true':
                    raise RuntimeError('isolated container stopped before producing a result')
                if time.monotonic() >= deadline:
                    command('kill',name)
                    raise RuntimeError(f'isolated replay exceeded {timeout}s')
                time.sleep(.1)
        finally:
            logs=subprocess.run(['podman','logs',name],capture_output=True,timeout=10)
            output.with_suffix('.log').write_bytes(logs.stdout+logs.stderr)
        if code: raise RuntimeError(f'isolated replay exited {code}; see {output.with_suffix(".log")}')
        command('cp',name+':/out/result.png',output.name,cwd=output.parent)
        with output.open('rb') as result:
            if result.read(8)!=b'\x89PNG\r\n\x1a\n': raise RuntimeError('replay did not produce a PNG')
        output.with_suffix('.policy.json').write_text(json.dumps({'admission':admission,'image_id':image_id,
            'host_config':policy['HostConfig'],'container_user':policy['Config']['User']},indent=2))
        return output
    finally:
        if created: command('rm','--force',name)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('journal',type=Path);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--frame',type=int,default=0);p.add_argument('--timeout',type=int,default=30)
    p.add_argument('--image',default='localhost/vkmin-replay:9');p.add_argument('--allow-legacy',action='store_true')
    a=p.parse_args()
    try: print(replay_isolated(a.journal,a.out,a.frame,a.timeout,a.image,a.allow_legacy))
    except (ValueError,RuntimeError,OSError,subprocess.SubprocessError) as error:
        p.exit(1,str(error)+'\n'+str(getattr(error,'stderr','') or ''))
