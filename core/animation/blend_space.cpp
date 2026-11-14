#include <core/animation/blend_space.h>

namespace lumen {

void blend_space_weights(const LBlendSpace& p_space, const bool* p_usable, float p_value, float* r_weights)
{
    const uint32_t count = (uint32_t)p_space.samples.size();
    for (uint32_t i = 0; i < count; i++) r_weights[i] = 0.0f;

    int32_t below = -1;
    int32_t above = -1;
    for (uint32_t i = 0; i < count; i++) {
        if (!p_usable[i]) continue;
        const float p = p_space.samples[i].position;
        if (p <= p_value && (below < 0 || p > p_space.samples[below].position)) below = (int32_t)i;
        if (p >= p_value && (above < 0 || p < p_space.samples[above].position)) above = (int32_t)i;
    }
    if (below < 0 && above < 0) return;
    if (below < 0) {
        r_weights[above] = 1.0f;
        return;
    }
    if (above < 0 || above == below) {
        r_weights[below] = 1.0f;
        return;
    }
    const float span = p_space.samples[above].position - p_space.samples[below].position;
    const float t = span > 1.0e-5f ? (p_value - p_space.samples[below].position) / span : 0.5f;
    r_weights[below] = 1.0f - t;
    r_weights[above] = t;
}

}
