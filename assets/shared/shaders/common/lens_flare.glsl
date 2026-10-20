#ifndef LENS_FLARE_GLSL
#define LENS_FLARE_GLSL

layout(buffer_reference, scalar) buffer LensFlareBuffer {
    vec3 color;
    float glow;
    vec2 sun_uv;
    float rays;
    float ray_length;
    float ghosts;
    float visibility;
    float _pad1;
    float _pad2;
};

const float FLARE_RAY_WIDTH = 0.0016;
const float FLARE_GLOW_CORE = 18.0;
const float FLARE_GLOW_HALO = 0.004;
const int FLARE_GHOST_COUNT = 5;
const vec3 FLARE_GHOSTS[FLARE_GHOST_COUNT] = vec3[](
    vec3(0.45, 0.012, 0.6),
    vec3(0.85, 0.008, 0.4),
    vec3(1.45, 0.020, 0.5),
    vec3(2.00, 0.035, 1.0),
    vec3(2.40, 0.015, 0.4)
);

vec3 flare_spectrum(float p_t) {
    const float t = clamp(p_t, 0.0, 1.0) * 6.0;
    return clamp(vec3(abs(t - 3.0) - 1.0, 2.0 - abs(t - 2.0), 2.0 - abs(t - 4.0)), 0.0, 1.0);
}

float flare_ray(vec2 p_d, float p_angle, float p_length) {
    const vec2 dir = vec2(cos(p_angle), sin(p_angle));
    const float along = abs(dot(p_d, dir));
    const float perp = abs(p_d.x * dir.y - p_d.y * dir.x);
    const float w = perp / FLARE_RAY_WIDTH;
    return exp(-w * w) * exp(-along / p_length);
}

vec3 lens_flare(LensFlareBuffer p_flare, vec2 p_uv, float p_aspect) {
    const vec3 color = p_flare.color;
    if (max(color.r, max(color.g, color.b)) <= 0.0) return vec3(0.0);

    const vec2 scale = vec2(p_aspect, 1.0);
    const vec2 d = (p_uv - p_flare.sun_uv) * scale;
    const float r2 = dot(d, d);

    const float glow = (0.6 * exp(-sqrt(r2) * FLARE_GLOW_CORE) + 0.4 * FLARE_GLOW_HALO / (r2 + FLARE_GLOW_HALO)) * p_flare.glow;

    const float len = p_flare.ray_length;
    const float PI = 3.14159265;
    float main_rays = flare_ray(d, 0.5 * PI, len);
    main_rays += 0.75 * (flare_ray(d, PI / 6.0, 0.75 * len) + flare_ray(d, 5.0 * PI / 6.0, 0.75 * len));
    float fine_rays = flare_ray(d, 0.0, 0.4 * len) + flare_ray(d, PI / 3.0, 0.4 * len) + flare_ray(d, 2.0 * PI / 3.0, 0.4 * len);
    const vec3 ray_tint = mix(vec3(1.0), vec3(0.8, 0.9, 1.15), clamp(sqrt(r2) / len, 0.0, 1.0));
    const vec3 rays = ray_tint * (main_rays + 0.15 * fine_rays) * p_flare.rays;

    vec3 ghosts = vec3(0.0);
    const vec2 axis = vec2(0.5) - p_flare.sun_uv;
    const vec2 axis_px = axis * scale;
    const vec2 axis_n = dot(axis_px, axis_px) > 1e-8 ? normalize(axis_px) : vec2(0.0, 1.0);
    for (int i = 0; i < FLARE_GHOST_COUNT; ++i) {
        const vec3 g = FLARE_GHOSTS[i];
        const vec2 o = (p_uv - (p_flare.sun_uv + axis * g.x)) * scale;
        const float q = length(o) / g.y;
        if (q >= 1.0) continue;
        const float x = dot(o, axis_n) / g.y;
        const vec3 hue = mix(vec3(1.0), flare_spectrum(0.5 + 0.5 * x), clamp(abs(x) * 1.4, 0.0, 1.0));
        ghosts += hue * ((1.0 - smoothstep(0.55, 1.0, q)) * (1.0 - 0.5 * q * q) * g.z);
    }
    ghosts *= p_flare.ghosts;

    return color * (vec3(glow) + rays + ghosts);
}

#endif