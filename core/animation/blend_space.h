#pragma once
#include <core/assets/lblendspace.h>
#include <cstdint>

namespace lumen {

inline constexpr float BLEND_MIN_WEIGHT = 0.001f;

void blend_space_weights(const LBlendSpace& p_space, const bool* p_usable, float p_value, float* r_weights);

}
