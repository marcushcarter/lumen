#ifndef SHADOW_CULL_GLSL
#define SHADOW_CULL_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint SHADOW_LIST_HEADER = 4u;
const uint SHADOW_LIST_COUNT = 3u;
const uint SHADOW_MAX_GROUPS = 65535u;
const uint SHADOW_CHUNK = 64u;

layout(buffer_reference, scalar) buffer ShadowWorkList { uint data[]; };
layout(buffer_reference, scalar) buffer ShadowClusterList { uvec4 data[]; };
layout(buffer_reference, scalar) buffer ShadowDrawArgs { uint data[]; };
layout(buffer_reference, scalar) buffer ShadowIndexBuffer { uint data[]; };
const uint SHADOW_ARGS_STRIDE = 12u;
const uint SHADOW_ARGS_CLUSTERS = 5u;
const uint SHADOW_ARGS_BUILD = 6u;
const uint SHADOW_LOCAL_BITS = 8u;

uint shadow_group_index() {
    return gl_WorkGroupID.y * SHADOW_MAX_GROUPS + gl_WorkGroupID.x;
}

bool shadow_cull_sphere(mat4 p_view_proj, vec3 p_center, float p_radius) {
    const vec3 c = (p_view_proj * vec4(p_center, 1.0)).xyz;
    const vec3 s = vec3(
        length(vec3(p_view_proj[0][0], p_view_proj[1][0], p_view_proj[2][0])),
        length(vec3(p_view_proj[0][1], p_view_proj[1][1], p_view_proj[2][1])),
        length(vec3(p_view_proj[0][2], p_view_proj[1][2], p_view_proj[2][2]))) * p_radius;
    return any(greaterThan(abs(c.xy), vec2(1.0) + s.xy)) || c.z < -s.z || c.z > 1.0 + s.z;
}

#endif
