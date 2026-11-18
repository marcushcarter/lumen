#pragma once
#include <core/scene/camera.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace lumen {

using namespace glm;

enum class RelevanceBand : uint8_t { CLOSE, MEDIUM, DISTANT, BEYOND };

struct RelevanceView
{
    static constexpr uint32_t MAX_POINTS = 2;
    static constexpr float CONE_ASPECT = 2.0f;
    static constexpr float CONE_MARGIN_DEG = 10.0f;
    static constexpr float SHADOW_MIN_ELEVATION = 0.15f;
    static constexpr float SHADOW_MAX_LENGTH = 60.0f;
    static constexpr uint32_t SHADOW_SAMPLES = 4;

    vec3 points[MAX_POINTS] = {};
    uint32_t count = 0;
    vec3 eye = vec3(0.0f);
    vec3 forward = vec3(0.0f, 0.0f, -1.0f);
    float half_angle = 3.14159265f;
    vec3 shadow = vec3(0.0f);

    void add(const vec3& p_point);
};

RelevanceView relevance_view(const Camera& p_camera);
float relevance_distance(const RelevanceView& p_view, const vec3& p_center, float p_radius);
bool relevance_visible(const RelevanceView& p_view, const vec3& p_center, float p_radius);
RelevanceBand relevance_band(float p_distance, float p_size);
uint32_t relevance_interval(RelevanceBand p_band);

}
