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

layout(buffer_reference, scalar) readonly buffer ClusterRefsBuffer { uint count; uint _pad; uvec2 data[]; };
layout(buffer_reference, scalar) readonly buffer VisibleClusterBuffer { uint count; uint data[]; };

const vec4 RASTER_CLIP_REJECT = vec4(2.0, 2.0, 2.0, 1.0);

// One 16B fetch per vertex: cluster_vertices holds position (xy) and attributes (zw) contiguously per cluster.
vec4 raster_vertex(CameraBuffer p_camera, GeometryBuffer p_geometry, InstanceBuffer p_instances, TransformBuffer p_transforms, uvec2 p_ref, uint p_local, out vec2 r_uv) {
    const uint mesh_id = p_instances.data[p_ref.x].mesh_id;
    const uint transform_id = p_instances.data[p_ref.x].transform_id;
    const uvec4 q = p_geometry.cluster_vertices.data[p_geometry.cluster_vertex_base.data[p_ref.y] + p_local];
    const vec3 p = p_geometry.meshes.data[mesh_id].pos_min + vertex_position_unorm(q.xy) * p_geometry.meshes.data[mesh_id].pos_extent;
    r_uv = p_geometry.meshes.data[mesh_id].uv_min + vertex_uv_unorm(q.zw) * p_geometry.meshes.data[mesh_id].uv_extent;
    const mat4 model = p_transforms.data[transform_id].curr_mtx;
    const vec3 world = model[0].xyz * p.x + model[1].xyz * p.y + model[2].xyz * p.z + model[3].xyz;
    return p_camera.curr_view_proj * vec4(world, 1.0);
}

#endif