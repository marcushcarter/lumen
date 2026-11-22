#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require

#include "water/draw.glsl"
#include "common/color.glsl"

layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 0, binding = 2) uniform sampler u_samplers[];

layout(location = 0) in vec2 v_sample;
layout(location = 1) in vec3 v_world;
layout(location = 2) in vec2 v_flow;
layout(location = 3) in float v_foam;
layout(location = 4) in float v_depth;
layout(location = 5) in float v_curl;
layout(location = 6) flat in uint v_item;
layout(location = 0) out vec4 o_color;

const float SKY_DISTANCE = 60000.0;
const float FLOW_PERIOD = 1.6;
const vec3 ABSORB = vec3(0.62, 0.17, 0.13);
const vec3 SCATTER = vec3(0.02, 0.10, 0.085);
const float CAUSTIC_MIN_ATTENUATION = 0.004;
const float FLOW_STRETCH = 0.6;
const float FLOW_STRETCH_SPEED = 3.0;
const float RIPPLE_CALM = 0.04;
const float FAR_DETAIL_START = 50.0;
const float FAR_DETAIL_END = 70.0;
const float RIPPLE_EDGE_TEXELS = 24.0;
const float RIPPLE_FLOW_FADE_START = 0.8;
const float RIPPLE_FLOW_FADE_END = 2.5;
const float RIPPLE_CURL = 0.08;
const float FOAM_ALBEDO = 0.92;
const float SHORE_FOAM_OPACITY = 0.55;
const float FOAM_WISP_FLOOR = 0.25;

vec3 color_heat(float p_t) {
    const float t = clamp(p_t, 0.0, 1.0);
    return srgb_to_linear(clamp(vec3(1.5 - abs(4.0 * t - 3.0), 1.5 - abs(4.0 * t - 2.0), 1.5 - abs(4.0 * t - 1.0)), 0.0, 1.0));
}

