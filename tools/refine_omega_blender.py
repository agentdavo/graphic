"""ISN close-up detail pass; run after build_omega_blender.py, before texturing.

Updates the existing authored collection. A marker prevents accidental repeat
application to edited artwork. Keep the earlier .blend when comparing revisions.
"""
import ast
import bpy
import math
from mathutils import Vector
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
scene=bpy.data.scenes['OMEGA | Reference rebuild']
bpy.context.window.scene=scene
ship=bpy.data.collections['OMEGA | Agamemnon']
if ship.get('isn_detail_revision'): raise RuntimeError('Detail pass already applied')
def getmat(prefix): return next(m for m in bpy.data.materials if m.name.startswith(prefix+' |'))
armor,edge,dark,red,bronze,black,blue,lamp,nav,white,plume=[getmat(f'{i:02}') for i in range(1,12)]
# Reuse the authored primitive helpers without running the scene generator.
tree=ast.parse((ROOT/'tools/build_omega_blender.py').read_text(encoding='utf-8'))
for node in tree.body:
    if isinstance(node,ast.FunctionDef) and node.name in ('mesh','box','tube','ring','strut'):
        exec(compile(ast.Module(body=[node],type_ignores=[]),str(ROOT/'tools/build_omega_blender.py'),'exec'))

for mat,color in [(armor,(.27,.285,.30)),(edge,(.44,.45,.43)),(dark,(.07,.08,.09)),
                  (red,(.23,.033,.020)),(bronze,(.19,.145,.085))]:
    mat.diffuse_color=(*color,1)
    mat.node_tree.nodes.get('Principled BSDF').inputs['Roughness'].default_value=.68
part=0
before=set(ship.objects)
# Expose a narrow service core instead of a large smooth drum.
for obj in list(ship.objects):
    if obj.name.startswith(('Habitat | central pressure drum','Habitat | red end cap')):
        bpy.data.objects.remove(obj,do_unlink=True)
    elif obj.name.startswith(('Engine | collar','Engine | armored longeron')):
        obj.data.materials[0]=dark if 'collar' in obj.name else armor

# Bow face: inset avionics tiers and an octagonal hangar opening, below the bridge.
box('Detail | bow recessed equipment face',(0,-.55,-19.82),(2.9,3.7,.13),black,.3)
for y in (-2.05,-1.75,.05,.8,2.20):
    box('Detail | bow tier separator',(0,y,-19.98),(3.2,.095,.15),edge,.03)
for sx in (-1,1):
    box('Detail | bow structural jamb',(sx*1.48,-.5,-19.97),(.14,3.7,.2),armor)
    for y in (-1.5,-1.05,-.6,1.95,2.4):
        box('Detail | avionics block',(sx*.78,y,-19.94),(.40,.24,.22),bronze,.04)
        box('Detail | avionics inset',(sx*.78,y,-20.07),(.25,.12,.015),black,.015)
    for y in (-2.45,2.45):
        tube('Detail | bow sensor',(sx*1.1,y,-19.82),(sx*1.1,y,-20.08),.16,.12,dark,12)
    for z in (-18.9,-16.2):
        tube('Detail | lateral antenna',(sx*1.8,-2.35,z),(sx*3.1,-2.35,z),.035,.015,edge,6)
        box('Detail | lateral beacon',(sx*3.1,-2.35,z),(.07,.07,.07),nav,.01)
    # Layered cheek armor with an inset field of vents.
    box('Detail | cheek machinery bed',(sx*1.995,.05,-18.7),(.09,1.25,.7),black)
    for k in range(6):
        box('Detail | cheek heat exchanger',(sx*2.06,-.43+k*.18,-18.7),(.08,.075,.59),edge,.02)

opening=ring('Detail | hangar octagonal rim',-20.04,.78,.65,.15,edge,8)
for v in opening.data.vertices: v.co.x*=1.37; v.co.y=v.co.y*.67-.62
box('Detail | hangar interior',(0,-.62,-19.94),(1.75,.72,.035),black,.2)
for sx in (-1,1):
    box('Detail | hangar threshold lamp',(sx*.77,-.89,-20.10),(.1,.04,.025),lamp,.01)
for x in (-.8,0,.8):
    tube('Detail | lower sensor housing',(x,-1.73,-19.9),(x,-1.73,-20.04),.22,.17,armor,12)
    box('Detail | lower longitudinal grille',(x,-2.55,-19.97),(.43,.68,.11),black)
    for k in range(5):
        box('Detail | grille slat',(x,-2.81+k*.13,-20.045),(.4,.025,.035),edge,.006)

