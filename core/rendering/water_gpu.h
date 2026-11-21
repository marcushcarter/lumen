#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace lumen {

using namespace glm;

struct WaterGpuParams
{
    float dx = 0.25f;
    float gravity = 9.81f;
    float manning = 0.032f;
    float vorticity = 1.6f;
    float vortex_min_depth = 0.08f;
    float cfl_limit = 0.45f;
    float flux_depth_cap = 1.0f;
    float foam_decay = 0.9f;
    float foam_gain = 2.0f;
    uint32_t substeps = 2;
};

struct WaterGpuCell
{
    uint32_t array = 0;
    uint32_t cell = 0;
    float a = 0.0f;
    float b = 0.0f;
    float c = 0.0f;
};

struct WaterGpuUpload
{
    uint32_t array = 0;
    uint32_t phys = 0;
    uint32_t offset = 0;
};

struct WaterGpuResult
{
    static constexpr uint32_t ARRAYS = 6;

    uint64_t layout = 0;
    ivec2 tile = ivec2(0);
    float data[ARRAYS][256] = {};
};

struct WaterGpuItem
{
    enum Kind : uint32_t { LIVE, FROZEN };

    uint32_t kind = LIVE;
    uint32_t slot = 0;
    vec2 origin = vec2(0.0f);
    float spacing = 1.0f;
    float ymin = 0.0f;
    float ymax = 0.0f;
};

struct WaterGpuFrozen
{
    uint64_t layout = 0;
    uint32_t capacity = 0;
    std::vector<uint32_t> slots;
    std::vector<vec4> samples;
    std::vector<uint32_t> masks;

    void clear_commands() { slots.clear(); samples.clear(); masks.clear(); }
};

struct WaterGpuLink
{
    static constexpr uint32_t MAX_STEPS = 4;

    uint64_t session = 0;
    uint64_t layout = 0;
    int32_t tiles_w = 0;
    int32_t slots = 0;
    ivec2 origin_tile = ivec2(0);
    uint32_t capacity = 0;
    WaterGpuParams params;

    std::vector<uint32_t> page;
    std::vector<uint32_t> awake;
    std::vector<uint32_t> surface;
    std::vector<WaterGpuCell> sources;

    std::vector<uint32_t> init;
    std::vector<WaterGpuUpload> uploads;
    std::vector<float> blob;
    std::vector<WaterGpuCell> events;

    WaterGpuFrozen frozen;
    std::vector<WaterGpuItem> items;
    uint32_t steps = 0;
    float lod_error = 0.01f;
    float min_quad = 0.25f;
    float time = 0.0f;

    std::vector<WaterGpuResult> results;
    uint64_t stats_layout = 0;
    std::vector<float> stats;

    void clear_commands() { init.clear(); uploads.clear(); blob.clear(); events.clear(); frozen.clear_commands(); }
};

}
