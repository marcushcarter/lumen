#ifndef SKY_GLSL
#define SKY_GLSL

#include "common/atmosphere.glsl"

const vec3 SUN_LIMB_DARKENING = vec3(0.48, 0.60, 0.72);
const vec3 NIGHT_TINT = vec3(0.70, 0.82, 1.00);
const float MOON_GEOMETRIC_ALBEDO = 0.1;
const float AIRGLOW_HEIGHT_KM = 90.0;

uvec3 sky_pcg3d(uvec3 p_v) {
    p_v = p_v * 1664525u + 1013904223u;
    p_v.x += p_v.y * p_v.z;
    p_v.y += p_v.z * p_v.x;
    p_v.z += p_v.x * p_v.y;
    p_v ^= p_v >> 16u;
    p_v.x += p_v.y * p_v.z;
    p_v.y += p_v.z * p_v.x;
    p_v.z += p_v.x * p_v.y;
    return p_v;
}

vec3 sky_hash33(vec3 p_p) { return vec3(sky_pcg3d(uvec3(ivec3(p_p) + 32768))) * (1.0 / 4294967295.0); }

float sky_gradient_noise(vec3 p_p) {
    vec3 i = floor(p_p);
    vec3 f = fract(p_p);
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float res[8];
    for (int k = 0; k < 8; k++) {
        vec3 o = vec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        res[k] = dot(sky_hash33(i + o) * 2.0 - 1.0, f - o);
    }
    float x0 = mix(res[0], res[1], u.x);
    float x1 = mix(res[2], res[3], u.x);
    float x2 = mix(res[4], res[5], u.x);
    float x3 = mix(res[6], res[7], u.x);
    return mix(mix(x0, x1, u.y), mix(x2, x3, u.y), u.z) * 1.6;
}

float sky_fbm(vec3 p_p, int p_octaves) {
    float sum = 0.0;
    float amp = 0.5;
    float norm = 0.0;
    for (int i = 0; i < p_octaves; i++) {
        sum += amp * sky_gradient_noise(p_p);
        norm += amp;
        p_p = p_p * 2.03 + vec3(1.7, 9.2, 3.1);
        amp *= 0.5;
    }
    return sum / max(norm, 1e-4);
}

float sky_luminance(vec3 p_c) { return dot(p_c, vec3(0.2126, 0.7152, 0.0722)); }

float sky_angle_between(vec3 p_a, vec3 p_b) { return 2.0 * asin(min(0.5 * length(p_a - p_b), 1.0)); }

int sky_cube_face(vec3 p_d, out vec2 r_uv) {
    vec3 a = abs(p_d);
    if (a.x >= a.y && a.x >= a.z) {
        r_uv = p_d.yz / a.x;
        return p_d.x > 0.0 ? 0 : 1;
    }
    if (a.y >= a.z) {
        r_uv = p_d.xz / a.y;
        return p_d.y > 0.0 ? 2 : 3;
    }
    r_uv = p_d.xy / a.z;
    return p_d.z > 0.0 ? 4 : 5;
}

vec3 sky_cube_dir(int p_face, vec2 p_uv) {
    vec3 d;
    if (p_face < 2) d = vec3(p_face == 0 ? 1.0 : -1.0, p_uv.x, p_uv.y);
    else if (p_face < 4) d = vec3(p_uv.x, p_face == 2 ? 1.0 : -1.0, p_uv.y);
    else d = vec3(p_uv.x, p_uv.y, p_face == 4 ? 1.0 : -1.0);
    return normalize(d);
}

vec3 sky_blackbody_rgb(float p_kelvin) {
    const vec3 nm = vec3(610.0, 550.0, 465.0);
    vec3 l = nm * 1e-3;
    vec3 b = 1.0 / (l * l * l * l * l * (exp(vec3(14387.77) / (nm * p_kelvin)) - 1.0));
    return b / max(sky_luminance(b), 1e-30);
}

