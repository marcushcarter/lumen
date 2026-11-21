#pragma once
#include <core/assets/guid.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lumen {

using namespace glm;

struct TerrainSource;

struct WaterTerrainHeights
{
    static constexpr size_t MAX_TILES = 48;

    struct Entry {
        std::vector<uint16_t> samples;
        uint64_t used = 0;
        bool ok = false;
    };

    const TerrainSource* source = nullptr;
    Guid guid;
    std::unordered_map<uint32_t, Entry> tiles;
    uint64_t clock = 0;

    void bind(const TerrainSource* p_source);
    bool valid() const;
    vec4 rect() const;
    void prefetch(const vec2& p_min, const vec2& p_max);
    bool height(float p_x, float p_z, float& r_y) const;
};

}
