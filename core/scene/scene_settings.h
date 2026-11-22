#pragma once
#include <glm/glm.hpp>
#include <core/assets/guid.h>
#include <cstdint>

namespace lumen {

using namespace glm;

struct EnvironmentSettings
{
    static constexpr vec3 SUN_DIRECTION = vec3(-0.6f, -0.35f, -0.7f);
    static constexpr vec3 SUN_COLOR = vec3(3.2f, 2.95f, 2.6f);
    static constexpr float SUN_ANGULAR_RADIUS = 0.0046525f;
    static constexpr float EXPOSURE = 0.5f;

    bool operator==(const EnvironmentSettings&) const = default;
};

struct ShadowSettings
{
    bool player_shadow = true;
    bool cascade0_shadow = true;
    bool cascade1_shadow = true;
    bool heightfield_shadow = true;

    bool operator==(const ShadowSettings&) const = default;
};

struct PhysicsSettings
{
    float gravity = 9.81f;

    bool operator==(const PhysicsSettings&) const = default;
};

struct TerrainSettings
{
    Guid terrain;

    bool operator==(const TerrainSettings&) const = default;
};

struct WaterSettings
{
    bool simulate = true;
    bool render = true;
    Guid bake;
    float bake_time = 120.0f;
    float cell_size = 0.25f;
    uint32_t substeps = 2;
    float sim_radius = 48.0f;
    float vorticity = 1.6f;
    float cfl_limit = 0.45f;
    float flux_depth_cap = 1.0f;
    float foam_decay = 0.9f;
    float foam_gain = 2.0f;
    float bed_probe = 10.0f;
    bool caustics = true;
    bool push_water = true;

    bool active() const { return simulate || render; }

    bool operator==(const WaterSettings&) const = default;
};

}
