#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#include "common/geometry.glsl"
#include "common/camera.glsl"
#include "common/instances.glsl"
#include "common/visbuffer.glsl"

layout(buffer_reference, scalar) readonly buffer ClusterRefsBuffer { uint count; uint _pad; uvec2 data[]; };
layout(buffer_reference, scalar) readonly buffer ScatterBuffer { uint data[]; };

layout(push_constant) uniform Push {
    CameraBuffer camera;
    GeometryBuffer geometry;
    InstanceBuffer instances;
    TransformBuffer transforms;
    ClusterRefsBuffer cluster_refs;
    ScatterBuffer scatter;
} pc;

layout(location = 0) flat out uint v_vis_base;

void main() {
    const uint ref_idx = pc.scatter.data[gl_InstanceIndex];
    const uint instance_id = pc.cluster_refs.data[ref_idx].x;
    const uint mesh_id = pc.instances.data[instance_id].mesh_id;
    const uint transform_id = pc.instances.data[instance_id].transform_id;

    const vec3 pos_min = pc.geometry.meshes.data[mesh_id].pos_min;
    const vec3 pos_extent = pc.geometry.meshes.data[mesh_id].pos_extent;
    const vec3 local = pos_min + vertex_position_unorm(pc.geometry.vertices.data[gl_VertexIndex]) * pos_extent;

    const mat4 model = pc.transforms.data[transform_id].curr_mtx;
    const vec3 world = model[0].xyz * local.x + model[1].xyz * local.y + model[2].xyz * local.z + model[3].xyz;

    gl_Position = pc.camera.curr_view_proj * vec4(world, 1.0);
    v_vis_base = vis_encode_cluster(ref_idx, 0u);
}