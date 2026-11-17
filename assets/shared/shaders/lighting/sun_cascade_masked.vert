#version 460

#include "lighting/sun_cascade_vertex.glsl"

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_material;

void main() {
    gl_Position = sun_cascade_vertex(true, v_uv, v_material);
}
