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
static constexpr uint32_t LIGHT_TILE_MIN_LIGHTS = 32;
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
    float pre_exposure;
    mat4 inv_view_proj;
};

struct Instance {
    uint32_t mesh_id;
    uint32_t transform_id;
    uint32_t entity_id;
    uint32_t slot_base;
};

struct Transform {
    mat4 prev_mtx;
    mat4 curr_mtx;
};

static constexpr uint32_t TEXTURE_NONE = 0xFFFFFFFFu;
static constexpr uint32_t MATERIAL_DEFAULT = 0u;
static constexpr uint32_t MATERIAL_ALPHA_SHIFT = 8u;
static constexpr uint32_t MATERIAL_FLAG_TWO_SIDED = 1u << 10;

struct GPUMaterial {
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

struct GpuAtmosphere {
    mat4 world_to_celestial;
    vec3 rayleigh_scattering;
    float rayleigh_density_scale;
    vec3 mie_scattering;
    float mie_density_scale;
    vec3 mie_extinction;
    float mie_g;
    vec3 ozone_absorption;
    float ozone_center_km;
    vec3 ground_albedo;
    float ozone_width_km;
    vec3 sun_direction;
    float sun_cos_angular_radius;
    vec3 sun_illuminance;
    float bottom_radius_km;
    vec3 moon_direction;
    float moon_cos_angular_radius;
    vec3 moon_illuminance;
    float top_radius_km;
    vec3 moon_up;
    float moon_phase_angle;
    vec3 observer_km;
    float altitude_km;
    float night_luminance;
    float star_brightness;
    float star_density;
    float moon_disk_brightness;
    vec3 strat_scattering;
    float strat_center_km;
    float strat_width_km;
    float strat_g;
    float sun_angular_radius;
    float moon_angular_radius;
    float skyview_zenith_horizon;
    float skyview_beta;
    float horizon_mu;
};

struct IndirectDispatch { uint32_t x, y, z; };

}