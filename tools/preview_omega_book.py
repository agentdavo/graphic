"""Render the book revision from both ends, restoring the editable scene."""
import bpy
from mathutils import Vector, Matrix
from pathlib import Path
out=Path(__file__).resolve().parents[1]/'assets/omega'
scene=bpy.data.scenes['OMEGA | Reference rebuild']
bpy.context.window.scene=scene
cam=scene.camera
original=cam.matrix_world.copy()
filepath=scene.render.filepath
def previews():
    try:
        scene.render.filepath=str(out/'preview.png')
        bpy.ops.render.render(write_still=True)
        cam.location=(27,14,35)
        direction=(Vector((0,0,-1))-cam.location).normalized()
        right=direction.cross(Vector((0,1,0))).normalized()
        up=right.cross(direction)
        cam.rotation_euler=Matrix((right,up,-direction)).transposed().to_euler()
        scene.render.filepath=str(out/'preview-rear.png')
        bpy.ops.render.render(write_still=True)
    finally:
        cam.matrix_world=original
        scene.render.filepath=filepath
        bpy.ops.wm.save_as_mainfile(filepath=str(out/'omega_destroyer.blend'))
    return None
bpy.app.timers.register(previews,first_interval=.5)
print('Queued front and rear reference-lit renders')
