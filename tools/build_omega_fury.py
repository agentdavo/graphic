"""Build the SA-23E Aurora Starfury in Blender and export it for the Omega demo.

Run inside Blender, against the same omega/omega_destroyer.blend the capital
ship is authored in:

    exec(open('tools/build_omega_fury.py').read())

It is idempotent: the collection is emptied and rebuilt, so editing a number
here and running it again is the whole workflow. The destroyer's own scene is
never touched.

Shape follows the published orthographic sheets -- Lawrence R. Miller's
"Starfury Fighter Detail" sheets 8 and 9 -- corrected against studio model
photographs and PTEN/Warner CGI stills. What the drawings alone got wrong, and
the photographs settle:

  * The wings are broad manta plates blended into a spine, not four struts.
    From above the ship is one arrowhead with the cockpit at its apex.
  * The nacelles are fat rounded pods, near enough two wing-thicknesses
    across, tapering aft into a stepped exhaust barrel with a deep bore. The
    forward intake is a smaller ribbed ring.
  * A long flat claw arm reaches forward and outboard from each nacelle,
    carrying a row of five spherical bulges. It is the ship's signature.
  * The cockpit is a tall hexagonal cage slung *under* the wing apex, not
    perched on a nose. The frame stands proud of recessed glass, a lit panel
    shows inside it, and four ribbed vernier cans surround it.
  * The forward thrusters are amber-rimmed. On the studio models that ring is
    the brightest thing on the ship, and it is what makes a head-on Starfury
    identifiable at any distance.
  * The upper wings carry the squadron's identification paint.

Scale is the number the demo actually cares about. The authored Omega hull is
43.800 model units long and about 1750 m, so a model unit is 39.95 m. This file
works in metres and packs millimetres, and OMEGA_FURY_UNIT converts. A Starfury
is 9.9 m long and 17.87 m across: a quarter of a model unit. The procedural
escorts it replaces were 0.8 by 1.8, three times too long and four times too
wide, which quietly made the destroyer read as a frigate.

Height is the one dimension the sources disagree on. The commonly quoted 4.4 m
cannot be the tip-to-tip height of an X-shaped fighter; measured off the front
elevation the nacelle box is almost exactly half as tall as it is wide, so 4.4
is taken as the pod and RISE is measured from the drawings.
"""
import bpy
import math
from pathlib import Path
from mathutils import Vector

ROOT = Path(bpy.data.filepath).resolve().parents[1]
COLLECTION = 'OMEGA | Starfury'
LENGTH, SPAN, RISE = 9.9, 17.87, 8.95     # metres, from the sheets
TIPX, TIPY = 7.55, 3.10                   # nacelle centre; claw and fin reach the extremes
MODEL_UNIT_METRES = 1750.0 / 43.8         # the destroyer, measured and canonical

# Shader material codes, from omega_hull.frag: 0 plated hull, 1 the same with
# lighter paint, 2 the ribbed dark treatment, 3 blue emissive, 4 a beacon that
# reads red when the tint is red and warm white otherwise.
HULL, EDGE, DARK, GLOW, BEACON = 0, 1, 2, 3, 4
PALETTE = {
    'fury hull':      ((.58, .60, .62), HULL),
    'fury edge':      ((.70, .71, .70), EDGE),
    'fury amber':     ((.86, .62, .12), EDGE),
    'fury paint':     ((.78, .44, .16), EDGE),
    'fury recess':    ((.13, .14, .16), DARK),
    'fury canopy':    ((.07, .11, .18), DARK),
    'fury thruster':  ((.55, .80, 1.0), GLOW),
    'fury beacon':    ((1.0, .10, .06), BEACON),
    'fury lamp':      ((1.0, .90, .70), BEACON),
}
PARTS = []                                # (name, material, verts, faces)


def emit(name, material, verts, faces):
    PARTS.append((name, material, [tuple(v) for v in verts], faces))


def frame(axis, across):
    """Right-handed axis/across/up triple, with across squared up to axis."""
    axis = Vector(axis).normalized()
    across = (Vector(across) - axis * Vector(across).dot(axis)).normalized()
    return axis, across, axis.cross(across)


