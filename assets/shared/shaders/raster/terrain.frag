#version 460

#include "common/visbuffer.glsl"

layout(location = 0) flat in uint v_patch;
layout(location = 0) out uint o_vis;

void main() {
    o_vis = vis_encode_terrain(v_patch, uint(gl_PrimitiveID) & VIS_TRI_MASK);
}
