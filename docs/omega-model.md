# Omega model and weapon geometry

The Blender source is `omega/omega_destroyer.blend`. Its reference rebuild
contains 160,120 triangles; the native scene draws four hulls plus gate,
escorts and effects (646,864 triangles). The Agamemnon and Alexander GLBs
contain the same refined geometry in their portable scenes.

The refinement follows the bow, top, underside, habitat and engine production
renders collected by [Starship Modeler](https://www.starshipmodeler.com/b5/b5tech.htm),
with the launch bay and outboard bow cannons checked against the
[Severed Dreams episode still](https://www.b5tech.org/science/scale/omega/Severed_Dreams_05b.jpg).
These references were viewed for modeling; their images are not redistributed
in this repository. The README pictures are renders of this project's model.

The [B5 Tech-Manual](https://www.b5tech.org/earthalliance/earthallianceshipsandvessels/earthcapships/omega/omega.html)
describes heavy and secondary particle/laser and pulse armament. It is a fan
reference, not a source of engineering limits. This demo depicts the paired
forward beams and selected secondary pulse batteries; it does not implement
the full listed arsenal or claim an exact canonical turret layout.

| Tech-Manual inventory | Current demo |
|---|---|
| 6 heavy particle/lasers | Two working forward beam cannons |
| 6 heavy pulse cannons | Separate heavy pulse batteries not implemented |
| 12 particle/lasers and 12 pulse cannons | Twelve twin-gun turrets; selected pulse batteries work, individual laser/pulse roles not assigned |
| Fusion missiles, 2 launchers | Not implemented |
| 18 Mk. II defense-grid energy projectors | Not implemented |
| 8–10 m armored hull | Visual plating; thickness and damage resistance not simulated |

The model uses Z along the hull, negative Z toward the bow, Y up. Fixed beam
mouths are at X ±2.39, Y 0.12, Z −20.105. Twelve secondary mounts sit on the
dorsal, ventral and side neck surfaces. Each has a stationary swivel, a yawing
housing, and elevating paired barrels. The port aft-neck mounts provide the
opponents' visible return fire. The demo gives these mounts full azimuth and
0–75° elevation above their mounting planes; those are authored mechanical
limits. They acquire their targets between 11.5 and 13.2 seconds, before the
first pulse at 13.5 seconds.

`tools/export_omega_blender.py`, run inside Blender, exports rest-pose vertices,
per-turret part tags, hardpoints and ray-cast armor contact points. The native
shader rotates the habitat independently and transforms turret vertices and
normals using CPU-computed rigid poses. Fixed beams follow the bore axes.
Pulses alternate barrels, with launch position and direction evaluated at the
birth tick and a target lead over their half-second flight. This is a scripted
battle, not a general collision or fire-control simulation.

`test_omega_weapons` exercises all 1,800 ticks: rigid transforms, elevation
stops, visible acquisition, launch-tip alignment, barrel/trajectory alignment,
and trajectories that stay fixed across successive frames. `--weapon-view`
provides a close camera for visual checks. To refresh all README media and the
30-second film, run `python tools/export_omega_video.py` after building Omega.
