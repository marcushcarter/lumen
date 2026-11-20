#pragma once
#include <core/assets/lterrain.h>
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <glm/glm.hpp>
#include <filesystem>
#include <vector>

namespace lumen {

using namespace glm;

struct TerrainSource
{
    static constexpr uint32_t NO_TILE = 0xFFFFFFFFu;

    Guid guid;
    std::filesystem::path path;
    LTerrainPayloadHeader header{};
    std::vector<uint16_t> bounds;
    std::vector<uint16_t> coarse;
    std::vector<LTerrainTileEntry> tiles;
    uint64_t lod_offsets[LTERRAIN_LOD_COUNT] = {};
    uint32_t bounds_level_base[LTERRAIN_LOD_COUNT] = {};

    Error open(Guid p_guid, const std::filesystem::path& p_path);
    void close();
    bool valid() const { return !tiles.empty(); }

    uint32_t tile_count() const { return header.tiles_x * header.tiles_y; }
    uint32_t tile_index(uint32_t p_x, uint32_t p_y) const { return p_y * header.tiles_x + p_x; }
    vec3 origin() const;
    float height_scale() const { return (header.height_max - header.height_min) / 65535.0f; }
    float height_meters(uint16_t p_value) const { return header.height_min + (float)p_value * height_scale(); }
    float coarse_height(float p_sample_x, float p_sample_y) const;

    void node_range(uint32_t p_tile, uint32_t p_level, uint32_t p_i, uint32_t p_j, float& r_min, float& r_max) const;
    bool read_lod(uint32_t p_tile, uint32_t p_lod, uint16_t* r_samples) const;
};

}
