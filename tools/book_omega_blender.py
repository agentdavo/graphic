"""Apply proportions measured from the user's official-book side elevation.

Image landmarks (approximate pixels): bow 207..398; forward bearing 565..604;
habitat 604..881; aft bearing 881..924; aft spine 924..1254; stern 1254..1664.
The model retains bow Z=-20 and exhaust Z=15.72 to preserve demo timing.
Run after reshape_omega_blender.py and before texture/light/export/package.
"""
import ast
import bpy
import math
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
scene=bpy.data.scenes['OMEGA | Reference rebuild']; bpy.context.window.scene=scene
ship=bpy.data.collections['OMEGA | Agamemnon']
if ship.get('book_proportions'): raise RuntimeError('Book proportions already applied')
def getmat(i): return next(m for m in bpy.data.materials if m.name.startswith(f'{i:02} |'))
armor,edge,dark,red,bronze,black,blue,lamp,nav,white,plume=[getmat(i) for i in range(1,12)]
for node in ast.parse((ROOT/'tools/build_omega_blender.py').read_text(encoding='utf-8')).body:
    if isinstance(node,ast.FunctionDef) and node.name in ('mesh','box','tube','ring','strut'):
        exec(compile(ast.Module(body=[node],type_ignores=[]),'<book primitives>','exec'))
part=0

def remap(z):
    anchors=[(-22.1,-22.1),(-20,-20),(-14.65,-15.32),(-7.66,-10.27),
             (4.1,-3.49),(7.7,5.65),(10,9.84),(15.72,15.72),(20.5,20.5)]
    for (a,b),(c,d) in zip(anchors,anchors[1:]):
        if z<=c: return b+(z-a)/(c-a)*(d-b)
    return z

# Replace old loose axial trusses with purpose-built enclosed sections.
old=('Spine |','Detail | spine','Shape | continuous axial','Shape | axial',
     'Shape | recessed spine','Shape | red conduit','Weapons | turret','Weapons | secondary')
for obj in list(ship.objects):
    if obj.name.startswith(old): bpy.data.objects.remove(obj,do_unlink=True)

for obj in list(ship.objects):
    if obj.type=='FONT':
        obj.location.z=remap(obj.location.z)
        obj.location.y*=.64 if obj.location.y>0 else 1.
        obj.data.size*=.87
        continue
    if obj.type!='MESH': continue
    # Export variants share mesh data. Work only on the editable master copy.
    if obj.data.users>1: obj.data=obj.data.copy()
    moving=obj.get('omega_part')==1
    isbank=moving and any(abs(v.co.y)>2.4 for v in obj.data.vertices)
    bearing=moving and any(term in obj.name for term in ('bearing','collar','bright lip'))
    meanz=sum(v.co.z for v in obj.data.vertices)/max(1,len(obj.data.vertices))
    cannon=obj.name.startswith(('Weapons | forward','Weapons | muzzle'))
    for v in obj.data.vertices:
        z=v.co.z
        if moving:
            if bearing:
                target=-10.80 if meanz<0 else -2.98
                v.co.z=target+(z-meanz)*.9
            elif isbank:
                v.co.z=-10.27+(z+7.66)/11.76*6.78
            else:
                v.co.z=-10.65+(z+7.15)/10.85*7.5
            v.co.x*=.92; v.co.y*=.76 if isbank else .92
        elif not cannon:
            v.co.z=remap(z)
            if meanz<-14.65:
                if v.co.y>0: v.co.y*=.64
            elif meanz>7.7:
                v.co.x*=.88; v.co.y*=.88
    obj.data.update()