vec2 hash22(vec2 p_p) {
    vec3 p3 = fract(vec3(p_p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float16_t voro(vec2 p_p, float p_t) {
    const vec2 i = floor(p_p);
    const f16vec2 f = f16vec2(fract(p_p));
    float16_t d1 = float16_t(8.0), d2 = float16_t(8.0);
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            const vec2 g = vec2(x, y);
            const f16vec2 o = float16_t(0.5) + float16_t(0.42) * f16vec2(sin(p_t + 6.2831 * hash22(i + g)));
            const float16_t d = length(f16vec2(g) + o - f);
            if (d < d1) {
                d2 = d1;
                d1 = d;
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    return d2 - d1;
}

float caustic(vec2 p_p, float p_t) {
    const float16_t a = voro(p_p, p_t);
    const float16_t b = voro(p_p * 1.6 + 3.1, p_t * 1.3 + 1.0);
    const float16_t one = float16_t(1.0);
    const float16_t ca = one - smoothstep(float16_t(0.0), float16_t(0.22), a);
    const float16_t cb = one - smoothstep(float16_t(0.0), float16_t(0.18), b);
    return float(ca * ca * sqrt(ca) * float16_t(0.8) + cb * cb * sqrt(cb) * float16_t(0.45));
}

vec3 fbmd(uint p_tex, vec2 p_p, vec2 p_dx, vec2 p_dy) {
    const float inv = 1.0 / WATER_NOISE_DOMAIN;
    return textureGrad(sampler2D(u_textures[p_tex], u_samplers[pc.noise_sampler]), p_p * inv, p_dx * inv, p_dy * inv).rgb;
}

mat2 flow_frame(vec2 p_vel) {
    const float speed = length(p_vel);
    const vec2 dir = speed > 1.0e-4 ? p_vel / speed : vec2(1.0, 0.0);
    const float along = 1.0 / (1.0 + FLOW_STRETCH * min(speed, FLOW_STRETCH_SPEED));
    const mat2 aligned = mat2(dir.x * along, -dir.y, dir.y * along, dir.x);
    return mat2(1.0) + (aligned - mat2(1.0)) * smoothstep(0.1, 0.6, speed);
}

vec3 flow_detail(uint p_tex, vec2 p_p, vec2 p_vel, float p_freq, vec2 p_dx, vec2 p_dy, bool p_shimmer, mat2 p_frame) {
    const float t = pc.time / FLOW_PERIOD;
    const float p0 = fract(t), p1 = fract(t + 0.5);
    const float16_t w0 = float16_t(1.0 - abs(1.0 - 2.0 * p0));
    const vec2 gx = p_frame * p_dx * p_freq, gy = p_frame * p_dy * p_freq;
    const f16vec3 a = f16vec3(fbmd(p_tex, p_frame * (p_p - p_vel * p0 * FLOW_PERIOD) * p_freq, gx, gy));
    const f16vec3 b = f16vec3(fbmd(p_tex, p_frame * (p_p - p_vel * p1 * FLOW_PERIOD) * p_freq + 0.37, gx, gy));
    f16vec3 s = mix(b, a, w0);
    if (p_shimmer) s += f16vec3(fbmd(p_tex, p_frame * p_p * p_freq * 0.7 + vec2(pc.time * 0.13, -pc.time * 0.09), gx * 0.7, gy * 0.7)) * float16_t(0.35);
    return vec3(s.x, transpose(p_frame) * vec2(s.yz));
}

vec3 sky_color(vec3 p_d, vec3 p_sun_dir, vec3 p_sun_col) {
    const float t = clamp(p_d.y, 0.0, 1.0);
    const vec3 hor = vec3(0.78, 0.84, 0.90);
    vec3 c = mix(hor, vec3(0.22, 0.42, 0.78), pow(t, 0.55));
    if (p_d.y < 0.0) c = mix(hor, vec3(0.45, 0.47, 0.48), clamp(-p_d.y * 3.0, 0.0, 1.0));
    const float s = max(dot(p_d, p_sun_dir), 0.0);
    const float s2 = s * s, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8, s32 = s16 * s16;
    c += p_sun_col * (0.06 * s2 * s4 + 0.25 * s32 * s32);
    return c * 1.15;
}

vec2 sample_gradient(WaterItem p_it, uint p_index) {
    return unpackHalf2x16(floatBitsToUint(p_it.samples.v[SURFACE_SAMPLES * 4u + p_index]));
}

vec3 surface_normal() {
    WaterItem it = pc.items.v[v_item];
    const vec2 s = clamp(v_sample, vec2(0.0), vec2(16.0));
    const ivec2 i0 = min(ivec2(s), ivec2(15));
    const vec2 t = s - vec2(i0);
    const uint b = uint(i0.y) * SURFACE_ROW + uint(i0.x);
    const vec2 g = mix(mix(sample_gradient(it, b), sample_gradient(it, b + 1u), t.x), mix(sample_gradient(it, b + SURFACE_ROW), sample_gradient(it, b + SURFACE_ROW + 1u), t.x), t.y);
    return normalize(vec3(-g.x, 1.0, -g.y));
}

vec2 ripple_gradient(vec2 p_xz, float p_speed) {
    if (pc.ripple_index == 0xFFFFFFFFu) return vec2(0.0);
    const float half_extent = (float(pc.ripple_size) * 0.5 - RIPPLE_EDGE_TEXELS) * pc.ripple_cell;
    const vec2 rel = abs(p_xz - pc.ripple_center);
    if (max(rel.x, rel.y) > half_extent) return vec2(0.0);
    const float inv = 1.0 / (pc.ripple_cell * float(pc.ripple_size));
    const vec2 uv = p_xz * inv;
    const float step_uv = 1.0 / float(pc.ripple_size);
    const float l = textureLod(sampler2D(u_textures[pc.ripple_index], u_samplers[pc.noise_sampler]), uv - vec2(step_uv, 0.0), 0.0).r;
    const float r = textureLod(sampler2D(u_textures[pc.ripple_index], u_samplers[pc.noise_sampler]), uv + vec2(step_uv, 0.0), 0.0).r;
    const float d = textureLod(sampler2D(u_textures[pc.ripple_index], u_samplers[pc.noise_sampler]), uv - vec2(0.0, step_uv), 0.0).r;
    const float u = textureLod(sampler2D(u_textures[pc.ripple_index], u_samplers[pc.noise_sampler]), uv + vec2(0.0, step_uv), 0.0).r;
    return vec2(r - l, u - d) / (2.0 * pc.ripple_cell) * (1.0 - smoothstep(RIPPLE_FLOW_FADE_START, RIPPLE_FLOW_FADE_END, p_speed));
}

vec4 backdrop(vec2 p_px) {
    const ivec2 limit = ivec2(int(pc.width) - 1, int(pc.height) - 1);
    return texelFetch(u_textures[pc.backdrop_index], clamp(ivec2(p_px), ivec2(0), limit), 0);
}

void main() {
    const vec3 cam = pc.camera.position.xyz;
    const float exposure = pc.camera.pre_exposure;
    const vec3 sun_dir = -pc.light.direction;
    const vec3 sun_col = pc.light.illuminance;
    const float wdist = distance(cam, v_world);
    const float speed = length(v_flow);
    const float foam_amt = clamp(v_foam, 0.0, 1.0);

    const vec2 ripple = ripple_gradient(v_world.xz, length(v_flow));
    const vec3 base_n = normalize(surface_normal() + vec3(-ripple.x, 0.0, -ripple.y));
    const vec2 dpx = dFdx(v_world.xz);
    const vec2 dpy = dFdy(v_world.xz);
    const mat2 frame = flow_frame(v_flow);
    const vec3 det = flow_detail(pc.detail_index, v_world.xz, v_flow, 1.7, dpx, dpy, true, frame);
    const f16vec2 g = f16vec2(det.yz);
    const float near_detail = 1.0 - smoothstep(FAR_DETAIL_START, FAR_DETAIL_END, wdist);
    const float16_t strength = float16_t(RIPPLE_CALM + 0.22 * clamp(speed / 2.5, 0.0, 1.0) + 0.25 * foam_amt + RIPPLE_CURL * clamp(abs(v_curl) / 3.0, 0.0, 1.0));
    const vec3 n = vec3(normalize(f16vec3(base_n) + f16vec3(-g.x, 0.0, -g.y) * strength));

    const vec3 v = normalize(cam - v_world);
    const float16_t ndv = float16_t(max(dot(n, v), 0.0));
    const float16_t inv_ndv = float16_t(1.0) - ndv;
    const float16_t inv_ndv2 = inv_ndv * inv_ndv;
    const float fresnel = float(float16_t(0.02) + float16_t(0.98) * inv_ndv2 * inv_ndv2 * inv_ndv);

    vec3 r = reflect(-v, n);
    r.y = abs(r.y);
    vec3 refl = sky_color(r, sun_dir, sun_col);
    const float sd = max(dot(r, sun_dir), 0.0);
    const float sd90 = pow(sd, 90.0);
    const float sd180 = sd90 * sd90, sd360 = sd180 * sd180, sd720 = sd360 * sd360;
    refl += sun_col * (sd720 * sd180 * 18.0 + sd90 * 0.6);

    const vec4 b0 = backdrop(gl_FragCoord.xy);
    const float thick0 = b0.a >= SKY_DISTANCE ? 1.0 : max(b0.a - wdist, 0.0);
    const vec2 offset = n.xz * 0.045 * clamp(thick0, 0.0, 1.0) * vec2(float(pc.width), float(pc.height));
    vec4 b1 = dot(offset, offset) >= 0.25 ? backdrop(gl_FragCoord.xy + offset) : b0;
    if (b1.a < wdist) b1 = b0;
    const bool sky = b1.a >= SKY_DISTANCE;
    const float thick = sky ? 50.0 : max(b1.a - wdist, 0.0);
    vec3 bg = b1.rgb;
    const bool caustics_on = (pc.flags & WATER_FLAG_CAUSTICS) != 0u;
    float caustic_term = 0.0;
    if (!sky && near_detail > 0.0 && (caustics_on || pc.debug_mode == WATER_DEBUG_CAUSTICS)) {
        const vec3 bed = cam + (v_world - cam) * (b1.a / max(wdist, 1.0e-3));
        const float d = v_world.y - bed.y;
        const float att = d > 0.0 ? exp(-d * 0.7) * smoothstep(0.0, 0.15, d) : 0.0;
        if (att > CAUSTIC_MIN_ATTENUATION) {
            caustic_term = caustic(bed.xz * 1.15 - vec2(0.0, pc.time * 0.35), pc.time * 1.4) * att * near_detail;
            if (caustics_on) bg += bg * caustic_term * 0.45;
        }
    }

    const vec3 trans = exp(-ABSORB * thick * 1.4);
    vec3 scatter = SCATTER * (sun_col * max(sun_dir.y, 0.0) + vec3(0.4, 0.5, 0.6));
    scatter *= 1.0 + 1.5 * foam_amt;
    const vec3 refr = bg * trans + scatter * exposure * (1.0 - trans);
    vec3 col = mix(refr, refl * exposure, fresnel);

    if (pc.debug_mode != WATER_DEBUG_NONE) {
        vec3 dc = vec3(0.0);
        if (pc.debug_mode == WATER_DEBUG_CAUSTICS) dc = vec3(caustic_term);
        else if (pc.debug_mode == WATER_DEBUG_DEPTH) dc = color_heat(v_depth / 1.5);
        else if (pc.debug_mode == WATER_DEBUG_SPEED) dc = color_heat(speed / 4.0);
        else if (pc.debug_mode == WATER_DEBUG_FOAM) dc = vec3(foam_amt);
        else if (pc.debug_mode == WATER_DEBUG_CURL) dc = srgb_to_linear(v_curl > 0.0 ? vec3(1.0, 0.25, 0.1) * clamp(v_curl / 3.0, 0.0, 1.0) : vec3(0.1, 0.35, 1.0) * clamp(-v_curl / 3.0, 0.0, 1.0));
        else if (pc.debug_mode == WATER_DEBUG_FRESNEL) dc = vec3(fresnel);
        else if (pc.debug_mode == WATER_DEBUG_THICKNESS) dc = color_heat(thick / 2.0);
        o_color = vec4(dc, 1.0);
        return;
    }

    if (near_detail > 0.0 && (foam_amt >= 0.02 || thick < 0.10)) {
        const float fn = flow_detail(pc.noise_index, v_world.xz * 1.3 + 5.0, v_flow, 2.3, dpx * 1.3, dpy * 1.3, false, frame).x;
        const float fn2 = flow_detail(pc.noise_index, v_world.xz * 4.0 - 3.0, v_flow, 2.0, dpx * 4.0, dpy * 4.0, false, frame).x;
        const float nz = fn * 0.7 + fn2 * 0.45;
        float fm = smoothstep(0.62 - 0.5 * foam_amt, 0.78 - 0.45 * foam_amt, nz) * smoothstep(0.02, 0.35, foam_amt);
        const float shore = 1.0 - smoothstep(0.0, 0.10, thick);
        fm = max(fm, shore * smoothstep(0.45, 0.75, nz) * SHORE_FOAM_OPACITY);
        const vec3 foam_col = vec3(FOAM_ALBEDO) * (sun_col * max(dot(base_n, sun_dir), 0.0) + vec3(0.42, 0.52, 0.62));
        const float lace = smoothstep(0.55, 0.95, 1.0 - abs(fn2 * 2.0 - 1.0));
        const float agitation = smoothstep(0.35, 0.9, foam_amt);
        const float density = mix(FOAM_WISP_FLOOR + (1.0 - FOAM_WISP_FLOOR) * lace, 1.0, agitation);
        col = mix(col, foam_col * exposure, clamp(fm, 0.0, 1.0) * density * near_detail);
    }

    col = mix(bg, col, smoothstep(0.0, 0.05, thick));
    o_color = vec4(col, 1.0);
}
