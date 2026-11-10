#ifndef CONE_CULL_GLSL
#define CONE_CULL_GLSL

#include "common/geometry.glsl"
#include "common/camera.glsl"
#include "common/instances.glsl"
#include "common/compact_draw.glsl"

const float CONE_UNIFORM_SCALE_TOLERANCE = 1e-3;
const float CONE_CUTOFF_MARGIN = 1.0 / 127.0;

bool cluster_backfacing(CameraBuffer p_camera, GeometryBuffer p_geometry, Instance p_instance, mat4 p_model, uint p_cluster) {
    if (p_instance.skin_id != SKIN_NONE) return false;
    const uint cone = p_geometry.cluster_cones.data[p_cluster];
    const int cutoff_q = int(cone) >> 24;
    if (cutoff_q >= 127) return false;
    const uint slot = p_geometry.cluster_slots.data[p_cluster];
    if (slot == CLUSTER_SLOT_MIXED) return false;
    if ((slot_bin(geometry_slot_entry(p_geometry, p_instance.slot_base, slot)) & RASTER_BIN_TWO_SIDED) != 0u) return false;

    const vec3 c0 = p_model[0].xyz;
    const vec3 c1 = p_model[1].xyz;
    const vec3 c2 = p_model[2].xyz;
    const float s0 = dot(c0, c0);
    const float s1 = dot(c1, c1);
    const float s2 = dot(c2, c2);
    const float s_max = max(s0, max(s1, s2));
    if (s_max - min(s0, min(s1, s2)) > CONE_UNIFORM_SCALE_TOLERANCE * s_max) return false;
    if (dot(cross(c0, c1), c2) <= 0.0) return false;

    const vec3 axis_q = vec3(float(int(cone << 24u) >> 24), float(int(cone << 16u) >> 24), float(int(cone << 8u) >> 24));
    const vec3 axis = normalize(mat3(p_model) * axis_q);
    const float cutoff = float(cutoff_q) / 127.0 + CONE_CUTOFF_MARGIN;
    const vec4 sphere = p_geometry.clusters.data[p_cluster].cull_sphere;
    const vec3 center = (p_model * vec4(sphere.xyz, 1.0)).xyz;
    const float radius = sphere.w * sqrt(s_max);
    const vec3 d = center - p_camera.position.xyz;
    return dot(d, axis) >= cutoff * length(d) + radius;
}

#endif
