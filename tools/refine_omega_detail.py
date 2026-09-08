"""Second Blender MCP refinement, using the viewed production and episode images.

Execute the four stages against the saved first refinement, after making a backup.
Proportions follow image evidence; internal machinery is modeled interpretation.
"""
import bpy
import bmesh
import math
import types
from pathlib import Path
from mathutils import Vector
from mathutils.geometry import tessellate_polygon

BASE = types.ModuleType('omega_reference_base')
exec(compile(Path(__file__).with_name('refine_omega_reference.py').read_text(encoding='utf-8'),
             'refine_omega_reference.py','exec'),BASE.__dict__)
S=BASE.SCENE
HULL,EDGE,DARK,BLACK,BRONZE=BASE.HULL,BASE.EDGE,BASE.DARK,BASE.BLACK,BASE.BRONZE

def material(name,color,metal=.4,rough=.5,emission=0):
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value=m.diffuse_color
    p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
    if emission:
        p.inputs['Emission Color'].default_value=m.diffuse_color;p.inputs['Emission Strength'].default_value=emission
    return m

def append(name,verts,faces,mat=HULL,part=0):BASE.append('Detail II '+name,verts,faces,mat,part)
def box(name,c,size,mat=HULL,part=0):BASE.box('Detail II '+name,c,size,mat,part)
def rod(name,a,b,r,mat=EDGE,part=0,end_radius=None,sides=16):
    BASE.rod('Detail II '+name,a,b,r,mat,part,end_radius,sides)

def lathe(name,center,profile,mat=HULL,axis=(0,0,1),segments=64,part=0):
    """Axial distance/radius profile; return to its first point for a closed ring."""
    axis=Vector(axis).normalized();u=axis.cross(Vector((0,1,0)))
    if u.length<.1:u=axis.cross(Vector((1,0,0)))
    u.normalize();v=axis.cross(u);center=Vector(center)
    verts=[tuple(center+axis*z+r*(u*math.cos(k*math.tau/segments)+v*math.sin(k*math.tau/segments)))
           for z,r in profile for k in range(segments)]
    faces=[(j*segments+k,j*segments+(k+1)%segments,(j+1)*segments+(k+1)%segments,(j+1)*segments+k)
           for j in range(len(profile)-1) for k in range(segments)]
    append(name,verts,faces,mat,part)

def ellipsoid(name,center,radii,mat=HULL,part=0,segments=32,rings=16):
    x,y,z=center;a,b,c=radii
    verts=[(x,y,z-c)]
    for j in range(1,rings):
        phi=-math.pi/2+j*math.pi/rings
        verts += [(x+a*math.cos(phi)*math.cos(k*math.tau/segments),
                   y+b*math.cos(phi)*math.sin(k*math.tau/segments),z+c*math.sin(phi)) for k in range(segments)]
    top=len(verts);verts.append((x,y,z+c))
    faces=[(0,1+(k+1)%segments,1+k) for k in range(segments)]
    faces += [(1+j*segments+k,1+j*segments+(k+1)%segments,1+(j+1)*segments+(k+1)%segments,1+(j+1)*segments+k)
              for j in range(rings-2) for k in range(segments)]
    faces += [(top,1+(rings-2)*segments+k,1+(rings-2)*segments+(k+1)%segments) for k in range(segments)]
    append(name,verts,faces,mat,part)

def tunnel(name,outline,sections,mat=HULL):
    # Each section gives depth and planar scale, preserving a genuinely open mouth.
    verts=[(x*sx,y*sy-.62,z) for z,sx,sy in sections for x,y in outline]
    n=len(outline)
    faces=[(j*n+k,j*n+(k+1)%n,(j+1)*n+(k+1)%n,(j+1)*n+k)
           for j in range(len(sections)-1) for k in range(n)]
    append(name,verts,faces,mat)

