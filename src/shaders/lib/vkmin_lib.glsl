// lib/vkmin_lib.glsl -- the whole shader library in one include, for a
// forward fragment shader.
//
// This is what render.h means by "the game's SPIR-V composed from
// shaders/lib". render_desc.fs is a caller-supplied fragment module; leave it
// empty and render.c pairs scene.vert with lit_pbr.frag, fill it in and the
// same scene.vert runs against yours. So a game's shader is not free-form: it
// must consume exactly the varyings lib/inputs.glsl declares and write exactly
// the targets lib/outputs.glsl declares, or the pipeline is wrong at the
// interface rather than merely wrong in the picture. Including this file gets
// both, in the right order, along with common.glsl and its transport contract.
//
// The pieces, and the order a fragment shader is expected to call them:
//
//   inputs   varyings from scene.vert -- v_world_pos, v_normal, v_tangent,
//            v_uv, v_instance. Declarations only; nothing to call.
//   surface  surface_fetch() -- one Material and the varyings in, a resolved
//            Surface (albedo, normal, metallic, roughness, emissive) out.
//            Start here unless you are lighting something that has no material.
//   shadow   cascade_for() picks the sun's cascade from view depth;
//            sun_shadow() and light_shadow() return the lit fraction.
//   lights   lights_for_pixel() gives the pixel's light list (clustered or
//            every light); light_at() and light_attenuation() walk it. The
//            sun is never in the list -- frame.sun_light names it separately.
//   pbr/cel  one of the two BRDFs, per light, accumulated into a colour.
//            Both take the light's radiance; neither knows about shadows.
//   fog      fog() over the accumulated colour, keyed on view depth.
//   outdoor  sky, aerial perspective, wind, terrain and cloud shadow. Optional
//            and independent of the rest; only used when frame.outdoor is set.
//   outputs  debug_color() to let the debug views replace the colour, then
//            write_outputs() exactly once. Nothing after it.
//
// The three canonical shaders are worked examples at three depths: unlit.frag
// (surface, fog, outputs), lit_cel.frag (adds shadows, lights and cel) and
// lit_pbr.frag (everything, including the outdoor path). Only lit_pbr is named
// by render.c, as the default fs; the other two are here to be passed in as
// render_desc.fs, and to be read.
#include "common.glsl"
#include "lib/inputs.glsl"
#include "lib/surface.glsl"
#include "lib/shadow.glsl"
#include "lib/lights.glsl"
#include "lib/pbr.glsl"
#include "lib/cel.glsl"
#include "lib/fog.glsl"
#include "lib/outputs.glsl"
#include "lib/outdoor.glsl"
