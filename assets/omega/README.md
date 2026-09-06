# Omega destroyer assets

Authored through the live Blender MCP connection using the supplied `b5.mp4`
and `isn.mp4` references. The most useful hull views were ISN at 9 and 17 seconds.
These are newly constructed reference-inspired models, not extracted production meshes.

- `omega_destroyer.blend`: editable source, packed textures, inspection lighting,
  a separate habitat animation pivot, and Agamemnon/Alexander export scenes.
- `omega_agamemnon.glb` and `omega_alexander.glb`: portable textured variants,
  with different geometry nameplates and shared hull construction.
- `preview.png`: Blender inspection render.
- `hull_*.png`: generated weathered armor albedo textures, also packed in Blender.

The native demo uses Agamemnon. Its mesh is compiled from `demo/omega_model.h`;
`demo/omega_surface.h` contains a compact version of the same armor pattern.
The renderer retains its own lighting, roughness variation, engine/plasma effects
and animation. The GLBs contain albedo, metallic/roughness factors and animation;
Blender's procedural bump nodes are not baked normal maps.

## Regeneration

Run these scripts **inside Blender**, in order. The first creates a new scene and
refuses to overwrite an existing authored scene. Save hand edits before rebuilding.

1. `tools/build_omega_blender.py`
2. `tools/refine_omega_blender.py`
3. `tools/reshape_omega_blender.py`
4. `tools/book_omega_blender.py`
5. `tools/texture_omega_blender.py`
6. `tools/light_omega_blender.py`
7. `tools/export_omega_blender.py`
8. `tools/package_omega_blender.py`

For an edited source mesh, run only the exporter to update native geometry.
The texture script regenerates UVs/material maps and the packed native atlas;
the packaging script creates new export scenes and both GLBs.
Rebuild the native demo with `tools/build-msys2.ps1 -Target omega`.

Native model coordinates: bow -Z, dorsal +Y; habitat rotation is around Z.
Mesh custom property `omega_part=1` identifies rotating geometry; other hull
geometry uses zero. Material `omega_material` tags identify armor, recesses,
engine emission, running lights and native exhaust proxies. The native export
contains 50,810 triangles, including exhaust proxies and lettering. The detail
revision adds inset bow machinery, a faceted habitat end web, recessed service
bays and armored nacelle equipment. Opaque exhaust proxies are omitted by the
native fragment shader; white-blue discs, bloom and local spill provide the glow.

The lighting revision uses a higher directional key, very low ambient fill,
distance-limited gate light and four local engine lights. The native shadow map
follows the ship after emergence so the dense machinery retains self-shadowing.
`lighting-comparison.png` compares the earlier and revised native frame 750;
`demo-preview-rear.png` shows the exhaust glow during the flyby.

The silhouette revision follows the later Agrippa and stern references: tall
wedge cheeks around an open bow machinery face, double-lobed habitat end plates,
wider radiator banks, continuous spine armor, and an open stern cradle around
four cylindrical engines. The seam contrast is reduced to favor mottled metal.
`design-preview.png` shows the revised Blender model.

The latest book-elevation pass moves the habitat forward and shortens it,
sets the exposed aft spine to approximately twice the forward spine length,
and narrows the stern. Recessed equipment rows, red sockets, continuous armor
and relocated turrets follow the supplied elevation. `book-side.png` is the
orthographic inspection; `preview.png` and `preview-rear.png` show current
reference lighting. The earlier clay preview records the preceding revision.

The initial Blender scene was saved separately to
`build/omega-review/blender-before.blend` before creating the asset scene.
