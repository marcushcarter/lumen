#version 460

layout(location = 0) flat in uint v_id;
layout(location = 0) out uint o_id;

void main() {
    o_id = v_id;
}
