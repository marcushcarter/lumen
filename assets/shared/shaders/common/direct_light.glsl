#ifndef DIRECT_LIGHT_GLSL
#define DIRECT_LIGHT_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout(buffer_reference, scalar) readonly buffer DirectLightBuffer {
    vec3 direction;
    float angular_radius;
    vec3 illuminance;
    float _pad;
};

#endif
