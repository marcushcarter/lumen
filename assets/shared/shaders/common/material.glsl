#ifndef MATERIAL_GLSL
#define MATERIAL_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint TEXTURE_NONE = 0xFFFFFFFFu;
const uint MATERIAL_DEFAULT = 0u;

struct Material {
    vec4 uv_transform;
    uint base_color_tex;
    float base_alpha;
    float alpha_cutoff;
    uint flags;
};

layout(buffer_reference, scalar) readonly buffer MaterialBuffer { Material data[]; };

uint material_alpha_mode(Material p_m) { return (p_m.flags >> 8u) & 0x3u; }
bool material_two_sided(Material p_m) { return (p_m.flags & (1u << 10u)) != 0u; }

#endif
