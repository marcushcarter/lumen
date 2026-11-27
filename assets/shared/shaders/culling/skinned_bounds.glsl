#ifndef SKINNED_BOUNDS_GLSL
#define SKINNED_BOUNDS_GLSL

#include "common/geometry.glsl"
#include "common/camera.glsl"
#include "common/instances.glsl"

const uint SKINNED_BOUNDS_MAX_JOINTS = 64u;
const uint SKINNED_ROWS_PER_JOINT = 6u;
const uint SKIN_CURR_ROWS = 0u;
const uint SKIN_PREV_ROWS = 3u;

uvec2 skinned_cluster_joint_range(GeometryBuffer p_geometry, uint p_cluster) {
    const uint format = cluster_streams(p_geometry, p_cluster).skin_format;
    const uint influences = format & 7u;
    const uint joint_bits = (format >> 3u) & 15u;
    const uint joint_lo = (format >> 8u) & 0xFFu;
    return uvec2(joint_lo, joint_lo + (influences == 0u ? 1u : (1u << joint_bits)));
}

bool skinned_cluster_rigid(GeometryBuffer p_geometry, uint p_cluster) {
    const uint format = cluster_streams(p_geometry, p_cluster).skin_format;
    return (format & 7u) <= 1u && ((format >> 3u) & 15u) == 0u;
}

mat4 skinned_joint_matrix(CameraBuffer p_camera, uint p_joint_base, uint p_joint, uint p_frame) {
    const uint row = (p_joint_base + p_joint) * SKINNED_ROWS_PER_JOINT + p_frame;
    const vec4 r0 = p_camera.joints.data[row];
    const vec4 r1 = p_camera.joints.data[row + 1u];
    const vec4 r2 = p_camera.joints.data[row + 2u];
    return mat4(vec4(r0.x, r1.x, r2.x, 0.0), vec4(r0.y, r1.y, r2.y, 0.0), vec4(r0.z, r1.z, r2.z, 0.0), vec4(r0.w, r1.w, r2.w, 1.0));
}

void _skinned_extend(CameraBuffer p_camera, uint p_row, vec4 p_center, float p_radius, inout vec3 r_lo, inout vec3 r_hi) {
    const vec4 r0 = p_camera.joints.data[p_row];
    const vec4 r1 = p_camera.joints.data[p_row + 1u];
    const vec4 r2 = p_camera.joints.data[p_row + 2u];
    const vec3 p = vec3(dot(r0, p_center), dot(r1, p_center), dot(r2, p_center));
    const vec3 c0 = vec3(r0.x, r1.x, r2.x);
    const vec3 c1 = vec3(r0.y, r1.y, r2.y);
    const vec3 c2 = vec3(r0.z, r1.z, r2.z);
    const float r = p_radius * sqrt(max(dot(c0, c0), max(dot(c1, c1), dot(c2, c2))));
    r_lo = min(r_lo, p - r);
    r_hi = max(r_hi, p + r);
}

bool _skinned_box_valid(vec3 p_lo, vec3 p_hi) {
    return !(any(isnan(p_lo)) || any(isnan(p_hi)) || any(isinf(p_lo)) || any(isinf(p_hi)));
}

bool skinned_cluster_boxes(CameraBuffer p_camera, GeometryBuffer p_geometry, Mesh p_mesh, SkinInstance p_skin, uint p_cluster, bool p_want_prev, out vec3 r_min, out vec3 r_extent, out vec3 r_prev_min, out vec3 r_prev_extent, out bool r_prev_valid) {
    const uvec2 range = skinned_cluster_joint_range(p_geometry, p_cluster);
    const uint last = min(range.y, skin_joints(p_skin));
    r_min = vec3(0.0);
    r_extent = vec3(0.0);
    r_prev_min = vec3(0.0);
    r_prev_extent = vec3(0.0);
    r_prev_valid = false;
    if (range.x >= last || last - range.x > SKINNED_BOUNDS_MAX_JOINTS) return false;

    vec3 bind_min, bind_extent;
    cluster_box(p_geometry, p_mesh, p_cluster, bind_min, bind_extent);
    const vec4 center = vec4(bind_min + bind_extent * 0.5, 1.0);
    const float radius = length(bind_extent) * 0.5;

    vec3 lo = vec3(3.402823e38);
    vec3 hi = vec3(-3.402823e38);
    vec3 prev_lo = lo;
    vec3 prev_hi = hi;
    for (uint j = range.x; j < last; j++) {
        const uint row = (p_skin.joint_base + j) * SKINNED_ROWS_PER_JOINT;
        _skinned_extend(p_camera, row + SKIN_CURR_ROWS, center, radius, lo, hi);
        if (p_want_prev) _skinned_extend(p_camera, row + SKIN_PREV_ROWS, center, radius, prev_lo, prev_hi);
    }
    if (!_skinned_box_valid(lo, hi)) return false;
    r_min = lo;
    r_extent = hi - lo;
    if (p_want_prev && _skinned_box_valid(prev_lo, prev_hi)) {
        r_prev_min = prev_lo;
        r_prev_extent = prev_hi - prev_lo;
        r_prev_valid = true;
    }
    return true;
}

bool skinned_cluster_box(CameraBuffer p_camera, GeometryBuffer p_geometry, Mesh p_mesh, SkinInstance p_skin, uint p_cluster, out vec3 r_min, out vec3 r_extent) {
    vec3 prev_min, prev_extent;
    bool prev_valid;
    return skinned_cluster_boxes(p_camera, p_geometry, p_mesh, p_skin, p_cluster, false, r_min, r_extent, prev_min, prev_extent, prev_valid);
}

#endif
