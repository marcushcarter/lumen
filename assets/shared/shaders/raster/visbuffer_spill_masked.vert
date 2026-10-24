#version 460

#include "raster/raster_common.glsl"

layout(push_constant) uniform Push {
    CameraBuffer camera;
    GeometryBuffer geometry;
    InstanceBuffer instances;
    TransformBuffer transforms;
    ClusterRefsBuffer cluster_refs;
    VisibleClusterBuffer visible;
    uint bin;
    uint sampler_index;
} pc;

layout(location = 0) flat out uint v_vis;
layout(location = 1) flat out uint v_material;
layout(location = 2) out vec2 v_uv;

void main() {
    v_material = 0u;
    v_uv = vec2(0.0);
    const uint spill_word = uint(gl_InstanceIndex);
    const uint ref_idx = pc.visible.data[spill_word & COMPACT_SPILL_SLOT_MASK];
    const uvec2 ref = pc.cluster_refs.data[ref_idx];
    const uint tri = uint(gl_VertexIndex) / 3u;
    const uint tri_index = pc.geometry.clusters.data[ref.y].index_base / 3u + tri;
    const uint lt = pc.geometry.local_triangles.data[tri_index];
    const uint entry = geometry_slot_entry(pc.geometry, pc.instances.data[ref.x].slot_base, geometry_tri_slot(pc.geometry, tri_index, lt));
    v_vis = vis_encode_cluster(ref_idx, tri);
    if (((spill_word >> (COMPACT_SPILL_SLOT_BITS + pc.bin)) & 1u) == 0u || slot_bin(entry) != pc.bin) {
        gl_Position = RASTER_CLIP_REJECT;
        return;
    }
    const uint local = (lt >> (8u * (uint(gl_VertexIndex) % 3u))) & 0xFFu;
    v_material = slot_material(entry);
    gl_Position = raster_vertex(pc.camera, pc.geometry, pc.instances, pc.transforms, ref, local, v_uv);
}