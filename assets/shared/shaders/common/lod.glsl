#ifndef LOD_GLSL
#define LOD_GLSL

#include "common/geometry.glsl"

const float LOD_THRESHOLD_PX = 1.0;
const uint CLUSTER_GROUP_NONE = 0xffffffffu;

float lod_sphere_error_px(vec4 s, float err, mat4 model, float scale, vec3 cam, float ppu) {
    vec3 c = (model * vec4(s.xyz, 1.0)).xyz;
    float d = max(length(c - cam) - s.w * scale, 1e-4);
    return err * scale * ppu / d;
}

float lod_group_error_px(GeometryBuffer geo, uint gi, mat4 model, float scale, vec3 cam, float ppu) {
    ClusterGroup g = geo.groups.data[gi];
    return lod_sphere_error_px(g.sphere, g.error, model, scale, cam, ppu);
}

float lod_self_error_px(GeometryBuffer geo, Cluster c, mat4 model, float scale, vec3 cam, float ppu) {
    return c.self_group == CLUSTER_GROUP_NONE ? 0.0 : lod_group_error_px(geo, c.self_group, model, scale, cam, ppu);
}

float lod_parent_error_px(GeometryBuffer geo, Cluster c, mat4 model, float scale, vec3 cam, float ppu) {
    return c.parent_group == CLUSTER_GROUP_NONE ? 3.402823e38 : lod_group_error_px(geo, c.parent_group, model, scale, cam, ppu);
}

uint lod_cluster_level(GeometryBuffer geo, uint cluster_id) {
    Cluster c = geo.clusters.data[cluster_id];
    return c.self_group == CLUSTER_GROUP_NONE ? 0u : geo.groups.data[c.self_group].level;
}

bool lod_selected(GeometryBuffer geo, Cluster c, mat4 model, float scale, vec3 cam, float ppu) {
    return lod_self_error_px(geo, c, model, scale, cam, ppu) <= LOD_THRESHOLD_PX && lod_parent_error_px(geo, c, model, scale, cam, ppu) > LOD_THRESHOLD_PX;
}

#endif