#ifndef WATER_ITEMS_GLSL
#define WATER_ITEMS_GLSL

#include "water/common.glsl"

struct WaterItem {
    WaterFloats samples;
    WaterUints masks;
    WaterUints bounds;
    vec2 origin;
    float spacing;
    float ymin;
    float ymax;
    uint has_bounds;
};

layout(buffer_reference, scalar) readonly buffer WaterItems { WaterItem v[]; };

struct WaterDrawArgs {
    uint vertex_count;
    uint instance_count;
    uint first_vertex;
    uint first_instance;
};

layout(buffer_reference, scalar) buffer WaterArgs { WaterDrawArgs v[]; };

const uint WATER_LOD_LEVELS = 5u;
const uint WATER_BLOCK_CELLS = 4u;
const float WATER_MORPH_START = 0.7;

#endif
