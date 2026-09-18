#ifndef GEOMETRY_GLSL
#define GEOMETRY_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

struct Vertex {
    u16vec3 position;
    i16vec2 normal;
    u16vec2 uv;
};

struct SkinVertex {
    u8vec4 joints;
    u8vec4 weights;
};

struct BVHNode {
    vec3 bounds_min;
    uint left;
    vec3 bounds_max;
    uint right;
};

struct Cluster {
    uint index_base;
    uint index_count;
    vec4 cull_sphere;
    uint self_group;
    uint parent_group;
};

struct ClusterGroup {
    vec4 sphere;
    float error;
    uint _pad;
};

struct Mesh {
    uint vertex_base;
    uint vertex_count;
    uint cluster_base;
    uint cluster_count;
    uint slot_table_base;
    uint slot_table_count;
    uint skin_base;
    uint bvh_node_base;
    uint bvh_node_count;
    vec3 pos_min, pos_extent;
    vec2 uv_min, uv_extent;
    vec4 bounds_sphere;
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer { Vertex data[]; };
layout(buffer_reference, scalar) readonly buffer IndexBuffer { uint data[]; };
layout(buffer_reference, scalar) readonly buffer TriSlotBuffer { uint data[]; };
layout(buffer_reference, scalar) readonly buffer SlotTableBuffer { uint data[]; };
layout(buffer_reference, scalar) readonly buffer ClusterBuffer { Cluster data[]; };
layout(buffer_reference, scalar) readonly buffer ClusterGroupBuffer { ClusterGroup data[]; };
layout(buffer_reference, scalar) readonly buffer SkinVerticesBuffer { SkinVertex data[]; };
layout(buffer_reference, scalar) readonly buffer BVHNodeBuffer { BVHNode data[]; };
layout(buffer_reference, scalar) readonly buffer MeshBuffer { Mesh data[]; };

layout(buffer_reference, scalar) readonly buffer GeometryBuffer {
    VertexBuffer vertices;
    IndexBuffer indices;
    TriSlotBuffer tri_slots;
    SlotTableBuffer slot_table;
    ClusterBuffer clusters;
    ClusterGroupBuffer groups;
    SkinVerticesBuffer skin_vertices;
    BVHNodeBuffer bvh_nodes;
    MeshBuffer meshes;
};

#endif