# Bow-to-engine pressure trunk, concealed behind service armor.
box('Book | pressure trunk',(0,0,-2.6),(1.75,1.92,26.0),dark,.20)
for begin,end in [(-15.32,-11.23),(-2.43,5.65)]:
    center=(begin+end)/2; length=end-begin
    for sx in (-1,1):
        # A continuous black equipment wall behind thin raised armor bands.
        box('Book | equipment recess',(sx*1.12,0,center),(.16,2.78,length),black)
        for sy in (-1,1):
            box('Book | broad spine armor',(sx*1.25,sy*1.17,center),(.26,.44,length),armor)
            box('Book | intermediate armor rail',(sx*1.37,sy*.44,center),(.16,.15,length),edge,.04)
        box('Book | axial recessed stripe',(sx*1.24,0,center),(.20,.28,length),dark)
        # Ordered pairs of red equipment sockets, with deliberate quiet gaps.
        for group in range(1 if length<5 else 2):
            gz=begin+1.05+group*3.1
            for k in range(6):
                z=gz+k*.31
                for sy in (-1,1):
                    y=sy*.245
                    tube('Book | red socket',(sx*1.25,y,z),(sx*1.45,y,z),.095,.085,red,8)
        # Asymmetric but deterministic small plumbing arrangements in top/bottom bays.
        for k in range(int(length/.55)):
            z=begin+.28+k*.55
            for sy in (-1,1):
                y=sy*.77
                if k%3==0:
                    box('Book | junction box',(sx*1.34,y,z),(.17,.20,.27),dark,.035)
                    tube('Book | pipe elbow leg',(sx*1.46,y-.12,z+.08),(sx*1.46,y+.12,z+.08),.023,mat=bronze,sides=6)
                elif k%3==1:
                    tube('Book | service pipe',(sx*1.40,y,z-.19),(sx*1.40,y,z+.19),.030,mat=edge,sides=6)
                else:
                    box('Book | vent cartridge',(sx*1.39,y,z),(.13,.25,.15),armor,.025)
    for sy in (-1,1):
        box('Book | continuous dorsal armor',(0,sy*1.42,center),(2.48,.22,length),armor,.09)
        for z in [begin+.7]+([begin+3.2,begin+6.1] if length>5 else []):
            box('Book | battery plinth',(0,sy*1.61,z),(.68,.21,.68),dark)
            box('Book | secondary turret',(0,sy*1.80,z),(.65,.23,.51),armor)
            for x in (-.16,.16):
                tube('Book | paired secondary gun',(x,sy*1.83,z-.13),(x,sy*1.83,z-.72),.045,mat=dark,sides=8)

# Broad tapered engineering shoulder: armor panels flank the cooling slots.
for sx in (-1,1):
    # Frame is narrow at the long aft spine and broad at the engine cradle.
    for sy in (-1,1):
        strut('Book | engineering shoulder edge',(sx*1.24,sy*1.40,5.65),(sx*2.90,sy*2.38,9.85),.19,armor)
    verts=[(sx*1.24,-1.40,5.65),(sx*1.24,1.40,5.65),(sx*2.90,2.38,9.85),(sx*2.90,-2.38,9.85)]
    mesh('Book | tapered shoulder armor',verts,[(0,1,2,3)],armor)
    for k in range(5):
        z=6.12+k*.67; x=sx*(1.24+(z-5.65)/4.2*1.66+.025)
        box('Book | shoulder cooling inset',(x,0,z),(.045,1.80,.17),black,.015)

# Keep a side-on inspection camera, with true world Y up and bow to the left.
from mathutils import Matrix
cam_data=bpy.data.cameras.get('Official-book side elevation') or bpy.data.cameras.new('Official-book side elevation')
cam=bpy.data.objects.get('Official-book side elevation')
if not cam:
    cam=bpy.data.objects.new('Official-book side elevation',cam_data); scene.collection.objects.link(cam)
cam.location=(-55,0,-2.1)
direction=Vector((1,0,0)); right=Vector((0,0,1)); up=Vector((0,1,0))
cam.rotation_euler=Matrix((right,up,-direction)).transposed().to_euler()
cam_data.type='ORTHO'; cam_data.ortho_scale=42
ship['book_proportions']=1
scene['book_landmarks']='Bow -20..-15.32; forward neck -15.32..-11.23; habitat -10.27..-3.49; aft neck -2.43..5.65; stern 5.65..15.72.'
scene['engine_center_x']=1.804; scene['engine_center_y']=1.628
print('Official-book proportions applied; fore/aft exposed spines 4.09 / 8.08 units')
