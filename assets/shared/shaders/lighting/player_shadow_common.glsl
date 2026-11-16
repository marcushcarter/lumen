#ifndef PLAYER_SHADOW_COMMON_GLSL
#define PLAYER_SHADOW_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint PLAYER_SHADOW_SLOT_SHIFT = 8u;
const uint PLAYER_SHADOW_LOCAL_MASK = (1u << PLAYER_SHADOW_SLOT_SHIFT) - 1u;
const uint PLAYER_SHADOW_DRAW_CULLED = 0u;
const uint PLAYER_SHADOW_DRAW_MASKED = 1u;
const uint PLAYER_SHADOW_OPAQUE = 0xFFFFFFFFu;

struct PlayerShadowDraw {
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;
};

layout(buffer_reference, scalar) buffer PlayerShadowListBuffer {
    PlayerShadowDraw draws[2];
    uint cluster_count;
    uint index_total;
    uint culled_total;
    uint masked_total;
    uvec3 skin_dispatch;
    uint pad0;
    uint pad1;
    uint pad2;
    uint clusters[];
};

layout(buffer_reference, scalar) buffer PlayerShadowIndexBuffer { uint data[]; };
layout(buffer_reference, scalar) buffer PlayerShadowMaterialBuffer { uint data[]; };

#endif
