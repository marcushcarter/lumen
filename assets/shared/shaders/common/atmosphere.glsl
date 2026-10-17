#ifndef ATMOSPHERE_GLSL
#define ATMOSPHERE_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout(buffer_reference, scalar) readonly buffer AtmosphereBuffer {
    mat4 world_to_celestial;
    vec3 rayleigh_scattering;
    float rayleigh_density_scale;
    vec3 mie_scattering;
    float mie_density_scale;
    vec3 mie_extinction;
    float mie_g;
    vec3 ozone_absorption;
    float ozone_center_km;
    vec3 ground_albedo;
    float ozone_width_km;
    vec3 sun_direction;
    float sun_cos_angular_radius;
    vec3 sun_illuminance;
    float bottom_radius_km;
    vec3 moon_direction;
    float moon_cos_angular_radius;
    vec3 moon_illuminance;
    float top_radius_km;
    vec3 moon_up;
    float moon_phase_angle;
    vec3 observer_km;
    float altitude_km;
    float night_luminance;
    float star_brightness;
    float star_density;
    float moon_disk_brightness;
};

layout(buffer_reference, scalar) buffer SkyLightingBuffer { vec4 sh[9]; };

const float ATMO_PI = 3.14159265359;
const float ATMO_INV_PI = 0.31830988618;

const uint AERIAL_SIZE = 32u;
const uint AERIAL_SLICES = 32u;
const float AERIAL_MAX_KM = 32.0;

const float ATMO_LUT_SCALE = 1e4;
const float ATMO_LUT_INV_SCALE = 1e-4;
const float ATMO_LUT_MAX = 65000.0;
const float ATMO_BODY_CUTOFF_SIN = -0.342;

AtmosphereBuffer g_atmo;
uint g_lut_sampler;
uint g_transmittance_lut;
uint g_multiscatter_lut;

vec4 atmo_lut(uint p_texture, vec2 p_uv) {
    return textureLod(sampler2D(u_textures[p_texture], u_samplers[g_lut_sampler]), p_uv, 0.0);
}

float atmo_rg() { return g_atmo.bottom_radius_km; }
float atmo_rt() { return g_atmo.top_radius_km; }
vec3 atmo_observer() { return vec3(0.0, g_atmo.bottom_radius_km + g_atmo.altitude_km, 0.0); }

float ray_sphere(vec3 p_ro, vec3 p_rd, float p_radius) {
    float b = dot(p_ro, p_rd);
    float c = dot(p_ro, p_ro) - p_radius * p_radius;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    float s = sqrt(disc);
    float t0 = -b - s;
    float t1 = -b + s;
    if (t0 >= 0.0) return t0;
    return t1 >= 0.0 ? t1 : -1.0;
}

struct Medium {
    vec3 scattering;
    vec3 extinction;
    vec3 rayleigh;
    vec3 mie;
};

Medium atmo_medium(float p_height) {
    float dr = exp(p_height * g_atmo.rayleigh_density_scale);
    float dm = exp(p_height * g_atmo.mie_density_scale);
    float doz = max(0.0, 1.0 - abs(p_height - g_atmo.ozone_center_km) / (0.5 * g_atmo.ozone_width_km));
    Medium m;
    m.rayleigh = g_atmo.rayleigh_scattering * dr;
    m.mie = g_atmo.mie_scattering * dm;
    m.scattering = m.rayleigh + m.mie;
    m.extinction = m.rayleigh + g_atmo.mie_extinction * dm + g_atmo.ozone_absorption * doz;
    return m;
}

float phase_rayleigh(float p_cos) { return 3.0 / (16.0 * ATMO_PI) * (1.0 + p_cos * p_cos); }

float phase_cornette_shanks(float p_g, float p_cos) {
    float k = 3.0 / (8.0 * ATMO_PI) * (1.0 - p_g * p_g) / (2.0 + p_g * p_g);
    return k * (1.0 + p_cos * p_cos) / pow(max(1.0 + p_g * p_g - 2.0 * p_g * p_cos, 1e-5), 1.5);
}

float unit_to_sub_uv(float p_u, float p_res) { return (p_u + 0.5 / p_res) * (p_res / (p_res + 1.0)); }
float sub_uv_to_unit(float p_u, float p_res) { return (p_u - 0.5 / p_res) * (p_res / (p_res - 1.0)); }

