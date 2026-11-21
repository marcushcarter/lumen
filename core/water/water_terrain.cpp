#include <core/water/water_terrain.h>
#include <core/terrain/terrain_source.h>
#include <algorithm>
#include <cmath>

namespace lumen {

void WaterTerrainHeights::bind(const TerrainSource* p_source)
{
    const Guid g = p_source && p_source->valid() ? p_source->guid : Guid{};
    if (p_source == source && g == guid) return;
    source = p_source && p_source->valid() ? p_source : nullptr;
    guid = g;
    tiles.clear();
    clock = 0;
}

bool WaterTerrainHeights::valid() const
{
    return source && source->valid();
}

vec4 WaterTerrainHeights::rect() const
{
    if (!valid()) return vec4(0.0f);
    const vec3 o = source->origin();
    const LTerrainPayloadHeader& h = source->header;
    return vec4(o.x, o.z, o.x + (float)(h.samples_x - 1) * h.spacing, o.z + (float)(h.samples_y - 1) * h.spacing);
}

void WaterTerrainHeights::prefetch(const vec2& p_min, const vec2& p_max)
{
    if (!valid()) return;
    const LTerrainPayloadHeader& h = source->header;
    const vec3 o = source->origin();
    const float size = (float)LTERRAIN_TILE_QUADS * h.spacing;
    const int32_t ax = std::max((int32_t)std::floor((p_min.x - o.x) / size), 0);
    const int32_t bx = std::min((int32_t)std::floor((p_max.x - o.x) / size), (int32_t)h.tiles_x - 1);
    const int32_t ay = std::max((int32_t)std::floor((p_min.y - o.z) / size), 0);
    const int32_t by = std::min((int32_t)std::floor((p_max.y - o.z) / size), (int32_t)h.tiles_y - 1);
    const uint32_t row = lterrain_lod_samples(0);
    clock++;
    for (int32_t y = ay; y <= by; y++) {
        for (int32_t x = ax; x <= bx; x++) {
            const uint32_t t = source->tile_index((uint32_t)x, (uint32_t)y);
            Entry& e = tiles[t];
            e.used = clock;
            if (!e.samples.empty()) continue;
            e.samples.resize((size_t)row * row);
            e.ok = source->read_lod(t, 0, e.samples.data());
        }
    }
    while (tiles.size() > MAX_TILES) {
        auto oldest = tiles.begin();
        for (auto it = tiles.begin(); it != tiles.end(); ++it) if (it->second.used < oldest->second.used) oldest = it;
        if (oldest->second.used == clock) break;
        tiles.erase(oldest);
    }
}

bool WaterTerrainHeights::height(float p_x, float p_z, float& r_y) const
{
    if (!valid()) return false;
    const LTerrainPayloadHeader& h = source->header;
    const vec3 o = source->origin();
    const float gx = (p_x - o.x) / h.spacing;
    const float gz = (p_z - o.z) / h.spacing;
    if (gx < 0.0f || gz < 0.0f || gx > (float)(h.samples_x - 1) || gz > (float)(h.samples_y - 1)) return false;
    const uint32_t q = LTERRAIN_TILE_QUADS;
    const uint32_t tx = std::min((uint32_t)gx / q, h.tiles_x - 1);
    const uint32_t tz = std::min((uint32_t)gz / q, h.tiles_y - 1);
    const auto it = tiles.find(source->tile_index(tx, tz));
    if (it == tiles.end() || !it->second.ok) return false;
    const float lx = gx - (float)(tx * q);
    const float lz = gz - (float)(tz * q);
    const uint32_t i0 = std::min((uint32_t)lx, q);
    const uint32_t j0 = std::min((uint32_t)lz, q);
    const float fx = lx - (float)i0;
    const float fz = lz - (float)j0;
    const uint32_t row = lterrain_lod_samples(0);
    const auto at = [&](uint32_t i, uint32_t j) { return (float)it->second.samples[(size_t)(j + LTERRAIN_APRON) * row + i + LTERRAIN_APRON]; };
    const float v = (at(i0, j0) * (1.0f - fx) + at(i0 + 1, j0) * fx) * (1.0f - fz) + (at(i0, j0 + 1) * (1.0f - fx) + at(i0 + 1, j0 + 1) * fx) * fz;
    r_y = source->height_meters(0) + v * source->height_scale();
    return true;
}

}
