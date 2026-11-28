#ifndef RASTER_MASKED_GLSL
#define RASTER_MASKED_GLSL

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 0, binding = 2) uniform sampler u_samplers[];

bool raster_alpha_clipped(GeometryBuffer p_geometry, uint p_material, vec2 p_uv, uint p_sampler) {
    const Material m = p_geometry.materials.data[p_material];
    if (m.mask_tex == TEXTURE_NONE) return false;
    const float alpha = texture(sampler2D(u_textures[nonuniformEXT(m.mask_tex)], u_samplers[p_sampler]), p_uv)[material_mask_channel(m)];
    return alpha < m.alpha_cutoff;
}

#endif