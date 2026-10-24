#ifndef RASTER_MASKED_GLSL
#define RASTER_MASKED_GLSL

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 0, binding = 2) uniform sampler u_samplers[];

bool raster_alpha_clipped(GeometryBuffer p_geometry, uint p_material, vec2 p_uv, uint p_sampler) {
    const uint tex = p_geometry.materials.data[p_material].base_color_tex;
    float alpha = p_geometry.materials.data[p_material].base_color.a;
    if (tex != TEXTURE_NONE) {
        const vec4 uvt = p_geometry.materials.data[p_material].uv_transform;
        alpha *= texture(sampler2D(u_textures[nonuniformEXT(tex)], u_samplers[p_sampler]), p_uv * uvt.xy + uvt.zw).a;
    }
    return alpha < p_geometry.materials.data[p_material].alpha_cutoff;
}

#endif