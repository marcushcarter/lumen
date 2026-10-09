#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace lumen {
    
using namespace glm;

static constexpr uint32_t MAX_INSTANCES = 64u * 1024;
static constexpr uint32_t MAX_CLUSTER_REFS = (1u << 22) - 1;
static constexpr uint32_t MAX_LIGHTS = 16u * 1024;
static constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 2;
static constexpr uint32_t MAX_VISIBLE_LIGHTS = 4096;
static constexpr uint32_t LIGHT_ZBIN_COUNT = 1024;
static constexpr uint32_t LIGHT_TILE_SIZE = 8;
static constexpr uint32_t LIGHT_TILE_WORDS = MAX_VISIBLE_LIGHTS / 32;
static constexpr uint64_t LIGHT_CULL_ZBINS_OFFSET = 16 + 2 * LIGHT_ZBIN_COUNT * sizeof(uint32_t);
static constexpr uint64_t LIGHT_CULL_DATA_SIZE = LIGHT_CULL_ZBINS_OFFSET + LIGHT_ZBIN_COUNT * 2 * sizeof(uint32_t);


struct CameraUniform {
    mat4 prev_view_proj;
    mat4 curr_view_proj;
    vec4 position;
    vec4 frustum_planes[6];
    float near_z;
    float far_z;
    float tan_half_fov_y;
    mat4 inv_view_proj;
};

struct Instance {
    uint32_t mesh_id;
    uint32_t transform_id;
    uint32_t entity_id;
    uint32_t _pad0;
};

struct Transform {
    mat4 prev_mtx;
    mat4 curr_mtx;
};

static constexpr uint32_t LIGHT_IES_NONE = 0xFFFF;
static constexpr uint32_t LIGHT_SHADOW_NONE = 0xFFFFFFFF;

struct GpuLight {
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