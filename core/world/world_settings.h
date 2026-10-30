#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace lumen {

using namespace glm;

// Values are mirrored as SKY_MODE_* in shaders/common/atmosphere.glsl.
enum class SkyMode : uint8_t { NONE = 0, SOLID_COLOR = 1, ATMOSPHERE = 2 };

struct AtmosphereSettings
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
    static constexpr float MIE_BASE_SCATTERING = 3.996e-3f;

    static constexpr float STRAT_AEROSOL_WIDTH_KM = 12.0f;
    static constexpr float STRAT_AEROSOL_ANGSTROM = 1.2f;
    static constexpr float STRAT_AEROSOL_G = 0.7f;

    float mie_turbidity = 1.0f;
    float mie_height_km = 1.2f;
    float mie_g = 0.8f;
    float mie_albedo = 0.9f;
    float mie_angstrom = 1.3f;
    float strat_aerosol_depth = 0.01f;
    float strat_aerosol_height_km = 20.0f;
    float sea_level_offset_km = 0.0f;
};

struct CelestialSettings
{
    static constexpr float SUN_ILLUMINANCE_LUX = 128000.0f;
    static constexpr float SUN_ANGULAR_RADIUS = 0.0046525f;
    static constexpr float MOON_ANGULAR_RADIUS = 0.0045197f;
    static constexpr float MOON_DISK_BRIGHTNESS = 1.0f;
    static constexpr uint32_t STAR_GRID_CELLS = 256;

    static constexpr double LATITUDE_DEG = 55.0;
    static constexpr double SUN_DECLINATION_DEG = 10.0;
    static constexpr double MOON_OFFSET_DEG = 5.0;

    float time_of_day = 12.0f;
    float star_brightness = 1.0f;
    float star_density = 0.8f;
};

struct EnvironmentSettings
{
    SkyMode sky_mode = SkyMode::ATMOSPHERE;
    vec3 background_color = vec3(0.5f);
    float background_intensity = 1000.0f;
    // Used when there is no sun to derive exposure from. Moves to the view's exposure settings once those exist.
    float manual_ev100 = 10.0f;

    AtmosphereSettings atmosphere;
    CelestialSettings celestial;
};

struct LensFlareSettings
{
    float intensity = 1.0f;
    float glow = 0.5f;
    float rays = 1.0f;
    float ray_length = 0.06f;
    float ghosts = 0.1f;
};

// World-level until per-camera/view post settings exist.
struct PostProcessSettings
{
    LensFlareSettings lens_flare;
};

}