def ring(hw, hh, chamfer, sides):
    """Chamfered rectangle, the silhouette the sheets show on every member."""
    if sides == 8:
        c = chamfer
        return [(-hw + c, -hh), (hw - c, -hh), (hw, -hh + c), (hw, hh - c),
                (hw - c, hh), (-hw + c, hh), (-hw, hh - c), (-hw, -hh + c)]
    return [(hw * math.cos(k * math.tau / sides), hh * math.sin(k * math.tau / sides))
            for k in range(sides)]


def loft(name, material, stations, axis=(0, 0, 1), across=(1, 0, 0), origin=(0, 0, 0), sides=8):
    """Sweep chamfered sections along an axis and cap both ends.

    stations is (distance along axis, half across, half up, chamfer, offset up).
    """
    axis, across, up = frame(axis, across)
    origin = Vector(origin)
    verts, faces = [], []
    for t, hw, hh, chamfer, lift in stations:
        base = origin + axis * t + up * lift
        verts += [tuple(base + across * x + up * y) for x, y in ring(hw, hh, chamfer, sides)]
    for j in range(len(stations) - 1):
        for k in range(sides):
            a, b = j * sides + k, j * sides + (k + 1) % sides
            faces.append((a, b, b + sides, a + sides))
    faces.append(tuple(reversed(range(sides))))
    faces.append(tuple(range(len(verts) - sides, len(verts))))
    emit(name, material, verts, faces)


def box(name, material, centre, size, axis=(0, 0, 1), across=(1, 0, 0), sides=8):
    hw, hh, hl = (v * .5 for v in size)
    loft(name, material, [(-hl, hw, hh, min(hw, hh) * .3, 0), (hl, hw, hh, min(hw, hh) * .3, 0)],
         axis=axis, across=across, origin=centre, sides=sides)


def lathe(name, material, profile, origin, axis=(0, 0, 1), sides=16):
    """Revolve a (distance, radius) profile. Radius 0 closes the end on itself."""
    axis, across, up = frame(axis, (1, 0, 0) if abs(Vector(axis)[0]) < .9 else (0, 1, 0))
    origin = Vector(origin)
    verts, faces = [], []
    for t, radius in profile:
        centre = origin + axis * t
        verts += [tuple(centre + radius * (across * math.cos(k * math.tau / sides)
                                           + up * math.sin(k * math.tau / sides)))
                  for k in range(sides)]
    for j in range(len(profile) - 1):
        for k in range(sides):
            a, b = j * sides + k, j * sides + (k + 1) % sides
            faces.append((a, b, b + sides, a + sides))
    faces.append(tuple(reversed(range(sides))))
    faces.append(tuple(range(len(verts) - sides, len(verts))))
    emit(name, material, verts, faces)


