#ifndef WATER_DRAW_GLSL
#define WATER_DRAW_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "common/camera.glsl"
#include "common/direct_light.glsl"
#include "water/items.glsl"

layout(push_constant, scalar) uniform Push {
    CameraBuffer camera;
    WaterItems items;
    WaterUints list;
    DirectLightBuffer light;
    uint capacity;
    float lod_error;
    float time;
    uint backdrop_index;
    uint width;
    uint height;
    uint flags;
    uint debug_mode;
    uint noise_index;
    uint noise_sampler;
} pc;

const float WATER_NOISE_DOMAIN = 16.0;

const uint WATER_FLAG_CAUSTICS = 1u;
const uint WATER_DEBUG_NONE = 0u;
const uint WATER_DEBUG_CAUSTICS = 1u;
const uint WATER_DEBUG_DEPTH = 2u;
const uint WATER_DEBUG_SPEED = 3u;
const uint WATER_DEBUG_FOAM = 4u;
const uint WATER_DEBUG_CURL = 5u;
const uint WATER_DEBUG_FRESNEL = 6u;
const uint WATER_DEBUG_THICKNESS = 7u;

#endif
