"""Run in Blender (including via Blender MCP). Builds editable Omega source assets.

Coordinates match the native demo: bow -Z, dorsal +Y, rotation around Z.
Geometry and material tags are exported by export_omega_blender.py.
"""
import bpy
import math
import random
from mathutils import Vector
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/omega'
OUT.mkdir(parents=True, exist_ok=True)
random.seed(21)
# Preserve the user's starting scene; author into a separate scene.
scene = bpy.data.scenes.get('OMEGA | Reference rebuild')
if scene:
    raise RuntimeError('Rebuild scene already exists; do not overwrite edited artwork.')
scene = bpy.data.scenes.new('OMEGA | Reference rebuild')
bpy.context.window.scene = scene
ship = bpy.data.collections.new('OMEGA | Agamemnon')
scene.collection.children.link(ship)
materials = {}

def material(name, color, tag=0, metallic=.65, rough=.55):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m['omega_material'] = tag
    m.use_nodes = True
    n, links = m.node_tree.nodes, m.node_tree.links
    bs = n.get('Principled BSDF')
    bs.inputs['Base Color'].default_value = (*color, 1)
    bs.inputs['Metallic'].default_value = metallic
    bs.inputs['Roughness'].default_value = rough
    if tag in (3, 4, 7):
        bs.inputs['Emission Color'].default_value = (*color, 1)
        bs.inputs['Emission Strength'].default_value = 5 if tag == 3 else 2
    else:
        tex = n.new('ShaderNodeTexNoise'); tex.inputs['Scale'].default_value = 38
        tex.inputs['Detail'].default_value = 3
        ramp = n.new('ShaderNodeValToRGB')
        ramp.color_ramp.elements[0].color = (*(v*.42 for v in color), 1)
        ramp.color_ramp.elements[1].color = (*color, 1)
        links.new(tex.outputs['Fac'], ramp.inputs[0]); links.new(ramp.outputs[0], bs.inputs['Base Color'])
        bump = n.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = .22
        bump.inputs['Distance'].default_value = .025
        links.new(tex.outputs['Fac'], bump.inputs['Height']); links.new(bump.outputs[0], bs.inputs['Normal'])
    materials[name] = m
    return m

armor = material('01 | weathered titanium', (.43,.45,.46))
edge = material('02 | exposed machined edges', (.63,.64,.61), rough=.38)
dark = material('03 | graphite recesses', (.105,.12,.135), 2)
red = material('04 | oxide red hull panels', (.34,.052,.034), metallic=.35)
bronze = material('05 | aged bronze machinery', (.31,.24,.13))
black = material('06 | hangar darkness', (.018,.023,.029), 2)
blue = material('07 | ion exhaust', (.3,.65,1), 3)
lamp = material('08 | warm running lights', (.8,.63,.35), 4)
nav = material('09 | red navigation lights', (1,.02,.008), 4)
white = material('10 | identification paint', (.78,.8,.74), metallic=.1)
plume = material('11 | exhaust plume', (.25,.5,1), 7)

part = 0
def mesh(name, verts, faces, mat):
    data = bpy.data.meshes.new(name)
    data.from_pydata(verts, [], faces); data.update()
    obj = bpy.data.objects.new(name, data); ship.objects.link(obj)
    obj.data.materials.append(mat); obj['omega_part'] = part
    return obj

def box(name, p, s, mat=armor, bevel=.08):
    # Chamfered cross section, sharp longitudinal armor seams.
    x,y,z = (v/2 for v in s); b = min(bevel,x*.7,y*.7)
    rim = [(-x+b,-y),(x-b,-y),(x,-y+b),(x,y-b),(x-b,y),(-x+b,y),(-x,y-b),(-x,-y+b)]
    verts = [(p[0]+a,p[1]+c,p[2]+d) for d in (-z,z) for a,c in rim]
    faces = [tuple(reversed(range(8))),tuple(range(8,16))]
    faces += [(i,(i+1)%8,(i+1)%8+8,i+8) for i in range(8)]
    return mesh(name,verts,faces,mat)

