#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "common/geometry.glsl"
#include "common/camera.glsl"
#include "common/instances.glsl"
#include "common/skinning.glsl"
#include "common/direct_light.glsl"
#include "lighting/player_shadow_common.glsl"

layout(push_constant, scalar) uniform Push {
    CameraBuffer camera;
    GeometryBuffer geometry;
    InstanceBuffer instances;
    TransformBuffer transforms;
    PlayerShadowListBuffer list;
    DirectLightBuffer direct_light;
    uint instance;
} pc;

void main() {
    const uint key = uint(gl_VertexIndex);
    const uint cluster = pc.list.clusters[key >> PLAYER_SHADOW_SLOT_SHIFT];
    const uint local = key & PLAYER_SHADOW_LOCAL_MASK;

    const Instance inst = pc.instances.data[pc.instance];
    const Mesh mesh = pc.geometry.meshes.data[inst.mesh_id];
    const ClusterStreams streams = cluster_streams(pc.geometry, cluster);
    vec3 p = stream_position(pc.geometry, streams, local, mesh.pos_min, mesh.pos_extent);
    if (inst.skin_id != SKIN_NONE) p = skin_point(skin_vertex_rows(pc.camera, pc.geometry, streams, local, pc.camera.skins.data[inst.skin_id].joint_base, SKIN_CURR), p);

    const mat4 model = pc.transforms.data[inst.transform_id].curr_mtx;
    gl_Position = pc.direct_light.player_shadow_view_proj * (model * vec4(p, 1.0));
}
