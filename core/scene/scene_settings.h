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

}