def tube(name,a,b,r1,r2=None,mat=edge,sides=10,open_end=False):
    r2 = r1 if r2 is None else r2
    axis = (Vector(b)-Vector(a)).normalized()
    u = axis.cross(Vector((0,1,0)) if abs(axis.y)<.9 else Vector((1,0,0))).normalized()
    v = axis.cross(u)
    verts = [tuple(Vector(p)+r*(u*math.cos(k*math.tau/sides)+v*math.sin(k*math.tau/sides)))
             for p,r in ((a,r1),(b,r2)) for k in range(sides)]
    faces = [(k,(k+1)%sides,(k+1)%sides+sides,k+sides) for k in range(sides)]
    if not open_end: faces += [tuple(reversed(range(sides))),tuple(range(sides,2*sides))]
    return mesh(name,verts,faces,mat)

def ring(name,z,outer,inner,depth,mat=edge,sides=24):
    verts=[(r*math.cos(k*math.tau/sides),r*math.sin(k*math.tau/sides),zz)
           for zz,r in ((z-depth/2,outer),(z+depth/2,outer),(z-depth/2,inner),(z+depth/2,inner)) for k in range(sides)]
    faces=[]
    for k in range(sides):
        j=(k+1)%sides
        faces += [(k,j,j+sides,k+sides),(k+2*sides,k+3*sides,j+3*sides,j+2*sides),
                  (k,k+2*sides,j+2*sides,j),(k+sides,j+sides,j+3*sides,k+3*sides)]
    return mesh(name,verts,faces,mat)

def strut(name,a,b,w=.10,mat=edge):
    return tube(name,a,b,w,mat=mat,sides=4)

# Tall hammerhead bow with dark inset flight deck and paired forward cannon.
box('Command | armored hammerhead',(0,0,-17.4),(3.7,6.3,4.6),armor,.6)
box('Command | dorsal crown',(0,3.15,-17.0),(2.7,.55,3.4),edge,.22)
box('Command | lower keel',(0,-3.12,-17.1),(2.8,.45,3.8),dark,.2)
box('Command | bow dark face',(0,0,-19.73),(2.65,3.5,.10),dark,.3)
box('Command | bridge visor',(0,1.48,-19.80),(2.2,.43,.1),black)
for i in range(9):
    box('Command | bridge slit',(-.94+i*.235,1.49,-19.87),(.12,.055,.025),lamp,.01)
for side in (-1,1):
    x = side*.9
    tube('Weapons | forward mantlet',(x,.4,-19.72),(x,.4,-19.93),.38,.30,dark,16)
    tube('Weapons | forward barrel',(x,.4,-19.93),(x,.4,-20.18),.21,.17,edge,16)
    tube('Weapons | muzzle',(x,.4,-20.18),(x,.4,-20.2),.12,mat=nav,sides=12)
    box('Command | flank hangar',(side*1.87,-.3,-17.25),(.08,1.8,2.6),black)
    for z in (-18.4,-17.9,-17.4,-16.9,-16.4):
        box('Command | flank ribs',(side*1.97,-.3,z),(.17,1.9,.12),edge)
    for y in (-2.0,2.25):
        box('Command | raised side armor',(side*1.84,y,-17.2),(.2,.78,3.5),armor)
    tube('Command | antenna',(side*1.2,3.3,-17),(side*1.2,5.25,-17),.032,.008,edge,6)
    box('Command | antenna beacon',(side*1.2,5.25,-17),(.07,.07,.07),nav,.01)

# Narrow continuous axial hull, panelled red service conduits and exposed bays.
box('Spine | pressure hull',(0,0,-1.2),(2.05,2.25,28),dark,.3)
for z in [ -14+i*1.65 for i in range(16) ]:
    box('Spine | frame',(0,0,z),(2.6,2.7,.22),edge,.3)
    for side in (-1,1):
        box('Spine | oxide service armor',(side*1.10,0,z+.65),(.22,1.65,1.18),red)
        box('Spine | dorsal plate',(0,side*1.18,z+.65),(1.7,.18,1.12),armor)
        for y in (-.65,.65):
            tube('Spine | service pipe',(side*1.34,y,z+.15),(side*1.34,y,z+1.42),.065,mat=bronze,sides=6)
