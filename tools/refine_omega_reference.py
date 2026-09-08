"""Run inside Blender through MCP against omega_destroyer.blend.

Reference: https://www.starshipmodeler.com/b5/b5tech.htm, Omega detail renders.
The caller saves a complete before.blend before invoking these stages.
"""
import bpy
import math
from mathutils import Vector

SCENE = bpy.data.scenes['OMEGA | Reference rebuild']
COLLECTION = bpy.data.collections['OMEGA | Agamemnon']
PARENT = bpy.data.objects['Habitat rotation | Z axis']
HULL = bpy.data.materials['01 | weathered titanium']
EDGE = bpy.data.materials['02 | exposed machined edges']
DARK = bpy.data.materials['03 | graphite recesses']
BLACK = bpy.data.materials['06 | hangar darkness']
PAINT = bpy.data.materials['10 | identification paint']
BRONZE = bpy.data.materials['05 | aged bronze machinery']
BUCKETS = {}

def mesh_data(name, verts, faces):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    return mesh

def attach(name, mesh, mat, part=0):
    mesh.materials.append(mat)
    obj = bpy.data.objects.new('Reference | '+name, mesh)
    COLLECTION.objects.link(obj)
    obj['omega_part'] = part
    obj['reference_source'] = 'Starship Modeler PTEN/WB Omega detail renders'
    if part: obj.parent = PARENT
    for ship in ('Agamemnon', 'Alexander'):
        copy = bpy.data.objects.new(ship+' | '+obj.name, mesh)
        bpy.data.collections['OMEGA | '+ship+' portable'].objects.link(copy)
        copy['omega_part'] = part
    return obj

def append(name, verts, faces, mat=HULL, part=0):
    key = (name, mat.name, part)
    v, f = BUCKETS.setdefault(key, ([], []))
    base = len(v)
    v.extend(verts)
    f.extend(tuple(base+i for i in face) for face in faces)

def flush():
    for (name, mat, part), (verts, faces) in BUCKETS.items():
        attach(name, mesh_data(name, verts, faces), bpy.data.materials[mat], part)
    BUCKETS.clear()

