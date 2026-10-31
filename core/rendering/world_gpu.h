#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace lumen {

using namespace glm;

static constexpr uint32_t MAX_INSTANCES = 64u * 1024;
static constexpr uint32_t MAX_CLUSTER_REFS = (1u << 22) - 1;

struct CameraUniform
{
    mat4 prev_view_proj;
    mat4 curr_view_proj;
    vec4 position;
    vec4 frustum_planes[6];
    float near_z;
    float far_z;
    float tan_half_fov_y;
    float pre_exposure;
    mat4 inv_view_proj;
    vec4 jitter;
};

struct Instance
{
    uint32_t mesh_id;
    uint32_t transform_id;
    uint32_t entity_id;
    uint32_t slot_base;
};

struct Transform
{
    mat4 prev_mtx;
    mat4 curr_mtx;
};

static constexpr uint32_t TEXTURE_NONE = 0xFFFFFFFFu;
static constexpr uint32_t MATERIAL_DEFAULT = 0u;
static constexpr uint32_t MATERIAL_ALPHA_SHIFT = 8u;
static constexpr uint32_t MATERIAL_FLAG_TWO_SIDED = 1u << 10;

static constexpr uint32_t RASTER_BIN_OPAQUE = 0u;
static constexpr uint32_t RASTER_BIN_TWO_SIDED = 1u;
static constexpr uint32_t RASTER_BIN_MASKED = 2u;
static constexpr uint32_t RASTER_BIN_COUNT = 4u;
static constexpr uint32_t SLOT_BIN_SHIFT = 29u;
static constexpr uint32_t SLOT_MATERIAL_MASK = (1u << SLOT_BIN_SHIFT) - 1u;

static constexpr uint32_t SHADING_TILE_SIZE = 8u;
static constexpr uint32_t SHADING_TILE_SKY = 0u;
static constexpr uint32_t SHADING_TILE_LIT = 1u;
static constexpr uint32_t SHADING_TILE_MIXED = 2u;
static constexpr uint32_t SHADING_TILE_TYPES = 3u;
static constexpr uint32_t SHADING_TILES_HEADER_SIZE = SHADING_TILE_TYPES * 16u + 16u;
static constexpr uint32_t SHADING_TILES_ARGS_STRIDE = 16u;

inline uint32_t shading_tile_count(uint32_t p_width, uint32_t p_height)
{
    return ((p_width + SHADING_TILE_SIZE - 1) / SHADING_TILE_SIZE) * ((p_height + SHADING_TILE_SIZE - 1) / SHADING_TILE_SIZE);
}

struct GPUMaterial
{
    uint32_t mask_tex;
    float alpha_cutoff;
    uint32_t flags;
};
static_assert(sizeof(GPUMaterial) == 12, "GPUMaterial layout changed");

struct IndirectDispatch { uint32_t x, y, z; };

}
