#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "common/geometry.glsl"
#include "common/camera.glsl"
#include "common/instances.glsl"
#include "common/skinning.glsl"
#include "editor/outline_mask_common.glsl"

layout(push_constant, scalar) uniform Push {
    CameraBuffer camera;
    GeometryBuffer geometry;
    InstanceBuffer instances;
    TransformBuffer transforms;
    OutlineMaskListBuffer list;
    OutlineMaskMaterialBuffer materials;
    uint sampler_index;
} pc;

layout(location = 0) out vec2 v_uv;

void main() {
    const uint key = uint(gl_VertexIndex);
    const uvec2 entry = pc.list.clusters[key >> OUTLINE_MASK_SLOT_SHIFT];
    const uint local = key & OUTLINE_MASK_LOCAL_MASK;
    const Instance inst = pc.instances.data[entry.y];
    const Mesh mesh = pc.geometry.meshes.data[inst.mesh_id];
    const ClusterStreams streams = cluster_streams(pc.geometry, entry.x);
    vec3 p = stream_position(pc.geometry, streams, local, mesh.pos_min, mesh.pos_extent);
    if (inst.skin_id != SKIN_NONE) p = skin_point(skin_vertex_rows(pc.camera, pc.geometry, streams, local, pc.camera.skins.data[inst.skin_id].joint_base, SKIN_CURR), p);
    vec4 clip = pc.camera.curr_view_proj * (pc.transforms.data[inst.transform_id].curr_mtx * vec4(p, 1.0));
    clip.xy -= pc.camera.jitter.xy * clip.w;
    gl_Position = clip;
    v_uv = stream_uv(pc.geometry, streams, local, mesh.uv_min, mesh.uv_extent);
}
