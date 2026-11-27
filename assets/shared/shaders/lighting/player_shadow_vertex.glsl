#ifndef PLAYER_SHADOW_VERTEX_GLSL
#define PLAYER_SHADOW_VERTEX_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "common/geometry.glsl"
#include "common/camera.glsl"
#include "common/instances.glsl"
#include "common/skinning.glsl"
#include "common/direct_light.glsl"
#include "lighting/player_shadow_common.glsl"

layout(buffer_reference, scalar) readonly buffer PlayerSkinTable { uint data[]; };

layout(push_constant, scalar) uniform Push {
    CameraBuffer camera;
    GeometryBuffer geometry;
    InstanceBuffer instances;
    TransformBuffer transforms;
    PlayerShadowListBuffer list;
    DirectLightBuffer direct_light;
    PlayerShadowMaterialBuffer materials;
    PlayerSkinTable player_table;
    SkinCacheBuffer skin_cache;
    uint instance;
    uint sampler_index;
} pc;

vec4 player_shadow_vertex(out ClusterStreams r_streams, out uint r_local, out Mesh r_mesh) {
    const uint key = uint(gl_VertexIndex);
    const uint cluster = pc.list.clusters[key >> PLAYER_SHADOW_SLOT_SHIFT];
    r_local = key & PLAYER_SHADOW_LOCAL_MASK;
    const Instance inst = pc.instances.data[pc.instance];
    r_mesh = pc.geometry.meshes.data[inst.mesh_id];
    r_streams = cluster_streams(pc.geometry, cluster);
    const uint cached = inst.skin_id != SKIN_NONE && !skin_view_split(pc.camera.skins.data[inst.skin_id]) && !all(equal(uvec2(pc.player_table), uvec2(0u))) ? pc.player_table.data[cluster] : SKIN_CACHE_NONE;
    vec3 p;
    if (cached != SKIN_CACHE_NONE) {
        p = skin_cache_position(pc.skin_cache, skin_quant(pc.camera.skins.data[inst.skin_id]), cached + r_local);
    } else {
        p = stream_position(pc.geometry, r_streams, r_local, r_mesh.pos_min, r_mesh.pos_extent);
        if (inst.skin_id != SKIN_NONE) p = skin_point(skin_vertex_rows(pc.camera, pc.geometry, r_streams, r_local, pc.camera.skins.data[inst.skin_id].joint_base, SKIN_CURR), p);
    }
    const mat4 model = pc.transforms.data[inst.transform_id].curr_mtx;
    return pc.direct_light.player_shadow_view_proj * (model * vec4(p, 1.0));
}

#endif