def blade(name, material, root, tip, chord, root_len, tip_len, root_thick, tip_thick):
    """Flat tapered plate: the claw fins, the wing strakes, the paint panels."""
    axis, across, up = frame(Vector(tip) - Vector(root), chord)
    verts = []
    for end, centre in ((0, Vector(root)), (1, Vector(tip))):
        half, thick = (tip_len if end else root_len) * .5, (tip_thick if end else root_thick) * .5
        verts += [tuple(centre + across * (sc * half) + up * (st * thick))
                  for sc, st in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    faces = [(k, (k + 1) % 4, (k + 1) % 4 + 4, k + 4) for k in range(4)]
    faces += [(3, 2, 1, 0), (4, 5, 6, 7)]
    emit(name, material, verts, faces)


def thruster(name, at, radius, depth, ribs=8):
    """Stepped exhaust barrel with a deep dark bore and a lit core.

    The stills show the aft end as a machined cylinder standing proud of the
    pod, not a cone faired into it, so the step is modelled rather than shaded.
    """
    lathe(name + ' barrel', 'fury hull', [
        (0, radius * 1.10), (depth * .18, radius * 1.02), (depth * .24, radius),
        (depth * .82, radius), (depth * .88, radius * 1.06), (depth, radius * 1.04)],
        at, sides=16)
    lathe(name + ' bore', 'fury recess', [
        (depth * .98, radius * .86), (depth * .55, radius * .74),
        (depth * .12, radius * .66)], at, sides=16)
    lathe(name + ' core', 'fury thruster', [
        (depth * .58, radius * .68), (depth * .86, radius * .78),
        (depth * .99, radius * .80)], at, sides=16)
    # The flare is the fighter, at any distance this demo actually frames one.
    # In the footage a Starfury is four blue points with an airframe attached,
    # and a core sunk down the bore cannot do that: the plume has to stand off
    # the barrel where nothing occludes it and the bloom pass can find it. It
    # is exhaust, not hull, so it is allowed past the quoted length.
    lathe(name + ' plume', 'fury thruster', [
        (depth * 1.00, radius * .88), (depth * 1.30, radius * 1.25),
        (depth * 1.85, radius * .95), (depth * 2.60, radius * .32)],
        at, sides=16)
    for k in range(ribs):
        angle = (k + .5) * math.tau / ribs
        arm = Vector((math.cos(angle), math.sin(angle), 0))
        blade(f'{name} rib {k}', 'fury edge', at + arm * (radius * .98) + Vector((0, 0, depth * .30)),
              at + arm * (radius * 1.04) + Vector((0, 0, depth * .76)), (0, 0, 1), .13, .13,
              radius * .30, radius * .22)


def brake(name, at, radius, depth, rim='fury amber', sides=16, ribs=10):
    """A ribbed forward thruster: amber rim, dark throat, and a lit core.

    Every photograph of the ship puts a bright ring here, so the rim is its own
    painted material rather than hull shaded a little differently.
    """
    lathe(name + ' rim', rim, [
        (0, radius * .84), (-depth * .55, radius), (-depth, radius * .92)], at, sides=sides)
    lathe(name + ' throat', 'fury recess', [
        (-depth * .96, radius * .82), (-depth * .40, radius * .58),
        (depth * .20, radius * .50)], at, sides=sides)
    lathe(name + ' core', 'fury thruster', [
        (-depth * .22, radius * .50), (-depth * .62, radius * .60),
        (-depth * .80, radius * .62)], at, sides=sides)
    for k in range(ribs):
        angle = (k + .5) * math.tau / ribs
        arm = Vector((math.cos(angle), math.sin(angle), 0))
        blade(f'{name} vane {k}', 'fury recess', at + arm * (radius * .62) - Vector((0, 0, depth * .30)),
              at + arm * (radius * .86) - Vector((0, 0, depth * .78)),
              (0, 0, 1), radius * .30, radius * .22, radius * .16, radius * .12)


def cockpit():
    """A tall hexagonal cage slung under the wing apex, ringed by verniers.

    The photographs do not show a canopy faired into a nose. They show a bay
    hung below where the four wings meet: a heavy frame standing proud of
    recessed glass, the pilot's lit panel visible inside it, and four ribbed
    vernier cans clustered round. The pilot stands, so the bay is half again
    as tall as it is wide.
    """
    face, back, drop = -4.30, -3.10, -.38
    corners = [Vector((.74 * math.cos(a), 1.08 * math.sin(a) + drop, 0))
               for a in (math.tau * k / 6 + math.tau / 12 for k in range(6))]
    for k, corner in enumerate(corners):
        nxt = corners[(k + 1) % 6]
        blade(f'canopy frame {k}', 'fury edge',
              Vector((corner.x, corner.y, face)), Vector((nxt.x, nxt.y, face)),
              (0, 0, 1), .50, .50, .17, .17)
        blade(f'canopy stay {k}', 'fury edge',
              Vector((corner.x, corner.y, face)), Vector((corner.x * .70, corner.y * .70 + drop * .30, back)),
              (0, 0, 1), .17, .15, .24, .30)
    loft('glazing', 'fury canopy', [
        (face + .06, .62, .96, .24, drop), (back - .55, .66, 1.00, .25, drop),
        (back, .52, .78, .20, drop * .8)], sides=6)
    # One vertical mullion down the glass, as on the filming model.
    blade('canopy mullion', 'fury edge', Vector((0, corners[1].y, face)),
          Vector((0, corners[4].y, face)), (0, 0, 1), .44, .44, .14, .14)
    # The pilot's panel, seen through the glass: warm, and the only lamp on the
    # ship that is not a navigation light.
    box('panel', 'fury lamp', (0, drop - .10, back - .30), (.34, .42, .08), sides=6)
    # Four ribbed vernier cans around the bay, and the chin pylon that hangs it
    # off the wing junction.
    for side in (-1, 1):
        brake(f'cockpit vernier low {side:+d}', Vector((side * .48, drop - .86, back - .32)),
              .21, .30, rim='fury edge', sides=8, ribs=6)
        brake(f'cockpit vernier high {side:+d}', Vector((side * .60, drop + .82, back - .10)),
              .19, .27, rim='fury edge', sides=8, ribs=6)
    box('chin pylon', 'fury hull', (0, drop * .35, back + .30), (.46, .96, 1.10))
    # The gun pods flank the bay, so the four linked barrels converge on the
    # pilot's own line of sight.
    for side in (-1, 1):
        lathe(f'cannon {side:+d}', 'fury edge', [
            (back + .60, .21), (back - .50, .19), (back - 1.40, .155), (back - 1.48, .17)],
            (side * .82, drop - .30, 0), sides=8)
        lathe(f'muzzle {side:+d}', 'fury recess', [
            (back - 1.46, .155), (back - 1.72, .13)], (side * .82, drop - .30, 0), sides=8)


def fuselage():
    """Spine: the shoulders where four wings meet, tapering to the tail."""
    loft('spine', 'fury hull', [
        (-3.30, .50, .44, .16, -.06), (-2.60, .78, .68, .26, -.02),
        (-1.60, 1.10, .98, .38, 0),
        (0.40, 1.02, .90, .35, 0), (2.30, .76, .68, .26, 0),
        (3.70, .42, .38, .14, 0), (4.20, .24, .22, .08, 0)])
    box('chin sensor', 'fury recess', (0, -.68, -1.30), (.70, .28, 1.30))
    box('dorsal spine', 'fury edge', (0, 1.02, 1.20), (.48, .32, 3.40))
    for side in (-1, 1):
        box(f'aft vernier {side:+d}', 'fury recess', (side * .84, .10, 3.10), (.32, .50, .72), sides=6)
    cockpit()


def wing(sx, sy):
    """One arm of the manta: a broad swept plate, its rail, and its paint."""
    tag = f'{sx:+d}{sy:+d}'
    root = Vector((sx * .90, sy * .70, -1.90))
    tip = Vector((sx * TIPX, sy * TIPY, 1.05))
    axis = (tip - root).normalized()
    span = (tip - root).length
    # Chord half-extents run along Z, so the arm's own aft sweep carries the
    # chord back with it: leading edge from beside the neck out to the nacelle.
    # The first two stations are the root fairing, thick where it meets the
    # spine and thinning quickly, which stops the join reading as a peg.
    loft('wing ' + tag, 'fury hull', [
        (0, 1.86, .52, .34, 0), (span * .14, 1.76, .40, .28, 0),
        (span * .36, 1.58, .32, .22, 0), (span * .62, 1.34, .28, .19, 0),
        (span * .84, 1.18, .25, .17, 0), (span, 1.10, .23, .16, 0)],
        axis=axis, across=(0, 0, 1), origin=root)
    blade('strake ' + tag, 'fury edge', root + axis * (span * .10) + Vector((0, 0, -1.72)),
          root + axis * (span * .94) + Vector((0, 0, -1.02)), (0, 0, 1), .60, .44, .26, .18)
    for k in range(6):
        t = span * (.16 + .13 * k)
        seat = root + axis * t
        box(f'rail {tag} {k}', 'fury edge', seat + Vector((0, 0, -1)) * (1.62 - .40 * t / span),
            (.34, .30, .28), axis=axis, across=(0, 0, 1), sides=6)
    for k in range(3):
        t = span * (.30 + .20 * k)
        box(f'panel {tag} {k}', 'fury recess',
            root + axis * t + Vector((0, 0, .94 - .34 * t / span)) + Vector((0, sy * .22, 0)),
            (.50, .16, .74), axis=axis, across=(0, 0, 1), sides=6)
    if sy > 0:                                    # squadron paint, upper wings only
        blade('paint ' + tag, 'fury paint', root + axis * (span * .20) + Vector((0, .30, -.60)),
              root + axis * (span * .84) + Vector((0, .24, -.24)), (0, 0, 1), .90, .62, .10, .09)


def nacelle(sx, sy):
    """A fat pod, a stepped exhaust aft, an intake forward, and the claw arm."""
    at = Vector((sx * TIPX, sy * TIPY, 0))
    tag = f'{sx:+d}{sy:+d}'
    loft('nacelle ' + tag, 'fury hull', [
        (-3.05, .48, .48, 0, 0), (-2.60, .86, .86, 0, 0), (-1.80, 1.00, 1.00, 0, 0),
        (0.00, 1.04, 1.04, 0, 0), (1.60, 1.02, 1.02, 0, 0), (2.55, .96, .96, 0, 0),
        (2.95, .90, .90, 0, 0)], origin=at, sides=16)
    brake('brake ' + tag, at + Vector((0, 0, -3.05)), .54, .58)
    # The main bell, with three smaller nozzles clustered round it. Close shots
    # of the type show a group at each arm's end, not one throat.
    thruster('exhaust ' + tag, at + Vector((0, 0, 2.95)), .62, 1.25)
    for k in range(3):
        angle = math.tau * k / 3 + math.tau / 12
        seat = at + Vector((.74 * math.cos(angle), .74 * math.sin(angle), 2.88))
        lathe(f'nozzle {tag} {k}', 'fury recess', [
            (0, .30), (.34, .27), (.62, .23)], seat, sides=10)
        lathe(f'nozzle core {tag} {k}', 'fury thruster', [
            (.40, .21), (.58, .18)], seat, sides=10)
        lathe(f'nozzle plume {tag} {k}', 'fury thruster', [
            (.64, .24), (.95, .33), (1.45, .12)], seat, sides=10)
    # Manoeuvring jets, four to a nacelle, facing out along the nacelle's own
    # axes. These are the reason the type exists: an Aurora yaws by firing
    # them rather than by banking, and in the footage only the ones on the
    # outside of a turn are lit.
    for k in range(4):
        angle = math.tau * k / 4
        out = Vector((math.cos(angle), math.sin(angle), 0))
        seat = at + out * .92 + Vector((0, 0, .10))
        blade(f'jet {tag} {k}', 'fury recess', seat, seat + out * .46,
              (0, 0, 1), .40, .34, .34, .30)
        lathe(f'jet core {tag} {k}', 'fury thruster', [
            (.30, .13), (.46, .11)], seat, axis=tuple(out), sides=8)
    # The claw: a flat arm reaching forward and outboard past the cockpit,
    # carrying the row of five bulges every photograph of the ship shows.
    root = at + Vector((sx * .46, sy * .08, -2.10))
    tip = at + Vector((sx * 1.11, sy * .20, -5.70))
    lie = (tip - root).cross(Vector((0, 1, 0)))
    blade('claw ' + tag, 'fury edge', root, tip, lie, 1.40, .70, .38, .20)
    for k in range(5):
        seat = root + (tip - root) * (.18 + .16 * k)
        lathe(f'bulge {tag} {k}', 'fury edge', [
            (-.30, .16), (-.14, .27), (.14, .27), (.30, .16)], seat, axis=(0, 1, 0), sides=10)
    # A short fin inboard on top of each pod, and the vernier saddles.
    blade('fin ' + tag, 'fury edge', at + Vector((0, sy * .96, .30)),
          at + Vector((-sx * .28, sy * 1.42, 1.10)), (0, 0, 1), 1.40, .66, .20, .12)
    for z in (-1.30, 1.45):
        box(f'vernier {tag} {z:+.1f}', 'fury recess',
            at + Vector((sx * 1.06, 0, z)), (.28, .58, .62), sides=6)
    box('nav ' + tag, 'fury beacon' if sx < 0 else 'fury lamp',
        at + Vector((sx * 1.04, sy * .46, -1.80)), (.18, .18, .24), sides=6)


def build():
    PARTS.clear()
    fuselage()
    for sx in (-1, 1):
        for sy in (-1, 1):
            wing(sx, sy)
            nacelle(sx, sy)

    collection = bpy.data.collections.get(COLLECTION)
    if collection is None:
        collection = bpy.data.collections.new(COLLECTION)
        bpy.context.scene.collection.children.link(collection)
    for obj in list(collection.objects):
        mesh = obj.data
        collection.objects.unlink(obj)
        bpy.data.objects.remove(obj)
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    for name, (colour, code) in PALETTE.items():
        material = bpy.data.materials.get(name)
        if material is None:
            material = bpy.data.materials.new(name)
        material.diffuse_color = (*colour, 1)
        material.metallic = .55 if code in (HULL, EDGE) else .1
        material.roughness = .45
        material['omega_material'] = code
    for name, material, verts, faces in PARTS:
        mesh = bpy.data.meshes.new('Starfury | ' + name)
        mesh.from_pydata(verts, [], faces)
        mesh.update()
        mesh.materials.append(bpy.data.materials[material])
        obj = bpy.data.objects.new(mesh.name, mesh)
        obj['omega_material'] = PALETTE[material][1]
        collection.objects.link(obj)
    return collection


def export(collection):
    rows, low, high = [], [1e9] * 3, [-1e9] * 3
    for obj in sorted(collection.objects, key=lambda o: o.name):
        if obj.type != 'MESH':
            continue
        mesh = obj.data
        mesh.calc_loop_triangles()
        transform = obj.matrix_world
        normals = transform.to_3x3().inverted().transposed()
        material = int(obj['omega_material'])
        rgb = [round(max(0, min(1, c)) * 255) for c in mesh.materials[0].diffuse_color[:3]]
        for tri in mesh.loop_triangles:
            for index, loop in zip(tri.vertices, tri.loops):
                point = transform @ mesh.vertices[index].co
                normal = (normals @ mesh.corner_normals[loop].vector).normalized()
                low = [min(a, b) for a, b in zip(low, point)]
                high = [max(a, b) for a, b in zip(high, point)]
                xyz = [round(c * 1000) for c in point]
                assert all(-32768 <= c <= 32767 for c in xyz), xyz
                rows.append('{' + ','.join(map(str, xyz + [round(c * 32767) for c in normal]
                                               + rgb + [material])) + '},\n')
    unit = 1e-3 / MODEL_UNIT_METRES
    (ROOT / 'omega/omega_fury.h').write_text(
        '/* Generated by tools/build_omega_fury.py; do not edit.\n'
        f' * SA-23E Aurora Starfury, {high[2] - low[2]:.2f} m long by {high[0] - low[0]:.2f} m across\n'
        f' * by {high[1] - low[1]:.2f} m, packed in millimetres. */\n'
        '#include <stdint.h>\n'
        'typedef struct { int16_t x,y,z,nx,ny,nz; uint8_t r,g,b,material; } OmegaFuryVertex;\n'
        f'#define OMEGA_FURY_COUNT {len(rows)}u\n'
        f'#define OMEGA_FURY_UNIT {unit:.9g}f /* model units per packed millimetre */\n'
        'extern const OmegaFuryVertex omega_fury[OMEGA_FURY_COUNT];\n'
        '#ifdef OMEGA_FURY_IMPLEMENTATION\n'
        'const OmegaFuryVertex omega_fury[OMEGA_FURY_COUNT] = {\n' + ''.join(rows) + '};\n#endif\n',
        encoding='utf-8')
    return len(rows), low, high


if __name__ == '__main__' or True:
    _collection = build()
    _count, _low, _high = export(_collection)
    print('Starfury:', len(PARTS), 'parts,', _count // 3, 'triangles,', _count, 'vertices')
    print('metres  x %.3f..%.3f  y %.3f..%.3f  z %.3f..%.3f' % (
        _low[0], _high[0], _low[1], _high[1], _low[2], _high[2]))
    print('model units: length %.4f  span %.4f  rise %.4f' % (
        (_high[2] - _low[2]) / MODEL_UNIT_METRES, (_high[0] - _low[0]) / MODEL_UNIT_METRES,
        (_high[1] - _low[1]) / MODEL_UNIT_METRES))
