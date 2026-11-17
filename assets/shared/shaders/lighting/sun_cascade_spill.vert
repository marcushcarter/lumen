#version 460

#include "lighting/sun_cascade_spill_vertex.glsl"

void main() {
    vec2 uv;
    uint material;
    gl_Position = sun_cascade_spill_vertex(false, uv, material);
}
