#ifndef GEOMETRY_GLSL
#define GEOMETRY_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#include "common/material.glsl"

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
    uint cluster_base;
    uint cluster_count;
    uint slot_count;
    uint node_base;
    uint node_count;
    uint page_base;
    uint page_count;
    uint flags;
    vec3 pos_min, pos_extent;
    vec2 uv_min, uv_extent;
    vec4 bounds_sphere;
};

struct Node {
    vec4 cull_sphere;
    vec4 lod_sphere;
    float max_parent_error;
    uint first;
    uint count;
    uint page;
};

const uint NODE_INNER = 0xFFFFFFFFu;

layout(buffer_reference, scalar) readonly buffer NodeBuffer { Node data[]; };
layout(buffer_reference, scalar) readonly buffer PageResidentBuffer { uint data[]; };
layout(buffer_reference, scalar) buffer PageStampBuffer { uint data[]; };
layout(buffer_reference, scalar) readonly buffer GroupRefinedBuffer { uint data[]; };
layout(buffer_reference, scalar) buffer StreamRequestBuffer { uint count; uint _pad; uvec2 data[]; };

const uint MESH_FLAG_STREAMED = 1u;

const uint PAGE_ABSENT = 0u;
const uint PAGE_STREAMED = 1u;
const uint PAGE_PERMANENT = 2u;
const uint STREAM_REQUEST_RESIDENT_BIT = 0x80000000u;
layout(buffer_reference, scalar) readonly buffer TriSlotBuffer { uint data[]; };
layout(buffer_reference, scalar) readonly buffer SlotTableBuffer { uint data[]; };
layout(buffer_reference, scalar) readonly buffer ClusterBuffer { Cluster data[]; };
layout(buffer_reference, scalar) readonly buffer ClusterGroupBuffer { ClusterGroup data[]; };
struct ClusterStreams {
    uint position;
    uint attrib;
    uint skin;
    uint pos_lo_xy;
    uint pos_lo_z_count;
    uint uv_lo;
    uint bits;
    uint skin_format;
};

layout(buffer_reference, scalar) readonly buffer MeshBuffer { Mesh data[]; };
layout(buffer_reference, scalar) readonly buffer UintBuffer { uint data[]; };
layout(buffer_reference, scalar) readonly buffer StreamBuffer { uint data[]; };
layout(buffer_reference, scalar, buffer_reference_align = 16) readonly buffer ClusterStreamsBuffer { ClusterStreams data[]; };
layout(buffer_reference, scalar) readonly buffer ClusterBoundsBuffer { uvec3 data[]; };

const uint LOCAL_TRI_SLOT_OVERFLOW = 255u;
const uint CLUSTER_SLOT_MIXED = 0xFFFFFFFFu;

const uint SLOT_BIN_SHIFT = 29u;
const uint SLOT_MATERIAL_MASK = (1u << SLOT_BIN_SHIFT) - 1u;

