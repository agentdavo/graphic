// lib/vkmin_lib.glsl -- the whole shader library in one include, for a
// forward fragment shader. Compose the pieces; the three canonical shaders
// (lit_pbr, lit_cel, unlit) are worked examples of doing so.
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