vec3 sky_stars(vec3 p_cel, float p_sigma, uint p_cells, float p_density) {
    vec2 uv;
    int face = sky_cube_face(p_cel, uv);
    int n = int(p_cells);
    vec2 f = (atan(uv) * (4.0 / ATMO_PI) * 0.5 + 0.5) * float(n) - 0.5;
    ivec2 base = ivec2(floor(f));
    float inv2s2 = 1.0 / (2.0 * p_sigma * p_sigma);
    float norm = inv2s2 * ATMO_INV_PI;
    vec3 acc = vec3(0.0);
    for (int k = 0; k < 4; k++) {
        ivec2 c = base + ivec2(k & 1, k >> 1);
        if (any(lessThan(c, ivec2(0))) || any(greaterThanEqual(c, ivec2(n)))) continue;
        vec3 h = sky_hash33(vec3(c, float(face) + 0.5));
        if (h.x > p_density) continue;
        vec3 h2 = sky_hash33(vec3(c, float(face) + 31.5));
        vec2 su = (vec2(c) + 0.15 + 0.7 * h2.xy) / float(n) * 2.0 - 1.0;
        vec3 sd = sky_cube_dir(face, tan(su * (ATMO_PI / 4.0)));
        vec3 dv = p_cel - sd;
        float d2 = dot(dv, dv) * inv2s2;
        if (d2 > 9.0) continue;
        const float SPAN = 8.0;
        float mag = -1.0 + log2(h.y * (exp2(0.45 * 3.321928 * SPAN) - 1.0) + 1.0) / (0.45 * 3.321928);
        float lux = 2.54e-6 * exp2(-0.4 * 3.321928 * mag);
        acc += sky_blackbody_rgb(mix(3200.0, 12000.0, h.z * h.z)) * (lux * exp(-d2) * norm);
    }
    return acc;
}

vec3 sky_sun_disk(vec3 p_dir, float p_pixel_angle, out float r_coverage) {
    r_coverage = 0.0;
    float radius = acos(g_atmo.sun_cos_angular_radius);
    float ang = sky_angle_between(p_dir, g_atmo.sun_direction);
    if (ang > radius + p_pixel_angle * 2.0) return vec3(0.0);
    r_coverage = 1.0 - smoothstep(radius - p_pixel_angle, radius + p_pixel_angle, ang);
    float x = clamp(ang / radius, 0.0, 1.0);
    float mu = sqrt(max(1.0 - x * x, 0.0));
    vec3 limb = (1.0 - SUN_LIMB_DARKENING * (1.0 - mu)) / (1.0 - SUN_LIMB_DARKENING / 3.0);
    float s = sin(0.5 * radius);
    return limb * g_atmo.sun_illuminance / (4.0 * ATMO_PI * s * s);
}