def box(name, center, size, mat=HULL, part=0):
    x,y,z=center; a,b,c=(v/2 for v in size)
    verts=[(x+dx*a,y+dy*b,z+dz*c) for dx,dy,dz in
           [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
    append(name,verts,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],mat,part)

def rod(name, start, end, radius, mat=EDGE, part=0, end_radius=None, sides=8):
    start,end=Vector(start),Vector(end)
    direction=(end-start).normalized()
    u=direction.cross(Vector((0,1,0)))
    if u.length < .01: u=direction.cross(Vector((1,0,0)))
    u.normalize();v=direction.cross(u)
    verts=[]
    for p,r in ((start,radius),(end,radius if end_radius is None else end_radius)):
        verts += [tuple(p+r*(u*math.cos(k*math.tau/sides)+v*math.sin(k*math.tau/sides))) for k in range(sides)]
    faces=[tuple(reversed(range(sides))),tuple(range(sides,2*sides))]
    faces += [(k,(k+1)%sides,(k+1)%sides+sides,k+sides) for k in range(sides)]
    append(name,verts,faces,mat,part)

def retire(prefixes):
    # Matched source meshes have portable copies sharing the same datablock.
    meshes={o.data for o in SCENE.objects if o.type=='MESH' and any(o.name.startswith(p) for p in prefixes)}
    targets=[o for o in bpy.data.objects if o.type=='MESH' and o.data in meshes]
    for o in targets: bpy.data.objects.remove(o,do_unlink=True)

def polygon_x(name, profile, x0, x1, mat=HULL):
    n=len(profile)
    verts=[(x,y,z) for x in (x0,x1) for z,y in profile]
    faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
    faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    append(name,verts,faces,mat)

def text_mesh(name, body, side, y, z, size):
    curve=bpy.data.curves.new(name,'FONT')
    curve.body=body;curve.align_x='CENTER';curve.align_y='CENTER'
    curve.size=size;curve.extrude=.001
    obj=bpy.data.objects.new(name,curve);COLLECTION.objects.link(obj)
    obj.location=(side*2.245,y,z);obj.rotation_euler=(0,side*math.pi/2,0)
    bpy.context.view_layer.update()
    mesh=bpy.data.meshes.new_from_object(obj.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    mesh.transform(obj.matrix_world)
    bpy.data.objects.remove(obj,do_unlink=True)
    bpy.data.curves.remove(curve)
    return mesh

def lettering(name, body, side, y, z, size, mat):
    attach(name,text_mesh(name,body,side,y,z,size),mat)

def bow():
    assert not SCENE.get('reference_bow_done'), 'bow stage already applied'
    retire(['Shape | wedge command cheek','Shape | bow perimeter rim','Shape | sloping command roof',
            'Shape | cheek midline relief','Shape | cheek inset identification','Shape | fleet emblem',
            'Shape | emblem inlay','Identity | nameplate','Command | antenna'])
    for o in list(bpy.data.objects):
        if o.type=='FONT' and o.name.startswith('Identity | AGAMEMNON'): bpy.data.objects.remove(o,do_unlink=True)
    profile=[(-20,1.35),(-19.45,2.24),(-17.35,2.24),(-15.32,1.088),
             (-15.32,-1.65),(-17.95,-4.7),(-19.2,-4.7),(-20,-1.65)]
    for s in (-1,1):
        polygon_x('stepped command cheek',profile,*sorted((s*1.75,s*2.12)))
        for i,(z,y) in enumerate(profile):
            zz,yy=profile[(i+1)%len(profile)]
            rod('dark command edge',(s*2.14,y,z),(s*2.14,yy,zz),.095,DARK)
        # Large framed command panel, identification and the lower hull letter.
        box('command inset border',(s*2.15,-.05,-17.74),(.045,2.8,4.06),DARK)
        box('command inset armor',(s*2.18,-.05,-17.74),(.035,2.5,3.76),HULL)
        box('nameplate black frame',(s*2.205,.78,-17.9),(.04,.55,2.94),BLACK)
        box('nameplate pale field',(s*2.23,.78,-17.9),(.015,.43,2.82),PAINT)
        lettering('AGAMEMNON identification','AGAMEMNON',s,.78,-17.9,.285,BLACK)
        lettering('bow hull A outline','A',s,-3.72,-18.55,1.03,BLACK)
        lettering('bow hull A inset','A',s,-3.72,-18.55,.83,HULL)
        # Bent conduit and raised equipment housing visible on the side render.
        box('bow conduit',(s*2.24,-.35,-17.55),(.055,.11,3.74),DARK)
        box('bow conduit riser',(s*2.24,-.08,-19.1),(.055,.66,.12),DARK)
        box('command equipment plinth',(s*2.245,-.4,-19.33),(.18,.57,.6),HULL)
        rod('command equipment cylinder',(s*2.4,-.5,-19.55),(s*2.4,-.5,-19.1),.11,EDGE)
        for y in (-.04,.13): box('command equipment vents',(s*2.34,y,-19.3),(.025,.05,.4),DARK)
        # The forward lance is a tapered housing and a fine aerial, not a bright light.
        rod('lower forward lance',(s*1.35,-2.75,-19.8),(s*1.35,-2.75,-21.7),.16,DARK,end_radius=.075)
        rod('lance probe',(s*1.35,-2.75,-21.7),(s*1.35,-2.75,-23.3),.026,EDGE,end_radius=.009)
        rod('upper command mast',(s*.9,2.24,-18.35),(s*.9,4.25,-18.35),.035,DARK)
        rod('ventral command mast',(s*.72,-4.63,-18.65),(s*.72,-6.05,-18.65),.025,DARK)
    # A continuous roof follows the clipped leading corner and the aft slope.
    upper=profile[:4]
    for (z,y),(zz,yy) in zip(upper,upper[1:]):
        append('command roof', [(-1.75,y,z),(1.75,y,z),(1.75,yy,zz),(-1.75,yy,zz)],[(0,1,2,3)],HULL)
    flush();SCENE['reference_bow_done']=True

def habitat_and_spine():
    assert not SCENE.get('reference_habitat_done'), 'habitat stage already applied'
    for side in (-1,1):
        # Banded panels leave the large truss bays exposed between them.
        for bank in (-1,1):
            for y, width in ((2.05,.29),(3.32,.31),(3.62,.22),(5.14,.29)):
                box('habitat layered band',(side*2.10,bank*y,-6.88),(.18,width,6.43),HULL,1)
                box('habitat band reveal',(side*2.203,bank*(y-width*.35),-6.88),(.025,.028,6.12),DARK,1)
                for k in range(12):
                    z=-9.81+k*.534
                    box('habitat band panel',(side*2.208,bank*y,z),(.038,width*.62,.47),EDGE if k%4==0 else HULL,1)
                    box('habitat panel slot',(side*2.231,bank*y,z),(.012,.045,.21),DARK,1)
            for y in (2.27,4.99):
                for k in range(24):
                    z=-9.88+k*.263
                    box('habitat service blocks',(side*2.135,bank*y,z),(.18,.12,.16),HULL,1)
            # Fine pipes in the open aperture carry an industrial hierarchy.
            for y in (2.67,2.84,4.32,4.49):
                rod('habitat aperture pipes',(side*2.065,bank*y,-9.94),(side*2.065,bank*y,-3.84),.028,BRONZE,1)
            for k in range(6):
                z=-9.54+k*1.06
                box('habitat service cover',(side*2.12,bank*3.68,z),(.20,.48,.70),HULL,1)
        # Neck armor should read as solid equipment-bearing hull around sockets.
        for start,end in ((-15.30,-11.25),(-2.41,5.63)):
            for y in (-.78,.78):
                box('neck equipment armor',(side*1.32,y,(start+end)/2),(.13,.58,end-start),HULL)
                for k in range(int((end-start)/.54)):
                    z=start+.3+k*.54
                    box('neck recessed vent',(side*1.395,y,z),(.023,.21,.25),DARK)
                    for t in (-1,0,1): box('neck vent blade',(side*1.413,y+t*.065,z),(.035,.023,.21),EDGE)
            for k in range(int((end-start)/1.3)):
                z=start+.6+k*1.3
                box('neck dorsal service hatch',(side*.63,1.415,z),(.67,.07,.78),DARK)
                box('neck dorsal hatch inset',(side*.63,1.46,z),(.57,.04,.66),HULL)
    # Raise the undersized silhouette of the existing secondary battery bodies.
    for o in SCENE.objects:
        if o.type=='MESH' and o.name.startswith('Book | secondary turret'):
            center=sum((v.co for v in o.data.vertices),Vector())/len(o.data.vertices)
            for v in o.data.vertices:
                v.co.y=center.y+(v.co.y-center.y)*1.9
            o.data.update()
    flush();SCENE['reference_habitat_done']=True

def engines():
    assert not SCENE.get('reference_engines_done'), 'engine stage already applied'
    mat=bpy.data.materials.new('12 | blue-black engine shrouds')
    mat.diffuse_color=(.025,.035,.072,1);mat.use_nodes=True
    p=mat.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value=mat.diffuse_color
    p.inputs['Metallic'].default_value=.62;p.inputs['Roughness'].default_value=.46
    for o in SCENE.objects:
        if o.type!='MESH': continue
        if o.name.startswith('Engine | flared bell'):
            mesh=o.data
            cx=(min(v.co.x for v in mesh.vertices)+max(v.co.x for v in mesh.vertices))/2
            cy=(min(v.co.y for v in mesh.vertices)+max(v.co.y for v in mesh.vertices))/2
            for v in mesh.vertices:
                delta=Vector((v.co.x-cx,v.co.y-cy))
                if delta.length:
                    t=max(0,min(1,(v.co.z-14.312)/(15.699-14.312)))
                    delta.normalize();r=1.0-.22*t
                    v.co.x=cx+delta.x*r;v.co.y=cy+delta.y*r
            mesh.materials.clear();mesh.materials.append(mat);mesh.update()
        elif o.name.startswith(('Engine | inner bell','Engine | blue exhaust')):
            mesh=o.data
            cx=(min(v.co.x for v in mesh.vertices)+max(v.co.x for v in mesh.vertices))/2
            cy=(min(v.co.y for v in mesh.vertices)+max(v.co.y for v in mesh.vertices))/2
            for v in mesh.vertices: v.co.x=cx+(v.co.x-cx)*.78;v.co.y=cy+(v.co.y-cy)*.78
            mesh.update()
    retire(['Detail | bell reinforcement'])
    for sx in (-1,1):
        for sy in (-1,1):
            cx,cy=sx*1.804,sy*1.628
            for k in range(8):
                angle=k*math.tau/8
                rod('shroud narrow rib',(cx+1.01*math.cos(angle),cy+1.01*math.sin(angle),14.34),
                    (cx+.79*math.cos(angle),cy+.79*math.sin(angle),15.65),.018,DARK)
            # A raised elbow and hydraulic cylinder make each cradle arm legible.
            points=[(sx*2.78,sy*2.68,10.1),(sx*2.78,sy*3.06,11.5),(sx*2.78,sy*2.67,13.0),(sx*2.78,sy*2.54,15.1)]
            for a,b in zip(points,points[1:]): rod('angled nacelle cradle',a,b,.075,HULL)
            rod('cradle hydraulic cylinder',(sx*2.75,sy*2.42,10.6),(sx*2.75,sy*2.42,12.15),.18,DARK)
            rod('cradle hydraulic piston',(sx*2.75,sy*2.42,12.15),(sx*2.75,sy*2.42,13.7),.06,EDGE)
    # Axial stern keel/spear between the four separated nozzles.
    box('stern axial equipment',(0,0,14.7),(1.04,.58,2.35),HULL)
    append('stern tapered projection',[(-.53,-.29,15.65),(.53,-.29,15.65),(.53,.29,15.65),(-.53,.29,15.65),(0,0,17.65)],
           [(0,3,2,1),(0,1,4),(1,2,4),(2,3,4),(3,0,4)],HULL)
    for sx in (-1,1):
        rod('stern keel conduit',(sx*.29,.34,13.8),(sx*.29,.34,16.2),.037,DARK)
        for z in (14.0,14.6,15.2): box('stern equipment rail',(sx*.6,0,z),(.17,.74,.14),EDGE)
    flush();SCENE['reference_engines_done']=True

def finish():
    assert not SCENE.get('reference_finish_done'), 'finish stage already applied'
    # Remove obsolete baked name meshes from the portable scenes.
    for o in list(bpy.data.objects):
        if o.type=='MESH' and '| Identity | AGAMEMNON' in o.name:
            bpy.data.objects.remove(o,do_unlink=True)
    for o in bpy.data.scenes['OMEGA | Alexander export'].objects:
        if 'Reference | AGAMEMNON identification' in o.name:
            side=1 if o.data.vertices[0].co.x>0 else -1
            o.data=text_mesh('Alexander identification','ALEXANDER',side,.78,-17.9,.285)
            o.data.materials.append(BLACK)
            o.name=o.name.replace('AGAMEMNON','ALEXANDER')
    for o in SCENE.objects:
        if o.type=='MESH' and o.name.startswith('Reference | bow hull A inset'):
            for v in o.data.vertices: v.co.x += .009 if v.co.x>0 else -.009
            o.data.update()
    # Small winged oval inferred from the gold EarthForce emblem in the side detail.
    for side in (-1,1):
        x=side*2.246;z=-17.50;y=-.93
        for k in range(24):
            a=k*math.tau/24;b=(k+1)*math.tau/24
            rod('EarthForce oval',(x,y+.16*math.sin(a),z+.59*math.cos(a)),
                (x,y+.16*math.sin(b),z+.59*math.cos(b)),.019,BRONZE,sides=6)
        for sign in (-1,1):
            rod('EarthForce wing',(x,y-.02,z),(x,y+.20,z+sign*.73),.022,BRONZE,sides=6)
            rod('EarthForce lower wing',(x,y-.17,z),(x,y+.20,z+sign*.73),.018,BRONZE,sides=6)
    flush()
    SCENE['reference_finish_done']=True
    SCENE['reference_revision']='2026-09-08: Starship Modeler PTEN/WB orthographic/detail comparison'
    SCENE['reference_urls']='https://www.starshipmodeler.com/b5/b5tech.htm; omeg_rs.jpg; omeg_top.jpg; omeg_rear.jpg; omeg_side_dtl1.jpg; omeg_side_dtl2.jpg; omeg_side_dtl3.jpg'
    SCENE['reference_changes']='Clipped wedge bow and roof; framed identification and hull A; habitat armor bands; neck equipment; tapered blue-black engine shrouds, cradle hydraulics and stern projection.'
