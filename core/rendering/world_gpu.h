#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace lumen {

using namespace glm;

static constexpr uint32_t MAX_INSTANCES = 64u * 1024;
static constexpr uint32_t MAX_CLUSTER_REFS = (1u << 22) - 1;
static constexpr uint32_t MAX_SKINNED_INSTANCES = 4u * 1024;
static constexpr uint32_t MAX_SKIN_JOINTS = 64u * 1024;
static constexpr uint32_t SKIN_NONE = 0xFFFFFFFFu;
static constexpr uint32_t SHADOW_CASCADES = 2;
static constexpr uint32_t SHADOW_RESOLUTIONS[SHADOW_CASCADES] = { 2048, 2048 };
static constexpr float SHADOW_SPLITS[SHADOW_CASCADES] = { 8.0f, 80.0f };
static constexpr float SHADOW_CASTER_REACH = 300.0f;
static constexpr float SHADOW_SOFTNESS = 0.04f;

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
    uint64_t skins;
    uint64_t joints;
};

struct DirectLight
{
    vec3 direction;
    float angular_radius;
    vec3 illuminance;
    uint32_t cascade_count;
    mat4 cascade_view_proj[SHADOW_CASCADES];
    vec4 cascade_splits;
    vec4 cascade_texel;
};
static_assert(sizeof(DirectLight) == 64 + 64 * SHADOW_CASCADES, "DirectLight layout changed");

struct Instance
{
    uint32_t mesh_id;
    uint32_t transform_id;
    uint32_t entity_id;
    uint32_t slot_base;
    uint32_t skin_id = SKIN_NONE;
};

struct SkinInstance
{
    vec3 box_min;
    uint32_t joint_base;
    vec3 box_extent;
    uint32_t joint_count;
};
static_assert(sizeof(SkinInstance) == 32, "SkinInstance layout changed");

struct JointMatrices
{
    vec4 curr[3];
    vec4 prev[3];
};
static_assert(sizeof(JointMatrices) == 96, "JointMatrices layout changed");

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
