#pragma once
#include <cmath>

namespace lumen {

inline float color_srgb_to_linear(float p_c)
{
    return p_c <= 0.04045f ? p_c / 12.92f : std::pow((p_c + 0.055f) / 1.055f, 2.4f);
}

inline float color_linear_to_srgb(float p_c)
{
    return p_c <= 0.0031308f ? p_c * 12.92f : 1.055f * std::pow(p_c, 1.0f / 2.4f) - 0.055f;
}

}
