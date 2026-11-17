#ifndef SUN_CASCADE_SPILL_VERTEX_GLSL
#define SUN_CASCADE_SPILL_VERTEX_GLSL

#include "lighting/sun_cascade_push.glsl"
#include "lighting/sun_cascade_kind.glsl"
#include "common/skinning.glsl"

vec4 sun_cascade_spill_vertex(bool p_uv, out vec2 r_uv, out uint r_material) {
    r_uv = vec2(0.0);
    r_material = 0u;
    const uint slot = pc.spill.data[uint(gl_InstanceIndex)];
    const uvec2 e = pc.list.entries[slot];
    const Cluster c = pc.geometry.clusters.data[e.y];
    const uint tri = uint(gl_VertexIndex) / 3u;
    if (tri * 3u >= c.index_count) return SUN_CASCADE_REJECT;
    const uint tri_index = c.index_base / 3u + tri;
    const uint lt = pc.geometry.local_triangles.data[tri_index];
    const Instance inst = pc.instances.data[e.x];
    const uint entry = geometry_slot_entry(pc.geometry, inst.slot_base, geometry_tri_slot(pc.geometry, tri_index, lt));
    if (sun_cascade_kind(entry, pc.geometry.cluster_slots.data[e.y] == CLUSTER_SLOT_MIXED) != pc.kind) return SUN_CASCADE_REJECT;

    const uint local = (lt >> (8u * (uint(gl_VertexIndex) % 3u))) & 0xFFu;
    const Mesh mesh = pc.geometry.meshes.data[inst.mesh_id];
    const ClusterStreams streams = cluster_streams(pc.geometry, e.y);
    vec3 p = stream_position(pc.geometry, streams, local, mesh.pos_min, mesh.pos_extent);
    if (inst.skin_id != SKIN_NONE) p = skin_point(skin_vertex_rows(pc.camera, pc.geometry, streams, local, pc.camera.skins.data[inst.skin_id].joint_base, SKIN_CURR), p);
    if (p_uv) {
        r_uv = stream_uv(pc.geometry, streams, local, mesh.uv_min, mesh.uv_extent);
        r_material = slot_material(entry);
    }
    return sun_cascade_clip(pc.light, pc.cascade, (pc.transforms.data[inst.transform_id].curr_mtx * vec4(p, 1.0)).xyz);
}

#endif
