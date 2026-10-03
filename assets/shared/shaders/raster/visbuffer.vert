#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_ARB_shader_draw_parameters : require

#include "common/geometry.glsl"
#include "common/camera.glsl"
#include "common/instances.glsl"

layout(buffer_reference, scalar) readonly buffer ClusterRefsBuffer { uint count; uint _pad; uvec2 data[]; };
layout(buffer_reference, scalar) readonly buffer ScatterBuffer { uint data[]; };
layout(buffer_reference, scalar) readonly buffer DrawMetaBuffer { uint base[]; };

layout(push_constant) uniform Push {
    CameraBuffer camera;
    GeometryBuffer geometry;
    InstanceBuffer instances;
    TransformBuffer transforms;
    ClusterRefsBuffer cluster_refs;
    ScatterBuffer scatter;
    DrawMetaBuffer draw_meta;
} pc;

layout(location = 0) flat out uint v_draw_id;

void main() {
    uint base = pc.draw_meta.base[gl_DrawIDARB];
    uint ref_idx = pc.scatter.data[base + gl_InstanceIndex];
    uvec2 ref = pc.cluster_refs.data[ref_idx];
    uint instance_id = ref.x;

    Instance instance = pc.instances.data[instance_id];
    Mesh mesh = pc.geometry.meshes.data[instance.mesh_id];
    mat4 model = pc.transforms.data[instance.transform_id].curr_mtx;

    Vertex v = pc.geometry.vertices.data[gl_VertexIndex];
    vec3 local = mesh.pos_min + (vec3(v.position) / 65535.0) * mesh.pos_extent;

    gl_Position = pc.camera.curr_view_proj * model * vec4(local, 1.0);
    v_draw_id = ref_idx;
}