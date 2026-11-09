#ifndef SKINNING_GLSL
#define SKINNING_GLSL

#include "common/geometry.glsl"
#include "common/instances.glsl"
#include "common/camera.glsl"

const uint SKIN_CURR = 0u;
const uint SKIN_PREV = 3u;

struct SkinRows {
    vec4 r0;
    vec4 r1;
    vec4 r2;
};

uvec2 skin_vertex(GeometryBuffer p_geometry, uint p_cluster, uint p_cluster_vertex) {
    return p_geometry.skin_vertices.data[p_geometry.cluster_skin_delta.data[p_cluster] + p_cluster_vertex];
}

SkinRows skin_blend(CameraBuffer p_camera, uint p_joint_base, uvec2 p_skin, uint p_frame) {
    const vec4 w = unpackUnorm4x8(p_skin.y);
    SkinRows s;
    s.r0 = vec4(0.0);
    s.r1 = vec4(0.0);
    s.r2 = vec4(0.0);
    for (uint k = 0u; k < 4u; k++) {
        if (w[k] == 0.0) continue;
        const uint row = (p_joint_base + ((p_skin.x >> (8u * k)) & 0xFFu)) * 6u + p_frame;
        s.r0 += w[k] * p_camera.joints.data[row];
        s.r1 += w[k] * p_camera.joints.data[row + 1u];
        s.r2 += w[k] * p_camera.joints.data[row + 2u];
    }
    return s;
}

vec3 skin_point(SkinRows p_s, vec3 p_p) {
    const vec4 h = vec4(p_p, 1.0);
    return vec3(dot(p_s.r0, h), dot(p_s.r1, h), dot(p_s.r2, h));
}

vec3 skin_direction(SkinRows p_s, vec3 p_d) {
    return vec3(dot(p_s.r0.xyz, p_d), dot(p_s.r1.xyz, p_d), dot(p_s.r2.xyz, p_d));
}

#endif
