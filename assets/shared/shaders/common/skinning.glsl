#ifndef SKINNING_GLSL
#define SKINNING_GLSL

#extension GL_EXT_buffer_reference_uvec2 : require

#include "common/geometry.glsl"
#include "common/instances.glsl"
#include "common/camera.glsl"

const uint SKIN_CURR = 0u;
const uint SKIN_PREV = 3u;
const uint SKIN_CACHE_NONE = 0xFFFFFFFFu;

struct SkinRows {
    vec4 r0;
    vec4 r1;
    vec4 r2;
};

layout(buffer_reference, scalar) buffer SkinRefBuffer { uint data[]; };
layout(buffer_reference, scalar) buffer SkinPositionBuffer { vec3 data[]; };
layout(buffer_reference, scalar) buffer SkinNormalBuffer { uint data[]; };

layout(buffer_reference, scalar, buffer_reference_align = 16) buffer SkinCacheBuffer {
    uint count;
    uint capacity;
    uint _pad0;
    uint _pad1;
    SkinRefBuffer refs;
    SkinPositionBuffer positions;
    SkinPositionBuffer prev_positions;
    SkinNormalBuffer normals;
};

SkinRows skin_blend(CameraBuffer p_camera, uint p_joint_base, uvec4 p_joints, vec4 p_weights, uint p_frame) {
    SkinRows s;
    s.r0 = vec4(0.0);
    s.r1 = vec4(0.0);
    s.r2 = vec4(0.0);
    for (uint k = 0u; k < 4u; k++) {
        if (p_weights[k] == 0.0) continue;
        const uint row = (p_joint_base + p_joints[k]) * 6u + p_frame;
        s.r0 += p_weights[k] * p_camera.joints.data[row];
        s.r1 += p_weights[k] * p_camera.joints.data[row + 1u];
        s.r2 += p_weights[k] * p_camera.joints.data[row + 2u];
    }
    return s;
}

SkinRows skin_vertex_rows(CameraBuffer p_camera, GeometryBuffer p_geometry, ClusterStreams p_streams, uint p_local, uint p_joint_base, uint p_frame) {
    uvec4 joints;
    vec4 weights;
    stream_skin(p_geometry, p_streams, p_local, joints, weights);
    return skin_blend(p_camera, p_joint_base, joints, weights, p_frame);
}

vec3 skin_point(SkinRows p_s, vec3 p_p) {
    const vec4 h = vec4(p_p, 1.0);
    return vec3(dot(p_s.r0, h), dot(p_s.r1, h), dot(p_s.r2, h));
}

vec3 skin_direction(SkinRows p_s, vec3 p_d) {
    return vec3(dot(p_s.r0.xyz, p_d), dot(p_s.r1.xyz, p_d), dot(p_s.r2.xyz, p_d));
}

uint skin_cache_base(SkinCacheBuffer p_cache, uint p_ref_index) {
    return all(equal(uvec2(p_cache), uvec2(0u))) ? SKIN_CACHE_NONE : p_cache.refs.data[p_ref_index];
}

#endif
