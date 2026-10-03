#version 460

layout(location = 0) flat in uint v_draw_id;
layout(location = 0) out uvec2 o_vis;

void main() {
    o_vis = uvec2(v_draw_id, uint(gl_PrimitiveID));
}