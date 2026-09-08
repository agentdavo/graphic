#version 450
// Native MSAA regression: MRT resolves and alpha-to-coverage without sample shading.
layout(location = 0) in vec3 v_color;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 extra;
layout(push_constant) uniform Test { float alpha; } test;
void main() {
    color = vec4(v_color, test.alpha);
    extra = vec4(1.0-v_color, test.alpha);
}
