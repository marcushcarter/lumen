#ifndef SHADING_TILES_GLSL
#define SHADING_TILES_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "common/gbuffer.glsl"

const uint SHADING_TILE_SIZE = 8u;

const uint SHADING_TILE_SKY = 0u;
const uint SHADING_TILE_LIT = 1u;
const uint SHADING_TILE_UNLIT = 2u;
const uint SHADING_TILE_MIXED = 3u;
const uint SHADING_TILE_TYPES = 4u;

const uint SHADING_PATH_SKY = 1u << 0u;
const uint SHADING_PATH_LIT = 1u << 1u;
const uint SHADING_PATH_UNLIT = 1u << 2u;

const uint SHADING_TILE_DISPATCH_MAX = 65535u;

layout(buffer_reference, scalar) buffer ShadingTileBuffer {
    uvec4 args[SHADING_TILE_TYPES];
    uint done;
    uint capacity;
    uint _pad0;
    uint _pad1;
    uint tiles[];
};

uint shading_path(uint p_model) {
    if (p_model == SHADING_MODEL_SKY) return SHADING_PATH_SKY;
    if (p_model == SHADING_MODEL_UNLIT) return SHADING_PATH_UNLIT;
    return SHADING_PATH_LIT;
}

uint shading_tile_type(uint p_paths) {
    if (p_paths == SHADING_PATH_SKY) return SHADING_TILE_SKY;
    if (p_paths == SHADING_PATH_LIT) return SHADING_TILE_LIT;
    if (p_paths == SHADING_PATH_UNLIT) return SHADING_TILE_UNLIT;
    return SHADING_TILE_MIXED;
}

uint shading_tile_pack(uvec2 p_tile) { return p_tile.x | (p_tile.y << 16u); }
uvec2 shading_tile_unpack(uint p_packed) { return uvec2(p_packed & 0xFFFFu, p_packed >> 16u); }

#endif