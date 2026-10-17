#version 460

layout(location = 0) flat in uint v_vis;
layout(location = 0) out uint o_vis;

void main() {
    o_vis = v_vis;
}