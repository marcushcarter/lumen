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
} pc;

#endif
