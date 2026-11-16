#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <filesystem>
#include <string>
#include <type_traits>
#include <cstdint>

namespace lumen {

static constexpr uint32_t LTERRAIN_FORMAT_VERSION = 1;
static constexpr uint32_t LTERRAIN_TILE_QUADS = 256;
static constexpr uint32_t LTERRAIN_APRON = 1;
static constexpr uint32_t LTERRAIN_LOD_COUNT = 6;
static constexpr uint32_t LTERRAIN_COARSE_STEP = 16;
static constexpr uint32_t LTERRAIN_PAGE_ALIGN = 4096;
static constexpr float LTERRAIN_DEFAULT_SPACING = 1.0f;
static constexpr float LTERRAIN_DEFAULT_HEIGHT_MIN = 0.0f;
static constexpr float LTERRAIN_DEFAULT_HEIGHT_MAX = 512.0f;

constexpr uint32_t lterrain_lod_quads(uint32_t p_lod) { return LTERRAIN_TILE_QUADS >> p_lod; }
constexpr uint32_t lterrain_lod_samples(uint32_t p_lod) { return lterrain_lod_quads(p_lod) + 1 + 2 * LTERRAIN_APRON; }
constexpr uint32_t lterrain_bounds_nodes()
{
    uint32_t n = 0;
    for (uint32_t l = 0; l < LTERRAIN_LOD_COUNT; l++) n += (1u << l) * (1u << l);
    return n;
}

struct LTerrainPayloadHeader
{
    uint32_t format_version;
    uint32_t tile_quads;
    uint32_t apron;
    uint32_t lod_count;
    uint32_t tiles_x;
    uint32_t tiles_y;
    uint32_t samples_x;
    uint32_t samples_y;
    float spacing;
    float height_min;
    float height_max;
    uint32_t layer_count;
    uint32_t coarse_step;
    uint32_t coarse_w;
    uint32_t coarse_h;
    uint32_t bounds_nodes;
    uint64_t bounds_offset;
    uint64_t bounds_size;
    uint64_t coarse_offset;
    uint64_t coarse_size;
    uint64_t directory_offset;
    uint64_t directory_size;
};
static_assert(sizeof(LTerrainPayloadHeader) == 112, "LTerrainPayloadHeader layout changed");
static_assert(std::is_trivially_copyable_v<LTerrainPayloadHeader>, "must be blittable");

struct LTerrainTileEntry
{
    uint64_t offset;
    uint32_t height_size;
    uint32_t weights_size;
    uint32_t holes_size;
    uint32_t _pad;
};
static_assert(sizeof(LTerrainTileEntry) == 24, "LTerrainTileEntry layout changed");
static_assert(std::is_trivially_copyable_v<LTerrainTileEntry>, "must be blittable");

struct LTerrain
{
    Guid guid;
    std::string source;
    uint32_t source_width = 0;
    uint32_t source_height = 0;
    uint32_t source_bits = 0;
    uint32_t samples_x = 0;
    uint32_t samples_y = 0;
    uint32_t tiles_x = 0;
    uint32_t tiles_y = 0;
    float spacing = LTERRAIN_DEFAULT_SPACING;
    float height_min = LTERRAIN_DEFAULT_HEIGHT_MIN;
    float height_max = LTERRAIN_DEFAULT_HEIGHT_MAX;

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;

    bool operator==(const LTerrain&) const = default;
};

}
