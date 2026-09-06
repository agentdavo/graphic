"""Create packed, UV-mapped weathered panel textures and portable glTF assets."""
import bpy
import numpy as np
from pathlib import Path
from mathutils import Vector, Matrix

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets/omega'
scene=bpy.data.scenes['OMEGA | Reference rebuild']
bpy.context.window.scene=scene
collection=bpy.data.collections['OMEGA | Agamemnon']
rng=np.random.default_rng(21)
n=512
y,x=np.mgrid[:n,:n]
# Staggered armor plates, narrow seams, edge scuffing and fine corrosion.
gx=(x+(y//128)*47)%128; gy=y%128
distance=np.minimum.reduce([gx,127-gx,gy,127-gy])
plates=rng.uniform(.84,1.0,(4,4))[(y//128)%4,((x+(y//128)*47)//128)%4]
weather=.80+.10*np.sin(x*.13+np.sin(y*.045)*2)*np.sin(y*.10)+rng.normal(0,.055,(n,n))
value=plates*weather
value*=np.where(distance<1,.86,np.where(distance<2,.96,1))
value+=np.where((distance>=2)&(distance<3),.025,0)
scratch=(rng.random((n,n))>.997)&(distance>5)
for k in range(5): value+=np.roll(scratch,k,axis=0)*.035
rivet=((gx-9)**2+(gy-9)**2<4)|((gx-118)**2+(gy-118)**2<4)
value=np.clip(value+rivet*.04,.12,1)
small=np.rint(value.reshape(256,2,256,2).mean(axis=(1,3))*255).astype(np.uint8)
header='/* Generated from the same panel texture as the Blender UV materials. */\nstatic const uint8_t omega_surface[256*256]={\n'
header+=''.join(','.join(map(str,row))+',\n' for row in small)
(ROOT/'demo/omega_surface.h').write_text(header+'};\n',encoding='utf-8')
used=set()
for obj in collection.objects:
    if obj.type!='MESH': continue
    data=obj.data
    uv=data.uv_layers.get('Armor projection') or data.uv_layers.new(name='Armor projection')
    for poly in data.polygons:
        axis=max(range(3),key=lambda k:abs(poly.normal[k]))
        axes=(1,2) if axis==0 else ((0,2) if axis==1 else (0,1))
        for loop in poly.loop_indices:
            p=data.vertices[data.loops[loop].vertex_index].co
            uv.data[loop].uv=(p[axes[0]]*.25,p[axes[1]]*.25)
    used.update(data.materials)
for mat in used:
    if mat.get('omega_material',0) in (3,4,7): continue
    color=np.array(mat.diffuse_color[:3])
    pixels=np.ones((n,n,4),dtype=np.float32)
    # Image stores sRGB so convert the linear material color for consistency.
    pixels[:,:,:3]=np.clip(value[:,:,None]*color[None,None,:],0,1)**(1/2.2)
    img=bpy.data.images.new(mat.name+' | panel albedo',width=n,height=n)
    img.pixels.foreach_set(pixels.ravel()); img.update()
    img.filepath_raw=str(OUT/('hull_'+mat.name[:2]+'.png')); img.file_format='PNG'; img.save(); img.pack()
    nodes=mat.node_tree.nodes; links=mat.node_tree.links
    tex=nodes.new('ShaderNodeTexImage'); tex.image=img; tex.label='UV weathered armor / exportable'
    bs=nodes.get('Principled BSDF'); links.new(tex.outputs['Color'],bs.inputs['Base Color'])
    bump=nodes.new('ShaderNodeBump'); bump.inputs['Strength'].default_value=.18; bump.inputs['Distance'].default_value=.018
    links.new(tex.outputs['Color'],bump.inputs['Height']); links.new(bump.outputs[0],bs.inputs['Normal'])

cam=scene.camera; cam.location=(34,18,-28)
for obj in collection.objects:
    if obj.type=='FONT':
        side=1 if obj.location.x>0 else -1
        obj.rotation_euler=Matrix(((0,0,side),(0,1,0),(-side,0,0))).to_euler()
direction=(Vector((0,0,-1))-cam.location).normalized()
right=direction.cross(Vector((0,1,0))).normalized(); up=right.cross(direction)
cam.rotation_euler=Matrix((right,up,-direction)).transposed().to_euler()
cam.data.ortho_scale=46
for name,power in [('Warm key',8500),('Cool rim',11000),('Soft fill',2400)]: bpy.data.lights[name].energy=power
# Hide analytic native plume proxies in Blender beauty renders.
for obj in collection.objects:
    if obj.name.startswith('Engine | plume'): obj.hide_render=True
scene.render.filepath=str(ROOT/'build/omega-review/blender-textured.png')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'omega_destroyer.blend'))
print('Packed UV armor maps; prepared textured review camera')
