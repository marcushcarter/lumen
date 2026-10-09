#ifndef LIGHTS_GLSL
#define LIGHTS_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint LIGHT_TYPE_DIRECTIONAL = 0u;
const uint LIGHT_TYPE_POINT = 1u;
const uint LIGHT_TYPE_SPOT = 2u;

const uint LIGHT_FLAG_CAST_SHADOWS = 1u << 0;
const uint LIGHT_FLAG_AFFECTS_GI = 1u << 1;

const uint LIGHT_IES_NONE = 0xFFFFu;
const uint LIGHT_SHADOW_NONE = 0xFFFFFFFFu;
const uint MAX_DIRECTIONAL_LIGHTS = 2u;

struct GpuLight {
    vec3 position;
    float range;
    vec3 radiance;
    float source_radius;
    vec3 direction;
    uint type_flags_ies;
    float cos_outer;
    float inv_cone_range;
    uint shadow_id;
    uint fade;
};

layout(buffer_reference, scalar) readonly buffer LightBuffer { GpuLight data[]; };

uint light_type(GpuLight p_light) { return p_light.type_flags_ies & 0xFu; }
uint light_flags(GpuLight p_light) { return (p_light.type_flags_ies >> 4) & 0xFu; }
uint light_ies(GpuLight p_light) { return (p_light.type_flags_ies >> 8) & 0xFFFFu; }
vec2 light_fade(GpuLight p_light) { return unpackHalf2x16(p_light.fade); }

float light_range_falloff(float p_dist_sq, float p_range) {
    float r = p_dist_sq / (p_range * p_range);
    float w = clamp(1.0 - r * r, 0.0, 1.0);
    return w * w / (p_dist_sq + 1.0);
}

float light_spot_falloff(GpuLight p_light, vec3 p_l) {
    float a = clamp((dot(-p_l, p_light.direction) - p_light.cos_outer) * p_light.inv_cone_range, 0.0, 1.0);
    return a * a;
}

float light_camera_fade(GpuLight p_light, float p_camera_dist) {
    vec2 f = light_fade(p_light);
    return f.y > f.x ? clamp((f.y - p_camera_dist) / (f.y - f.x), 0.0, 1.0) : 1.0;
}

vec3 light_sphere_specular_dir(vec3 p_to_light, vec3 p_r, float p_radius) {
    vec3 center_to_ray = dot(p_to_light, p_r) * p_r - p_to_light;
    vec3 closest = p_to_light + center_to_ray * clamp(p_radius * inversesqrt(max(dot(center_to_ray, center_to_ray), 1e-8)), 0.0, 1.0);
    return normalize(closest);
}

#endif