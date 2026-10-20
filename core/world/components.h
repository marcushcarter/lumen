#pragma once
#include <core/assets/guid.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace lumen {

using namespace glm;

struct EntityIdComponent { Guid guid; };

struct NameComponent {
    static constexpr uint32_t MAX_LENGTH = 64;
    char name[MAX_LENGTH] = {};
};

struct TransformComponent {
    vec3 position = vec3(0.0f);
    quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
    vec3 scale = vec3(1.0f);
};

struct ModelComponent {
    Guid model;
};

struct ModelGridComponent {
    Guid model;
    uvec3 count = uvec3(10, 1, 10);
    float spacing = 0.0f;
};

enum class LightType : uint8_t { POINT = 1, SPOT = 2 };
struct LightComponent {
    static constexpr uint8_t FLAG_CAST_SHADOWS = 1 << 0;
    static constexpr uint8_t FLAG_AFFECTS_GI = 1 << 1;

    LightType type = LightType::POINT;
    uint8_t flags = FLAG_CAST_SHADOWS | FLAG_AFFECTS_GI;
    vec3 color = vec3(1.0f);
    float intensity = 1.0f;
    float range = 10.0f;
    float source_radius = 0.0f;
    float inner_cone_angle = 20.0f;
    float outer_cone_angle = 30.0f;
    float fade_start = 0.0f;
    float fade_end = 0.0f;

    float shadow_bias = 1.0f;
    float shadow_normal_bias = 1.0f;
};

struct LightProfileComponent { Guid ies; };

struct AtmosphereComponent
{
    static constexpr float PLANET_RADIUS_KM = 6360.0f;
    static constexpr float ATMOSPHERE_HEIGHT_KM = 100.0f;
    static constexpr vec3 RAYLEIGH_SCATTERING = vec3(5.802e-3f, 13.558e-3f, 33.1e-3f);
    static constexpr float RAYLEIGH_HEIGHT_KM = 8.0f;
    static constexpr vec3 OZONE_ABSORPTION = vec3(0.650e-3f, 1.881e-3f, 0.085e-3f);
    static constexpr float OZONE_CENTER_KM = 25.0f;
    static constexpr float OZONE_WIDTH_KM = 30.0f;
    static constexpr vec3 GROUND_ALBEDO = vec3(0.3f);
    static constexpr float NIGHT_LUMINANCE = 0.00025f;
    static constexpr float MOON_DISK_BRIGHTNESS = 1.0f;
    static constexpr uint32_t STAR_GRID_CELLS = 256;

    static constexpr float SUN_ILLUMINANCE_LUX = 128000.0f;
    static constexpr float SUN_ANGULAR_RADIUS = 0.0046525f;
    static constexpr float MOON_ANGULAR_RADIUS = 0.0045197f;
    static constexpr float MIE_BASE_SCATTERING = 3.996e-3f;

    static constexpr float STRAT_AEROSOL_WIDTH_KM = 12.0f;
    static constexpr float STRAT_AEROSOL_ANGSTROM = 1.2f;
    static constexpr float STRAT_AEROSOL_G = 0.7f;

    static constexpr double LATITUDE_DEG = 55.0;
    static constexpr double SUN_DECLINATION_DEG = 10.0;
    static constexpr double MOON_OFFSET_DEG = 5.0;

    float time_of_day = 12.0f;
    float mie_turbidity = 1.0f;
    float mie_height_km = 1.2f;
    float mie_g = 0.8f;
    float mie_albedo = 0.9f;
    float mie_angstrom = 1.3f;
    float strat_aerosol_depth = 0.01f;
    float strat_aerosol_height_km = 20.0f;
    float star_brightness = 1.0f;
    float star_density = 0.8f;
    float sea_level_offset_km = 0.0f;

    float flare_intensity = 1.0f;
    float flare_glow = 0.5f;
    float flare_rays = 1.0f;
    float flare_ray_length = 0.06f;
    float flare_ghosts = 0.1f;
};

struct StaticTag {};
struct EditorHiddenTag {};

struct EditorFolderComponent {
    static constexpr uint32_t ROOT = 0xFFFFFFFF;
    uint32_t folder = ROOT;
};

}