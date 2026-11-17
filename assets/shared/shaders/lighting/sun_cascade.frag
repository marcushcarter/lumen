#version 460

#include "lighting/sun_cascade_push.glsl"
#include "lighting/sun_cascade_alpha.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_material;

void main() {
    if (sun_cascade_alpha_clipped(v_material, v_uv)) discard;
}
