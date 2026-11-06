#ifndef DIRECT_LIGHT_GLSL
#define DIRECT_LIGHT_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint SHADOW_CASCADES = 2;

layout(buffer_reference, scalar) readonly buffer DirectLightBuffer {
    vec3 direction;
    float angular_radius;
    vec3 illuminance;
    uint cascade_count;
    mat4 cascade_view_proj[SHADOW_CASCADES];
    vec4 cascade_splits;
    vec4 cascade_texel;
};

#endif
