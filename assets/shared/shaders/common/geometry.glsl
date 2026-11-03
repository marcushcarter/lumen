#ifndef GEOMETRY_GLSL
#define GEOMETRY_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#include "common/material.glsl"

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
    uint level;
};

struct Mesh {
    uint vertex_base;
    uint vertex_count;
    uint cluster_base;
    uint cluster_count;
    uint slot_count;
    uint skin_base;
    uint bvh_node_base;
    uint bvh_node_count;
    vec3 pos_min, pos_extent;
    vec2 uv_min, uv_extent;
    vec4 bounds_sphere;
};

layout(buffer_reference, scalar, buffer_reference_align = 8) readonly buffer VertexPositionBuffer { uvec2 data[]; };
layout(buffer_reference, scalar, buffer_reference_align = 8) readonly buffer VertexAttributeBuffer { uvec2 data[]; };
layout(buffer_reference, scalar) readonly buffer IndexBuffer { uint data[]; };
layout(buffer_reference, scalar) readonly buffer TriSlotBuffer { uint data[]; };
layout(buffer_reference, scalar) readonly buffer SlotTableBuffer { uint data[]; };
layout(buffer_reference, scalar) readonly buffer ClusterBuffer { Cluster data[]; };
layout(buffer_reference, scalar) readonly buffer ClusterGroupBuffer { ClusterGroup data[]; };
layout(buffer_reference, scalar, buffer_reference_align = 8) readonly buffer SkinVerticesBuffer { uvec2 data[]; };
layout(buffer_reference, scalar) readonly buffer BVHNodeBuffer { BVHNode data[]; };
layout(buffer_reference, scalar) readonly buffer MeshBuffer { Mesh data[]; };
layout(buffer_reference, scalar) readonly buffer UintBuffer { uint data[]; };
layout(buffer_reference, scalar, buffer_reference_align = 16) readonly buffer ClusterVertexBuffer { uvec4 data[]; };
layout(buffer_reference, scalar) readonly buffer ClusterBoundsBuffer { uvec3 data[]; };

const uint LOCAL_TRI_SLOT_OVERFLOW = 255u;
const uint CLUSTER_SLOT_MIXED = 0xFFFFFFFFu;

const uint SLOT_BIN_SHIFT = 29u;
const uint SLOT_MATERIAL_MASK = (1u << SLOT_BIN_SHIFT) - 1u;

layout(buffer_reference, scalar) readonly buffer GeometryBuffer {
    VertexPositionBuffer vertices;
    VertexAttributeBuffer attributes;
    IndexBuffer indices;
    TriSlotBuffer tri_slots;
    SlotTableBuffer slot_table;
    ClusterBuffer clusters;
    ClusterGroupBuffer groups;
    SkinVerticesBuffer skin_vertices;
    BVHNodeBuffer bvh_nodes;
    MeshBuffer meshes;
    UintBuffer meshlet_vertices;
    UintBuffer cluster_vertex_base;
    UintBuffer local_triangles;
    ClusterVertexBuffer cluster_vertices;
    MaterialBuffer materials;
    UintBuffer cluster_slots;
    ClusterBoundsBuffer cluster_bounds;
};

void cluster_box(GeometryBuffer p_geometry, Mesh p_mesh, uint p_cluster, out vec3 r_min, out vec3 r_extent) {
    const uvec3 q = p_geometry.cluster_bounds.data[p_cluster];
    const vec3 lo = vec3(q.x & 0xFFFFu, q.x >> 16u, q.y & 0xFFFFu) / 65535.0;
    const vec3 hi = vec3(q.y >> 16u, q.z & 0xFFFFu, q.z >> 16u) / 65535.0;
    r_min = p_mesh.pos_min + lo * p_mesh.pos_extent;
    r_extent = (hi - lo) * p_mesh.pos_extent;
}

uint slot_material(uint p_entry) { return p_entry & SLOT_MATERIAL_MASK; }
uint slot_bin(uint p_entry) { return p_entry >> SLOT_BIN_SHIFT; }

uint geometry_tri_slot(GeometryBuffer p_geometry, uint p_tri_index, uint p_lt) {
    const uint slot = p_lt >> 24u;
    return slot == LOCAL_TRI_SLOT_OVERFLOW ? p_geometry.tri_slots.data[p_tri_index] : slot;
}

uint geometry_slot_entry(GeometryBuffer p_geometry, uint p_slot_base, uint p_slot) {
    return p_slot_base == 0u ? 0u : p_geometry.slot_table.data[p_slot_base + p_slot];
}

vec3 vertex_position_unorm(uvec2 p_p) { return vec3(unpackUnorm2x16(p_p.x), unpackUnorm2x16(p_p.y).x); }
vec2 vertex_normal_oct(uvec2 p_a) { return unpackSnorm2x16(p_a.x); }
vec2 vertex_uv_unorm(uvec2 p_a) { return unpackUnorm2x16(p_a.y); }

bool ray_barycentrics(vec3 p_dir, vec3 p_0, vec3 p_1, vec3 p_2, out vec3 r_bary) {
    vec3 e1 = p_1 - p_0;
    vec3 e2 = p_2 - p_0;
    vec3 pv = cross(p_dir, e2);
    float det = dot(e1, pv);
    if (abs(det) < 1e-20) return false;
    float inv_det = 1.0 / det;
    vec3 tv = -p_0;
    float u = dot(tv, pv) * inv_det;
    vec3 qv = cross(tv, e1);
    float v = dot(p_dir, qv) * inv_det;
    r_bary = vec3(1.0 - u - v, u, v);
    return true;
}

vec3 oct_decode(vec2 p_f) {
    vec3 n = vec3(p_f, 1.0 - abs(p_f.x) - abs(p_f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

#endif