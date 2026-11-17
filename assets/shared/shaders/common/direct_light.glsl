#ifndef DIRECT_LIGHT_GLSL
#define DIRECT_LIGHT_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout(buffer_reference, scalar) readonly buffer DirectLightBuffer {
    vec3 direction;
    float angular_radius;
    vec3 illuminance;
    float _pad;
    mat4 player_shadow_view_proj;
    mat4 player_shadow_screen;
    vec4 player_shadow_params;
    uvec4 player_shadow_rect;
    mat4 cascade_view_proj[2];
    vec4 cascade_params[2];
    vec4 cascade_splits;
    vec4 cascade_planes[32];
    uvec4 cascade_plane_count;
};

#endif
