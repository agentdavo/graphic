"""Offline asset authoring only; never run by the demo or make test.
Requires Pillow. Authored terrain contours, paint masks and tree geometry are
baked into committed images + scene.txt. Atlas views rasterize that geometry.
All artwork here is original to this repository (same licence as the code).
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter
import math

ROOT = Path(__file__).resolve().parents[1] / 'assets' / 'valley'
ROOT.mkdir(exist_ok=True)
N, CELL = 513, 3.0
MIN, RANGE = -6.0, 260.0

def centre(z):
    return 755 + 24*math.sin((z-540)/115)

def height(x,z):
    d=abs(x-centre(z))
    # A broad, flat floodplain with a submerged channel; authored ridge masses.
    bank=1.8+0.9*math.sin(z/165)
    bed=-2.8+0.6*math.sin(z/100)
    t=max(0,min(1,(d-17)/15)); t=t*t*(3-2*t)
    h=bed*(1-t)+bank*t
    h+=max(d-75,0)**1.22*0.10
    for cx,cz,amp,sx,sz in [(440,700,75,145,280),(1080,960,118,190,240),(540,1250,125,160,170),(1140,300,96,200,230)]:
        h+=amp*math.exp(-((x-cx)/sx)**2-((z-cz)/sz)**2)*min(1,max(0,(d-55)/90))
    h += max(0,min(1,(d-50)/100))*(4*math.sin(x/37+z/67)+2*math.sin(z/19-x/31))
    return h

maps={name:Image.new('RGBA',(N,N)) for name in ['height','splat','grass','flowers','rocks','trees']}
px={name:im.load() for name,im in maps.items()}
for z in range(N):
    for x in range(N):
        wx,wz=x*CELL,z*CELL; h=height(wx,wz); d=abs(wx-centre(wz))
        value=round(max(0,min(1,(h-MIN)/RANGE))*65535)
        px['height'][x,z]=(value>>8,value&255,0,255)
        sand=max(0,1-abs(h-0.1)/1.6); rock=max(0,min(1,(h-22)/70)); dirt=max(0,1-abs(d-32)/9)*0.55
        grass=max(0,1-sand-rock-dirt); total=grass+sand+rock+dirt
        px['splat'][x,z]=tuple(round(v/total*255) for v in [grass,dirt,rock,sand])
        grass_d=max(0,min(1,(d-29)/12))*max(0,min(1,(55-h)/30))
        tree_zone=(.42+.58*math.exp(-((wz-738)/70)**4))*max(0,min(1,(d-31)/12))*max(0,1-(d-48)/45)
        values={'grass':grass_d,'flowers':grass_d*(.35+.65*max(0,math.sin(wx*.19+wz*.07)))*math.exp(-((wz-650)/140)**2),
                'rocks':max(0,1-abs(d-26)/22)*0.65,'trees':tree_zone*0.9}
        for name,val in values.items():
            c=round(max(0,min(1,val))*255); px[name][x,z]=(c,c,c,255)
for name,im in maps.items(): im.save(ROOT/(name+'.png'))

# Small tiling material maps. The sinusoids are periodic in both dimensions.
colors=[(0.18,0.29,0.055),(0.27,0.17,0.09),(0.30,0.32,0.29),(0.53,0.43,0.25)]
for material,base in enumerate(colors):
    albedo=Image.new('RGBA',(64,64)); normal=Image.new('RGBA',(64,64))
    for y in range(64):
        for x in range(64):
            a=x*2*math.pi/64; b=y*2*math.pi/64
            def grain(gx,gy):
                v=((gx%64)*747796405+(gy%64)*2891336453)&0xffffffff
                v=((v^(v>>16))*2246822519)&0xffffffff
                return (v&65535)/65535
            detail=0.85+0.2*grain(x,y)+0.08*sum(grain(x+dx,y+dy) for dx,dy in [(-1,0),(1,0),(0,1),(0,-1)])/4
            albedo.putpixel((x,y),tuple(round(c*detail*255) for c in base)+(255,))
            nx=0.025*(grain(x+1,y)-grain(x-1,y)); ny=0.025*(grain(x,y+1)-grain(x,y-1))
            normal.putpixel((x,y),(round(128+nx*127),round(128+ny*127),253,255))
    albedo.save(ROOT/f'albedo{material}.png'); normal.save(ROOT/f'normal{material}.png')
bark=Image.new('RGBA',(64,64))
for y in range(64):
    for x in range(64):
        stripe=.65+.35*abs(math.sin(x*.65+math.sin(y*.098)*.65))
        grain=.9+.1*math.sin(x*2.4+y*1.9)
        bark.putpixel((x,y),tuple(round(c*stripe*grain) for c in (93,65,38))+(255,))
bark.save(ROOT/'bark.png')
# Periodic smooth noise slopes avoid the visible crossed sine-wave grid.
def ripple_noise(x,y,period,seed):
    x=x/64*period;y=y/64*period;ix=math.floor(x);iy=math.floor(y)
    tx=x-ix;ty=y-iy;tx=tx*tx*(3-2*tx);ty=ty*ty*(3-2*ty)
    def h(a,b):
        value=(((a%period)*747796405+(b%period)*2891336453+seed*277803737)&0xffffffff)
        value=((value^(value>>16))*2246822519)&0xffffffff
        return (value&65535)/65535
    return (h(ix,iy)*(1-tx)+h(ix+1,iy)*tx)*(1-ty)+(h(ix,iy+1)*(1-tx)+h(ix+1,iy+1)*tx)*ty
for k in range(2):
    im=Image.new('RGBA',(64,64))
    def wave(x,y):
        return sum(ripple_noise(x,y,n,11+k)*a for n,a in [(4,.55),(8,.3),(16,.15)])
    for y in range(64):
        for x in range(64):
            nx=(wave(x+1,y)-wave(x-1,y))*2.8;ny=(wave(x,y+1)-wave(x,y-1))*2.8
            im.putpixel((x,y),(round(128+nx*127),round(128+ny*127),round(ripple_noise(x,y,8,91+k)*255),255))
    im.save(ROOT/f'water{k}.png')
cloud=Image.new('RGBA',(128,128))
def cloud_grain(x,y):
    h=(((x%16)*747796405+(y%16)*2891336453)^912783)&0xffffffff
    h=((h^(h>>16))*2246822519)&0xffffffff
    return (h&65535)/65535
for y in range(128):
    for x in range(128):
        gx=x/8;gy=y/8;ix=int(gx);iy=int(gy);tx=gx-ix;ty=gy-iy
        tx=tx*tx*(3-2*tx);ty=ty*ty*(3-2*ty)
        n=(cloud_grain(ix,iy)*(1-tx)+cloud_grain(ix+1,iy)*tx)*(1-ty)+(cloud_grain(ix,iy+1)*(1-tx)+cloud_grain(ix+1,iy+1)*tx)*ty
        c=round(n*255);cloud.putpixel((x,y),(c,c,c,255))
cloud.save(ROOT/'cloud.png')
# A twig spray with separated lance-shaped leaves; alpha keeps fine silhouettes.
leaf=Image.new('RGBA',(64,64)); draw=ImageDraw.Draw(leaf)
draw.line((9,57,49,9), fill=(49,66,16,255),width=2)
for k in range(13):
    t=(k+1)/15; x=9+40*t; y=57-48*t
    side=1 if k%2 else -1; dx=side*(10+3*math.sin(k*2.7)); dy=side*6-5
    green=70+int(24*(.5+.5*math.sin(k*3.3)))
    draw.polygon([(x,y),(x+dx*.5-dy*.25,y+dy*.5+dx*.25),(x+dx,y+dy),
                  (x+dx*.5+dy*.25,y+dy*.5-dx*.25)],fill=(green//2,green,15,255))
leaf.save(ROOT/'leaf.png')
flower=Image.new('RGBA',(32,32)); draw=ImageDraw.Draw(flower)
draw.line((16,31,16,5),fill=(36,71,16,255),width=2)
for k in range(7):
    y=5+k*2; x=16+(-1 if k%2 else 1)*(2+k*.45)
    draw.ellipse((x-2,y-2,x+2,y+2),fill=(174+k*7,76+k*8,176+k*6,255))
flower.save(ROOT/'flower.png')

# Tree geometry, shared by the demo and the atlas baker through scene.txt.
meshes=[]
verts=[];indices=[]
for bottom,top,r0,r1 in [((0,0,0),(0,6.5,0),.38,.14),((0,3.6,0),(-2,6,0),.18,.06),
                         ((0,4.3,0),(2.4,6.3,.5),.16,.04),((0,4,0),(.3,6,-2.2),.16,.04)]:
    direction=[top[k]-bottom[k] for k in range(3)]; l=math.sqrt(sum(c*c for c in direction));direction=[c/l for c in direction]
    u=[direction[1],-direction[0],0];l=math.sqrt(sum(c*c for c in u));u=[c/l for c in u]
    v=[direction[1]*u[2]-direction[2]*u[1],direction[2]*u[0]-direction[0]*u[2],direction[0]*u[1]-direction[1]*u[0]]
    start=len(verts)
    for end in range(2):
        for k in range(9):
            a=k*math.pi/4;n=[u[j]*math.cos(a)+v[j]*math.sin(a) for j in range(3)]
            p=[(top if end else bottom)[j]+n[j]*(r1 if end else r0) for j in range(3)]
            verts.append(p+n+u+[k/8,end])
    for k in range(8):
        a=start+k; indices.extend([a,a+9,a+1,a+1,a+9,a+10])
meshes.append((verts,indices))
verts=[];indices=[]
clusters=[(0,7,0,2.1),(-1.9,5.9,0,1.6),(2,6.1,.5,1.8),(.3,5.9,-1.9,1.7),(-.9,6.7,1.5,1.6),(1.1,7.3,-.8,1.6)]
def canopy(clusters, sprays):
    verts=[];indices=[]
    for cx,cy,cz,r in clusters:
        for k in range(sprays):
            a=k*2.399963; ny=1-2*(k+.5)/sprays; radial=math.sqrt(1-ny*ny)
            n=(math.cos(a)*radial,ny,math.sin(a)*radial)
            u=(-math.sin(a),0,math.cos(a)); v=(-ny*math.cos(a),radial,-ny*math.sin(a))
            shell=.60+.22*math.sin(k*7.3+cx*2)
            centre=(cx+n[0]*r*shell,cy+n[1]*r*shell,cz+n[2]*r*shell)
            half=r*.47;start=len(verts)
            for j in range(4):
                sx=-1 if j%2==0 else 1;sy=-1 if j<2 else 1
                pos=[centre[q]+half*(u[q]*sx+v[q]*sy) for q in range(3)]
                # Canopy normals point outwards, avoiding crossed-card lighting seams.
                verts.append(pos+list(n)+list(u)+[(sx+1)/2,(1-sy)/2])
            indices.extend([start,start+1,start+3,start,start+3,start+2])
    return verts,indices
meshes.append(canopy(clusters,48))
meshes.append(canopy([(0,.65,0,.85),(.55,.6,.1,.6),(-.4,.6,.3,.65)],24))
# The path travels roughly 190m in 120 seconds. Look targets turn toward the
# river in the shallows and toward distant ridges for the final wide view.
keys=[]
for z,offset,eye,target_x,target_z,target_y in [(594,-53,1.7,8,38,-.5),(620,-43,1.7,20,33,-1),
        (646,-29,1.7,18,13,-6),(672,-29,1.7,18,13,-6),(698,-37,1.8,18,35,-.5),
        (724,-40,1.8,10,36,.5),(752,-40,1.8,20,65,6),(782,-42,3.0,45,130,14)]:
    x=centre(z)+offset;y=height(x,z)+eye
    keys.append((x,y,z,x+target_x,y+target_y,z+target_z))
with (ROOT/'scene.txt').open('w') as f:
    f.write(f'{N} 64 {CELL} {MIN} {RANGE} 0\n{len(keys)}\n')
    for key in keys:f.write(' '.join(f'{v:.6f}' for v in key)+'\n')
    for verts,indices in meshes:
        f.write(f'{len(verts)} {len(indices)}\n')
        for vertex in verts:f.write(' '.join(f'{v:.6f}' for v in vertex)+'\n')
        f.write(' '.join(str(i) for i in indices)+'\n')

# Orthographic z-buffer rasterization of the exact tree, eight horizontal
# views. The canopy cutouts are sampled from leaf.png, not hand-drawn blobs.
S=128; atlas=Image.new('RGBA',(8*S,S)); leafpx=leaf.load()
for view in range(8):
    theta=(view+.5)*math.pi/4;eye=(math.cos(theta),0,math.sin(theta));right=(eye[2],0,-eye[0])
    tile=Image.new('RGBA',(S,S));pixels=tile.load();zbuf=[-1e30]*(S*S)
    for mesh_id,(verts,indices) in enumerate(meshes[:2]):
        projected=[]
        for vert in verts:
            x,y,z=vert[:3];projected.append(((x*right[0]+z*right[2])/8*S+S/2,(9-y)/9*S,x*eye[0]+z*eye[2]))
        for at in range(0,len(indices),3):
            ids=indices[at:at+3];a,b,c=[projected[i] for i in ids]
            denominator=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(denominator)<1e-8:continue
            for y in range(max(0,int(min(a[1],b[1],c[1]))),min(S,int(max(a[1],b[1],c[1]))+1)):
                for x in range(max(0,int(min(a[0],b[0],c[0]))),min(S,int(max(a[0],b[0],c[0]))+1)):
                    wa=((b[1]-c[1])*(x+.5-c[0])+(c[0]-b[0])*(y+.5-c[1]))/denominator
                    wb=((c[1]-a[1])*(x+.5-c[0])+(a[0]-c[0])*(y+.5-c[1]))/denominator;wc=1-wa-wb
                    if min(wa,wb,wc)<0:continue
                    depth=wa*a[2]+wb*b[2]+wc*c[2]
                    if depth<=zbuf[y*S+x]:continue
                    if mesh_id:
                        u=sum(w*verts[i][9] for w,i in zip((wa,wb,wc),ids));v=sum(w*verts[i][10] for w,i in zip((wa,wb,wc),ids))
                        color=leafpx[max(0,min(63,int(u*64))),max(0,min(63,int(v*64)))]
                        if color[3]<128:continue
                    else:color=(79,52,29,255)
                    pixels[x,y]=color;zbuf[y*S+x]=depth
    atlas.paste(tile,(view*S,0))
atlas.save(ROOT/'tree_atlas.png')
print(f'Baked valley assets to {ROOT}')
