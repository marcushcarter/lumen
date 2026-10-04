#version 450
#extension GL_EXT_nonuniform_qualifier : require

#include "common/color.glsl"

layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 0, binding = 2) uniform sampler u_samplers[];

layout(push_constant) uniform PC {
    uint src_index;
    uint sampler_index;
} pc;

vec4 sample_bindless(uint tex_id, uint sampler_id, vec2 uv) {
    return texture(sampler2D(u_textures[nonuniformEXT(tex_id)], u_samplers[nonuniformEXT(sampler_id)]), uv);
}

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

void main()
{
    vec2 flipped_uv = vec2(vUV.x, 1.0 - vUV.y);
    vec4 color = sample_bindless(pc.src_index, pc.sampler_index, flipped_uv);
    color.rgb = linear_to_srgb(color.rgb);
    oColor = color;
}