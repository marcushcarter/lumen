#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace lumen {

using namespace glm;

struct EnvironmentSettings
{
    static constexpr vec3 SUN_DIRECTION = vec3(-0.35f, -0.8f, -0.48f);
    static constexpr float SUN_ILLUMINANCE_LUX = 128000.0f;

    vec3 sky_color = vec3(0.45f, 0.6f, 0.85f);
    float sky_intensity = 8000.0f;
    float manual_ev100 = 15.0f;
};

}
