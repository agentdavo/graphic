"""Reshape the authored Omega from the Agrippa and stern reference stills.

Run after refine_omega_blender.py and before texture/light/export scripts.
"""
import ast
import bpy
import math
from mathutils import Vector
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
scene=bpy.data.scenes['OMEGA | Reference rebuild']; bpy.context.window.scene=scene
ship=bpy.data.collections['OMEGA | Agamemnon']
if ship.get('silhouette_revision'): raise RuntimeError('Silhouette pass already applied')
def getmat(i): return next(m for m in bpy.data.materials if m.name.startswith(f'{i:02} |'))
armor,edge,dark,red,bronze,black,blue,lamp,nav,white,plume=[getmat(i) for i in range(1,12)]
tree=ast.parse((ROOT/'tools/build_omega_blender.py').read_text(encoding='utf-8'))
for node in tree.body:
    if isinstance(node,ast.FunctionDef) and node.name in ('mesh','box','tube','ring','strut'):
        exec(compile(ast.Module(body=[node],type_ignores=[]),'<omega primitives>','exec'))
part=0

def extrude_profile(name,profile,lo,hi,axis,mat):
    # Polygon lies in YZ for an X extrusion, XY for a Z extrusion.
    verts=[(d,a,b) if axis==0 else (a,b,d) for d in (lo,hi) for a,b in profile]
    n=len(profile)
    faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
    faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    obj=mesh(name,verts,faces,mat)
    # Recalculate outward winding for concave profiles and mirrored cheeks.
    import bmesh
    bm=bmesh.new(); bm.from_mesh(obj.data); bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces)); bm.to_mesh(obj.data); bm.free()
    return obj

def sphere(name,p,scale,mat):
    verts=[]; faces=[]; sides=12; rings=8
    for j in range(rings+1):
        theta=math.pi*j/rings
        for k in range(sides):
            a=math.tau*k/sides
            verts.append((p[0]+scale[0]*math.sin(theta)*math.cos(a),p[1]+scale[1]*math.cos(theta),p[2]+scale[2]*math.sin(theta)*math.sin(a)))
    for j in range(rings):
        for k in range(sides):
            a=j*sides+k; b=j*sides+(k+1)%sides
            if j>0: faces.append((a,b,a+sides))
            if j<rings-1: faces.append((b,b+sides,a+sides))
    return mesh(name,verts,faces,mat)

# Remove the old rectangular bow armor and obsolete overlaid side details.
remove=('Command | armored hammerhead','Command | dorsal crown','Command | lower keel',
        'Command | flank hangar','Command | flank ribs','Command | raised side armor',
        'Detail | cheek','Detail | lower sensor housing','Detail | lower longitudinal grille','Detail | grille slat',
        'Detail | habitat faceted end plate','Detail | habitat end ',
        'Detail | nacelle armor cap','Detail | nacelle flank shield','Detail | nacelle service',
        'Engine | armored longeron')
for obj in list(ship.objects):
    if obj.name.startswith(remove): bpy.data.objects.remove(obj,do_unlink=True)

# Tall wedge cheeks: broad forward edge, sloping lower edge and narrow aft neck.
profile=[(-4.7,-20.0),(3.5,-20.0),(3.0,-17.0),(1.7,-14.65),(-1.65,-14.65)]
for side in (-1,1):
    lo,hi=sorted((side*1.75,side*2.12))
    extrude_profile('Shape | wedge command cheek',profile,lo,hi,0,armor)
    x=side*2.145
    for i,(y,z) in enumerate(profile):
        yn,zn=profile[(i+1)%len(profile)]
        strut('Shape | bow perimeter rim',(x,y,z),(x,yn,zn),.045,edge)
    box('Shape | cheek midline relief',(x,-.35,-18.55),(.07,.12,2.65),edge,.02)
    box('Shape | cheek inset identification',(x,2.10,-17.75),(.06,.57,2.25),dark,.04)
    # Simple inset naval emblem, gold rim with a blue center, on the cheek.
    emblem=extrude_profile('Shape | fleet emblem',[(-.62,-18.9),(-.27,-19.18),(.08,-18.9),(-.27,-18.3)],side*2.17,side*2.19,0,bronze)
    extrude_profile('Shape | emblem inlay',[(-.53,-18.87),(-.27,-19.08),(-.03,-18.87),(-.27,-18.4)],side*2.20,side*2.22,0,dark)
    # Slim lower antenna follows the long tip of the wedge.
    tube('Shape | lower forward antenna',(side*1.0,-4.05,-19.7),(side*1.0,-4.05,-22.1),.055,.016,edge,8)
    box('Shape | antenna tip beacon',(side*1.0,-4.05,-22.1),(.06,.06,.06),nav,.01)