vec2 transmittance_uv(float p_r, float p_mu) {
    float rg = atmo_rg();
    float rt = atmo_rt();
    float h = sqrt(max(rt * rt - rg * rg, 0.0));
    float rho = sqrt(max(p_r * p_r - rg * rg, 0.0));
    float disc = p_r * p_r * (p_mu * p_mu - 1.0) + rt * rt;
    float d = max(0.0, -p_r * p_mu + sqrt(max(disc, 0.0)));
    float d_min = rt - p_r;
    float d_max = rho + h;
    return vec2((d - d_min) / max(d_max - d_min, 1e-5), rho / h);
}

void transmittance_params(vec2 p_uv, out float r_r, out float r_mu) {
    float rg = atmo_rg();
    float rt = atmo_rt();
    float h = sqrt(max(rt * rt - rg * rg, 0.0));
    float rho = h * p_uv.y;
    r_r = sqrt(rho * rho + rg * rg);
    float d_min = rt - r_r;
    float d_max = rho + h;
    float d = d_min + p_uv.x * (d_max - d_min);
    r_mu = d == 0.0 ? 1.0 : (h * h - rho * rho - d * d) / (2.0 * r_r * d);
    r_mu = clamp(r_mu, -1.0, 1.0);
}

vec3 transmittance_to_top(float p_r, float p_mu) {
    return exp(-atmo_lut(g_transmittance_lut, transmittance_uv(p_r, p_mu)).rgb);
}

vec3 light_transmittance(vec3 p_pos, vec3 p_l) {
    float r = max(length(p_pos), atmo_rg() + 0.001);
    float mu = dot(p_pos / r, p_l);
    float sin_h = atmo_rg() / r;
    float mu_h = -sqrt(max(1.0 - sin_h * sin_h, 0.0));
    float vis = smoothstep(mu_h - 0.0047, mu_h + 0.0047, mu);
    return transmittance_to_top(r, mu) * vis;
}

vec3 multiscatter(float p_r, float p_mu_s) {
    vec2 uv = vec2(clamp(0.5 + 0.5 * p_mu_s, 0.0, 1.0), clamp((p_r - atmo_rg()) / (atmo_rt() - atmo_rg()), 0.0, 1.0));
    const float res = 32.0;
    uv = vec2(unit_to_sub_uv(uv.x, res), unit_to_sub_uv(uv.y, res));
    return atmo_lut(g_multiscatter_lut, uv).rgb * ATMO_LUT_INV_SCALE;
}

vec3 skyview_sample(uint p_texture, vec2 p_uv) {
    return atmo_lut(p_texture, p_uv).rgb * ATMO_LUT_INV_SCALE;
}

bool atmo_sun_active() { return g_atmo.sun_direction.y > ATMO_BODY_CUTOFF_SIN; }
bool atmo_moon_active() { return g_atmo.moon_direction.y > ATMO_BODY_CUTOFF_SIN && g_atmo.sun_direction.y < 0.0; }

struct ScatterResult {
    vec3 sun;
    vec3 moon;
    vec3 transmittance;
    bool hit_ground;
};

