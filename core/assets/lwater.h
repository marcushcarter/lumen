#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <filesystem>
#include <string>
#include <type_traits>
#include <cstdint>

namespace lumen {

static constexpr uint32_t LWATER_FORMAT_VERSION = 2;

struct LWaterPayloadHeader
{
    uint32_t format_version;
    uint32_t tile_count;
    float cell_size;
    uint32_t _pad;
    uint64_t directory_offset;
    uint64_t state_offset;
};
static_assert(sizeof(LWaterPayloadHeader) == 32, "LWaterPayloadHeader layout changed");
static_assert(std::is_trivially_copyable_v<LWaterPayloadHeader>, "must be blittable");

struct LWaterTileEntry
{
    int32_t tile_x;
    int32_t tile_y;
    float ymin;
    float ymax;
    uint32_t has_surface;
    uint32_t _pad;
    uint64_t state_offset;
};
static_assert(sizeof(LWaterTileEntry) == 32, "LWaterTileEntry layout changed");
static_assert(std::is_trivially_copyable_v<LWaterTileEntry>, "must be blittable");

struct LWater
{
    Guid guid;
    Guid terrain;
    float cell_size = 0.0f;
    uint32_t tiles = 0;
    float wet_area = 0.0f;
    float bake_seconds = 0.0f;
    uint64_t file_bytes = 0;
    std::string baked_at;

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;
};

}
