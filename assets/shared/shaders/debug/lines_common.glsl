#ifndef DEBUG_LINES_COMMON_GLSL
#define DEBUG_LINES_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "common/camera.glsl"

struct DebugLine {
    vec3 a;
    uint color;
    vec3 b;
    uint flags;
};

layout(buffer_reference, scalar) readonly buffer DebugLineBuffer {
    DebugLine lines[];
};

layout(push_constant, scalar) uniform Push {
    DebugLineBuffer lines;
    CameraBuffer camera;
    uvec2 out_size;
    uvec2 depth_size;
    uint depth_index;
    float half_width;
} pc;

const uint DEBUG_LINE_FLAG_XRAY = 1u << 8;
const uint DEBUG_LINE_FLAG_POINT = 1u << 9;
const uint DEBUG_LINE_POINT_RADIUS_SHIFT = 16u;
const float DEBUG_LINE_POINT_RADIUS_STEPS = 4.0;
const float DEBUG_LINE_AA_PX = 1.0;

#endif