def bow():
    assert not S.get('detail_ii_bow'), 'bow already refined'
    BASE.retire(['Detail | hangar','Shape | rounded bow housing','Shape | paired lower intake',
                 'Shape | lower intake throat','Shape | intake divider','Weapons | forward','Weapons | muzzle',
                 'Command | bridge visor','Command | bridge slit'])
    amber=material('13 | launch bay amber',(.8,.14,.025),.1,.5,2.4)
    outline=[(-1.24,-.32),(-.98,-.57),(.98,-.57),(1.24,-.32),(1.24,.32),(.98,.57),(-.98,.57),(-1.24,.32)]
    tunnel('octagonal launch cowl',outline,[(-20.0,1.13,1.12),(-20.49,1.13,1.12),(-20.56,1.07,1.07),(-20.56,1,1),(-20.47,.97,.97)],HULL)
    tunnel('launch cowl edge',outline,[(-20.565,1.035,1.035),(-20.565,1.005,1.005)],EDGE)
    tunnel('deep launch tunnel',outline,[(-20.47,.97,.97),(-20.30,.95,.95),(-20.10,.89,.89),(-19.99,.84,.84)],DARK)
    box('launch bay back',(0,-.62,-19.975),(2.35,1.13,.018),BLACK)
    for z in (-20.39,-20.22,-20.05):
        tunnel('launch tunnel structural ring',outline,[(z,.965,.965),(z+.045,.965,.965)],BLACK)
        for x in (-.99,.99):box('launch guide light',(x,-.84,z),(.025,.10,.025),amber)
    for x in (-.7,-.35,0,.35,.7):rod('launch floor guide',(x,-1.117,-20.44),(x*.87,-1.08,-20.03),.015,EDGE)
    # Upper avionics face and its real modeled conduit bends.
    box('upper avionics deck',(0,.72,-19.965),(3.10,1.42,.10),HULL)
    for x in (-1.20,-.80,-.40,0,.40,.80,1.20):
        box('avionics cartridge',(x,.60,-20.04),(.24,.27,.12),DARK)
        box('avionics face',(x,.60,-20.113),(.19,.21,.026),EDGE)
        for t in (-1,0,1):box('avionics vent',(x+t*.055,.61,-20.133),(.022,.12,.014),BLACK)
    for sx in (-1,1):
        points=[(sx*1.35,1.23,-20.04),(sx*.92,1.23,-20.04),(sx*.76,.98,-20.04),(sx*.34,.98,-20.04)]
        for a,b in zip(points,points[1:]):rod('avionics conduit',a,b,.042,DARK)
        ellipsoid('avionics dome',(sx*.44,1.28,-20.02),(.12,.12,.12),DARK,segments=24,rings=12)
        # Hollow forward beam barrels on the outside of the command cheeks.
        box('beam mounting block',(sx*2.30,.06,-19.26),(.32,.72,.80),DARK)
        box('beam lower bracket',(sx*2.37,-.36,-19.22),(.42,.12,.79),HULL)
        lathe('forward beam cannon',(sx*2.39,.12,-19.37),[(.26,.24),(.16,.28),(-.45,.28),(-.53,.23),(-.70,.23),(-.73,.19),(-.73,.135),(-.51,.135)],DARK,segments=48)
        lathe('beam muzzle rim',(sx*2.39,.12,-19.37),[(-.735,.19),(-.735,.14),(-.69,.14),(-.69,.19),(-.735,.19)],EDGE,segments=48)
        rod('beam bore darkness',(sx*2.39,.12,-19.85),(sx*2.39,.12,-19.87),.134,BLACK,sides=32)
    for x,r in ((-.89,.37),(0,.30),(.89,.37)):
        ellipsoid('rounded forward machinery',(x,-1.84,-20.04),(r,r*.89,r*.92),HULL,segments=40,rings=20)
        lathe('forward housing flange',(x,-1.84,-19.98),[(0,r*1.02),(-.06,r*1.02),(-.06,r*.93),(0,r*.93),(0,r*1.02)],DARK,segments=40)
    for x in (-.53,.53):
        ellipsoid('lower capsule housing',(x,-3.0,-19.94),(.40,.87,.31),HULL,segments=40,rings=20)
        # Recessed capsule face with a rolled perimeter; section outlines are centered on the bay helper.
        shape=[]
        for k in range(17):
            a=k*math.pi/16;shape.append((x+.255*math.cos(a),-3+.43+.255*math.sin(a)+.62))
        for k in range(17):
            a=math.pi+k*math.pi/16;shape.append((x+.255*math.cos(a),-3-.43+.255*math.sin(a)+.62))
        tunnel('capsule rim',shape,[(-20.265,1,1),(-20.295,1,1)],EDGE)
        verts=[(xx,yy-.62,-20.268) for xx,yy in shape]
        append('capsule recess',verts,[tuple(range(len(verts)))],BLACK)
        for y in (-3.62,-3.38,-3.14,-2.90,-2.66,-2.42):
            box('capsule internal vane',(x,y,-20.283),(.36,.034,.045),DARK)
    BASE.flush();S['detail_ii_bow']=True

