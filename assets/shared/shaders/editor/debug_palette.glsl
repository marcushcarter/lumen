#ifndef DEBUG_PALETTE_GLSL
#define DEBUG_PALETTE_GLSL

#include "common/color.glsl"

const vec3 DEBUG_LEAF_COLOR = vec3(0.5);
const vec3 DEBUG_ERROR_COLOR = vec3(1.0, 0.0, 1.0);

vec3 hash_color(uint k) {
    uint h = k * 2654435761u;
    return vec3(float(h & 0xffu), float((h >> 8) & 0xffu), float((h >> 16) & 0xffu)) / 255.0;
}

uint hash2(uint a, uint b) { return (a * 2654435761u) ^ (b * 2246822519u); }

const vec3 VIRIDIS_LUT[17] = vec3[17](
    vec3(0.2670, 0.0049, 0.3294),
    vec3(0.2823, 0.0950, 0.4173),
    vec3(0.2788, 0.1755, 0.4834),
    vec3(0.2590, 0.2515, 0.5247),
    vec3(0.2297, 0.3224, 0.5457),
    vec3(0.1994, 0.3876, 0.5546),
    vec3(0.1727, 0.4488, 0.5579),
    vec3(0.1490, 0.5081, 0.5573),
    vec3(0.1276, 0.5669, 0.5506),
    vec3(0.1206, 0.6258, 0.5335),
    vec3(0.1579, 0.6838, 0.5017),
    vec3(0.2461, 0.7389, 0.4520),
    vec3(0.3692, 0.7889, 0.3829),
    vec3(0.5160, 0.8312, 0.2943),
    vec3(0.6785, 0.8637, 0.1895),
    vec3(0.8456, 0.8873, 0.0997),
    vec3(0.9932, 0.9062, 0.1439)
);

const vec3 HEAT_LUT[17] = vec3[17](
    vec3(0.0800, 0.2000, 0.6200),
    vec3(0.0876, 0.2985, 0.6579),
    vec3(0.0952, 0.3970, 0.6958),
    vec3(0.1027, 0.4955, 0.7336),
    vec3(0.1103, 0.5939, 0.7715),
    vec3(0.1179, 0.6924, 0.8094),
    vec3(0.2373, 0.7377, 0.7382),
    vec3(0.4002, 0.7623, 0.6245),
    vec3(0.5630, 0.7870, 0.5109),
    vec3(0.7259, 0.8116, 0.3973),
    vec3(0.8888, 0.8362, 0.2836),
    vec3(0.9671, 0.7893, 0.2103),
    vec3(0.9376, 0.6515, 0.1882),
    vec3(0.9082, 0.5136, 0.1662),
    vec3(0.8788, 0.3757, 0.1441),
    vec3(0.8494, 0.2379, 0.1221),
    vec3(0.8200, 0.1000, 0.1000)
);

vec3 color_map_viridis(float t) {
    float x = clamp(t, 0.0, 1.0) * 16.0;
    uint i = min(uint(x), 15u);
    return srgb_to_linear(mix(VIRIDIS_LUT[i], VIRIDIS_LUT[i + 1u], x - float(i)));
}

vec3 color_map_heat(float t) {
    float x = clamp(t, 0.0, 1.0) * 16.0;
    uint i = min(uint(x), 15u);
    return srgb_to_linear(mix(HEAT_LUT[i], HEAT_LUT[i + 1u], x - float(i)));
}

#endif
