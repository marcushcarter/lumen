#include <core/scene/relevance.h>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace lumen {

static constexpr float BAND_MIN_SIZE = 0.5f;
static constexpr float NEAR_PER_METRE = 12.0f;
static constexpr float MID_PER_METRE = 30.0f;
static constexpr float FAR_PER_METRE = 70.0f;
static constexpr float NEAR_MIN = 8.0f;
static constexpr float MID_MIN = 20.0f;
static constexpr float FAR_MIN = 40.0f;

void RelevanceView::add(const vec3& p_point)
{
    if (count < MAX_POINTS) points[count++] = p_point;
}

RelevanceView relevance_view(const Camera& p_camera)
{
    RelevanceView v;
    v.add(p_camera.position);
    v.eye = p_camera.position;
    v.forward = p_camera.forward();
    const float half_y = p_camera.fov_y * 0.5f;
    const float half_diag = std::atan(std::tan(half_y) * std::sqrt(1.0f + RelevanceView::CONE_ASPECT * RelevanceView::CONE_ASPECT));
    v.half_angle = std::min(half_diag + glm::radians(RelevanceView::CONE_MARGIN_DEG), 3.14159265f);
    return v;
}

float relevance_distance(const RelevanceView& p_view, const vec3& p_center, float p_radius)
{
    float best = FLT_MAX;
    for (uint32_t i = 0; i < p_view.count; i++) best = std::min(best, length(p_center - p_view.points[i]));
    return best == FLT_MAX ? 0.0f : std::max(best - p_radius, 0.0f);
}

static bool _in_cone(const RelevanceView& p_view, const vec3& p_center, float p_radius)
{
    const vec3 to = p_center - p_view.eye;
    const float dist = length(to);
    if (dist <= p_radius) return true;
    const float angle = std::acos(std::clamp(dot(to / dist, p_view.forward), -1.0f, 1.0f));
    return angle - std::asin(std::clamp(p_radius / dist, 0.0f, 1.0f)) <= p_view.half_angle;
}

bool relevance_visible(const RelevanceView& p_view, const vec3& p_center, float p_radius)
{
    if (_in_cone(p_view, p_center, p_radius)) return true;
    if (dot(p_view.shadow, p_view.shadow) < 0.5f) return false;
    const float elevation = std::max(-p_view.shadow.y, RelevanceView::SHADOW_MIN_ELEVATION);
    const float reach = std::min(2.0f * p_radius / elevation, RelevanceView::SHADOW_MAX_LENGTH);
    const float step = reach / (float)RelevanceView::SHADOW_SAMPLES;
    const float radius = p_radius + step * 0.5f;
    for (uint32_t i = 1; i <= RelevanceView::SHADOW_SAMPLES; i++) {
        if (_in_cone(p_view, p_center + p_view.shadow * (step * ((float)i - 0.5f)), radius)) return true;
    }
    return false;
}

RelevanceBand relevance_band(float p_distance, float p_size)
{
    const float size = std::max(p_size, BAND_MIN_SIZE);
    if (p_distance <= std::max(NEAR_MIN, size * NEAR_PER_METRE)) return RelevanceBand::CLOSE;
    if (p_distance <= std::max(MID_MIN, size * MID_PER_METRE)) return RelevanceBand::MEDIUM;
    if (p_distance <= std::max(FAR_MIN, size * FAR_PER_METRE)) return RelevanceBand::DISTANT;
    return RelevanceBand::BEYOND;
}

uint32_t relevance_interval(RelevanceBand p_band)
{
    switch (p_band) {
        case RelevanceBand::CLOSE: return 1;
        case RelevanceBand::MEDIUM: return 2;
        case RelevanceBand::DISTANT: return 4;
        default: return 0;
    }
}

}