def turret(position,normal):
    p=Vector(position);n=Vector(normal);u=n.cross(Vector((0,0,1))).normalized()
    def point(x,y,z):return tuple(p+u*x+n*y+Vector((0,0,z)))
    lathe('turret swivel',p,[(0,.31),(.10,.34),(.17,.31),(.22,.24),(.22,.18),(0,.18),(0,.31)],DARK,normal,32)
    # Faceted breech with a raised, chamfered cap and external trunnions.
    profile=[(-.32,-.29),(.32,-.29),(.39,-.15),(.39,.18),(.25,.31),(-.25,.31),(-.39,.18),(-.39,-.15)]
    verts=[point(x,y,z) for y in (.18,.55) for x,z in profile]
    faces=[tuple(reversed(range(8))),tuple(range(8,16))]+[(k,(k+1)%8,(k+1)%8+8,k+8) for k in range(8)]
    append('turret armored breech',verts,faces,HULL)
    for x in (-.17,.17):
        lathe('twin gun barrel',point(x,.39,-.26),[(0,.102),(-.20,.102),(-.25,.075),(-.63,.075),(-.68,.10),(-.73,.10),(-.73,.045),(-.59,.045)],DARK,segments=24)
        lathe('gun muzzle lip',point(x,.39,-.26),[(-.736,.10),(-.736,.045),(-.70,.045),(-.70,.10),(-.736,.10)],EDGE,segments=24)
        rod('gun bore',point(x,.39,-.84),point(x,.39,-.85),.044,BLACK,sides=24)
    for side in (-1,1):
        rod('turret trunnion',point(side*.33,.35,.07),point(side*.46,.35,.07),.12,DARK,sides=24)

