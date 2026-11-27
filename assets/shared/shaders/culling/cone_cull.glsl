#ifndef CONE_CULL_GLSL
#define CONE_CULL_GLSL

#include "common/geometry.glsl"
#include "common/camera.glsl"
#include "common/instances.glsl"
#include "common/compact_draw.glsl"
#include "culling/skinned_bounds.glsl"

const float CONE_UNIFORM_SCALE_TOLERANCE = 1e-3;
const float CONE_CUTOFF_MARGIN = 1.0 / 127.0;

struct ClusterCone {
    vec3 axis;
    float cutoff;
    vec3 center;
    float radius;
};

bool cluster_cone(CameraBuffer p_camera, GeometryBuffer p_geometry, Instance p_instance, mat4 p_model, uint p_cluster, out ClusterCone r_cone) {
    r_cone = ClusterCone(vec3(0.0), 0.0, vec3(0.0), 0.0);
    const uint cone = p_geometry.cluster_cones.data[p_cluster];
    const int cutoff_q = int(cone) >> 24;
    if (cutoff_q >= 127) return false;
    const uint slot = p_geometry.cluster_slots.data[p_cluster];
    if (slot == CLUSTER_SLOT_MIXED) return false;
    if ((slot_bin(geometry_slot_entry(p_geometry, p_instance.slot_base, slot)) & RASTER_BIN_TWO_SIDED) != 0u) return false;

    mat4 model = p_model;
    if (p_instance.skin_id != SKIN_NONE) {
        if (!skinned_cluster_rigid(p_geometry, p_cluster)) return false;
        const SkinInstance skin = p_camera.skins.data[p_instance.skin_id];
        const uint joint = skinned_cluster_joint_range(p_geometry, p_cluster).x;
        if (joint >= skin_joints(skin)) return false;
        model = p_model * skinned_joint_matrix(p_camera, skin.joint_base, joint, 0u);
    }

    const vec3 c0 = model[0].xyz;
    const vec3 c1 = model[1].xyz;
    const vec3 c2 = model[2].xyz;
    const float s0 = dot(c0, c0);
    const float s1 = dot(c1, c1);
    const float s2 = dot(c2, c2);
    const float s_max = max(s0, max(s1, s2));
    if (s_max - min(s0, min(s1, s2)) > CONE_UNIFORM_SCALE_TOLERANCE * s_max) return false;
    if (dot(cross(c0, c1), c2) <= 0.0) return false;

    const vec3 axis_q = vec3(float(int(cone << 24u) >> 24), float(int(cone << 16u) >> 24), float(int(cone << 8u) >> 24));
    const vec4 sphere = p_geometry.clusters.data[p_cluster].cull_sphere;
    r_cone.axis = normalize(mat3(model) * axis_q);
    r_cone.cutoff = float(cutoff_q) / 127.0 + CONE_CUTOFF_MARGIN;
    r_cone.center = (model * vec4(sphere.xyz, 1.0)).xyz;
    r_cone.radius = sphere.w * sqrt(s_max);
    return true;
}

bool cluster_backfacing(CameraBuffer p_camera, GeometryBuffer p_geometry, Instance p_instance, mat4 p_model, uint p_cluster) {
    ClusterCone c;
    if (!cluster_cone(p_camera, p_geometry, p_instance, p_model, p_cluster, c)) return false;
    const vec3 d = c.center - p_camera.position.xyz;
    return dot(d, c.axis) >= c.cutoff * length(d) + c.radius;
}

#endif
