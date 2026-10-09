#ifndef LIGHTS_GLSL
#define LIGHTS_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "common/camera.glsl"

const uint LIGHT_TYPE_DIRECTIONAL = 0u;
const uint LIGHT_TYPE_POINT = 1u;
const uint LIGHT_TYPE_SPOT = 2u;

const uint LIGHT_FLAG_CAST_SHADOWS = 1u << 0;
const uint LIGHT_FLAG_AFFECTS_GI = 1u << 1;

const uint LIGHT_IES_NONE = 0xFFFFu;
const uint LIGHT_SHADOW_NONE = 0xFFFFFFFFu;
const uint MAX_DIRECTIONAL_LIGHTS = 2u;

const uint MAX_VISIBLE_LIGHTS = 4096u;
const uint LIGHT_ZBIN_COUNT = 1024u;
const uint LIGHT_TILE_SIZE = 8u;
const uint LIGHT_TILE_WORDS = MAX_VISIBLE_LIGHTS / 32u;

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

layout(buffer_reference, scalar) buffer LightCullData {
    uint visible_count;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    uint histogram[LIGHT_ZBIN_COUNT];
    uint cursor[LIGHT_ZBIN_COUNT];
    uvec2 zbins[LIGHT_ZBIN_COUNT];
};
layout(buffer_reference, scalar) buffer VisibleLightBuffer { uvec2 data[]; };
layout(buffer_reference, scalar) buffer SortedLightBuffer { GpuLight data[]; };
layout(buffer_reference, scalar) buffer LightTileMaskBuffer { uint data[]; };

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

vec4 light_bounds(GpuLight p_light) {
    if (light_type(p_light) == LIGHT_TYPE_SPOT) {
        float c = p_light.cos_outer;
        if (c < 0.70710678) return vec4(p_light.position + p_light.direction * (p_light.range * c), p_light.range * sqrt(max(1.0 - c * c, 0.0)));
        float r = p_light.range / (2.0 * c);
        return vec4(p_light.position + p_light.direction * r, r);
    }
    return vec4(p_light.position, p_light.range);
}

float camera_view_depth(CameraBuffer p_camera, vec3 p_world) {
    mat4 m = p_camera.curr_view_proj;
    return m[0][3] * p_world.x + m[1][3] * p_world.y + m[2][3] * p_world.z + m[3][3];
}

float camera_linear_depth(CameraBuffer p_camera, float p_depth) {
    float n = p_camera.near_z;
    float f = p_camera.far_z;
    return n * f / (p_depth * (f - n) + n);
}

uint light_zbin(CameraBuffer p_camera, float p_view_depth) {
    float scale = float(LIGHT_ZBIN_COUNT) / log(p_camera.far_z / p_camera.near_z);
    float b = log(max(p_view_depth, p_camera.near_z) / p_camera.near_z) * scale;
    return uint(clamp(b, 0.0, float(LIGHT_ZBIN_COUNT - 1u)));
}

vec3 light_sphere_specular_dir(vec3 p_to_light, vec3 p_r, float p_radius) {
    vec3 center_to_ray = dot(p_to_light, p_r) * p_r - p_to_light;
    vec3 closest = p_to_light + center_to_ray * clamp(p_radius * inversesqrt(max(dot(center_to_ray, center_to_ray), 1e-8)), 0.0, 1.0);
    return normalize(closest);
}

#endif