def habitat_and_weapons():
    assert not S.get('detail_ii_habitat'), 'habitat already refined'
    BASE.retire(['Book | battery plinth','Book | secondary turret','Book | paired secondary gun'])
    for sign in (-1,1):
        for z in (-13.55,-.65,3.45):turret((0,sign*1.50,z),(0,sign,0))
        for z in (-13.55,-.65,3.45):turret((sign*1.46,0,z),(sign,0,0))
    # Endcap armor follows the actual double-lobed profile, with inset triangular sectors.
    for o in list(S.objects):
        if o.type!='MESH' or not o.name.startswith('Shape | lobed habitat endplate'):continue
        verts=[Vector(v.co) for v in o.data.vertices[:16]]
        centerz=sum(v.co.z for v in o.data.vertices)/len(o.data.vertices)
        direction=-1 if centerz<-6.88 else 1
        z=(min(v.co.z for v in o.data.vertices) if direction<0 else max(v.co.z for v in o.data.vertices))+direction*.04
        tris=tessellate_polygon([verts])
        for i,tri in enumerate(tris):
            tri=[verts[v] if isinstance(v,int) else v for v in tri]
            center=sum(tri,Vector())/3
            corners=[center+(v-center)*.90 for v in tri]
            points=[(v.x,v.y,z+depth) for depth in (0,direction*.035) for v in corners]
            append('endcap inset armor',points,[(2,1,0),(3,4,5),(0,1,4,3),(1,2,5,4),(2,0,3,5)],HULL if i%4 else DARK,1)
        bank=-1 if verts[0].y<0 else 1
        for x in (-.95,.95):
            for y in (2.66,4.53):
                box('endcap recessed service bay',(x,bank*y,z+direction*.084),(.46,.54,.045),DARK,1)
                for k in range(5):box('endcap louver',(x,bank*y+(k-2)*.09,z+direction*.115),(.37,.035,.06),EDGE,1)
                for xx in (-.18,.18):
                    for yy in (-.21,.21):rod('endcap fastener',(x+xx,bank*y+yy,z+direction*.10),(x+xx,bank*y+yy,z+direction*.15),.026,DARK,1,sides=12)
    # High-resolution bearing rims and discrete locking lugs.
    bearings=[]
    for o in S.objects:
        if o.type=='MESH' and o.name.startswith('Shape | bearing outer collar'):
            bearings.append(sum(v.co.z for v in o.data.vertices)/len(o.data.vertices))
    BASE.retire(['Shape | bearing outer collar','Shape | bearing thin bright lip'])
    for z in bearings:
        lathe('habitat machined bearing',(0,0,z),[(-.20,1.84),(-.17,1.99),(-.10,2.055),(-.07,2.055),(-.04,2.015),
              (.04,2.015),(.07,2.055),(.10,2.055),(.17,1.99),(.20,1.84),(.20,1.77),(-.20,1.77),(-.20,1.84)],HULL,segments=96)
        for k in range(32):
            a=k*math.tau/32
            rod('bearing fastener',(1.96*math.cos(a),1.96*math.sin(a),z-.175),(1.96*math.cos(a),1.96*math.sin(a),z-.208),.035,DARK,sides=12)
    # Paired dorsal/ventral conduits, valves and hatches visible in top/bottom references.
    for sign in (-1,1):
        for start,end in ((-15.25,-11.28),(-2.38,5.57)):
            box('axial recessed deck stripe',(.53,sign*1.46,(start+end)/2),(.13,.02,end-start),BLACK)
            for x in (-.25,.02):
                points=[(x,sign*1.50,start+.18),(x,sign*1.50,start+.78),(x-.18,sign*1.50,start+1.05),
                        (x-.18,sign*1.50,end-.75),(x-.32,sign*1.50,end-.5)]
                for a,b in zip(points,points[1:]):rod('dorsal bent conduit',a,b,.042,DARK)
            for z in (start+1.2,end-.7):
                ellipsoid('deck valve',(-.41,sign*1.53,z),(.15,.12,.15),DARK,segments=24,rings=12)
                rod('deck valve stem',(-.41,sign*1.57,z),(-.41,sign*1.73,z),.032,EDGE,sides=12)
    BASE.flush();S['detail_ii_habitat']=True