# Sloping roof joins the two cheek plates without filling the forward recess.
mesh('Shape | sloping command roof',[(-1.75,3.5,-20),(1.75,3.5,-20),(1.75,3.0,-17),(-1.75,3.0,-17)],[(0,1,2,3)],armor)
box('Shape | lower face machinery well',(0,-2.9,-19.78),(2.9,2.45,.15),black,.25)
for x in (-.85,0,.85):
    sphere('Shape | rounded bow housing',(x,-1.85,-19.97),(.36,.32,.32),armor)
for x in (-.53,.53):
    # Rounded vertically stretched ducts, with a dark mouth on the fore face.
    sphere('Shape | paired lower intake',(x,-3.0,-19.94),(.36,.82,.28),edge)
    box('Shape | lower intake throat',(x,-3.0,-20.205),(.36,1.18,.025),black,.16)
    for k in range(5): box('Shape | intake divider',(x,-3.47+k*.23,-20.225),(.33,.022,.018),dark,.008)
for obj in ship.objects:
    if obj.name.startswith('Identity | nameplate'):
        for v in obj.data.vertices: v.co.x+=math.copysign(.23,v.co.x)
    if obj.type=='FONT': obj.location.x=math.copysign(2.245,obj.location.x)

# Stronger continuous armor framing covers the skeletal gaps along the spine.
for center,length in ((-10.8,8.4),(6.3,5.6)):
    for sx in (-1,1):
        for sy in (-1,1):
            box('Shape | continuous axial armor rail',(sx*1.58,sy*1.46,center),(.35,.30,length),armor)
        box('Shape | axial middle rail',(sx*1.72,0,center),(.20,.16,length),edge,.04)
        for k in range(int(length/.9)):
            z=center-length*.5+.45+k*.9
            for sy in (-1,1):
                box('Shape | recessed spine frame',(sx*1.66,sy*.77,z),(.12,1.0,.10),dark)
                box('Shape | red conduit panel',(sx*1.54,sy*.52,z),(.13,.31,.38),red)
    for sy in (-1,1): box('Shape | axial dorsal armor',(0,sy*1.55,center),(2.5,.20,length),dark)

# Bulk out both habitat banks; preserve central bearings and radial attachment.
for obj in ship.objects:
    if obj.get('omega_part')!=1 or obj.type!='MESH': continue
    if any(abs(v.co.y)>2.4 for v in obj.data.vertices):
        for v in obj.data.vertices:
            v.co.x*=1.34; v.co.y*=1.12
part=1
before=set(ship.objects)
# Double-lobed, chamfered end profile, instead of a single flat rectangular cap.
outline=[(-1.45,2.60),(1.45,2.60),(2.28,3.22),(2.28,4.15),(1.50,4.62),
         (1.50,4.86),(2.28,5.24),(2.28,6.43),(1.52,7.15),(-1.52,7.15),
         (-2.28,6.43),(-2.28,5.24),(-1.50,4.86),(-1.50,4.62),(-2.28,4.15),(-2.28,3.22)]
