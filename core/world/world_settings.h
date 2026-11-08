#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace lumen {

using namespace glm;

struct EnvironmentSettings
{
    static constexpr vec3 SUN_DIRECTION = vec3(-0.35f, -0.8f, -0.48f);
    static constexpr float SUN_ILLUMINANCE_LUX = 128000.0f;
    static constexpr float SUN_ANGULAR_RADIUS = 0.0046525f;

    float manual_ev100 = 15.0f;
};

}
