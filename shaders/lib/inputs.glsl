// lib/inputs.glsl -- what scene.vert hands every forward fragment shader.
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_tangent;
layout(location = 3) in vec2 v_uv;
layout(location = 4) flat in uint v_instance;
