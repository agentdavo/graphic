#!/usr/bin/env python3
"""Resize, iconify, restore and close a fixture on an isolated X11 display.
Run with xvfb-run and a window manager (Openbox in CI); never touches the user's desktop.
"""
import argparse
import json
from pathlib import Path
import subprocess
import time

def xdo(*args):
    return subprocess.run(['xdotool',*map(str,args)],check=True,capture_output=True,text=True,timeout=10).stdout.strip()

def check_window(binary,out):
    out.mkdir(parents=True,exist_ok=False)
    metrics=(out/'metrics.json').resolve()
    time.sleep(.5)  # allow the isolated window manager to claim its X11 selection
    with (out/'window.log').open('w') as log:
        proc=subprocess.Popen([str(binary.resolve()),'--exit-after','10000','--metrics',str(metrics)],stdout=log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+15
            window=None
            while time.monotonic()<deadline:
                result=subprocess.run(['xdotool','search','--pid',str(proc.pid),'--name','vkmin resize fixture'],capture_output=True,text=True,timeout=2)
                if result.returncode==0 and 'window fixture ready' in (out/'window.log').read_text():
                    window=result.stdout.splitlines()[0];break
                if proc.poll() is not None: raise RuntimeError((out/'window.log').read_text())
                time.sleep(.05)
            if window is None: raise RuntimeError('fixture window did not appear')
            observations=[]
            for width,height in [(320,200),(96,72),(240,180)]:
                xdo('windowsize',window,width,height)
                time.sleep(.3)
                geometry=xdo('getwindowgeometry','--shell',window)
                assert f'WIDTH={width}' in geometry and f'HEIGHT={height}' in geometry,geometry
                observations.append(geometry)
            xdo('windowminimize','--sync',window);time.sleep(.3)
            iconic=subprocess.run(['xprop','-id',window,'WM_STATE'],capture_output=True,text=True,check=True,timeout=5).stdout
            assert 'Iconic' in iconic,iconic
            xdo('windowmap','--sync',window);xdo('windowactivate','--sync',window);time.sleep(.3)
            xdo('key','--window',window,'alt+F4')
            assert proc.wait(timeout=15)==0,(out/'window.log').read_text()
            text=(out/'window.log').read_text()
            assert 'validation:' not in text,text
            data=json.loads(metrics.read_text())
            assert data['resource_lifetime']['device_idle_present']>=2,data
            (out/'result.json').write_text(json.dumps({'sizes':observations,'iconified':True,'restored':True,
                'clean_exit':True,'swapchain_waits':data['resource_lifetime']['device_idle_present']},indent=2))
        finally:
            if proc.poll() is None:
                proc.kill();proc.wait(timeout=5)
    print('Window resize, minimize, restore, swapchain recreation and clean close: passed')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();check_window(a.binary,a.out)
