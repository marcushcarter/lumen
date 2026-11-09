#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "debug/lines_common.glsl"

layout(location = 0) out vec4 v_color;
layout(location = 1) out float v_dist;
layout(location = 2) flat out uint v_flags;

const vec2 CORNERS[6] = vec2[](vec2(0.0, -1.0), vec2(0.0, 1.0), vec2(1.0, -1.0), vec2(1.0, -1.0), vec2(0.0, 1.0), vec2(1.0, 1.0));

void main() {
    const uint line_index = uint(gl_VertexIndex) / 6u;
    const vec2 corner = CORNERS[uint(gl_VertexIndex) % 6u];
    const DebugLine l = pc.lines.lines[line_index];
    CameraBuffer cam = pc.camera;

    vec4 pa = cam.curr_view_proj * vec4(l.a, 1.0);
    vec4 pb = cam.curr_view_proj * vec4(l.b, 1.0);
    pa.xy -= cam.jitter.xy * pa.w;
    pb.xy -= cam.jitter.xy * pb.w;

    v_color = unpackUnorm4x8(l.color);
    v_flags = l.flags;

    const float near_w = cam.near_z;
    if (pa.w < near_w && pb.w < near_w) {
        v_dist = 0.0;
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    if (pa.w < near_w) pa = mix(pa, pb, (near_w - pa.w) / (pb.w - pa.w));
    else if (pb.w < near_w) pb = mix(pb, pa, (near_w - pb.w) / (pa.w - pb.w));

    const vec2 half_size = vec2(pc.out_size) * 0.5;
    const vec2 sa = pa.xy / pa.w * half_size;
    const vec2 sb = pb.xy / pb.w * half_size;
    const vec2 delta = sb - sa;
    const float len = length(delta);
    const vec2 dir = len > 1e-4 ? delta / len : vec2(1.0, 0.0);
    const vec2 normal = vec2(-dir.y, dir.x);

    const float extent = pc.half_width + DEBUG_LINE_AA_PX;
    const bool at_b = corner.x > 0.5;
    vec4 p = at_b ? pb : pa;
    const vec2 offset_px = normal * corner.y * extent + dir * (at_b ? DEBUG_LINE_AA_PX : -DEBUG_LINE_AA_PX);
    p.xy += offset_px / half_size * p.w;

    v_dist = corner.y * extent;
    gl_Position = p;
}
