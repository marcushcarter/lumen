#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace lumen {

using namespace glm;

static constexpr uint32_t MAX_INSTANCES = 64u * 1024;
static constexpr uint32_t MAX_CLUSTER_REFS = (1u << 22) - 1;
static constexpr uint32_t MAX_LIGHTS = 16u * 1024;
static constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 2;

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
static constexpr uint32_t SHADING_TILE_UNLIT = 2u;
static constexpr uint32_t SHADING_TILE_MIXED = 3u;
static constexpr uint32_t SHADING_TILE_TYPES = 4u;
static constexpr uint32_t SHADING_TILES_HEADER_SIZE = SHADING_TILE_TYPES * 16u + 16u;
static constexpr uint32_t SHADING_TILES_ARGS_STRIDE = 16u;

inline uint32_t shading_tile_count(uint32_t p_width, uint32_t p_height)
{
    return ((p_width + SHADING_TILE_SIZE - 1) / SHADING_TILE_SIZE) * ((p_height + SHADING_TILE_SIZE - 1) / SHADING_TILE_SIZE);
}

struct GPUMaterial
{
    vec4 base_color;
    vec3 emissive;
    float alpha_cutoff;
    vec4 custom;
    vec4 uv_transform;
    float roughness;
    float metallic;
    float normal_scale;
    float ao_strength;
    uint32_t base_color_tex;
    uint32_t normal_tex;
    uint32_t roughness_tex;
    uint32_t metallic_tex;
    uint32_t ao_tex;
    uint32_t emissive_tex;
    uint32_t custom_tex;
    uint32_t flags;
};
static_assert(sizeof(GPUMaterial) == 112, "GPUMaterial layout changed");

static constexpr uint32_t LIGHT_IES_NONE = 0xFFFF;
static constexpr uint32_t LIGHT_SHADOW_NONE = 0xFFFFFFFF;

struct GpuLight
{
    vec3 position;
    float range;
    vec3 radiance;
    float source_radius;
    vec3 direction;
    uint32_t type_flags_ies;
    float cos_outer;
    float inv_cone_range;
    uint32_t shadow_id;
    uint32_t fade;
};

struct IndirectDispatch { uint32_t x, y, z; };

}
