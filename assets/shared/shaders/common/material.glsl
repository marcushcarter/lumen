#ifndef MATERIAL_GLSL
#define MATERIAL_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint TEXTURE_NONE = 0xFFFFFFFFu;
const uint MATERIAL_DEFAULT = 0u;

struct Material {
    vec4 base_color;
    vec3 emissive;
    float alpha_cutoff;
    vec4 custom;
    vec4 uv_transform;
    float roughness;
    float metallic;
    float normal_scale;
    float ao_strength;
    uint base_color_tex;
    uint normal_tex;
    uint roughness_tex;
    uint metallic_tex;
    uint ao_tex;
    uint emissive_tex;
    uint custom_tex;
    uint flags;
};

layout(buffer_reference, scalar) readonly buffer MaterialBuffer { Material data[]; };

uint material_shading_model(Material p_m) { return p_m.flags & 0xFFu; }
uint material_alpha_mode(Material p_m) { return (p_m.flags >> 8u) & 0x3u; }
bool material_two_sided(Material p_m) { return (p_m.flags & (1u << 10u)) != 0u; }

#endif
