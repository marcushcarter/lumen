#ifndef SUN_CASCADE_VERTEX_GLSL
#define SUN_CASCADE_VERTEX_GLSL

#include "lighting/sun_cascade_push.glsl"
#include "common/skinning.glsl"

vec4 sun_cascade_vertex(bool p_uv, out vec2 r_uv, out uint r_material) {
    r_uv = vec2(0.0);
    r_material = 0u;
    const uint key = uint(gl_VertexIndex);
    const uint local = key & SUN_CASCADE_LOCAL_MASK;
    const uvec2 e = pc.list.entries[key >> SUN_CASCADE_SLOT_SHIFT];
    const Instance inst = pc.instances.data[e.x];
    const Mesh mesh = pc.geometry.meshes.data[inst.mesh_id];
    const ClusterStreams streams = cluster_streams(pc.geometry, e.y);
    vec3 p = stream_position(pc.geometry, streams, local, mesh.pos_min, mesh.pos_extent);
    if (inst.skin_id != SKIN_NONE) p = skin_point(skin_vertex_rows(pc.camera, pc.geometry, streams, local, pc.camera.skins.data[inst.skin_id].joint_base, SKIN_CURR), p);
    if (p_uv) {
        r_uv = stream_uv(pc.geometry, streams, local, mesh.uv_min, mesh.uv_extent);
        const uint cluster_slot = pc.geometry.cluster_slots.data[e.y];
        if (cluster_slot != CLUSTER_SLOT_MIXED) r_material = slot_material(geometry_slot_entry(pc.geometry, inst.slot_base, cluster_slot));
    }
    return sun_cascade_clip(pc.light, pc.cascade, (pc.transforms.data[inst.transform_id].curr_mtx * vec4(p, 1.0)).xyz);
}

#endif
