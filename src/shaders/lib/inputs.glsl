// lib/inputs.glsl -- what scene.vert hands every forward fragment shader.
// Declarations only, and the half of an interface a game shader does not get
// to choose: render.c always pairs the caller's fs with scene.vert, so these
// locations and types are fixed. Include this file rather than retyping them;
// a location that drifts is a link error at best and stale registers at worst.
// v_instance indexes frame.instances -- it is gl_InstanceIndex, which arrives
// via DrawCmd.first_instance from the cull shader, not a vertex attribute.
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_tangent;
layout(location = 3) in vec2 v_uv;
layout(location = 4) flat in uint v_instance;
