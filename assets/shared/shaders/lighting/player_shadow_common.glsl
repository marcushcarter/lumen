#ifndef PLAYER_SHADOW_COMMON_GLSL
#define PLAYER_SHADOW_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint PLAYER_SHADOW_SLOT_SHIFT = 8u;
const uint PLAYER_SHADOW_LOCAL_MASK = (1u << PLAYER_SHADOW_SLOT_SHIFT) - 1u;

layout(buffer_reference, scalar) buffer PlayerShadowListBuffer {
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;
    uint cluster_count;
    uint index_total;
    uint valid_end;
    uint clusters[];
};

layout(buffer_reference, scalar) buffer PlayerShadowIndexBuffer { uint data[]; };

#endif