for side in (-1,1):
    points=[(x,y*side) for x,y in outline]
    for z in (-7.48,3.92):
        extrude_profile('Shape | lobed habitat endplate',points,z-.18,z+.18,2,dark)
        face=z+(-.20 if z<0 else .20)
        for i,(x,y) in enumerate(points):
            xx,yy=points[(i+1)%len(points)]
            strut('Shape | endplate perimeter',(x,y,face),(xx,yy,face),.06,armor)
        for sy in (3.65,5.95):
            strut('Shape | endplate horizontal web',(-2.0,side*sy,face),(2.0,side*sy,face),.06,armor)
            for sx in (-1,1):
                strut('Shape | endplate diagonal web',(sx*1.72,side*(sy-.73),face),(-sx*1.72,side*(sy+.73),face),.065,armor)
        strut('Shape | endplate vertical web',(0,side*2.68,face),(0,side*7.02,face),.07,armor)
    # Broad armored crowns and a deep channel under each radiator bank.
    for y in (3.65,5.9):
        box('Shape | radiator armored crown',(0,side*(y+.69),-1.75),(4.35,.22,10.6),armor,.12)
        for x in (-2.24,2.24):
            box('Shape | radiator dark inset',(x,side*y,-1.75),(.08,.56,10.45),black)
            for k in range(24):
                box('Shape | dense radiator teeth',(x,side*y,-6.9+k*.44),(.17,.72,.056),dark,.015)
# Wider, more solid bearing rings and pressure-shell armor islands.
for z in (-7.18,3.68):
    ring('Shape | bearing outer collar',z,2.20,1.88,.34,armor,32)
    ring('Shape | bearing thin bright lip',z-.22,2.24,2.13,.08,edge,32)
for side in (-1,1):
    for z in (-5.8,-3.65,-1.5,.65,2.6):
        box('Shape | habitat shell plate',(side*1.76,0,z),(.20,1.55,1.28),armor,.15)
        for sy in (-1,1):
            box('Shape | shell recessed piping',(side*1.89,sy*.92,z),(.10,.35,1.42),black)
            for k in range(3):
                tube('Shape | shell bent pipe',(side*1.97,sy*(.80+k*.10),z-.5),(side*1.97,sy*(.80+k*.10),z+.5),.028,mat=bronze,sides=6)
pivot=bpy.data.objects['Habitat rotation | Z axis']
for obj in set(ship.objects)-before:
    if obj.get('omega_part')==1: obj.parent=pivot

# Open angular stern cradle around four cylindrical engines, not box nacelles.
part=0
for sx in (-1,1):
    for sy in (-1,1):
        x,y=sx*2.05,sy*1.85
        tube('Shape | exposed red engine collar',(x,y,10.6),(x,y,11.9),.91,mat=red,sides=20)
        for z in (10.65,11.02,11.39,11.76):
            tube('Shape | engine collar band',(x,y,z),(x,y,z+.08),.96,mat=dark,sides=20)
        box('Shape | nacelle longitudinal armor',(x,y+sy*.93,13.4),(.63,.16,2.8),armor)
        tube('Shape | reinforced engine sleeve',(x,y,11.95),(x,y,12.22),1.02,.91,armor,20)
    box('Shape | stern transverse upright',(sx*3.35,0,9.9),(.32,5.65,.35),armor,.10)
    box('Shape | stern separator flange',(sx*2.0,0,12.8),(2.9,.22,5.1),armor)
    for sy in (-1,1):
        strut('Shape | stern flared shoulder',(sx*1.55,sy*1.52,7.7),(sx*3.35,sy*2.42,10.0),.28,armor)
        box('Shape | stern corner longeron',(sx*3.35,sy*2.78,12.5),(.22,.22,5.8),armor)
for sy in (-1,1):
    box('Shape | stern transverse frame',(0,sy*2.94,10.0),(6.7,.35,.45),edge)
    box('Shape | stern central divider',(0,sy*1.65,13.0),(.22,1.6,4.8),dark)
ship['silhouette_revision']=1
scene['geometry_reference']='Wedge bow, double-lobed habitat banks, continuous armored spine and open four-cylinder engine cradle; Agrippa and rear-view user stills.'
print('Reshaped Omega:',len(ship.objects),'objects')
