#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#include "common/geometry.glsl"
#include "common/camera.glsl"
#include "common/instances.glsl"
#include "common/compact_draw.glsl"

layout(buffer_reference, scalar) readonly buffer ClusterRefsBuffer { uint count; uint _pad; uvec2 data[]; };
layout(buffer_reference, scalar) readonly buffer VisibleClusterBuffer { uint count; uint data[]; };

layout(push_constant) uniform Push {
    CameraBuffer camera;
    GeometryBuffer geometry;
    InstanceBuffer instances;
    TransformBuffer transforms;
    ClusterRefsBuffer cluster_refs;
    VisibleClusterBuffer visible;
    CompactPrimBuffer prims;
    CompactDrawArgs args;
    uint phase;
} pc;

void main() {
    const uint slot = uint(gl_VertexIndex) >> COMPACT_LOCAL_BITS;
    const uint local = uint(gl_VertexIndex) & COMPACT_LOCAL_MASK;

    const uint ref_idx = pc.visible.data[slot];
    const uvec2 ref = pc.cluster_refs.data[ref_idx];
    const uint mesh_id = pc.instances.data[ref.x].mesh_id;
    const uint transform_id = pc.instances.data[ref.x].transform_id;
    const uint vertex = pc.geometry.meshlet_vertices.data[pc.geometry.cluster_vertex_base.data[ref.y] + local];

    const vec3 pos_min = pc.geometry.meshes.data[mesh_id].pos_min;
    const vec3 pos_extent = pc.geometry.meshes.data[mesh_id].pos_extent;
    const vec3 p = pos_min + vertex_position_unorm(pc.geometry.vertices.data[vertex]) * pos_extent;

    const mat4 model = pc.transforms.data[transform_id].curr_mtx;
    const vec3 world = model[0].xyz * p.x + model[1].xyz * p.y + model[2].xyz * p.z + model[3].xyz;
    gl_Position = pc.camera.curr_view_proj * vec4(world, 1.0);
}