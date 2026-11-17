#pragma once
#include <glm/glm.hpp>
#include <core/assets/guid.h>
#include <cstdint>

namespace lumen {

using namespace glm;

struct EnvironmentSettings
{
    static constexpr vec3 SUN_DIRECTION = vec3(-0.6f, -0.35f, -0.7f);
    static constexpr float SUN_ILLUMINANCE_LUX = 128000.0f;
    static constexpr float SUN_ANGULAR_RADIUS = 0.0046525f;

    float manual_ev100 = 15.0f;
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
    bool cloth = true;
    bool chains = true;

    bool operator==(const PhysicsSettings&) const = default;
};

struct TerrainSettings
{
    Guid terrain;

    bool operator==(const TerrainSettings&) const = default;
};

}
