#ifndef SHADING_TILES_GLSL
#define SHADING_TILES_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint SHADING_TILE_SIZE = 8u;

const uint SHADING_TILE_SKY = 0u;
const uint SHADING_TILE_LIT = 1u;
const uint SHADING_TILE_MIXED = 2u;
const uint SHADING_TILE_TYPES = 3u;

const uint SHADING_PATH_SKY = 1u << 0u;
const uint SHADING_PATH_LIT = 1u << 1u;

const uint SHADING_TILE_DISPATCH_MAX = 65535u;

layout(buffer_reference, scalar) buffer ShadingTileBuffer {
    uvec4 args[SHADING_TILE_TYPES];
    uint done;
    uint capacity;
    uint _pad0;
    uint _pad1;
    uint tiles[];
};

uint shading_tile_type(uint p_paths) {
    if (p_paths == SHADING_PATH_SKY) return SHADING_TILE_SKY;
    if (p_paths == SHADING_PATH_LIT) return SHADING_TILE_LIT;
    return SHADING_TILE_MIXED;
}

uint shading_tile_pack(uvec2 p_tile) { return p_tile.x | (p_tile.y << 16u); }
uvec2 shading_tile_unpack(uint p_packed) { return uvec2(p_packed & 0xFFFFu, p_packed >> 16u); }

#endif
