#pragma once
#include <cstdint>

namespace lumen {

void blend_space_1d_weights(const float* p_positions, uint32_t p_count, float p_value, float* r_weights);
void blend_space_polar_weights(const float* p_speeds, const float* p_directions, uint32_t p_count, float p_speed, float p_direction, float* r_weights);
float blend_cycle_duration(const float* p_durations, const float* p_weights, uint32_t p_count);

}
