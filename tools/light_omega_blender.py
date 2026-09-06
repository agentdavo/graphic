"""Reference lighting: hard key, very low fill, local blue-white exhaust spill."""
import bpy
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
scene=bpy.data.scenes['OMEGA | Reference rebuild']
bpy.context.window.scene=scene
for name,power,size in [('Warm key',5800,6),('Cool rim',700,8),('Soft fill',180,12)]:
    light=bpy.data.lights[name]; light.energy=power; light.size=size
from mathutils import Vector
key=bpy.data.objects['Warm key']; key.location=(12,22,-32)
key.rotation_euler=(Vector((0,0,-1))-key.location).to_track_quat('-Z','Y').to_euler()
scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.001,.002,.004,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=.10
scene.view_settings.view_transform='AgX'
scene.view_settings.exposure=-.35
for sx in (-1,1):
    for sy in (-1,1):
        name=f'Exhaust spill {sx} {sy}'
        obj=bpy.data.objects.get(name)
        if not obj:
            data=bpy.data.lights.new(name,'POINT'); obj=bpy.data.objects.new(name,data)
            scene.collection.objects.link(obj)
        obj.location=(sx*scene.get('engine_center_x',2.05),sy*scene.get('engine_center_y',1.85),16.2)
        obj.data.energy=380; obj.data.color=(.22,.40,1); obj.data.shadow_soft_size=.65
for m in bpy.data.materials:
    if m.name.startswith('07 |'):
        bs=m.node_tree.nodes.get('Principled BSDF')
        bs.inputs['Emission Color'].default_value=(.55,.72,1,1)
        bs.inputs['Emission Strength'].default_value=18
scene.cycles.samples=32
# Blender 5.2 compositor: optical glow around emissive exhaust discs.
nt=bpy.data.node_groups.get('OMEGA optical bloom') or bpy.data.node_groups.new('OMEGA optical bloom','CompositorNodeTree')
nt.nodes.clear()
if not list(nt.interface.items_tree): nt.interface.new_socket(name='Image',in_out='OUTPUT',socket_type='NodeSocketColor')
layers=nt.nodes.new('CompositorNodeRLayers'); layers.scene=scene
glare=nt.nodes.new('CompositorNodeGlare')
glare.inputs['Type'].default_value='Fog Glow'
glare.inputs['Quality'].default_value='High'
glare.inputs['Threshold'].default_value=1.3
glare.inputs['Size'].default_value=.30
glare.inputs['Strength'].default_value=.55
output=nt.nodes.new('NodeGroupOutput')
nt.links.new(layers.outputs['Image'],glare.inputs['Image'])
nt.links.new(glare.outputs['Image'],output.inputs['Image'])
scene.compositing_node_group=nt
scene.render.filepath=str(ROOT/'assets/omega/preview.png')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'assets/omega/omega_destroyer.blend'))
print('Set reference lighting and local exhaust spill')
