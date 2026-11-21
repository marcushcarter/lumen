#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

#include "water/draw.glsl"

layout(set = 0, binding = 0) uniform texture2D u_textures[];

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_world;
layout(location = 2) in vec2 v_flow;
layout(location = 3) in float v_foam;
layout(location = 0) out vec4 o_color;

const float SKY_DISTANCE = 60000.0;
const float FLOW_PERIOD = 1.6;
const vec3 ABSORB = vec3(0.62, 0.17, 0.13);
const vec3 SCATTER = vec3(0.02, 0.10, 0.085);

float hash12(vec2 p_p) {
    vec3 p3 = fract(vec3(p_p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 noised(vec2 p_x) {
    const vec2 i = floor(p_x), f = fract(p_x);
    const vec2 u = f * f * (3.0 - 2.0 * f);
    const vec2 du = 6.0 * f * (1.0 - f);
    const float a = hash12(i), b = hash12(i + vec2(1, 0)), c = hash12(i + vec2(0, 1)), d = hash12(i + vec2(1, 1));
    return vec3(a + (b - a) * u.x + (c - a) * u.y + (a - b - c + d) * u.x * u.y, du * (vec2(b - a, c - a) + (a - b - c + d) * u.yx));
}

vec3 fbmd(vec2 p_p) {
    vec3 s = vec3(0.0);
    float a = 0.5;
    const mat2 m = mat2(1.6, 1.2, -1.2, 1.6);
    mat2 acc = mat2(1.0);
    vec2 p = p_p;
    for (int k = 0; k < 4; k++) {
        const vec3 n = noised(p);
        s += a * vec3(n.x, transpose(acc) * n.yz);
        p = m * p;
        acc = m * acc;
        a *= 0.5;
    }
    return s;
}

vec3 flow_detail(vec2 p_p, vec2 p_vel, float p_freq) {
    const float t = pc.time / FLOW_PERIOD;
    const float p0 = fract(t), p1 = fract(t + 0.5);
    const float w0 = 1.0 - abs(1.0 - 2.0 * p0);
    const vec3 a = fbmd((p_p - p_vel * p0 * FLOW_PERIOD) * p_freq);
    const vec3 b = fbmd((p_p - p_vel * p1 * FLOW_PERIOD) * p_freq + 0.37);
    const vec3 c = fbmd(p_p * p_freq * 0.7 + vec2(pc.time * 0.13, -pc.time * 0.09));
    return mix(b, a, w0) + c * 0.35;
}

vec3 sky_color(vec3 p_d, vec3 p_sun_dir, vec3 p_sun_col) {
    const float t = clamp(p_d.y, 0.0, 1.0);
    const vec3 hor = vec3(0.78, 0.84, 0.90);
    vec3 c = mix(hor, vec3(0.22, 0.42, 0.78), pow(t, 0.55));
    if (p_d.y < 0.0) c = mix(hor, vec3(0.45, 0.47, 0.48), clamp(-p_d.y * 3.0, 0.0, 1.0));
    const float s = max(dot(p_d, p_sun_dir), 0.0);
    c += p_sun_col * (0.06 * pow(s, 6.0) + 0.25 * pow(s, 64.0));
    return c * 1.15;
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

    const vec3 det = flow_detail(v_world.xz, v_flow, 1.7);
    const vec3 det2 = flow_detail(v_world.xz * 2.7 + 11.0, v_flow, 1.7);
    const vec2 g = det.yz + det2.yz * 0.5;
    const float strength = 0.10 + 0.22 * clamp(speed / 2.5, 0.0, 1.0) + 0.25 * foam_amt;
    const vec3 n = normalize(normalize(v_normal) + vec3(-g.x, 0.0, -g.y) * strength);

    const vec3 v = normalize(cam - v_world);
    const float ndv = max(dot(n, v), 0.0);
    const float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);

    vec3 r = reflect(-v, n);
    r.y = abs(r.y);
    vec3 refl = sky_color(r, sun_dir, sun_col);
    const float sd = max(dot(r, sun_dir), 0.0);
    refl += sun_col * (pow(sd, 900.0) * 18.0 + pow(sd, 90.0) * 0.6);

    const vec4 b0 = backdrop(gl_FragCoord.xy);
    const float thick0 = b0.a >= SKY_DISTANCE ? 1.0 : max(b0.a - wdist, 0.0);
    const vec2 offset = n.xz * 0.045 * clamp(thick0, 0.0, 1.0) * vec2(float(pc.width), float(pc.height));
    vec4 b1 = backdrop(gl_FragCoord.xy + offset);
    if (b1.a < wdist) b1 = b0;
    const float thick = b1.a >= SKY_DISTANCE ? 50.0 : max(b1.a - wdist, 0.0);
    const vec3 bg = b1.rgb;

    const vec3 trans = exp(-ABSORB * thick * 1.4);
    vec3 scatter = SCATTER * (sun_col * max(sun_dir.y, 0.0) + vec3(0.4, 0.5, 0.6));
    scatter *= 1.0 + 1.5 * foam_amt;
    const vec3 refr = bg * trans + scatter * exposure * (1.0 - trans);
    vec3 col = mix(refr, refl * exposure, fresnel);

    const float fn = flow_detail(v_world.xz * 1.3 + 5.0, v_flow, 2.3).x;
    const float fn2 = flow_detail(v_world.xz * 4.0 - 3.0, v_flow, 2.0).x;
    const float nz = fn * 0.7 + fn2 * 0.45;
    float fm = smoothstep(0.62 - 0.5 * foam_amt, 0.78 - 0.45 * foam_amt, nz) * smoothstep(0.02, 0.35, foam_amt);
    const float shore = 1.0 - smoothstep(0.0, 0.10, thick);
    fm = max(fm, shore * smoothstep(0.45, 0.75, nz) * 0.55);
    const vec3 foam_col = vec3(0.92) * (sun_col * max(dot(normalize(v_normal), sun_dir), 0.0) + vec3(0.42, 0.52, 0.62));
    col = mix(col, foam_col * exposure, clamp(fm, 0.0, 1.0));

    col = mix(bg, col, smoothstep(0.0, 0.05, thick));
    o_color = vec4(col, 1.0);
}
