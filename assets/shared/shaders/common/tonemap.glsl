#ifndef TONEMAP_GLSL
#define TONEMAP_GLSL

#include "common/color.glsl"

vec3 aces_hill(vec3 p_c) {
    const mat3 aces_in = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 aces_out = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    vec3 c = aces_in * p_c;
    c = (c * (c + 0.0245786) - 0.000090537) / (c * (0.983729 * c + 0.4329510) + 0.238081);
    return clamp(aces_out * c, 0.0, 1.0);
}

vec4 display_output(vec3 p_hdr, ivec2 p_pixel) {
    vec3 display = linear_to_srgb(aces_hill(p_hdr));
    const vec2 f = vec2(p_pixel) + 0.5;
    const float n0 = fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
    const float n1 = fract(52.9829189 * fract(dot(f + 17.0, vec2(0.06711056, 0.00583715))));
    display += (n0 + n1 - 1.0) / 255.0;
    return vec4(srgb_to_linear(clamp(display, 0.0, 1.0)), 1.0);
}

#endif