for side in (-1,1):
    for z in (-13.8,-11.8,-9.8,6.5,8.5):
        strut('Spine | diagonal truss',(side*1.55,-1.2,z),(side*1.55,1.2,z+1.8),.10)

# Two opposed habitat banks on a rotating drum: this is NOT a cylindrical cage.
part = 1
tube('Habitat | central pressure drum',(0,0,-7),(0,0,3.6),1.68,mat=dark,sides=20)
for z in (-7.15,3.7): ring('Habitat | bearing race',z,2.05,1.55,.36)
for side in (-1,1):
    for z in (-6.8,3.3):
        box('Habitat | radial web',(0,side*3.15,z),(1.6,3.3,.42),bronze,.16)
        for x in (-1.32,1.32):
            strut('Habitat | open end diagonal',(x*.5,side*1.6,z),(x,side*5.65,z),.14)
            strut('Habitat | crossed end web',(-x,side*3.3,z),(x,side*5.65,z),.12)
    for tier,y in enumerate((3.25,5.25)):
        box('Habitat | rectangular bank',(0,side*y,-1.75),(3.25,1.18,10.8),dark,.23)
        for x in (-1.65,1.65):
            for yy in (-.6,.6):
                box('Habitat | longitudinal rail',(x,side*y+yy,-1.75),(.16,.14,10.95),edge,.04)
            for k in range(12):
                z=-6.95+k*.91
                strut('Habitat | lattice diagonal',(x,side*y-.55,z),(x,side*y+.55,z+.86),.065)
                box('Habitat | recessed panel',(x*.965,side*y,z+.4),(.05,.70,.61),bronze if k%4==0 else armor)
        for k in range(16):
            z=-6.8+k*.65
            box('Habitat | radiator teeth',(0,side*(y+.63),z),(3.0,.15,.18),edge,.03)
            for x in (-.95,.95):
                box('Habitat | window',(x,side*(y+.725),z),(.17,.025,.08),lamp,.01)
    for k in range(9):
        z=-6.6+k*1.15
        box('Habitat | outer armor tile',(0,side*5.97,z),(2.65,.18,.99),armor)
    box('Habitat | red end cap',(0,side*4.3,-7.3),(2.4,2.3,.16),red)
    for z in (-7.4,3.8):
        box('Habitat | end frame crossbar',(0,side*5.95,z),(3.4,.20,.2),edge)
        box('Habitat | navigation light',(1.6,side*6.1,z),(.08,.08,.08),nav,.02)

part=0
# Aft engineering shoulder, broad four-engine cluster and external conduits.
box('Engineering | reactor',(0,0,10.3),(3.8,4.4,5.2),armor,.55)
for side in (-1,1):
    for y in (-2.2,2.2):
        strut('Engineering | splayed shoulder',(side*.95,y*.52,7.4),(side*2.6,y,11.2),.27)
    for k in range(6):
        box('Engineering | cooling ribs',(side*1.95,0,8.2+k*.7),(.18,3.6,.21),edge)
    box('Engineering | red armor',(side*2.05,0,11.6),(.18,2.6,1.6),red)
for sx in (-1,1):
    for sy in (-1,1):
        x,y=sx*2.05,sy*1.85
        tube('Engine | pressure body',(x,y,10.9),(x,y,14.4),.86,mat=dark,sides=16)
        for k in range(5):
            tube('Engine | collar',(x,y,11+k*.64),(x,y,11.16+k*.64),.94,mat=edge,sides=16)
        for ox,oy in ((.82,0),(-.82,0),(0,.82),(0,-.82)):
            box('Engine | armored longeron',(x+ox,y+oy,12.5),(.22,.22,3.8),armor)
        tube('Engine | flared bell',(x,y,14.35),(x,y,15.7),.77,1.12,edge,24,True)
        tube('Engine | inner bell',(x,y,15.45),(x,y,15.72),.86,1.0,black,24,True)
        tube('Engine | blue exhaust',(x,y,15.66),(x,y,15.70),.91,mat=blue,sides=24)
        tube('Engine | plume',(x,y,15.75),(x,y,20.5),.84,.02,plume,16)