ScatterResult integrate_scattering(vec3 p_ro, vec3 p_rd, float p_t_limit, float p_steps, bool p_add_ground, bool p_sun, bool p_moon) {
    ScatterResult res;
    res.sun = vec3(0.0);
    res.moon = vec3(0.0);
    res.transmittance = vec3(1.0);
    res.hit_ground = false;

    float t_bottom = ray_sphere(p_ro, p_rd, atmo_rg());
    float t_top = ray_sphere(p_ro, p_rd, atmo_rt());
    float t_max = 0.0;
    if (t_bottom < 0.0) {
        if (t_top < 0.0) return res;
        t_max = t_top;
    } else {
        t_max = t_top > 0.0 ? min(t_top, t_bottom) : t_bottom;
        res.hit_ground = true;
    }
    t_max = min(t_max, p_t_limit);

    vec3 l0 = g_atmo.sun_direction;
    vec3 l1 = g_atmo.moon_direction;
    float spp = mix(max(p_steps * 0.25, 4.0), p_steps, clamp(t_max * 0.01, 0.0, 1.0));
    float dt = t_max / spp;
    int count = int(ceil(spp));
    float c0 = dot(p_rd, l0);
    float c1 = dot(p_rd, l1);
    float pr0 = phase_rayleigh(c0);
    float pr1 = phase_rayleigh(c1);
    float pm0 = phase_cornette_shanks(g_atmo.mie_g, c0);
    float pm1 = phase_cornette_shanks(g_atmo.mie_g, c1);

    vec3 throughput = vec3(1.0);
    for (int i = 0; i < count; i++) {
        float t0 = float(i) * dt;
        float t1 = min(t0 + dt, t_max);
        float seg = t1 - t0;
        if (seg <= 0.0) break;
        vec3 p = p_ro + p_rd * (t0 + 0.3 * seg);
        float r = length(p);
        vec3 up = p / r;
        Medium m = atmo_medium(r - atmo_rg());
        vec3 sample_t = exp(-m.extinction * seg);
        vec3 inv_ext = 1.0 / max(m.extinction, vec3(1e-7));
        if (p_sun) {
            vec3 s0 = (m.rayleigh * pr0 + m.mie * pm0) * light_transmittance(p, l0) + multiscatter(r, dot(up, l0)) * m.scattering;
            res.sun += throughput * (s0 - s0 * sample_t) * inv_ext;
        }
        if (p_moon) {
            vec3 s1 = (m.rayleigh * pr1 + m.mie * pm1) * light_transmittance(p, l1) + multiscatter(r, dot(up, l1)) * m.scattering;
            res.moon += throughput * (s1 - s1 * sample_t) * inv_ext;
        }
        throughput *= sample_t;
    }

    if (p_add_ground && res.hit_ground && t_max >= t_bottom - 1e-3) {
        vec3 p = p_ro + p_rd * t_bottom;
        vec3 n = normalize(p);
        if (p_sun) res.sun += throughput * light_transmittance(p, l0) * max(dot(n, l0), 0.0) * g_atmo.ground_albedo * ATMO_INV_PI;
        if (p_moon) res.moon += throughput * light_transmittance(p, l1) * max(dot(n, l1), 0.0) * g_atmo.ground_albedo * ATMO_INV_PI;
    }
    res.transmittance = throughput;
    return res;
}

vec2 skyview_uv(vec3 p_dir, float p_r) {
    float rg = atmo_rg();
    float v_horizon = sqrt(max(p_r * p_r - rg * rg, 0.0));
    float beta = acos(clamp(v_horizon / p_r, -1.0, 1.0));
    float zenith_horizon = ATMO_PI - beta;
    float zenith = acos(clamp(p_dir.y, -1.0, 1.0));
    float v;
    if (zenith < zenith_horizon) {
        float c = 1.0 - sqrt(max(1.0 - zenith / zenith_horizon, 0.0));
        v = c * 0.5;
    } else {
        float c = sqrt(max((zenith - zenith_horizon) / max(beta, 1e-5), 0.0));
        v = c * 0.5 + 0.5;
    }
    float u = atan(p_dir.x, -p_dir.z) * (0.5 / ATMO_PI);
    return vec2(fract(u), v);
}

vec3 skyview_dir(vec2 p_uv, float p_r) {
    float rg = atmo_rg();
    float v_horizon = sqrt(max(p_r * p_r - rg * rg, 0.0));
    float beta = acos(clamp(v_horizon / p_r, -1.0, 1.0));
    float zenith_horizon = ATMO_PI - beta;
    float zenith;
    if (p_uv.y < 0.5) {
        float c = 1.0 - 2.0 * p_uv.y;
        zenith = zenith_horizon * (1.0 - c * c);
    } else {
        float c = p_uv.y * 2.0 - 1.0;
        zenith = zenith_horizon + beta * c * c;
    }
    float azimuth = p_uv.x * 2.0 * ATMO_PI;
    float sz = sin(zenith);
    return vec3(sz * sin(azimuth), cos(zenith), -sz * cos(azimuth));
}

float aerial_slice_km(float p_slice) {
    float w = p_slice / float(AERIAL_SLICES);
    return w * w * AERIAL_MAX_KM;
}

vec3 sh_irradiance(SkyLightingBuffer p_sh, vec3 p_n) {
    vec3 r = p_sh.sh[0].rgb * 0.282095;
    r += p_sh.sh[1].rgb * 0.488603 * p_n.y + p_sh.sh[2].rgb * 0.488603 * p_n.z + p_sh.sh[3].rgb * 0.488603 * p_n.x;
    r += p_sh.sh[4].rgb * 1.092548 * p_n.x * p_n.y + p_sh.sh[5].rgb * 1.092548 * p_n.y * p_n.z + p_sh.sh[6].rgb * 0.315392 * (3.0 * p_n.z * p_n.z - 1.0);
    r += p_sh.sh[7].rgb * 1.092548 * p_n.x * p_n.z + p_sh.sh[8].rgb * 0.546274 * (p_n.x * p_n.x - p_n.y * p_n.y);
    return max(r, vec3(0.0));
}

#endif