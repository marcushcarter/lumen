#ifndef TONEMAP_GLSL
#define TONEMAP_GLSL

#include "common/color.glsl"

const uint TONEMAPPER_AGX = 0u;
const uint TONEMAPPER_ACES = 1u;
const uint TONEMAPPER_PBR_NEUTRAL = 2u;
const uint TONEMAPPER_NONE = 3u;
const uint TONEMAPPER_AGX_PUNCHY = 4u;
const float AGX_PUNCHY_POWER = 1.35;
const float AGX_PUNCHY_SATURATION = 1.4;

vec3 agx_contrast(vec3 p_x) {
    vec3 x2 = p_x * p_x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * p_x + 31.96 * x4 - 6.868 * x2 * p_x + 0.4298 * x2 + 0.1191 * p_x - 0.00232;
}

vec3 agx_punchy(vec3 p_c) {
    const vec3 c = pow(max(p_c, vec3(0.0)), vec3(AGX_PUNCHY_POWER));
    const float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return l + AGX_PUNCHY_SATURATION * (c - l);
}

vec3 agx(vec3 p_c, bool p_punchy) {
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992, 0.878468636469772, 0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368, 1.15190312990417, -0.0980434501171241, -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float min_ev = -12.47393;
    const float max_ev = 4.026069;
    vec3 c = inset * p_c;
    c = clamp(log2(max(c, vec3(1e-10))), min_ev, max_ev);
    c = (c - min_ev) / (max_ev - min_ev);
    c = agx_contrast(c);
    if (p_punchy) c = agx_punchy(c);
    c = outset * c;
    return clamp(c, 0.0, 1.0);
}

vec3 aces_hill(vec3 p_c) {
    const mat3 aces_in = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 aces_out = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    vec3 c = aces_in * p_c;
    c = (c * (c + 0.0245786) - 0.000090537) / (c * (0.983729 * c + 0.4329510) + 0.238081);
    return clamp(aces_out * c, 0.0, 1.0);
}

vec3 pbr_neutral(vec3 p_c) {
    const float start = 0.8 - 0.04;
    const float desat = 0.15;
    vec3 c = p_c;
    float x = min(c.r, min(c.g, c.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    c -= offset;
    float peak = max(c.r, max(c.g, c.b));
    if (peak < start) return c;
    float d = 1.0 - start;
    float new_peak = 1.0 - d * d / (peak + d - start);
    c *= new_peak / peak;
    float g = 1.0 - 1.0 / (desat * (peak - new_peak) + 1.0);
    return mix(c, vec3(new_peak), g);
}

const vec3 NIGHT_SHIFT_TINT = vec3(0.55, 0.78, 1.35) / 0.7723;
const float NIGHT_SHIFT_SCOTOPIC = 0.02;
const float NIGHT_SHIFT_PHOTOPIC = 0.2;

vec3 night_shift(vec3 p_hdr, float p_amount) {
    if (p_amount <= 0.0) return p_hdr;
    const float l = dot(p_hdr, vec3(0.2126, 0.7152, 0.0722));
    const float w = p_amount * (1.0 - smoothstep(NIGHT_SHIFT_SCOTOPIC, NIGHT_SHIFT_PHOTOPIC, l));
    return mix(p_hdr, l * NIGHT_SHIFT_TINT, w);
}

vec3 display_encode(vec3 p_hdr, uint p_tonemapper) {
    if (p_tonemapper == TONEMAPPER_AGX) return agx(p_hdr, false);
    if (p_tonemapper == TONEMAPPER_AGX_PUNCHY) return agx(p_hdr, true);
    if (p_tonemapper == TONEMAPPER_ACES) return linear_to_srgb(aces_hill(p_hdr));
    if (p_tonemapper == TONEMAPPER_PBR_NEUTRAL) return linear_to_srgb(clamp(pbr_neutral(p_hdr), 0.0, 1.0));
    return linear_to_srgb(clamp(p_hdr, 0.0, 1.0));
}

vec4 display_output(vec3 p_hdr, uint p_tonemapper, ivec2 p_pixel) {
    vec3 display = display_encode(p_hdr, p_tonemapper);
    const vec2 f = vec2(p_pixel) + 0.5;
    const float n0 = fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
    const float n1 = fract(52.9829189 * fract(dot(f + 17.0, vec2(0.06711056, 0.00583715))));
    display += (n0 + n1 - 1.0) / 255.0;
    return vec4(srgb_to_linear(clamp(display, 0.0, 1.0)), 1.0);
}

#endif