# Raised armor islands and recognizable twin-barrel secondary batteries.
for z in (-13.4,-10.8,6.0,8.0):
    for side in (-1,1):
        box('Weapons | turret base',(0,side*1.45,z),(1.2,.45,1.0),dark)
        box('Weapons | turret housing',(0,side*1.73,z),(.85,.42,.72),armor)
        for x in (-.24,.24):
            tube('Weapons | secondary barrel',(x,side*1.73,z-.30),(x,side*1.73,z-1.05),.065,mat=edge,sides=8)

# Geometry nameplates survive the native mesh export, unlike text-only decals.
for side in (-1,1):
    box('Identity | nameplate',(side*1.97,2.1,-17.1),(.04,.4,2.55),black)
    curve=bpy.data.curves.new('AGAMEMNON lettering','FONT'); curve.body='AGAMEMNON'
    curve.align_x='CENTER'; curve.align_y='CENTER'; curve.size=.29; curve.extrude=0
    obj=bpy.data.objects.new('Identity | AGAMEMNON',curve); ship.objects.link(obj)
    obj.location=(side*2.003,2.1,-17.1)
    obj.rotation_euler=(math.pi/2,0,side*math.pi/2)
    obj.data.materials.append(white); obj['omega_part']=0

# Parent the moving pieces to a clearly named animation pivot.
pivot=bpy.data.objects.new('Habitat rotation | Z axis',None); ship.objects.link(pivot)
for obj in list(ship.objects):
    if obj.get('omega_part')==1: obj.parent=pivot
pivot.rotation_euler.z=.23; pivot.keyframe_insert(data_path='rotation_euler',frame=1)
pivot.rotation_euler.z=.23+.17*20; pivot.keyframe_insert(data_path='rotation_euler',frame=1201)
scene.render.fps=60; scene.frame_end=1200; scene.frame_set(1)
scene['reference']='b5.mp4 0–10s; isn.mp4 7–42s, especially 9s and 17s'
scene['native_export']='Model-space geometry; omega_part=1 rotates in the native vertex shader.'

def aim(obj,point): obj.rotation_euler=(Vector(point)-obj.location).to_track_quat('-Z','Y').to_euler()
cam_data=bpy.data.cameras.new('Omega inspection camera'); cam=bpy.data.objects.new('Omega inspection camera',cam_data)
scene.collection.objects.link(cam); cam.location=(36,24,-40); aim(cam,(0,0,-1))
cam_data.type='ORTHO'; cam_data.ortho_scale=47; scene.camera=cam
for name,pos,power,color,size in [('Warm key',(10,22,-15),22000,(1,.83,.67),22),('Cool rim',(-15,7,16),26000,(.35,.55,1),18),('Soft fill',(20,-12,-8),9500,(.65,.73,1),15)]:
    d=bpy.data.lights.new(name,'AREA'); d.energy=power; d.color=color; d.shape='DISK'; d.size=size
    o=bpy.data.objects.new(name,d); scene.collection.objects.link(o); o.location=pos; aim(o,(0,0,0))
scene.world=bpy.data.worlds.new('Deep space studio'); scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.012,.018,.035,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=.35
scene.render.engine='CYCLES'; scene.cycles.samples=24
scene.render.resolution_x=1440; scene.render.resolution_y=900; scene.render.resolution_percentage=100
scene.view_settings.view_transform='AgX'
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_perspective='CAMERA'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'omega_destroyer.blend'))
print('Created',len(ship.objects),'editable ship parts')
