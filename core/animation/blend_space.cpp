#include <core/animation/blend_space.h>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace lumen {

static constexpr float RING_EPSILON = 1.0e-3f;

static float _wrap_360(float p_degrees)
{
    const float d = std::fmod(p_degrees, 360.0f);
    return d < 0.0f ? d + 360.0f : d;
}

static void _ring_weights(const float* p_speeds, const float* p_directions, uint32_t p_count, float p_ring, float p_direction, float p_factor, float* r_weights)
{
    int32_t left = -1;
    int32_t right = -1;
    float to_left = FLT_MAX;
    float to_right = FLT_MAX;
    for (uint32_t i = 0; i < p_count; i++) {
        if (std::fabs(p_speeds[i] - p_ring) > RING_EPSILON) continue;
        const float r = _wrap_360(p_directions[i] - p_direction);
        const float l = _wrap_360(p_direction - p_directions[i]);
        if (r < to_right) { to_right = r; right = (int32_t)i; }
        if (l < to_left) { to_left = l; left = (int32_t)i; }
    }
    if (left < 0) return;
    if (left == right || p_ring <= RING_EPSILON || to_left + to_right <= RING_EPSILON) {
        r_weights[left] += p_factor;
        return;
    }
    const float t = to_left / (to_left + to_right);
    r_weights[left] += p_factor * (1.0f - t);
    r_weights[right] += p_factor * t;
}

void blend_space_polar_weights(const float* p_speeds, const float* p_directions, uint32_t p_count, float p_speed, float p_direction, float* r_weights)
{
    for (uint32_t i = 0; i < p_count; i++) r_weights[i] = 0.0f;
    if (p_count == 0) return;

    float below = -FLT_MAX;
    float above = FLT_MAX;
    for (uint32_t i = 0; i < p_count; i++) {
        if (p_speeds[i] <= p_speed + RING_EPSILON) below = std::max(below, p_speeds[i]);
        if (p_speeds[i] >= p_speed - RING_EPSILON) above = std::min(above, p_speeds[i]);
    }
    if (below == -FLT_MAX) below = above;
    if (above == FLT_MAX) above = below;
    if (above - below <= RING_EPSILON) {
        _ring_weights(p_speeds, p_directions, p_count, below, p_direction, 1.0f, r_weights);
        return;
    }
    const float t = (p_speed - below) / (above - below);
    _ring_weights(p_speeds, p_directions, p_count, below, p_direction, 1.0f - t, r_weights);
    _ring_weights(p_speeds, p_directions, p_count, above, p_direction, t, r_weights);
}

float blend_cycle_duration(const float* p_durations, const float* p_weights, uint32_t p_count)
{
    float total = 0.0f;
    float duration = 0.0f;
    for (uint32_t i = 0; i < p_count; i++) {
        if (p_weights[i] <= 0.0f || p_durations[i] <= 0.0f) continue;
        total += p_weights[i];
        duration += p_durations[i] * p_weights[i];
    }
    return total > 0.0f ? duration / total : 0.0f;
}

}