def engines():
    assert not S.get('detail_ii_engines'), 'engines already refined'
    BASE.retire(['Engine | flared bell','Engine | inner bell','Engine | blue exhaust','Reference | shroud narrow rib'])
    shell=bpy.data.materials['12 | blue-black engine shrouds']
    liner=material('14 | engine graphite liner',(.052,.066,.085),.72,.38)
    ion=material('15 | recessed ion core',(.10,.33,1.0),0,.4,5)
    for sx in (-1,1):
        for sy in (-1,1):
            c=(sx*1.804,sy*1.628,0)
            lathe('engine pressure shroud',c,[(14.30,1.0),(14.34,1.025),(14.42,1.025),(14.47,.988),
                  (15.40,.804),(15.63,.78),(15.72,.78),(15.72,.64),(15.63,.64),(15.40,.67),(14.47,.88),(14.30,.90),(14.30,1.0)],shell,segments=96)
            lathe('engine rolled nozzle rim',c,[(15.65,.783),(15.73,.783),(15.76,.755),(15.76,.66),(15.73,.633),(15.65,.633)],DARK,segments=96)
            lathe('engine deep throat',c,[(15.68,.635),(15.50,.62),(15.25,.58),(15.05,.51),(14.92,.43)],liner,segments=96)
            rod('engine recessed core',(c[0],c[1],14.90),(c[0],c[1],14.92),.429,ion,sides=96)
            for k in range(32):
                a=k*math.tau/32
                rod('engine throat rib',(c[0]+.52*math.cos(a),c[1]+.52*math.sin(a),15.08),
                    (c[0]+.628*math.cos(a),c[1]+.628*math.sin(a),15.58),.012,EDGE,sides=8)
            for k in range(12):
                a=k*math.tau/12
                rod('engine shroud seam',(c[0]+1.024*math.cos(a),c[1]+1.024*math.sin(a),14.44),
                    (c[0]+.787*math.cos(a),c[1]+.787*math.sin(a),15.63),.012,DARK,sides=8)
            # Saddle junctions and clamp bolts on the outer cradle hydraulics.
            for z in (10.8,11.45,12.10):
                box('cradle saddle',(sx*2.76,sy*2.44,z),(.40,.44,.10),HULL)
                for dx in (-.14,.14):rod('cradle clamp bolt',(sx*2.76+dx,sy*2.68,z),(sx*2.76+dx,sy*2.75,z),.028,DARK,sides=12)
    # Aft central service decks: conduits break up the previously blank reactor.
    for sy in (-1,1):
        for sx in (-1,1):
            for z in (6.4,7.6,8.8,10.0,11.2):
                box('reactor deck service frame',(sx*.75,sy*1.978,z),(.93,.075,.89),DARK)
                box('reactor deck service cover',(sx*.75,sy*2.028,z),(.80,.045,.77),HULL)
                for k in range(6):box('reactor heat louver',(sx*.75+(k-2.5)*.10,sy*2.065,z),(.045,.035,.45),DARK)
            points=[(sx*.20,sy*2.00,6.0),(sx*.20,sy*2.00,7.0),(sx*.34,sy*2.00,7.35),(sx*.34,sy*2.00,11.7)]
            for a,b in zip(points,points[1:]):rod('reactor deck pipe',a,b,.037,DARK)
    BASE.flush();S['detail_ii_engines']=True

def finish():
    assert not S.get('detail_ii_finished'), 'finish already applied'
    beveled=[];seen=set()
    selected=('Reference | stepped command cheek','Reference | command inset armor','Reference | Detail II turret armored breech',
              'Reference | Detail II upper avionics deck','Reference | Detail II avionics face','Reference | Detail II reactor deck service cover',
              'Reference | Detail II endcap inset armor','Reference | Detail II beam mounting block','Book | broad spine armor',
              'Book | continuous dorsal armor','Book | tapered shoulder armor','Shape | lobed habitat endplate')
    for o in S.objects:
        if o.type!='MESH' or o.data in seen:continue
        mesh=o.data;seen.add(mesh)
        if o.name.startswith(selected):
            bm=bmesh.new();bm.from_mesh(mesh)
            edges=[e for e in bm.edges if e.is_manifold and e.calc_face_angle()>.52]
            if edges:bmesh.ops.bevel(bm,geom=edges,offset=.018,segments=3,affect='EDGES',clamp_overlap=True)
            bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free();mesh.update();beveled.append(o.name)
        elif o.name.startswith('Reference | Detail II'):
            bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free();mesh.update()
        if o.name.startswith('Reference | Detail II') and any(term in o.name for term in ('shroud','throat','bearing','rounded forward','capsule housing','deck valve')):
            for p in mesh.polygons:p.use_smooth=True
    S['detail_ii_finished']=True
    return beveled

def inspect_image(path,camera=None):
    light_energy={o.name:o.data.energy for o in S.objects if o.type=='LIGHT'}
    old=(S.camera,S.view_settings.exposure,S.render.filepath,S.render.resolution_x,S.render.resolution_y)
    try:
        if camera:S.camera=camera
        for name,factor in (('Warm key',3),('Soft fill',10),('Cool rim',3)):S.objects[name].data.energy*=factor
        S.view_settings.exposure=1.0;S.render.resolution_x=1600;S.render.resolution_y=1000
        S.render.filepath=str(path);bpy.ops.render.render(write_still=True)
    finally:
        S.camera,S.view_settings.exposure,S.render.filepath,S.render.resolution_x,S.render.resolution_y=old
        for name,energy in light_energy.items():S.objects[name].data.energy=energy
