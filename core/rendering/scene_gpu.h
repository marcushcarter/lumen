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
static constexpr uint32_t INSTANCE_FLAG_PLAYER = 1u << 0;
static constexpr uint32_t PLAYER_SHADOW_SIZE = 1024;
static constexpr uint32_t PLAYER_SHADOW_MAX_CLUSTERS = 16u * 1024;
static constexpr uint32_t PLAYER_SHADOW_MAX_TRIANGLES = 768u * 1024;
static constexpr uint32_t PLAYER_SHADOW_MAX_INDICES = PLAYER_SHADOW_MAX_TRIANGLES * 3u;
static constexpr uint32_t PLAYER_SHADOW_SLOT_SHIFT = 8u;
static constexpr uint32_t SUN_CASCADE_COUNT = 2;
static constexpr uint32_t SUN_CASCADE_SIZE = 2048;
static constexpr uint32_t SUN_HEIGHTFIELD_SIZE = 1024;
static constexpr uint32_t SUN_CASCADE_MAX_CLUSTERS = 128u * 1024;
static constexpr uint32_t SUN_CASCADE_MAX_PLANES = 16;
static constexpr uint32_t SUN_RECEIVER_SIZE = 128;
static constexpr uint32_t SUN_RECEIVER_LEVELS = 8;
static constexpr uint32_t SUN_RECEIVER_CELLS = (SUN_RECEIVER_SIZE * SUN_RECEIVER_SIZE * 4 - 1) / 3;

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
    float _pad;
    mat4 player_shadow_view_proj = mat4(1.0f);
    mat4 player_shadow_screen = mat4(1.0f);
    vec4 player_shadow_params = vec4(0.0f);
    uvec4 player_shadow_rect = uvec4(0u);
    mat4 cascade_view_proj[SUN_CASCADE_COUNT] = { mat4(1.0f), mat4(1.0f) };
    vec4 cascade_params[SUN_CASCADE_COUNT] = {};
    vec4 cascade_splits = vec4(0.0f);
    vec4 cascade_planes[SUN_CASCADE_COUNT][SUN_CASCADE_MAX_PLANES] = {};
    uvec4 cascade_plane_count = uvec4(0u);
};

static_assert(sizeof(DirectLight) == 896, "DirectLight layout changed");

struct Instance
{
    uint32_t mesh_id;
    uint32_t transform_id;
    uint32_t entity_id;
    uint32_t slot_base;
    uint32_t skin_id = SKIN_NONE;
    uint32_t flags = 0;
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

static constexpr uint32_t TERRAIN_PATCH_QUADS = 16;
static constexpr uint32_t TERRAIN_QUARTER_QUADS = 8;
static constexpr uint32_t TERRAIN_QUARTER_VERTS = (TERRAIN_QUARTER_QUADS + 1) * (TERRAIN_QUARTER_QUADS + 1);
static constexpr uint32_t TERRAIN_QUARTER_INDICES = TERRAIN_QUARTER_QUADS * TERRAIN_QUARTER_QUADS * 6;
static constexpr uint32_t TERRAIN_LEVELS = 5;
static constexpr uint32_t TERRAIN_COARSE_LEVEL = TERRAIN_LEVELS - 1;
static constexpr uint32_t TERRAIN_MAX_PATCHES = 16u * 1024;
static constexpr uint32_t TERRAIN_FINE_SLOTS = 32;
static constexpr uint32_t TERRAIN_SMALL_SLOTS = 256;
static constexpr uint32_t TERRAIN_LOD_NONE = 0xFFFFFFFFu;

struct TerrainPatch
{
    uint32_t origin_x;
    uint32_t origin_z;
    uint32_t tile;
    uint32_t packed;
};
static_assert(sizeof(TerrainPatch) == 16, "TerrainPatch layout changed");

struct TerrainTileGpu
{
    uint32_t word_base;
    uint32_t lod;
};
static_assert(sizeof(TerrainTileGpu) == 8, "TerrainTileGpu layout changed");

struct TerrainUniform
{
    vec4 origin_spacing;
    vec4 morph_camera;
    vec4 morph[TERRAIN_LEVELS];
    float height_min;
    float height_scale;
    uint32_t tiles_x;
    uint32_t tiles_y;
    uint32_t samples_x;
    uint32_t samples_y;
    uint32_t coarse_w;
    uint32_t coarse_h;
    uint64_t heights;
    uint64_t tile_table;
    uint64_t patches;
    uint64_t _pad;
};
static_assert(sizeof(TerrainUniform) == 176, "TerrainUniform layout changed");

}