# Dense service modules in the forward and aft non-rotating truss bays.
for z in (-14,-12.2,-10.4,5.0,6.8,8.6):
    for sx in (-1,1):
        for y in (-.75,0,.75):
            box('Detail | spine equipment',(sx*1.43,y,z),(.34,.34,.60),dark)
            tube('Detail | pipe union',(sx*1.61,y,z-.25),(sx*1.61,y,z+.33),.055,mat=bronze,sides=8)
            box('Detail | pipe mounting strap',(sx*1.62,y,z),(.14,.14,.07),edge,.02)
        for y in (-.99,.99):
            strut('Detail | spine cable run',(sx*1.49,y,z-.6),(sx*1.49,y,z+.7),.036,bronze)

part=1
tube('Detail | narrow habitat pressure core',(0,0,-7),(0,0,3.6),1.15,mat=dark,sides=16)
for z in (-6.6,-5.25,-3.9,-2.55,-1.2,.15,1.5,2.85):
    for sx in (-1,1):
        box('Detail | habitat core armor',(sx*1.20,.68,z),(.23,.60,.9),armor)
        box('Detail | habitat core lower armor',(sx*1.20,-.68,z),(.23,.60,.9),red)
        box('Detail | habitat exposed bay',(sx*1.4,0,z),(.36,.45,.8),black)
        for k in range(3):
            tube('Detail | habitat manifold',(sx*1.60,-.14+k*.14,z-.30),(sx*1.60,-.14+k*.14,z+.30),.035,mat=bronze,sides=6)
    box('Detail | habitat dorsal armor',(0,1.2,z),(1.15,.18,.83),armor)
    box('Detail | habitat ventral armor',(0,-1.2,z),(1.15,.18,.83),armor)
for side in (-1,1):
    for z in (-7.35,3.83):
        box('Detail | habitat faceted end plate',(0,side*4.5,z),(3.45,3.65,.18),bronze,.68)
        zz=z+(-.13 if z<0 else .13)
        for sx in (-1,1):
            # Crossed web reinforces both the outer plate and the central cut-in.
            strut('Detail | habitat end X',(-sx*1.24,side*3.1,zz),(sx*1.24,side*5.85,zz),.075,dark)
            strut('Detail | habitat end perimeter',(sx*1.55,side*3.25,zz),(sx*1.55,side*5.65,zz),.075,edge)
            strut('Detail | habitat angled corner',(sx*1.55,side*5.65,zz),(sx*.85,side*6.25,zz),.075,edge)
        strut('Detail | habitat end crossbar',(-1.50,side*4.5,zz),(1.50,side*4.5,zz),.065,edge)
    # Deep black exchanger slots, with repeated fins and small pipe bends.
    for y in (3.25,5.25):
        for sx in (-1,1):
            for k in range(22):
                z=-6.85+k*.475
                box('Detail | radiator micro fin',(sx*1.73,side*y,z),(.075,.78,.047),dark,.01)
        for k in range(18):
            z=-6.7+k*.56
            box('Detail | habitat spine fastener',(0,side*(y+.77),z),(.34,.06,.13),dark,.02)

part=0
# Four armored nacelles with lateral hardware and aft bridgework.
for sx in (-1,1):
    for sy in (-1,1):
        x,y=sx*2.05,sy*1.85
        box('Detail | nacelle armor cap',(x,y+sy*.86,12.9),(1.5,.22,3.65),armor,.1)
        box('Detail | nacelle flank shield',(x+sx*.86,y,12.9),(.20,1.5,3.65),dark)
        for k in range(5):
            z=11.3+k*.68
            box('Detail | nacelle service unit',(x+sx*.99,y,z),(.30,.58,.32),armor,.07)
            box('Detail | nacelle service recess',(x+sx*1.16,y,z),(.04,.32,.16),black,.02)
        for offset in (-.49,.49):
            tube('Detail | nacelle hydraulic line',(x+offset,y+sy*1.04,11.2),(x+offset,y+sy*1.04,14.5),.045,mat=bronze,sides=8)
        for k in range(8):
            a=k*math.tau/8
            strut('Detail | bell reinforcement',(x+math.cos(a)*.82,y+math.sin(a)*.82,14.3),
                  (x+math.cos(a)*1.12,y+math.sin(a)*1.12,15.62),.045,armor)
for sy in (-1,1):
    for z in (11.3,12.8,14.3):
        box('Detail | aft equipment bridge',(0,sy*1.85,z),(2.25,.7,.35),dark)
        for x in (-.75,0,.75):
            box('Detail | aft bridge equipment',(x,sy*2.32,z),(.43,.42,.44),armor)
    strut('Detail | aft triangular truss',(-1.65,sy*2.25,14.2),(0,sy*2.6,15.3),.08,edge)
    strut('Detail | aft triangular truss',(1.65,sy*2.25,14.2),(0,sy*2.6,15.3),.08,edge)

pivot=bpy.data.objects['Habitat rotation | Z axis']
for obj in set(ship.objects)-before:
    if obj.get('omega_part')==1: obj.parent=pivot
ship['isn_detail_revision']=1
scene['lighting_reference']='Hard restrained key; black shadow side; local blue-white engine spill. User ISN stills.'
print('Detail pass complete:',len(ship.objects),'objects')