layout(buffer_reference, scalar) readonly buffer GeometryBuffer {
    TriSlotBuffer tri_slots;
    SlotTableBuffer slot_table;
    ClusterBuffer clusters;
    ClusterGroupBuffer groups;
    StreamBuffer skin;
    MeshBuffer meshes;
    ClusterStreamsBuffer cluster_streams;
    UintBuffer local_triangles;
    StreamBuffer positions;
    StreamBuffer attributes;
    MaterialBuffer materials;
    UintBuffer cluster_slots;
    ClusterBoundsBuffer cluster_bounds;
    UintBuffer cluster_cones;
    NodeBuffer nodes;
    PageResidentBuffer page_resident;
    PageStampBuffer page_stamp;
    GroupRefinedBuffer group_refined;
    StreamRequestBuffer stream_requests;
    uint stream_frame;
    uint stream_request_capacity;
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

const uint STREAM_NORMAL_BITS = 12u;
const uint STREAM_WEIGHT_BITS = 8u;
const float STREAM_GRID_SCALE = 1.0 / 65535.0;
const float STREAM_NORMAL_SCALE = 2.0 / 4095.0;

const uint STREAM_POSITIONS = 0u;
const uint STREAM_ATTRIBUTES = 1u;
const uint STREAM_SKIN = 2u;

uvec3 stream_window(GeometryBuffer p_geometry, uint p_stream, uint p_word) {
    if (p_stream == STREAM_POSITIONS) return uvec3(p_geometry.positions.data[p_word], p_geometry.positions.data[p_word + 1u], p_geometry.positions.data[p_word + 2u]);
    if (p_stream == STREAM_ATTRIBUTES) return uvec3(p_geometry.attributes.data[p_word], p_geometry.attributes.data[p_word + 1u], p_geometry.attributes.data[p_word + 2u]);
    return uvec3(p_geometry.skin.data[p_word], p_geometry.skin.data[p_word + 1u], p_geometry.skin.data[p_word + 2u]);
}

uint window_bits(uvec3 p_window, uint p_offset, uint p_count) {
    if (p_count == 0u) return 0u;
    const uint i = p_offset >> 5u;
    const uint s = p_offset & 31u;
    const uint lo = i == 0u ? p_window.x : (i == 1u ? p_window.y : p_window.z);
    const uint hi = i == 0u ? p_window.y : p_window.z;
    uint v = lo >> s;
    if (s != 0u) v |= hi << (32u - s);
    return bitfieldExtract(v, 0, int(p_count));
}

ClusterStreams cluster_streams(GeometryBuffer p_geometry, uint p_cluster) {
    return p_geometry.cluster_streams.data[p_cluster];
}

uint cluster_vertex_count(ClusterStreams p_streams) {
    return p_streams.pos_lo_z_count >> 16u;
}

vec3 stream_position(GeometryBuffer p_geometry, ClusterStreams p_streams, uint p_local, vec3 p_pos_min, vec3 p_pos_extent) {
    const uint bx = p_streams.bits & 31u;
    const uint by = (p_streams.bits >> 5u) & 31u;
    const uint bz = (p_streams.bits >> 10u) & 31u;
    const uint bit = p_local * (bx + by + bz);
    const uvec3 w = stream_window(p_geometry, STREAM_POSITIONS, p_streams.position + (bit >> 5u));
    const uint o = bit & 31u;
    const uvec3 lo = uvec3(p_streams.pos_lo_xy & 0xFFFFu, p_streams.pos_lo_xy >> 16u, p_streams.pos_lo_z_count & 0xFFFFu);
    const uvec3 q = lo + uvec3(window_bits(w, o, bx), window_bits(w, o + bx, by), window_bits(w, o + bx + by, bz));
    return p_pos_min + vec3(q) * STREAM_GRID_SCALE * p_pos_extent;
}

uint stream_attribute_bit(ClusterStreams p_streams, uint p_local) {
    const uint bu = (p_streams.bits >> 15u) & 31u;
    const uint bv = (p_streams.bits >> 20u) & 31u;
    return p_local * (2u * STREAM_NORMAL_BITS + bu + bv);
}

vec2 stream_normal_oct(GeometryBuffer p_geometry, ClusterStreams p_streams, uint p_local) {
    const uint bit = stream_attribute_bit(p_streams, p_local);
    const uint w = p_streams.attrib + (bit >> 5u);
    const uvec2 pair = uvec2(p_geometry.attributes.data[w], p_geometry.attributes.data[w + 1u]);
    const uint o = bit & 31u;
    const uint v = o == 0u ? pair.x : (pair.x >> o) | (pair.y << (32u - o));
    const uint nx = bitfieldExtract(v, 0, int(STREAM_NORMAL_BITS));
    const uint ny = bitfieldExtract(v, int(STREAM_NORMAL_BITS), int(STREAM_NORMAL_BITS));
    return vec2(nx, ny) * STREAM_NORMAL_SCALE - 1.0;
}

vec2 stream_uv(GeometryBuffer p_geometry, ClusterStreams p_streams, uint p_local, vec2 p_uv_min, vec2 p_uv_extent) {
    const uint bu = (p_streams.bits >> 15u) & 31u;
    const uint bv = (p_streams.bits >> 20u) & 31u;
    const uint bit = stream_attribute_bit(p_streams, p_local) + 2u * STREAM_NORMAL_BITS;
    const uvec3 w = stream_window(p_geometry, STREAM_ATTRIBUTES, p_streams.attrib + (bit >> 5u));
    const uint o = bit & 31u;
    const uvec2 q = uvec2(p_streams.uv_lo & 0xFFFFu, p_streams.uv_lo >> 16u) + uvec2(window_bits(w, o, bu), window_bits(w, o + bu, bv));
    return p_uv_min + vec2(q) * STREAM_GRID_SCALE * p_uv_extent;
}

void stream_skin(GeometryBuffer p_geometry, ClusterStreams p_streams, uint p_local, out uvec4 r_joints, out vec4 r_weights) {
    const uint influences = p_streams.skin_format & 7u;
    const uint joint_bits = (p_streams.skin_format >> 3u) & 15u;
    const uint joint_lo = (p_streams.skin_format >> 8u) & 0xFFu;
    r_joints = uvec4(joint_lo);
    r_weights = vec4(0.0);
    if (influences == 0u) {
        r_weights.x = 1.0;
        return;
    }
    const uint bit = p_local * (influences * joint_bits + (influences - 1u) * STREAM_WEIGHT_BITS);
    const uvec3 w = stream_window(p_geometry, STREAM_SKIN, p_streams.skin + (bit >> 5u));
    const uint o = bit & 31u;
    for (uint k = 0u; k < influences; k++) r_joints[k] = joint_lo + window_bits(w, o + k * joint_bits, joint_bits);
    const uint weight_bit = o + influences * joint_bits;
    uint rest = 255u;
    for (uint k = 0u; k + 1u < influences; k++) {
        const uint wk = window_bits(w, weight_bit + k * STREAM_WEIGHT_BITS, STREAM_WEIGHT_BITS);
        r_weights[k] = float(wk) / 255.0;
        rest -= wk;
    }
    r_weights[influences - 1u] = float(rest) / 255.0;
}

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

vec3 oct_decode_unnormalized(vec2 p_f) {
    vec3 n = vec3(p_f, 1.0 - abs(p_f.x) - abs(p_f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return n;
}

vec2 oct_encode(vec3 p_n) {
    vec2 p = p_n.xy * (1.0 / (abs(p_n.x) + abs(p_n.y) + abs(p_n.z)));
    if (p_n.z < 0.0) p = (1.0 - abs(p.yx)) * vec2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    return p;
}

vec3 oct_decode(vec2 p_f) {
    vec3 n = vec3(p_f, 1.0 - abs(p_f.x) - abs(p_f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

#endif