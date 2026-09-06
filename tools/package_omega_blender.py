"""Package two named Omega variants with shared hull geometry and packed textures."""
import bpy
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets/omega'
main=bpy.data.scenes['OMEGA | Reference rebuild']
source=bpy.data.collections['OMEGA | Agamemnon']

def export_variant(label):
    # Replace only this script's previously generated export scene/collection.
    previous=bpy.data.scenes.get('OMEGA | '+label+' export')
    if previous: bpy.data.scenes.remove(previous)
    previous=bpy.data.collections.get('OMEGA | '+label+' portable')
    if previous:
        for obj in list(previous.objects): bpy.data.objects.remove(obj,do_unlink=True)
        bpy.data.collections.remove(previous)
    stage=bpy.data.scenes.new('OMEGA | '+label+' export')
    coll=bpy.data.collections.new('OMEGA | '+label+' portable'); stage.collection.children.link(coll)
    bpy.context.window.scene=main
    deps=bpy.context.evaluated_depsgraph_get()
    mapping={}
    for obj in source.objects:
        if obj.name.startswith('Engine | plume'): continue
        clone=obj.copy()
        if obj.type=='FONT':
            # Convert a temporary text object after assigning the variant name.
            temp=obj.copy(); temp.data=obj.data.copy(); source.objects.link(temp)
            temp.data.body=label.upper(); bpy.context.view_layer.update()
            clone=bpy.data.objects.new('Identity | '+label,bpy.data.meshes.new_from_object(temp.evaluated_get(deps)))
            clone.matrix_basis=obj.matrix_basis.copy(); clone['omega_part']=0
            bpy.data.objects.remove(temp,do_unlink=True)
        clone.name=label+' | '+obj.name
        coll.objects.link(clone); mapping[obj]=clone
    for old,new in mapping.items():
        if old.parent: new.parent=mapping.get(old.parent)
    bpy.context.window.scene=stage
    stage.render.fps=60; stage.frame_end=1200; stage.frame_set(1)
    bpy.ops.export_scene.gltf(filepath=str(OUT/('omega_'+label.lower()+'.glb')),
        export_format='GLB',use_active_scene=True,export_animations=True,export_apply=True)
    return stage

export_variant('Agamemnon')
export_variant('Alexander')
bpy.context.window.scene=main
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'omega_destroyer.blend'))
print('Saved source and two textured GLB variants')
