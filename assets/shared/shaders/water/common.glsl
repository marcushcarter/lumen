#ifndef WATER_COMMON_GLSL
#define WATER_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_16bit_storage : require

layout(buffer_reference, scalar) buffer WaterFloats { float v[]; };
layout(buffer_reference, scalar) buffer WaterUints { uint v[]; };
layout(buffer_reference, scalar) buffer WaterHalfs { float16_t v[]; };

struct WaterCell {
    uint array;
    uint cell;
    float a;
    float b;
    float c;
};

layout(buffer_reference, scalar) readonly buffer WaterCells { WaterCell v[]; };

layout(buffer_reference, scalar) readonly buffer WaterLevel {
    WaterFloats state;
    WaterHalfs state16;
    WaterUints stats;
    WaterUints page;
    WaterUints awake;
    WaterUints init;
    WaterUints surface;
    WaterCells sources;
    WaterCells events;
    WaterFloats pool;
    WaterUints masks;
    WaterUints bounds;
    WaterUints gather;
    WaterFloats readback;
    ivec2 origin;
    int slots;
    int tiles_w;
    int n;
    uint cells;
    uint awake_count;
    uint init_count;
    uint surface_count;
    uint source_count;
    uint event_count;
    uint gather_count;
    float dx;
    float gravity;
    float manning;
    float vorticity;
    float vortex_min_depth;
    float cfl_limit;
    float flux_depth_cap;
    float foam_decay;
    float foam_gain;
};

const uint A_H = 0u;
const uint A_B = 1u;
const uint A_U = 2u;
const uint A_W = 3u;
const uint A_UT = 4u;
const uint A_WT = 5u;
const uint A_FLAGS = 6u;
const uint A_FLUX_U = 7u;
const uint A_FLUX_W = 8u;
const uint A_SCALE = 9u;
const uint A_FOAM = 10u;
const uint A_FOAM_OLD = 11u;
const uint A_UC = 12u;
const uint A_WC = 13u;
const uint A_CURL = 14u;
const uint A_FX = 15u;
const uint A_FZ = 16u;
const uint A_COUNT = 17u;
const uint A_WIDE = 10u;

const uint CELL_KNOWN = 1u;
const uint CELL_DRAIN = 2u;
const uint PAGE_EMPTY = 0x00FFFFFFu;
const uint PAGE_PHYS = 0x00FFFFFFu;
const uint PAGE_ACTIVE = 1u << 30;
const uint PAGE_AWAKE = 1u << 31;

const float W_EPS = 0.004;
const float W_WET = 0.01;
const float W_SURFACE_WET = 0.01;
const float W_SURFACE_KEEP = 0.006;
const float W_WALL = 1.0e6;
const float W_WALL_TEST = 0.5e6;
const float W_FOAM_MAX = 1.5;
const float W_SHORE_TOLERANCE = 0.02;
const float W_HIDE_DEPTH = 0.08;
const float W_FALL_SLOPE = 2.0;
const float W_FALL_MIN = 0.3;
const int W_SHORE_REACH = 2;
const int W_EXT_REACH = 4;
const float W_SURFACE_BLEND = 0.58;

const float SAMPLE_INVALID = 0.0;
const float SAMPLE_HIDDEN = 1.0;
const float SAMPLE_SHORE = 2.0;
const float SAMPLE_WET = 3.0;

const uint SURFACE_ROW = 17u;
const uint SURFACE_SAMPLES = 289u;
const uint SURFACE_SLOT_FLOATS = SURFACE_SAMPLES * 5u;
const uint SURFACE_MASK_WORDS = 8u;

int water_wrap(int p_a, int p_m) {
    return (p_a + p_m * 65536) % p_m;
}

float water_default(uint p_a) {
    return p_a == A_B ? W_WALL : (p_a == A_SCALE ? 1.0 : 0.0);
}

ivec2 water_unpack(uint p_packed) {
    return ivec2(int(p_packed & 0x7FFFu) - 1, int((p_packed >> 16) & 0x7FFFu) - 1);
}

uint water_sortable(float p_f) {
    const uint u = floatBitsToUint(p_f);
    return (u & 0x80000000u) != 0u ? ~u : (u | 0x80000000u);
}

float water_unsortable(uint p_u) {
    return uintBitsToFloat((p_u & 0x80000000u) != 0u ? (p_u & 0x7FFFFFFFu) : ~p_u);
}

#endif