vec3 sky_moon_disk(vec3 p_dir, float p_pixel_angle, out float r_coverage) {
    r_coverage = 0.0;
    vec3 m = g_atmo.moon_direction;
    float radius = acos(g_atmo.moon_cos_angular_radius);
    float ang = sky_angle_between(p_dir, m);
    if (ang > radius + p_pixel_angle * 2.0) return vec3(0.0);
    r_coverage = 1.0 - smoothstep(radius - p_pixel_angle, radius + p_pixel_angle, ang);
    vec3 up = g_atmo.moon_up;
    vec3 right = normalize(cross(up, -m));
    vec3 off = p_dir - m * dot(p_dir, m);
    vec2 xy = vec2(dot(off, right), dot(off, up)) / radius;
    float rr = dot(xy, xy);
    if (rr >= 0.998) {
        xy *= inversesqrt(rr) * 0.999;
        rr = 0.998;
    }
    float z = sqrt(1.0 - rr);
    vec3 n = normalize(xy.x * right + xy.y * up - z * m);
    vec3 pn = vec3(xy, z);

    const vec3 MARIA[13] = vec3[13](
        vec3(33.0, -16.0, 17.0), vec3(28.0, 17.5, 9.0), vec3(8.5, 31.0, 11.0), vec3(17.0, 59.0, 6.5),
        vec3(-8.0, 51.0, 9.0), vec3(-15.0, 35.0, 5.5), vec3(-21.0, -17.0, 9.0), vec3(18.0, -57.0, 24.0),
        vec3(-24.0, -39.0, 5.0), vec3(56.0, 1.0, 7.0), vec3(55.0, -25.0, 6.0), vec3(7.0, -31.0, 7.0), vec3(-10.0, -23.0, 6.0)
    );

    vec3 warp = vec3(sky_fbm(pn * 4.0, 3), sky_fbm(pn * 4.0 + 5.0, 3), 0.0) * 0.12;
    vec3 pw = normalize(pn + warp);
    float mare = 0.0;
    for (int i = 0; i < 13; i++) {
        float la = radians(MARIA[i].x);
        float lo = radians(MARIA[i].y);
        vec3 cn = vec3(cos(la) * sin(lo), sin(la), cos(la) * cos(lo));
        float dist = sky_angle_between(pw, cn);
        mare = max(mare, 1.0 - smoothstep(radians(MARIA[i].z) * 0.6, radians(MARIA[i].z) * 1.15, dist));
    }
    float highland = sky_fbm(pn * 22.0, 3) * 0.5 + 0.5;
    float craters = sky_fbm(pn * 70.0 + 3.0, 2) * 0.5 + 0.5;
    float la_t = radians(-43.3);
    float lo_t = radians(-11.2);
    float tycho_d = sky_angle_between(pn, vec3(cos(la_t) * sin(lo_t), sin(la_t), cos(la_t) * cos(lo_t)));
    float albedo_rel = mix(1.15 + 0.15 * (highland - 0.5), 0.55 + 0.1 * craters, mare * 0.9) * (0.9 + 0.2 * craters) + exp(-tycho_d / 0.05) * 0.6;

    float mu0 = max(dot(n, g_atmo.sun_direction), 0.0);
    float ls = mu0 > 0.0 ? 2.0 * mu0 / (mu0 + max(z, 1e-3)) : 0.0;
    float phase = g_atmo.moon_phase_angle;
    float phase_term = (exp(-phase / 0.12) * 0.25 + 1.0) * exp(-0.7 * phase / ATMO_PI);
    return g_atmo.sun_illuminance * (MOON_GEOMETRIC_ALBEDO * ATMO_INV_PI * albedo_rel * ls * phase_term * g_atmo.moon_disk_brightness);
}

vec3 sky_color(vec3 p_dir, uint p_skyview_sun, uint p_skyview_moon, float p_pixel_angle, float p_exposure) {
    vec3 ro = atmo_observer();
    float r = length(ro);
    vec2 uv = skyview_uv(p_dir, r);
    vec3 color = atmo_lut(p_skyview_sun, uv).rgb * g_atmo.sun_illuminance + atmo_lut(p_skyview_moon, uv).rgb * g_atmo.moon_illuminance;

    if (ray_sphere(ro, p_dir, atmo_rg()) > 0.0) return max(color, vec3(0.0));

    vec3 t_view = transmittance_to_top(r, p_dir.y);
    vec3 space = vec3(0.0);

    float sigma = p_pixel_angle * 0.6;
    float brightest = 2.54e-6 * 2.5 / (2.0 * ATMO_PI * sigma * sigma) * g_atmo.star_brightness;
    if (brightest * p_exposure > 1e-3) {
        vec3 cel = (g_atmo.world_to_celestial * vec4(p_dir, 0.0)).xyz;
        space += sky_stars(cel, sigma, 256u, g_atmo.star_density) * g_atmo.star_brightness;
    }
    float cov;
    vec3 sun = sky_sun_disk(p_dir, p_pixel_angle, cov);
    space = mix(space, sun, cov);
    vec3 moon = sky_moon_disk(p_dir, p_pixel_angle, cov);
    space = mix(space, moon, cov);
    color += space * t_view;

    float rh = atmo_rg() / (atmo_rg() + AIRGLOW_HEIGHT_KM);
    float van_rhijn = inversesqrt(max(1.0 - rh * rh * (1.0 - p_dir.y * p_dir.y), 1e-3));
    color += NIGHT_TINT * (g_atmo.night_luminance * van_rhijn) * sqrt(t_view);
    return max(color, vec3(0.0));
}

#endif