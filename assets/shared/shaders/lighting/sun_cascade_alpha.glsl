#ifndef SUN_CASCADE_ALPHA_GLSL
#define SUN_CASCADE_ALPHA_GLSL

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 0, binding = 2) uniform sampler u_samplers[];

bool sun_cascade_alpha_clipped(uint p_material, vec2 p_uv) {
    const Material m = pc.geometry.materials.data[p_material];
    if (m.mask_tex == TEXTURE_NONE) return false;
    const float alpha = texture(sampler2D(u_textures[nonuniformEXT(m.mask_tex)], u_samplers[pc.sampler_index]), p_uv, pc.mip_bias)[material_mask_channel(m)];
    return alpha < m.alpha_cutoff;
}

#endif
