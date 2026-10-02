#version 460

layout(early_fragment_tests) in;

layout(location = 0) flat in uint v_draw_id;
layout(location = 0) out vec4 o_count;

void main() {
    o_count = vec4(1.0 / 255.0, 0.0, 0.0, 1.0);
}