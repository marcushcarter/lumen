#ifndef OUTLINE_MASK_COMMON_GLSL
#define OUTLINE_MASK_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint OUTLINE_MASK_SLOT_SHIFT = 8u;
const uint OUTLINE_MASK_LOCAL_MASK = (1u << OUTLINE_MASK_SLOT_SHIFT) - 1u;
const uint OUTLINE_MASK_OPAQUE = 0xFFFFFFFFu;

struct OutlineMaskInstance {
    uint instance;
    uint mesh;
    uint slot_base;
    float lod_density;
};

struct OutlineMaskDraw {
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;
};

layout(buffer_reference, scalar) readonly buffer OutlineMaskInstanceBuffer { uint count; uint _pad[3]; OutlineMaskInstance data[]; };
layout(buffer_reference, scalar) buffer OutlineMaskListBuffer { OutlineMaskDraw draw; uint cluster_count; uint _pad[2]; uvec2 clusters[]; };
layout(buffer_reference, scalar) buffer OutlineMaskIndexBuffer { uint data[]; };
layout(buffer_reference, scalar) buffer OutlineMaskMaterialBuffer { uint data[]; };

#endif
