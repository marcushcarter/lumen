#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "debug/lines_common.glsl"

layout(set = 0, binding = 0) uniform texture2D u_textures[];

layout(location = 0) in vec4 v_color;
layout(location = 1) in float v_dist;
layout(location = 2) flat in uint v_flags;

layout(location = 0) out vec4 o_color;

const float HIDDEN_ALPHA = 0.25;
const float DEPTH_TOLERANCE = 1e-3;
const float MIN_ALPHA = 1.0 / 255.0;

vec3 to_linear(vec3 p_c) {
    return mix(p_c / 12.92, pow((max(p_c, vec3(0.0)) + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), p_c));
}

void main() {
    float alpha = clamp(pc.half_width + 0.5 - abs(v_dist), 0.0, 1.0) * v_color.a;

    if ((v_flags & DEBUG_LINE_FLAG_XRAY) == 0u) {
        const ivec2 limit = ivec2(pc.depth_size) - 1;
        const ivec2 sp = clamp(ivec2(gl_FragCoord.xy * vec2(pc.depth_size) / vec2(pc.out_size)), ivec2(0), limit);
        const float scene = texelFetch(u_textures[pc.depth_index], sp, 0).r * (1.0 - DEPTH_TOLERANCE);
        if (gl_FragCoord.z < scene) alpha *= HIDDEN_ALPHA;
    }

    if (alpha < MIN_ALPHA) discard;
    o_color = vec4(to_linear(v_color.rgb), alpha);
}
