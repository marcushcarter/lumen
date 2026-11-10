#ifndef RASTER_COMMON_GLSL
#define RASTER_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#include "common/geometry.glsl"
#include "common/camera.glsl"
#include "common/instances.glsl"
#include "common/visbuffer.glsl"
#include "common/compact_draw.glsl"
#include "common/skinning.glsl"

layout(buffer_reference, scalar) readonly buffer ClusterRefsBuffer { uint count; uint _pad; uvec2 data[]; };
layout(buffer_reference, scalar) readonly buffer VisibleClusterBuffer { uint count; uint data[]; };

const vec4 RASTER_CLIP_REJECT = vec4(2.0, 2.0, 2.0, 1.0);

vec4 raster_vertex(CameraBuffer p_camera, GeometryBuffer p_geometry, InstanceBuffer p_instances, TransformBuffer p_transforms, SkinCacheBuffer p_skin_cache, uint p_ref_index, uvec2 p_ref, uint p_local, out vec2 r_uv) {
    const uint mesh_id = p_instances.data[p_ref.x].mesh_id;
    const uint transform_id = p_instances.data[p_ref.x].transform_id;
    const uint skin_id = p_instances.data[p_ref.x].skin_id;
    const ClusterStreams streams = cluster_streams(p_geometry, p_ref.y);
    const uint cached = skin_id != SKIN_NONE ? skin_cache_base(p_skin_cache, p_ref_index) : SKIN_CACHE_NONE;
    vec3 p;
    if (cached != SKIN_CACHE_NONE) {
        p = p_skin_cache.positions.data[cached + p_local];
    } else {
        p = stream_position(p_geometry, streams, p_local, p_geometry.meshes.data[mesh_id].pos_min, p_geometry.meshes.data[mesh_id].pos_extent);
        if (skin_id != SKIN_NONE) p = skin_point(skin_vertex_rows(p_camera, p_geometry, streams, p_local, p_camera.skins.data[skin_id].joint_base, SKIN_CURR), p);
    }
    r_uv = stream_uv(p_geometry, streams, p_local, p_geometry.meshes.data[mesh_id].uv_min, p_geometry.meshes.data[mesh_id].uv_extent);
    const mat4 model = p_transforms.data[transform_id].curr_mtx;
    const vec3 world = model[0].xyz * p.x + model[1].xyz * p.y + model[2].xyz * p.z + model[3].xyz;
    return p_camera.curr_view_proj * vec4(world, 1.0);
}

#endif